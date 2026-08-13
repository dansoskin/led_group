#include "led_group.h"

#include <stdio.h>

/* Example consuming-project glue code - this is what leds.h/leds.c would
 * look like in a project using led_group instead of the original
 * LightenObject (see biostaq_dispenser/cannadorf_v2's lib/leds/). Named
 * groups, project-specific color macros, and the write_pixel callback all
 * live here, in the consuming project - never in the library itself. */

#define COLOR_READY 0, 255, 0
#define COLOR_ERROR 255, 0, 0

/* Stands in for a real driver callback (e.g. one that calls FastLED's
 * `leds[index] = CRGB(r, g, b)` or Adafruit_NeoPixel's
 * `pixels->setPixelColor(index, pixels->Color(r, g, b))`). */
static void console_write_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b, void *ctx)
{
    (void)ctx;
    printf("pixel[%u] = (%u, %u, %u)\n", index, r, g, b);
}

int main(void)
{
    led_strip_t strip = { console_write_pixel, NULL };

    static const uint16_t status_indices[] = { 0 };
    static const uint16_t error_indices[] = { 1, 2 };

    led_group_t status_led;
    led_group_t error_led;

    led_group_init(&status_led, &strip, status_indices, 1);
    led_group_init(&error_led, &strip, error_indices, 2);

    /* Status LED breathes green to show the device is idle and ready.
     * Durations are in ticks: at a 10ms rendering cadence, 200 ticks = 2s. */
    led_group_set_color(&status_led, COLOR_READY);
    led_group_set_brightness_pct(&status_led, 50);
    led_group_set_period_ticks(&status_led, 200);
    led_group_set_state(&status_led, LED_GROUP_BREATHING);

    /* Error LED signals "error code 2": 2 blinks, then a long pause, on repeat. */
    led_group_set_color(&error_led, COLOR_ERROR);
    led_group_set_period_ticks(&error_led, 30);
    led_group_set_blink_code(&error_led, 2, 100);
    led_group_set_state(&error_led, LED_GROUP_BLINK_CODE);

    /* The library has no clock - led_group_tick(), called once per rendering
     * pass, is its only view of time. On hardware the loop below would run
     * every 10ms; here it just runs flat out. */
    for (uint32_t tick = 0; tick <= 100; tick++) {
        if (tick % 10 == 0) {
            printf("-- tick %u --\n", tick);
        }
        led_group_update(&status_led);
        led_group_update(&error_led);
        led_group_tick();
    }

    return 0;
}
