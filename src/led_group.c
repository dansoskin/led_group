#include "led_group.h"

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
    (void)group;
    (void)r;
    (void)g;
    (void)b;
}

void led_group_set_brightness_pct(led_group_t *group, uint8_t pct)
{
    (void)group;
    (void)pct;
}

void led_group_set_period_ms(led_group_t *group, uint32_t period_ms)
{
    (void)group;
    (void)period_ms;
}

void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ms)
{
    (void)group;
    (void)count;
    (void)pause_ms;
}

void led_group_set_state(led_group_t *group, led_group_state_t state, uint32_t now_ms)
{
    (void)group;
    (void)state;
    (void)now_ms;
}

led_group_state_t led_group_get_state(const led_group_t *group)
{
    return group->state;
}

static void led_group_effective_color(const led_group_t *group, uint32_t now_ms,
                                       uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    (void)now_ms;
    switch (group->state) {
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
