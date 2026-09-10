#include "led_group.h"

#define BREATHE_LUT_SIZE 200

/* Ported from biostaq_dispenser/cannadorf_v2's `effect1` float table
 * (0.0-1.0), converted to uint8_t percentages (0-100). See
 * docs/superpowers/specs/2026-08-12-led-group-design.md for why this is an
 * integer LUT rather than float. */
static const uint8_t breathe_lut[BREATHE_LUT_SIZE] = {
    4,5,5,5,6,6,6,7,7,7,8,8,9,9,10,10,11,11,12,13,13,14,15,16,16,17,18,19,20,20,21,22,23,24,25,26,28,29,30,31,32,33,35,36,
    37,38,40,41,43,44,45,47,48,50,51,53,54,56,57,59,60,62,63,65,66,68,69,70,72,73,75,76,77,79,80,81,83,84,85,86,87,88,89,90,91,92,93,94,
    95,95,96,97,97,97,98,98,99,99,99,99,99,99,99,99,99,98,98,97,97,97,96,95,95,94,93,92,91,90,89,88,87,86,85,84,83,81,80,79,77,76,75,73,
    72,70,69,68,66,65,63,62,60,59,57,56,54,53,51,50,48,47,45,44,43,41,40,38,37,36,35,33,32,31,30,29,28,26,25,24,23,22,21,20,20,19,18,17,
    16,16,15,14,13,13,12,11,11,10,10,9,9,8,8,7,7,7,6,6,6,5,5,5
};

/* The shared tick counter every group phases off - advancing it once per
 * rendering pass is what keeps same-period groups in lockstep. */
static uint32_t s_ticks = 0;

void led_group_tick(void)
{
    s_ticks++;
}

static void led_group_recompute_scaled(led_group_t *group)
{
    group->scaled_r = (uint8_t)((uint16_t)group->base_r * group->brightness_pct / 100);
    group->scaled_g = (uint8_t)((uint16_t)group->base_g * group->brightness_pct / 100);
    group->scaled_b = (uint8_t)((uint16_t)group->base_b * group->brightness_pct / 100);
    group->scaled_amb_r = (uint8_t)((uint16_t)group->amb_r * group->brightness_pct / 100);
    group->scaled_amb_g = (uint8_t)((uint16_t)group->amb_g * group->brightness_pct / 100);
    group->scaled_amb_b = (uint8_t)((uint16_t)group->amb_b * group->brightness_pct / 100);
}

void led_group_init(led_group_t *group, const led_strip_t *strip,
                     const uint16_t *indices, uint16_t indices_count)
{
    group->strip = strip;
    group->indices = indices;
    group->indices_count = indices_count;

    group->base_r = 0;
    group->base_g = 0;
    group->base_b = 0;
    group->brightness_pct = 100;
    group->scaled_r = 0;
    group->scaled_g = 0;
    group->scaled_b = 0;

    group->state = LED_GROUP_OFF;
    group->state_entered_tick = s_ticks;
    group->period_ticks = 100;
    group->blink_code_count = 0;
    group->blink_code_pause_ticks = 0;

    group->spot_size = 0;
    group->amb_r = 0;
    group->amb_g = 0;
    group->amb_b = 0;
    group->scaled_amb_r = 0;
    group->scaled_amb_g = 0;
    group->scaled_amb_b = 0;
    group->spot_dir = LED_GROUP_SPOT_FORWARD;
    group->last_spot_pos = 0;

    group->last_r = 0;
    group->last_g = 0;
    group->last_b = 0;
    group->dirty = true;
}

void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b)
{
    group->base_r = r;
    group->base_g = g;
    group->base_b = b;
    led_group_recompute_scaled(group);
    group->dirty = true;
}

void led_group_set_brightness_pct(led_group_t *group, uint8_t pct)
{
    group->brightness_pct = pct > 100 ? 100 : pct;
    led_group_recompute_scaled(group);
    group->dirty = true;
}

void led_group_set_period_ticks(led_group_t *group, uint32_t period_ticks)
{
    group->period_ticks = period_ticks;
    group->dirty = true;
}

void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ticks)
{
    group->blink_code_count = count;
    group->blink_code_pause_ticks = pause_ticks;
    group->dirty = true;
}

void led_group_set_spot(led_group_t *group, uint16_t spot_size,
                         uint8_t amb_r, uint8_t amb_g, uint8_t amb_b)
{
    /* Clamp so a wrapped spot can never overlap its own tail, which would
     * otherwise write one pixel twice in a single pass with two different
     * colors. led_group_init() is a precondition of every setter, so
     * indices_count is already known here. */
    group->spot_size = spot_size > group->indices_count
                           ? group->indices_count
                           : spot_size;
    group->amb_r = amb_r;
    group->amb_g = amb_g;
    group->amb_b = amb_b;
    led_group_recompute_scaled(group);
    group->dirty = true;
}

