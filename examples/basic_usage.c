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

/* Stands in for the platform clock (millis()/HAL_GetTick()) that the library
 * reads through led_group_attach_ms_timer(). */
static uint32_t fake_now_ms;

static uint32_t fake_clock(void)
{
    return fake_now_ms;
}

int main(void)
{
    led_strip_t strip = { console_write_pixel, NULL };

    led_group_attach_ms_timer(fake_clock);

    static const uint16_t status_indices[] = { 0 };
    static const uint16_t error_indices[] = { 1, 2 };

    led_group_t status_led;
    led_group_t error_led;

    led_group_init(&status_led, &strip, status_indices, 1);
    led_group_init(&error_led, &strip, error_indices, 2);

    /* Status LED breathes green to show the device is idle and ready. */
    led_group_set_color(&status_led, COLOR_READY);
    led_group_set_brightness_pct(&status_led, 50);
    led_group_set_period_ms(&status_led, 2000);
    led_group_set_state(&status_led, LED_GROUP_BREATHING);

    /* Error LED signals "error code 2": 2 blinks, then a long pause, on repeat. */
    led_group_set_color(&error_led, COLOR_ERROR);
    led_group_set_period_ms(&error_led, 300);
    led_group_set_blink_code(&error_led, 2, 1000);
    led_group_set_state(&error_led, LED_GROUP_BLINK_CODE);

    /* The attached timer is the library's only view of time - stepping the
     * fake clock here drives every effect. */
    for (fake_now_ms = 0; fake_now_ms <= 1000; fake_now_ms += 100) {
        printf("-- now_ms = %u --\n", fake_now_ms);
        led_group_update(&status_led);
        led_group_update(&error_led);
    }

    return 0;
}
