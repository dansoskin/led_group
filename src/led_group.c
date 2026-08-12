#include "led_group.h"

void led_group_init(led_group_t *group, const led_strip_t *strip,
                     const uint16_t *indices, uint16_t indices_count)
{
    (void)group;
    (void)strip;
    (void)indices;
    (void)indices_count;
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
    (void)group;
    return LED_GROUP_OFF;
}

void led_group_update(led_group_t *group, uint32_t now_ms)
{
    (void)group;
    (void)now_ms;
}
