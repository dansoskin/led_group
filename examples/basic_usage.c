/* A runnable tour of everything led_group does, on one 38-LED strip.
 *
 * This is example consuming-project glue code - what leds.h/leds.c would
 * look like in a project using led_group (see biostaq_dispenser and
 * cannadorf_v2's lib/leds/, or 164_traps_motor's Core/Src/leds.c). Named
 * groups, project-specific color macros, and the write_pixel callback all
 * live here, in the consuming project - never in the library itself.
 *
 * The strip is carved into nine non-overlapping groups, one per effect
 * state, so every state is visible side by side. Each group's setup below
 * carries a short note on what that state does and which setters it needs.
 *
 * Build and run (from the repo root):
 *     cmake -S . -B build && cmake --build build
 *     ./build/examples/led_group_example
 */

#include "led_group.h"

#include <stdio.h>

#define STRIP_LEN 38

/* Project colors. The macros expand to three arguments, which is why they
 * drop straight into set_color and set_spot. */
#define COLOR_READY   0, 255, 0
#define COLOR_ERROR   255, 0, 0
#define COLOR_BLUE    0, 80, 255
#define COLOR_ORANGE  255, 60, 0
#define COLOR_AMBIENT 10, 10, 10   /* background for the moving effects */

/* The driver's frame buffer. A real project hands these bytes to FastLED
 * (leds[index] = CRGB(r, g, b)), Adafruit_NeoPixel (setPixelColor(...)) or
 * a WS2812B DMA buffer, then flushes the whole strip once per pass. */
static uint8_t fb_r[STRIP_LEN], fb_g[STRIP_LEN], fb_b[STRIP_LEN];

static void buffer_write_pixel(uint16_t index, uint8_t r, uint8_t g,
                                uint8_t b, void *ctx)
{
    (void)ctx;
    fb_r[index] = r;
    fb_g[index] = g;
    fb_b[index] = b;
}

/* One character per LED, by brightness, so the effects are actually
 * visible in a terminal: ' ' is off and '#' is full. */
static char shade(uint16_t index)
{
    unsigned v = ((unsigned)fb_r[index] + fb_g[index] + fb_b[index]) / 3u;

    if (v == 0)   return ' ';
    if (v < 8)    return '.';
    if (v < 40)   return ':';
    if (v < 100)  return 'o';
    if (v < 180)  return 'O';
    return '#';
}