void led_group_set_spot_direction(led_group_t *group, led_group_spot_dir_t dir)
{
    group->spot_dir = dir;
    group->dirty = true;
}

void led_group_set_state(led_group_t *group, led_group_state_t state)
{
    group->state = state;
    group->state_entered_tick = s_ticks;
    group->dirty = true;
}

led_group_state_t led_group_get_state(const led_group_t *group)
{
    return group->state;
}

/* Blend fg toward bg by an integer percentage: w == 100 is pure fg,
 * w == 0 pure bg. The uint16_t intermediates keep the products in range
 * (the worst case is 255 * 100 == 25500). Callers must pass w <= 100,
 * which the weight formula in the SPOT case below guarantees. */
static uint8_t led_group_blend(uint8_t fg, uint8_t bg, uint8_t w)
{
    return (uint8_t)(((uint16_t)fg * w + (uint16_t)bg * (uint8_t)(100 - w)) / 100);
}

/* The spot head's position within the group's indices array. Phase derives
 * from the shared tick counter, like BREATHING/BLINK, so groups sharing a
 * period travel in lockstep, and period_ticks is guarded against 0 the same
 * way. One period is one full traversal of the group.
 *
 * Returns 0 for every state other than SPOT. That is what lets
 * led_group_update() fold the head position into its change detection
 * unconditionally: for a uniform state the position is always 0, so it
 * always compares equal and can never trigger a spurious rewrite. */
static uint16_t led_group_spot_pos(const led_group_t *group)
{
    uint32_t period_ticks;
    uint32_t phase;
    uint16_t pos;

    if (group->state != LED_GROUP_SPOT) {
        return 0;
    }

    period_ticks = group->period_ticks != 0 ? group->period_ticks : 1;
    phase = s_ticks % period_ticks;
    pos = (uint16_t)(phase * group->indices_count / period_ticks);

    /* Reverse just mirrors the position across the group, which keeps the
     * motion continuous across the seam: as the phase runs a full period,
     * a mirrored position sweeps from the last array slot down to 0. */
    if (group->spot_dir == LED_GROUP_SPOT_REVERSE) {
        pos = (uint16_t)(group->indices_count - 1u - pos);
    }

    return pos;
}

/* The color of a single pixel, identified by its position in the group's
 * indices array. Every state resolves through here. The four uniform
 * states ignore the index - their answer is the same for every pixel in
 * the group - but SPOT is per-pixel, which is why the index is a
 * parameter at all. */
