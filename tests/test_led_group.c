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

int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    test_on_writes_scaled_color_and_fans_out_to_all_indices();
    return 0;
}
