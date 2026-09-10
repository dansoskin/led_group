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
}

void led_group_set_brightness_pct(led_group_t *group, uint8_t pct)
{
    group->brightness_pct = pct > 100 ? 100 : pct;
    led_group_recompute_scaled(group);
}

void led_group_set_period_ticks(led_group_t *group, uint32_t period_ticks)
{
    group->period_ticks = period_ticks;
}

void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ticks)
{
    group->blink_code_count = count;
    group->blink_code_pause_ticks = pause_ticks;
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
}

void led_group_set_state(led_group_t *group, led_group_state_t state)
{
    group->state = state;
    group->state_entered_tick = s_ticks;
}

led_group_state_t led_group_get_state(const led_group_t *group)
{
    return group->state;
}

static void led_group_effective_color(const led_group_t *group,
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

    case LED_GROUP_OFF:
    default:
        *out_r = 0;
        *out_g = 0;
        *out_b = 0;
        break;
    }
}

/* Blend fg toward bg by an integer percentage: w == 100 is pure fg,
 * w == 0 pure bg. The uint16_t intermediates keep the products in range
 * (the worst case is 255 * 100 == 25500). Callers must pass w <= 100,
 * which the weight formula in led_group_render_spot() guarantees. */
static uint8_t led_group_blend(uint8_t fg, uint8_t bg, uint8_t w)
{
    return (uint8_t)(((uint16_t)fg * w + (uint16_t)bg * (uint8_t)(100 - w)) / 100);
}

/* The head's position within the group's indices array. Phase comes off
 * the shared tick counter, like BREATHING/BLINK, so groups sharing a
 * period travel in lockstep; period_ticks is guarded against 0 the same
 * way BREATHING guards it. One period is one full traversal. */
static uint16_t led_group_spot_pos(const led_group_t *group)
{
    uint32_t period_ticks = group->period_ticks != 0 ? group->period_ticks : 1;
    uint32_t phase = s_ticks % period_ticks;

    return (uint16_t)(phase * group->indices_count / period_ticks);
}

/* The one state whose pixels are not all the same color, so it renders
 * per-pixel and tracks its own change detection (head position) rather
 * than the group-wide last_r/last_g/last_b. */
static void led_group_render_spot(led_group_t *group)
{
    uint16_t count = group->indices_count;
    uint16_t pos;
    uint16_t i;

    if (count == 0) {
        return;
    }

    pos = led_group_spot_pos(group);

    if (!group->dirty && pos == group->last_spot_pos) {
        return;
    }

    for (i = 0; i < count; i++) {
        /* Offset back from the head, wrapping at the end of the group:
         * 0 is the head, the tail runs to lower positions, and anything
         * past the tail is background. spot_size == 0 makes this
         * comparison false for every pixel, which is both what "no spot"
         * means and what keeps the division below unreachable. */
        uint16_t offset = (uint16_t)((pos + count - i) % count);
        uint8_t r, g, b;

        if (offset < group->spot_size) {
            /* Falls linearly from 100% at the head to 100/spot_size at
             * the last tail pixel - never to 0, so the trailing edge
             * stays visible instead of vanishing into the background. */
            uint8_t w = (uint8_t)((uint32_t)(group->spot_size - offset) * 100u
                                   / group->spot_size);
            r = led_group_blend(group->scaled_r, group->scaled_amb_r, w);
            g = led_group_blend(group->scaled_g, group->scaled_amb_g, w);
            b = led_group_blend(group->scaled_b, group->scaled_amb_b, w);
        } else {
            r = group->scaled_amb_r;
            g = group->scaled_amb_g;
            b = group->scaled_amb_b;
        }

        group->strip->write_pixel(group->indices[i], r, g, b, group->strip->ctx);
    }

    group->last_spot_pos = pos;
    group->dirty = false;
}

void led_group_update(led_group_t *group)
{
    uint8_t r, g, b;

    if (group->state == LED_GROUP_SPOT) {
        led_group_render_spot(group);
        return;
    }

    led_group_effective_color(group, &r, &g, &b);

    if (group->dirty || r != group->last_r || g != group->last_g || b != group->last_b) {
        uint16_t i;
        for (i = 0; i < group->indices_count; i++) {
            group->strip->write_pixel(group->indices[i], r, g, b, group->strip->ctx);
        }
        group->last_r = r;
        group->last_g = g;
        group->last_b = b;
        group->dirty = false;
    }
}
