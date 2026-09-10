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

static void assert_pixel(const call_recorder_t *rec, size_t call,
                          uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    assert(call < rec->count);
    assert(rec->calls[call].index == index);
    assert(rec->calls[call].r == r);
    assert(rec->calls[call].g == g);
    assert(rec->calls[call].b == b);
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

static void test_set_spot_stores_size_and_scales_ambient(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0, 1, 2, 3 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 4);
    assert(group.spot_size == 0);
    assert(group.amb_r == 0 && group.amb_g == 0 && group.amb_b == 0);
    assert(group.scaled_amb_r == 0 && group.scaled_amb_g == 0 &&
           group.scaled_amb_b == 0);
    assert(group.last_spot_pos == 0);

    led_group_set_spot(&group, 2, 200, 100, 50);
    assert(group.spot_size == 2);
    assert(group.amb_r == 200 && group.amb_g == 100 && group.amb_b == 50);
    /* brightness defaults to 100, so scaled == base */
    assert(group.scaled_amb_r == 200 && group.scaled_amb_g == 100 &&
           group.scaled_amb_b == 50);

    /* The ambient color is brightness-scaled exactly like the spot color,
     * and recomputed from the untouched base every time so it cannot
     * compound - the same bug class as the spot color's regression test
     * above. */
    led_group_set_brightness_pct(&group, 50);
    assert(group.scaled_amb_r == 100 && group.scaled_amb_g == 50 &&
           group.scaled_amb_b == 25);
    led_group_set_brightness_pct(&group, 50);
    assert(group.scaled_amb_r == 100 && group.scaled_amb_g == 50 &&
           group.scaled_amb_b == 25);

    /* A size past the end of the group clamps to the group's length, so a
     * wrapped spot can never overlap its own tail. */
    led_group_set_spot(&group, 99, 0, 0, 0);
    assert(group.spot_size == 4);
}

static void test_spot_travels_in_index_order_and_wraps(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    led_group_t group;
    uint16_t i;

    led_group_init(&group, &strip, indices, 8);
    led_group_update(&group); /* consume the initial OFF dirty write */

    led_group_set_color(&group, 0, 255, 0);
    led_group_set_spot(&group, 1, 20, 20, 20); /* size 1 -> no tail yet */
    led_group_set_period_ticks(&group, 8);     /* 8 pixels -> 1 tick each */

    /* Enter at a non-zero position: with size 1 the head is the only lit
     * pixel, so an arbitrary starting position keeps the assertion below
     * unambiguous. */
    align_to_phase0(8);
    advance(3);
    led_group_set_state(&group, LED_GROUP_SPOT);
    recorder_reset(&rec);

    /* pos 3: every pixel is written, head at index 3, ambient elsewhere. */
    led_group_update(&group);
    assert(rec.count == 8);
    for (i = 0; i < 8; i++) {
        if (i == 3) {
            assert_pixel(&rec, i, i, 0, 255, 0);
        } else {
            assert_pixel(&rec, i, i, 20, 20, 20);
        }
    }

    /* One tick -> one pixel of travel, in indices-array order. */
    advance(1);
    led_group_update(&group);
    assert(rec.count == 16);
    assert_pixel(&rec, 8 + 3, 3, 20, 20, 20);
    assert_pixel(&rec, 8 + 4, 4, 0, 255, 0);

    /* Same tick, unmoved head -> no new writes. */
    led_group_update(&group);
    assert(rec.count == 16);

    /* pos 8 wraps to 0. */
    advance(4);
    led_group_update(&group);
    assert(rec.count == 24);
    assert_pixel(&rec, 16 + 0, 0, 0, 255, 0);
    assert_pixel(&rec, 16 + 7, 7, 20, 20, 20);
}

static void test_spot_tail_fades_linearly_into_ambient(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 8);
    led_group_update(&group); /* consume the initial OFF dirty write */

    led_group_set_color(&group, 0, 255, 0);
    led_group_set_spot(&group, 4, 20, 20, 20);
    led_group_set_period_ticks(&group, 8);

    align_to_phase0(8);
    advance(5);
    led_group_set_state(&group, LED_GROUP_SPOT);
    recorder_reset(&rec);

    /* pos 5, size 4 -> the spot covers 5 (head), 4, 3, 2 with weights
     * 100, 75, 50, 25; indices 6, 7, 0, 1 are ambient. Values come from
     * out = (spot * w + ambient * (100 - w)) / 100 per channel. */
    led_group_update(&group);
    assert(rec.count == 8);
    assert_pixel(&rec, 0, 0, 20, 20, 20);
    assert_pixel(&rec, 1, 1, 20, 20, 20);
    assert_pixel(&rec, 2, 2, 15, 78, 15);   /* offset 3, w = 25 */
    assert_pixel(&rec, 3, 3, 10, 137, 10);  /* offset 2, w = 50 */
    assert_pixel(&rec, 4, 4, 5, 196, 5);    /* offset 1, w = 75 */
    assert_pixel(&rec, 5, 5, 0, 255, 0);    /* offset 0, head */
    assert_pixel(&rec, 6, 6, 20, 20, 20);
    assert_pixel(&rec, 7, 7, 20, 20, 20);
}