static void led_group_effective_color(const led_group_t *group, uint16_t i,
                                       uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    switch (group->state) {
    case LED_GROUP_ON:
        *out_r = group->scaled_r;
        *out_g = group->scaled_g;
        *out_b = group->scaled_b;
        break;

    case LED_GROUP_BREATHING: {
        /* period_ticks is a public setter's input; guard the mod/div below
         * against a caller passing 0, which would otherwise be a
         * divide-by-zero crash on every embedded target this runs on.
         *
         * Phase derives from the shared tick counter, not state entry, so
         * every group sharing a period breathes in lockstep no matter when
         * each one entered the state. */
        uint32_t period_ticks = group->period_ticks != 0 ? group->period_ticks : 1;
        uint32_t phase = s_ticks % period_ticks;
        uint32_t idx = phase * BREATHE_LUT_SIZE / period_ticks;
        uint8_t factor = breathe_lut[idx];
        *out_r = (uint8_t)((uint16_t)group->scaled_r * factor / 100);
        *out_g = (uint8_t)((uint16_t)group->scaled_g * factor / 100);
        *out_b = (uint8_t)((uint16_t)group->scaled_b * factor / 100);
        break;
    }

    case LED_GROUP_BLINK: {
        /* Shared-counter phase, same as BREATHING - synced across groups. */
        uint32_t period_ticks = group->period_ticks != 0 ? group->period_ticks : 1;
        bool on = (s_ticks % period_ticks) < period_ticks / 2;
        if (on) {
            *out_r = group->scaled_r;
            *out_g = group->scaled_g;
            *out_b = group->scaled_b;
        } else {
            *out_r = 0;
            *out_g = 0;
            *out_b = 0;
        }
        break;
    }

    case LED_GROUP_BLINK_CODE: {
        uint32_t period_ticks = group->period_ticks != 0 ? group->period_ticks : 1;
        uint32_t burst_ticks = (uint32_t)group->blink_code_count * period_ticks;
        uint32_t cycle_ticks = burst_ticks + group->blink_code_pause_ticks;
        /* Both blink_code_count and blink_code_pause_ticks default to 0, so a
         * caller that forgets to call set_blink_code() before entering this
         * state would otherwise hit cycle_ticks == 0 here - guard the same
         * way period_ticks is guarded above. */
        if (cycle_ticks == 0) {
            cycle_ticks = 1;
        }
        /* Unlike BREATHING/BLINK, a blink code anchors to state entry so the
         * code always plays from its first blink, never from mid-cycle. */
        uint32_t t = (s_ticks - group->state_entered_tick) % cycle_ticks;
        if (t < burst_ticks) {
            uint32_t phase = t % period_ticks;
            bool on = phase < period_ticks / 2;
            if (on) {
                *out_r = group->scaled_r;
                *out_g = group->scaled_g;
                *out_b = group->scaled_b;
            } else {
                *out_r = 0;
                *out_g = 0;
                *out_b = 0;
            }
        } else {
            *out_r = 0;
            *out_g = 0;
            *out_b = 0;
        }
        break;
    }

    case LED_GROUP_SPOT: {
        /* indices_count is never 0 here - led_group_update() returns
         * before calling this when the group is empty, which is what
         * keeps the modulo below safe. */
        uint16_t count = group->indices_count;
        uint16_t pos = led_group_spot_pos(group);
        /* Offset back from the head, wrapping at the end of the group:
         * 0 is the head, the tail runs to lower positions, and anything
         * past the tail is background. spot_size == 0 makes this
         * comparison false for every pixel, which is both what "no spot"
         * means and what keeps the weight division unreachable. */
        uint16_t offset = (uint16_t)((pos + count - i) % count);
        if (offset < group->spot_size) {
            /* Brightest in the middle, fading symmetrically toward both
             * ends, so the spot reads the same whichever way it travels.
             *
             * Distances are doubled throughout so an even spot_size - whose
             * middle falls between two pixels - stays exact in integer
             * arithmetic. span2 is the doubled distance between the two end
             * pixels, d2 the doubled distance from this pixel to the middle,
             * and ring the whole-pixel step count outward from there.
             *
             * levels is how many distinct brightness steps there are, and
             * (spot_size / 2 + 1 - ring) counts down from it, so the middle
             * always lands on exactly 100 and each end pixel on 100/levels -
             * never 0, which keeps the spot's edges visible against the
             * background. An even spot_size has no single middle pixel, so
             * the peak plateaus across the middle two. */
            uint16_t span2 = (uint16_t)(group->spot_size - 1u);
            uint16_t o2 = (uint16_t)(offset * 2u);
            uint16_t d2 = o2 >= span2 ? (uint16_t)(o2 - span2)
                                      : (uint16_t)(span2 - o2);
            uint16_t ring = (uint16_t)((d2 + 1u) / 2u);
            uint16_t levels = (uint16_t)((group->spot_size + 1u) / 2u);
            uint8_t w = (uint8_t)((uint32_t)(group->spot_size / 2u + 1u - ring)
                                   * 100u / levels);
            *out_r = led_group_blend(group->scaled_r, group->scaled_amb_r, w);
            *out_g = led_group_blend(group->scaled_g, group->scaled_amb_g, w);
            *out_b = led_group_blend(group->scaled_b, group->scaled_amb_b, w);
        } else {
            *out_r = group->scaled_amb_r;
            *out_g = group->scaled_amb_g;
            *out_b = group->scaled_amb_b;
        }
        break;
    }

    case LED_GROUP_OFF:
    default:
        *out_r = 0;
        *out_g = 0;
        *out_b = 0;
        break;
    }
}

void led_group_update(led_group_t *group)
{
    uint8_t probe_r, probe_g, probe_b;
    uint16_t pos;
    uint16_t i;

    /* Nothing to render, and it is also what keeps the SPOT case's modulo
     * safe against a zero-length group. */
    if (group->indices_count == 0) {
        return;
    }

    /* Change detection has to cover both shapes of effect. A uniform state
     * is fully described by its resolved color, so pixel 0's color speaks
     * for the whole group. The spot needs its head position as well: while
     * the spot sits away from pixel 0 that pixel stays at the ambient
     * color for many ticks, even though the strip as a whole changes every
     * few ticks. For a uniform state the position is always 0 (see
     * led_group_spot_pos), so including it costs nothing. */
    pos = led_group_spot_pos(group);
    led_group_effective_color(group, 0, &probe_r, &probe_g, &probe_b);

    if (!group->dirty && pos == group->last_spot_pos &&
        probe_r == group->last_r && probe_g == group->last_g &&
        probe_b == group->last_b) {
        return;
    }

    for (i = 0; i < group->indices_count; i++) {
        uint8_t r, g, b;
        led_group_effective_color(group, i, &r, &g, &b);
        group->strip->write_pixel(group->indices[i], r, g, b, group->strip->ctx);
    }

    group->last_r = probe_r;
    group->last_g = probe_g;
    group->last_b = probe_b;
    group->last_spot_pos = pos;
    group->dirty = false;
}
