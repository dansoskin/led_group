// FastLED wiring example for led_group.
//
// This is the same consumer-side pattern as examples/basic_usage.c (named
// groups, project color macros, a write_pixel callback) but wired to a real
// driver instead of a printf stand-in - mirroring leds.h/leds.cpp from the
// biostaq_dispenser/cannadorf_v2 projects this library was extracted from.
//
// Requires the FastLED library (Arduino Library Manager, or
// `lib_deps = FastLED` in platformio.ini) in addition to led_group.

#include <Arduino.h>
#include <FastLED.h>
#include <led_group.h>

#define NUM_LEDS 6
#define DATA_PIN 18

CRGB leds[NUM_LEDS];

#define COLOR_READY 0, 255, 0
#define COLOR_ERROR 255, 0, 0

// Stands in for the library's write_pixel callback - this is the one line
// that's specific to FastLED. Swapping drivers (e.g. Adafruit_NeoPixel) only
// ever means rewriting this one function.
static void fastled_write_pixel(uint16_t index, uint8_t r, uint8_t g, uint8_t b, void *ctx)
{
    (void)ctx;
    leds[index] = CRGB(r, g, b);
}

static led_strip_t strip = { fastled_write_pixel, NULL };

// millis() returns unsigned long, which isn't the same type as uint32_t on
// every Arduino core - a one-line wrapper keeps the attach type-exact.
static uint32_t arduino_millis(void)
{
    return (uint32_t)millis();
}

static const uint16_t status_indices[] = { 0 };
static const uint16_t error_indices[] = { 1, 2 };

static led_group_t status_led;
static led_group_t error_led;

void setup()
{
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

    led_group_attach_ms_timer(arduino_millis);

    led_group_init(&status_led, &strip, status_indices, 1);
    led_group_init(&error_led, &strip, error_indices, 2);

    // Status LED breathes green to show the device is idle and ready.
    led_group_set_color(&status_led, COLOR_READY);
    led_group_set_brightness_pct(&status_led, 50);
    led_group_set_period_ms(&status_led, 2000);
    led_group_set_state(&status_led, LED_GROUP_BREATHING);

    // Error LED signals "error code 2": 2 blinks, then a long pause, on repeat.
    led_group_set_color(&error_led, COLOR_ERROR);
    led_group_set_period_ms(&error_led, 300);
    led_group_set_blink_code(&error_led, 2, 1000);
    led_group_set_state(&error_led, LED_GROUP_BLINK_CODE);
}

void loop()
{
    static uint32_t leds_timer = 0;
    uint32_t now = millis();

    if (now - leds_timer >= 10) {
        leds_timer = now;

        led_group_update(&status_led);
        led_group_update(&error_led);

        FastLED.show();
    }
}
