#include "led_group.h"
#include <assert.h>
#include <stddef.h>

#define MAX_RECORDED_CALLS 64

typedef struct {
    uint16_t index;
    uint8_t r, g, b;
} recorded_call_t;

typedef struct {
    recorded_call_t calls[MAX_RECORDED_CALLS];
    size_t count;
} call_recorder_t;

static void recording_write_pixel(uint16_t index, uint8_t r, uint8_t g,
                                   uint8_t b, void *ctx)
{
    call_recorder_t *rec = (call_recorder_t *)ctx;
    assert(rec->count < MAX_RECORDED_CALLS);
    rec->calls[rec->count].index = index;
    rec->calls[rec->count].r = r;
    rec->calls[rec->count].g = g;
    rec->calls[rec->count].b = b;
    rec->count++;
}

static void recorder_reset(call_recorder_t *rec)
{
    rec->count = 0;
}

/* The library's tick counter is shared across all tests in this process, so
 * every advance goes through this helper, which mirrors the count. Tests
 * that assert exact LUT/phase values first align to phase 0 of their period
 * instead of assuming the counter starts at 0. */
static uint32_t g_ticks;

static void advance(uint32_t n)
{
    g_ticks += n;
    while (n--) {
        led_group_tick();
    }
}

static void align_to_phase0(uint32_t period_ticks)
{
    uint32_t rem = g_ticks % period_ticks;
    if (rem) {
        advance(period_ticks - rem);
    }
}

static void test_off_writes_black_once_via_dirty_flag(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 5 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 1);
    assert(led_group_get_state(&group) == LED_GROUP_OFF);

    led_group_update(&group);
    assert(rec.count == 1);
    assert(rec.calls[0].index == 5);
    assert(rec.calls[0].r == 0);
    assert(rec.calls[0].g == 0);
    assert(rec.calls[0].b == 0);

    advance(1);
    led_group_update(&group);
    assert(rec.count == 1); /* no change -> no second write */
}

static void test_on_writes_scaled_color_and_fans_out_to_all_indices(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 3, 5 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 2);
    led_group_update(&group); /* consume the initial OFF dirty write */
    recorder_reset(&rec);

    led_group_set_color(&group, 10, 20, 30);
    led_group_set_state(&group, LED_GROUP_ON);
    assert(led_group_get_state(&group) == LED_GROUP_ON);

    led_group_update(&group);
    assert(rec.count == 2);
    assert(rec.calls[0].index == 3);
    assert(rec.calls[0].r == 10 && rec.calls[0].g == 20 && rec.calls[0].b == 30);
    assert(rec.calls[1].index == 5);
    assert(rec.calls[1].r == 10 && rec.calls[1].g == 20 && rec.calls[1].b == 30);

    advance(10);
    led_group_update(&group);
    assert(rec.count == 2); /* unchanged color -> no new writes */

    led_group_set_state(&group, LED_GROUP_OFF);
    led_group_update(&group);
    assert(rec.count == 4);
    assert(rec.calls[2].r == 0 && rec.calls[2].g == 0 && rec.calls[2].b == 0);
    assert(rec.calls[3].r == 0 && rec.calls[3].g == 0 && rec.calls[3].b == 0);
}

static void test_brightness_does_not_compound_on_repeated_calls(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 1);
    led_group_set_color(&group, 200, 100, 50);

    led_group_set_brightness_pct(&group, 50);
    assert(group.scaled_r == 100 && group.scaled_g == 50 && group.scaled_b == 25);

    /* Calling the same brightness again must NOT re-scale the already-scaled
     * value (100 * 50 / 100 = 50, the historical bug) - it must recompute
     * from the untouched base color every time. */
    led_group_set_brightness_pct(&group, 50);
    assert(group.scaled_r == 100 && group.scaled_g == 50 && group.scaled_b == 25);

    led_group_set_brightness_pct(&group, 25);
    assert(group.scaled_r == 50 && group.scaled_g == 25 && group.scaled_b == 12);

    /* Values above 100 clamp to 100 rather than wrapping/overflowing. */
    led_group_set_brightness_pct(&group, 255);
    assert(group.scaled_r == 200 && group.scaled_g == 100 && group.scaled_b == 50);
}

static void test_breathing_follows_lut_and_wraps_at_period_boundary(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 1);
    /* scaled_r/g/b = 100 so the effective color equals the LUT factor
     * directly (100 * factor / 100 == factor), which keeps the assertions
     * below readable without hand-computing every multiply. */
    led_group_set_color(&group, 100, 100, 100);
    led_group_set_period_ticks(&group, 200); /* 200 LUT entries -> 1 entry per tick */
    align_to_phase0(200);
    led_group_set_state(&group, LED_GROUP_BREATHING);
    recorder_reset(&rec);

    led_group_update(&group); /* phase 0 -> lut[0] == 4 */
    assert(rec.count == 1);
    assert(rec.calls[0].r == 4 && rec.calls[0].g == 4 && rec.calls[0].b == 4);

    advance(100);
    led_group_update(&group); /* phase 100 -> lut[100] == 99 */
    assert(rec.count == 2);
    assert(rec.calls[1].r == 99);

    advance(99);
    led_group_update(&group); /* phase 199 -> lut[199] == 5 */
    assert(rec.count == 3);
    assert(rec.calls[2].r == 5);

    advance(1);
    led_group_update(&group); /* phase wraps to 0 -> lut[0] == 4 again */
    assert(rec.count == 4);
    assert(rec.calls[3].r == 4);

    led_group_update(&group); /* same tick, same color -> no new write */
    assert(rec.count == 4);
}