int main(void)
{
    led_strip_t strip = { buffer_write_pixel, NULL };

    /* Every group is a slice of the one strip, and they never overlap -
     * two groups sharing an index would fight over it every pass. */
    static const uint16_t on_idx[]      = { 0 };
    static const uint16_t off_idx[]     = { 1 };
    static const uint16_t blink_idx[]   = { 2 };
    static const uint16_t breathe_idx[] = { 3 };
    static const uint16_t code_idx[]    = { 4, 5 };
    static const uint16_t spot_f_idx[]  = { 6, 7, 8, 9, 10, 11, 12, 13 };
    static const uint16_t spot_b_idx[]  = { 14, 15, 16, 17, 18, 19, 20, 21 };
    static const uint16_t comet_f_idx[] = { 22, 23, 24, 25, 26, 27, 28, 29 };
    static const uint16_t comet_b_idx[] = { 30, 31, 32, 33, 34, 35, 36, 37 };

    led_group_t power, spare, heartbeat, status, error;
    led_group_t spot_f, spot_b, comet_f, comet_b;

    /* Groups printed as one labelled segment each. */
    struct { const char *name; uint16_t first, len; } segs[] = {
        { "ON",         0,  1 },
        { "OFF",        1,  1 },
        { "BLINK",      2,  1 },
        { "BREATHING",  3,  1 },
        { "BLINK_CODE", 4,  2 },
        { "SPOT_FWD",   6,  8 },
        { "SPOT_BWD",   14, 8 },
        { "COMET_FWD",  22, 8 },
        { "COMET_BWD",  30, 8 },
    };
    const size_t seg_count = sizeof segs / sizeof segs[0];

    uint32_t tick;
    size_t s;

    led_group_init(&power,     &strip, on_idx,      1);
    led_group_init(&spare,     &strip, off_idx,     1);
    led_group_init(&heartbeat, &strip, blink_idx,   1);
    led_group_init(&status,    &strip, breathe_idx, 1);
    led_group_init(&error,     &strip, code_idx,    2);
    led_group_init(&spot_f,    &strip, spot_f_idx,  8);
    led_group_init(&spot_b,    &strip, spot_b_idx,  8);
    led_group_init(&comet_f,   &strip, comet_f_idx, 8);
    led_group_init(&comet_b,   &strip, comet_b_idx, 8);

    /* LED_GROUP_ON - solid color, the simplest state. Needs only a color. */
    led_group_set_color(&power, COLOR_READY);
    led_group_set_state(&power, LED_GROUP_ON);

    /* LED_GROUP_OFF - dark, and the state every group starts in. A group
     * left like this still gets written once, so the driver's buffer is
     * cleared rather than left holding whatever was there before. */
    led_group_set_state(&spare, LED_GROUP_OFF);

    /* LED_GROUP_BLINK - hard on/off, half the period each. Groups sharing
     * a period blink in lockstep however staggered their entry was,
     * because the phase comes off the shared tick counter. */
    led_group_set_color(&heartbeat, COLOR_BLUE);
    led_group_set_period_ticks(&heartbeat, 20);
    led_group_set_state(&heartbeat, LED_GROUP_BLINK);

    /* LED_GROUP_BREATHING - smooth pulse through an internal curve.
     * set_brightness_pct caps the whole group; it always recomputes from
     * the color as given, so calling it repeatedly cannot compound. */
    led_group_set_color(&status, COLOR_READY);
    led_group_set_brightness_pct(&status, 60);
    led_group_set_period_ticks(&status, 40);
    led_group_set_state(&status, LED_GROUP_BREATHING);

    /* LED_GROUP_BLINK_CODE - "error 2": two blinks, a pause, repeat. This
     * is the one state that anchors to when it was entered rather than to
     * the shared counter, so a code always plays from its first blink. */
    led_group_set_color(&error, COLOR_ERROR);
    led_group_set_period_ticks(&error, 10);
    led_group_set_blink_code(&error, 2, 30);
    led_group_set_state(&error, LED_GROUP_BLINK_CODE);

    /* The four moving states all take their length and background from
     * set_spot, their color from set_color, and their speed from
     * set_period_ticks - one period is one full lap of the group.
     *
     * LED_GROUP_SPOT_* is brightest in its MIDDLE, fading toward both
     * ends, so it looks the same whichever way it travels. */
    led_group_set_color(&spot_f, COLOR_ORANGE);
    led_group_set_spot(&spot_f, 4, COLOR_AMBIENT);
    led_group_set_period_ticks(&spot_f, 80);
    led_group_set_state(&spot_f, LED_GROUP_SPOT_FORWARD);

    /* ...and _BACKWARD is the same shape running the other way, toward
     * lower positions in the group's index array. */
    led_group_set_color(&spot_b, COLOR_ORANGE);
    led_group_set_spot(&spot_b, 4, COLOR_AMBIENT);
    led_group_set_period_ticks(&spot_b, 80);
    led_group_set_state(&spot_b, LED_GROUP_SPOT_BACKWARD);

    /* LED_GROUP_COMET_* is brightest at its LEADING pixel and fades back
     * along the tail behind it, so unlike the spot its direction shows in
     * its shape. Watch which end of the segment is bright below. */
    led_group_set_color(&comet_f, COLOR_BLUE);
    led_group_set_spot(&comet_f, 5, COLOR_AMBIENT);
    led_group_set_period_ticks(&comet_f, 80);
    led_group_set_state(&comet_f, LED_GROUP_COMET_FORWARD);

    led_group_set_color(&comet_b, COLOR_BLUE);
    led_group_set_spot(&comet_b, 5, COLOR_AMBIENT);
    led_group_set_period_ticks(&comet_b, 80);
    led_group_set_state(&comet_b, LED_GROUP_COMET_BACKWARD);

    /* get_state reads back what was set - handy for "am I already showing
     * an error?" checks in application code. */
    printf("error group state = %d (LED_GROUP_BLINK_CODE = %d)\n\n",
           (int)led_group_get_state(&error), (int)LED_GROUP_BLINK_CODE);

    printf("brightness: ' '=off  .  :  o  O  #=full\n\n     ");
    for (s = 0; s < seg_count; s++) {
        printf("|%-*s", (int)segs[s].len, segs[s].name);
    }
    printf("|\n");

    /* The library has no clock - led_group_tick(), called once per
     * rendering pass, is its only view of time, and every duration is a
     * count of those ticks. On hardware this loop would run every 10ms,
     * so the 80-tick lap below is 0.8s; here it just runs flat out.
     *
     * The order matters: update every group for the current tick, flush
     * the strip once, then advance the counter. */
    for (tick = 0; tick <= 80; tick++) {
        led_group_update(&power);
        led_group_update(&spare);
        led_group_update(&heartbeat);
        led_group_update(&status);
        led_group_update(&error);
        led_group_update(&spot_f);
        led_group_update(&spot_b);
        led_group_update(&comet_f);
        led_group_update(&comet_b);

        /* Where a real driver would push the buffer to the strip. */
        if (tick % 4 == 0) {
            printf("t=%2lu ", (unsigned long)tick);
            for (s = 0; s < seg_count; s++) {
                uint16_t i;
                putchar('|');
                for (i = 0; i < segs[s].len; i++) {
                    putchar(shade((uint16_t)(segs[s].first + i)));
                }
            }
            printf("|\n");
        }

        led_group_tick();
    }

    return 0;
}
