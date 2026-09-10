#ifndef LED_GROUP_H
#define LED_GROUP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_GROUP_OFF,
    LED_GROUP_ON,
    LED_GROUP_BREATHING,
    LED_GROUP_BLINK,
    LED_GROUP_BLINK_CODE,
    LED_GROUP_SPOT,
} led_group_state_t;

typedef void (*led_group_write_pixel_fn)(uint16_t index, uint8_t r, uint8_t g,
                                          uint8_t b, void *ctx);

/* One instance per physical strip/driver. Shared by every led_group_t that
 * lives on that strip - the write function is a property of the driver,
 * not of any individual logical LED group. */
typedef struct {
    led_group_write_pixel_fn write_pixel;
    void *ctx;
} led_strip_t;

typedef struct {
    const led_strip_t *strip;
    const uint16_t *indices;
    uint16_t indices_count;

    uint8_t base_r, base_g, base_b;
    uint8_t brightness_pct;
    uint8_t scaled_r, scaled_g, scaled_b;

    led_group_state_t state;
    uint32_t state_entered_tick;
    uint32_t period_ticks;
    uint8_t blink_code_count;
    uint32_t blink_code_pause_ticks;

    /* LED_GROUP_SPOT only. The spot's own color is the group's base
     * color; these are the background it fades into and its length.
     * Ambient is kept both as given and pre-scaled, for the same reason
     * the spot color is: brightness_pct can change at any time, so the
     * caller's value has to survive in order to be re-scaled. */
    uint16_t spot_size;
    uint8_t amb_r, amb_g, amb_b;
    uint8_t scaled_amb_r, scaled_amb_g, scaled_amb_b;
    uint16_t last_spot_pos;

    uint8_t last_r, last_g, last_b;
    bool dirty;
} led_group_t;

/* The library has no clock - time is a tick counter shared by every group.
 * Call led_group_tick() exactly once per rendering pass (e.g. every 10ms),
 * then led_group_update() for each group in that same pass. All durations
 * (periods, pauses) are expressed in those ticks, so at a 10ms cadence a
 * period of 100 ticks is one second. Effect tempo tracks the call cadence:
 * if the caller's loop stalls, effects stretch rather than skip ahead. */
void led_group_tick(void);

void led_group_init(led_group_t *group, const led_strip_t *strip,
                     const uint16_t *indices, uint16_t indices_count);

void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b);
void led_group_set_brightness_pct(led_group_t *group, uint8_t pct);
void led_group_set_period_ticks(led_group_t *group, uint32_t period_ticks);
void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ticks);

/* LED_GROUP_SPOT: the spot's own color is the group's base color (set with
 * led_group_set_color, so brightness_pct scales it like every other
 * state); this adds the ambient background the tail fades into, and the
 * spot's length in pixels including the head. spot_size clamps to the
 * group's indices_count, and spot_size == 0 means no spot at all - the
 * group renders pure ambient. Travel speed is period_ticks: one period is
 * one full traversal of the group. */
void led_group_set_spot(led_group_t *group, uint16_t spot_size,
                         uint8_t amb_r, uint8_t amb_g, uint8_t amb_b);

void led_group_set_state(led_group_t *group, led_group_state_t state);
led_group_state_t led_group_get_state(const led_group_t *group);

void led_group_update(led_group_t *group);

#ifdef __cplusplus
}
#endif

#endif /* LED_GROUP_H */