static void test_spot_tail_spans_the_wrap_seam(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 8);
    led_group_update(&group); /* consume the initial OFF dirty write */

    led_group_set_color(&group, 0, 255, 0);
    led_group_set_spot(&group, 3, 20, 20, 20);
    led_group_set_period_ticks(&group, 8);

    /* The tail runs to lower positions, so the seam is crossed when the
     * head is near the START of the group: head at 1 covers 1, 0, 7. */
    align_to_phase0(8);
    advance(1);
    led_group_set_state(&group, LED_GROUP_SPOT);
    recorder_reset(&rec);

    /* size 3 -> weights 100, 66, 33. */
    led_group_update(&group);
    assert(rec.count == 8);
    assert_pixel(&rec, 0, 0, 6, 175, 6);    /* offset 1, w = 66 */
    assert_pixel(&rec, 1, 1, 0, 255, 0);    /* offset 0, head */
    assert_pixel(&rec, 2, 2, 20, 20, 20);
    assert_pixel(&rec, 3, 3, 20, 20, 20);
    assert_pixel(&rec, 4, 4, 20, 20, 20);
    assert_pixel(&rec, 5, 5, 20, 20, 20);
    assert_pixel(&rec, 6, 6, 20, 20, 20);
    assert_pixel(&rec, 7, 7, 13, 97, 13);   /* offset 2, w = 33 */
}

static void test_spot_degenerate_sizes_and_brightness(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    led_group_t group;
    uint16_t i;

    led_group_init(&group, &strip, indices, 8);
    led_group_update(&group); /* consume the initial OFF dirty write */

    led_group_set_color(&group, 0, 255, 0);
    led_group_set_period_ticks(&group, 8);

    /* size 0 means no spot: the whole group is ambient, and the weight
     * division is never reached. */
    led_group_set_spot(&group, 0, 20, 20, 20);
    align_to_phase0(8);
    advance(2);
    led_group_set_state(&group, LED_GROUP_SPOT);
    recorder_reset(&rec);

    led_group_update(&group);
    assert(rec.count == 8);
    for (i = 0; i < 8; i++) {
        assert_pixel(&rec, i, i, 20, 20, 20);
    }

    /* size 1 is a hard single-pixel spot: weight 100, no tail. */
    led_group_set_spot(&group, 1, 20, 20, 20);
    advance(1); /* pos 3, so the position check cannot suppress the write */
    recorder_reset(&rec);

    led_group_update(&group);
    assert(rec.count == 8);
    assert_pixel(&rec, 2, 2, 20, 20, 20);
    assert_pixel(&rec, 3, 3, 0, 255, 0);
    assert_pixel(&rec, 4, 4, 20, 20, 20);

    /* brightness scales the spot AND the ambient color: 255 * 50 / 100
     * is 127, 20 * 50 / 100 is 10. */
    led_group_set_brightness_pct(&group, 50);
    advance(1); /* pos 4 */
    recorder_reset(&rec);

    led_group_update(&group);
    assert(rec.count == 8);
    assert_pixel(&rec, 3, 3, 10, 10, 10);
    assert_pixel(&rec, 4, 4, 0, 127, 0);
    assert_pixel(&rec, 5, 5, 10, 10, 10);
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
    test_set_spot_stores_size_and_scales_ambient();
    test_spot_travels_in_index_order_and_wraps();
    test_spot_tail_fades_linearly_into_ambient();
    test_spot_tail_spans_the_wrap_seam();
    test_spot_degenerate_sizes_and_brightness();
    test_breathing_follows_lut_and_wraps_at_period_boundary();
    test_periodic_effects_are_synced_across_groups();
    test_blink_code_bursts_pauses_and_repeats();
    test_blink_code_anchors_to_state_entry();
    return 0;
}
