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

static void led_group_recompute_scaled(led_group_t *group)
{
    group->scaled_r = (uint8_t)((uint16_t)group->base_r * group->brightness_pct / 100);
    group->scaled_g = (uint8_t)((uint16_t)group->base_g * group->brightness_pct / 100);
    group->scaled_b = (uint8_t)((uint16_t)group->base_b * group->brightness_pct / 100);
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
    group->state_entered_ms = 0;
    group->period_ms = 1000;
    group->blink_code_count = 0;
    group->blink_code_pause_ms = 0;

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

void led_group_set_period_ms(led_group_t *group, uint32_t period_ms)
{
    group->period_ms = period_ms;
}

void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ms)
{
    (void)group;
    (void)count;
    (void)pause_ms;
}

void led_group_set_state(led_group_t *group, led_group_state_t state, uint32_t now_ms)
{
    group->state = state;
    group->state_entered_ms = now_ms;
}

led_group_state_t led_group_get_state(const led_group_t *group)
{
    return group->state;
}

static void led_group_effective_color(const led_group_t *group, uint32_t now_ms,
                                       uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    switch (group->state) {
    case LED_GROUP_ON:
        *out_r = group->scaled_r;
        *out_g = group->scaled_g;
        *out_b = group->scaled_b;
        break;

    case LED_GROUP_BREATHING: {
        /* period_ms is a public setter's input; guard the mod/div below
         * against a caller passing 0, which would otherwise be a
         * divide-by-zero crash on every embedded target this runs on. */
        uint32_t period_ms = group->period_ms != 0 ? group->period_ms : 1;
        uint32_t phase_ms = (now_ms - group->state_entered_ms) % period_ms;
        uint32_t idx = phase_ms * BREATHE_LUT_SIZE / period_ms;
        uint8_t factor = breathe_lut[idx];
        *out_r = (uint8_t)((uint16_t)group->scaled_r * factor / 100);
        *out_g = (uint8_t)((uint16_t)group->scaled_g * factor / 100);
        *out_b = (uint8_t)((uint16_t)group->scaled_b * factor / 100);
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

void led_group_update(led_group_t *group, uint32_t now_ms)
{
    uint8_t r, g, b;
    led_group_effective_color(group, now_ms, &r, &g, &b);

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