static void test_periodic_effects_are_synced_across_groups(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices_a[] = { 0 };
    static const uint16_t indices_b[] = { 1 };
    led_group_t group_a, group_b;

    led_group_init(&group_a, &strip, indices_a, 1);
    led_group_init(&group_b, &strip, indices_b, 1);
    led_group_set_color(&group_a, 50, 50, 50);
    led_group_set_color(&group_b, 50, 50, 50);
    led_group_set_period_ticks(&group_a, 100);
    led_group_set_period_ticks(&group_b, 100);

    /* The two groups enter BLINK half a period apart - with shared-counter
     * phasing that must NOT matter: same period means same phase, always. */
    align_to_phase0(100);
    led_group_set_state(&group_a, LED_GROUP_BLINK);
    advance(50);
    led_group_set_state(&group_b, LED_GROUP_BLINK);
    recorder_reset(&rec);

    /* phase 60: both in the off half. */
    advance(10);
    led_group_update(&group_a);
    led_group_update(&group_b);
    assert(rec.count == 2);
    assert(rec.calls[0].index == 0 && rec.calls[0].r == 0);
    assert(rec.calls[1].index == 1 && rec.calls[1].r == 0);

    /* phase 10 (wrapped): both in the on half, in lockstep. */
    advance(50);
    led_group_update(&group_a);
    led_group_update(&group_b);
    assert(rec.count == 4);
    assert(rec.calls[2].index == 0 && rec.calls[2].r == 50);
    assert(rec.calls[3].index == 1 && rec.calls[3].r == 50);

    /* Same for BREATHING: staggered entry, identical LUT factor every tick. */
    led_group_set_state(&group_a, LED_GROUP_BREATHING);
    advance(37);
    led_group_set_state(&group_b, LED_GROUP_BREATHING);
    advance(13);
    recorder_reset(&rec);

    led_group_update(&group_a);
    led_group_update(&group_b);
    assert(rec.count == 2);
    assert(rec.calls[0].r == rec.calls[1].r);
    assert(rec.calls[0].g == rec.calls[1].g);
    assert(rec.calls[0].b == rec.calls[1].b);
}

static void test_blink_code_bursts_pauses_and_repeats(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 1);
    led_group_set_color(&group, 80, 80, 80);
    led_group_set_period_ticks(&group, 10);
    led_group_set_blink_code(&group, 3, 40); /* burst 30 + pause 40 = 70/cycle */
    led_group_set_state(&group, LED_GROUP_BLINK_CODE);
    recorder_reset(&rec);

    led_group_update(&group); /* t=0: burst, phase 0 < 5 -> on */
    assert(rec.count == 1 && rec.calls[0].r == 80);

    advance(6);
    led_group_update(&group); /* t=6: burst, phase 6 -> off */
    assert(rec.count == 2 && rec.calls[1].r == 0);

    advance(5);
    led_group_update(&group); /* t=11: burst, phase 1 -> on (2nd blink) */
    assert(rec.count == 3 && rec.calls[2].r == 80);

    advance(20);
    led_group_update(&group); /* t=31: past burst 30 -> pause, off */
    assert(rec.count == 4 && rec.calls[3].r == 0);

    advance(34);
    led_group_update(&group); /* t=65: still in pause -> off, no new write */
    assert(rec.count == 4);

    advance(5);
    led_group_update(&group); /* t=70 wraps to t=0 of next cycle -> on again */
    assert(rec.count == 5 && rec.calls[4].r == 80);
}

static void test_blink_code_anchors_to_state_entry(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 1);
    led_group_set_color(&group, 80, 80, 80);
    led_group_set_period_ticks(&group, 10);
    led_group_set_blink_code(&group, 3, 40);

    /* Entering at an arbitrary counter value must still start the code at
     * its first blink - blink codes anchor to state entry, not the shared
     * counter. Advance to a tick that is NOT a multiple of the 70-tick
     * cycle, where counter-phased logic would land mid-pause (off). */
    align_to_phase0(70);
    advance(37);
    led_group_set_state(&group, LED_GROUP_BLINK_CODE);
    recorder_reset(&rec);

    led_group_update(&group); /* t=0 since entry: first blink, on */
    assert(rec.count == 1 && rec.calls[0].r == 80);
}

int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    test_on_writes_scaled_color_and_fans_out_to_all_indices();
    test_brightness_does_not_compound_on_repeated_calls();
    test_breathing_follows_lut_and_wraps_at_period_boundary();
    test_periodic_effects_are_synced_across_groups();
    test_blink_code_bursts_pauses_and_repeats();
    test_blink_code_anchors_to_state_entry();
    return 0;
}
