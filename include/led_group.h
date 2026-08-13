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
} led_group_state_t;

typedef void (*led_group_write_pixel_fn)(uint16_t index, uint8_t r, uint8_t g,
                                          uint8_t b, void *ctx);

/* Millisecond clock source (e.g. Arduino's millis(), STM32's HAL_GetTick()).
 * Attached once, library-wide - every group reads time through it. */
typedef uint32_t (*led_group_ms_fn)(void);

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
    uint32_t state_entered_ms;
    uint32_t period_ms;
    uint8_t blink_code_count;
    uint32_t blink_code_pause_ms;

    uint8_t last_r, last_g, last_b;
    bool dirty;
} led_group_t;

/* Attach the millisecond clock the library reads time from. Call once at
 * startup, before any led_group_set_state()/led_group_update(). With no
 * timer attached the clock reads as a constant 0 (effects freeze at their
 * phase-0 color rather than crashing). */
void led_group_attach_ms_timer(led_group_ms_fn ms_fn);

void led_group_init(led_group_t *group, const led_strip_t *strip,
                     const uint16_t *indices, uint16_t indices_count);

void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b);
void led_group_set_brightness_pct(led_group_t *group, uint8_t pct);
void led_group_set_period_ms(led_group_t *group, uint32_t period_ms);
void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ms);

void led_group_set_state(led_group_t *group, led_group_state_t state);
led_group_state_t led_group_get_state(const led_group_t *group);

void led_group_update(led_group_t *group);

#ifdef __cplusplus
}
#endif

#endif /* LED_GROUP_H */
