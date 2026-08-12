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

static void test_off_writes_black_once_via_dirty_flag(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 5 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 1);
    assert(led_group_get_state(&group) == LED_GROUP_OFF);

    led_group_update(&group, 0);
    assert(rec.count == 1);
    assert(rec.calls[0].index == 5);
    assert(rec.calls[0].r == 0);
    assert(rec.calls[0].g == 0);
    assert(rec.calls[0].b == 0);

    led_group_update(&group, 10);
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
    led_group_update(&group, 0); /* consume the initial OFF dirty write */
    recorder_reset(&rec);

    led_group_set_color(&group, 10, 20, 30);
    led_group_set_state(&group, LED_GROUP_ON, 100);
    assert(led_group_get_state(&group) == LED_GROUP_ON);

    led_group_update(&group, 100);
    assert(rec.count == 2);
    assert(rec.calls[0].index == 3);
    assert(rec.calls[0].r == 10 && rec.calls[0].g == 20 && rec.calls[0].b == 30);
    assert(rec.calls[1].index == 5);
    assert(rec.calls[1].r == 10 && rec.calls[1].g == 20 && rec.calls[1].b == 30);

    led_group_update(&group, 200);
    assert(rec.count == 2); /* unchanged color -> no new writes */

    led_group_set_state(&group, LED_GROUP_OFF, 300);
    led_group_update(&group, 300);
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
    led_group_set_period_ms(&group, 200); /* 200 LUT entries -> 1 entry per ms */
    led_group_set_state(&group, LED_GROUP_BREATHING, 1000);
    recorder_reset(&rec);

    led_group_update(&group, 1000); /* phase 0 -> lut[0] == 4 */
    assert(rec.count == 1);
    assert(rec.calls[0].r == 4 && rec.calls[0].g == 4 && rec.calls[0].b == 4);

    led_group_update(&group, 1100); /* phase 100 -> lut[100] == 99 */
    assert(rec.count == 2);
    assert(rec.calls[1].r == 99);

    led_group_update(&group, 1199); /* phase 199 -> lut[199] == 5 */
    assert(rec.count == 3);
    assert(rec.calls[2].r == 5);

    led_group_update(&group, 1200); /* phase wraps to 0 -> lut[0] == 4 again */
    assert(rec.count == 4);
    assert(rec.calls[3].r == 4);

    led_group_update(&group, 1200); /* same phase, same color -> no new write */
    assert(rec.count == 4);
}

int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    test_on_writes_scaled_color_and_fans_out_to_all_indices();
    test_brightness_does_not_compound_on_repeated_calls();
    test_breathing_follows_lut_and_wraps_at_period_boundary();
    return 0;
}
