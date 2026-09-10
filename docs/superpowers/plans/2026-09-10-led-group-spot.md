# LED_GROUP_SPOT Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a sixth effect state to `led_group` in which a bright spot travels along the group, wrapping at the end, trailing a linearly-fading tail into an ambient background color.

**Architecture:** The five existing states resolve to one uniform color per group, fanned out to every index by `led_group_update()` with `last_r`/`last_g`/`last_b` as change detection. The spot is per-pixel, so it gets its own render function (`led_group_render_spot()`) reached by a single branch at the top of `led_group_update()`, with head position as its change detection. Spot color reuses the existing base color and tempo reuses `period_ticks` (one period per full traversal), so the only new setter carries the ambient color and the spot size.

**Tech Stack:** C99, no dependencies. CMake + CTest, assert-based tests in a single test file. Host builds use the msys64 UCRT64 gcc (verified working; see the build commands below).

**Spec:** `docs/superpowers/specs/2026-09-10-led-group-spot-design.md` — read it before starting. It carries the reasoning behind each decision.

---

## Build and test commands

Every task uses these three. Run them from the repo root
(`Drivers/led_group`), in the Bash tool (msys64's gcc is on its PATH; the
default CMake generator fails here because no MSVC is installed):

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_C_COMPILER=gcc
cmake --build build
ctest --test-dir build --output-on-failure
```

The first command only needs re-running if `CMakeLists.txt` changes, but
it is harmless to repeat. The baseline before this work is **7 tests
passing** (one CTest entry, `led_group_tests`, containing 7 assert-based
cases).

If a freshly-built `.exe` vanishes or reports `Permission denied`, that is
the known CrowdStrike Falcon false positive documented in `CLAUDE.md`,
not a code bug.

---

## File structure

Only three files change; no new files, and no restructuring. The library
is two files by design and stays that way.

| File | Responsibility | Change |
|---|---|---|
| `include/led_group.h` | Public API: state enum, `led_group_t`, setters | Add `LED_GROUP_SPOT`, four field groups, `led_group_set_spot()` declaration |
| `src/led_group.c` | All effect logic | Add ambient scaling, `led_group_set_spot()`, a blend helper, position helper, `led_group_render_spot()`, the dispatch branch, and the dirty invariant in every setter |
| `tests/test_led_group.c` | Assert-based unit tests | Add an `assert_pixel()` helper and 5 new test functions |

Two documentation files are updated at the end: `CLAUDE.md` (which
currently claims "all 5 effect states complete") and
`examples/basic_usage.c`.

---

## Reference: the expected values used throughout

Every test below uses an 8-pixel group with indices `{0,1,2,3,4,5,6,7}`,
spot color green `(0,255,0)`, and ambient `(20,20,20)`. All values are
derived from the spec's two formulas:

```
w   = (spot_size - offset) * 100 / spot_size        /* integer percent */
out = (spot * w + ambient * (100 - w)) / 100        /* per channel */
```

**`spot_size = 4`** → weights `100, 75, 50, 25`:

| offset | w | expected RGB |
|---|---|---|
| 0 (head) | 100 | `(0, 255, 0)` |
| 1 | 75 | `(5, 196, 5)` |
| 2 | 50 | `(10, 137, 10)` |
| 3 | 25 | `(15, 78, 15)` |
| 4+ | — | `(20, 20, 20)` ambient |

**`spot_size = 3`** → weights `100, 66, 33`:

| offset | w | expected RGB |
|---|---|---|
| 0 (head) | 100 | `(0, 255, 0)` |
| 1 | 66 | `(6, 175, 6)` |
| 2 | 33 | `(13, 97, 13)` |
| 3+ | — | `(20, 20, 20)` ambient |

**`spot_size = 1`** → weight `100` only: head is exactly `(0, 255, 0)`,
every other pixel exactly `(20, 20, 20)`.

With `period_ticks = 8` over 8 pixels, `pos == s_ticks % 8` — one tick
per pixel, which is what makes the travel assertions readable.

---

## Task 1: Ambient color and spot size plumbing

Adds the enum value, the struct fields, and `led_group_set_spot()`. No
rendering yet — this task ends with the data model in place and the
ambient color correctly brightness-scaled.

**Files:**
- Modify: `include/led_group.h` — enum at `:11-17`, struct at `:26-43`, setter declarations at `:60-63`
- Modify: `src/led_group.c` — `led_group_recompute_scaled()` at `:26-32`, `led_group_init()` at `:34-61`, new setter after `led_group_set_blink_code()` at `:80-84`
- Test: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

Add this function to `tests/test_led_group.c`, immediately after
`test_brightness_does_not_compound_on_repeated_calls()` (which ends
around line 140):

```c
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
```

Register it in `main()` (currently at line 302), after
`test_brightness_does_not_compound_on_repeated_calls();`:

```c
    test_set_spot_stores_size_and_scales_ambient();
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
```
Expected: FAIL to compile, with errors like
`'led_group_t' has no member named 'spot_size'` and
`implicit declaration of function 'led_group_set_spot'`.

- [ ] **Step 3: Add the enum value**

In `include/led_group.h`, replace the enum:

```c
typedef enum {
    LED_GROUP_OFF,
    LED_GROUP_ON,
    LED_GROUP_BREATHING,
    LED_GROUP_BLINK,
    LED_GROUP_BLINK_CODE,
    LED_GROUP_SPOT,
} led_group_state_t;
```

`LED_GROUP_SPOT` goes last so the numeric values of the five existing
states stay stable for any consumer that persists or transmits them.

- [ ] **Step 4: Add the struct fields**

In `include/led_group.h`, in `led_group_t`, insert these after the
`blink_code_pause_ticks` line and before the `last_r, last_g, last_b`
line:

```c
    /* LED_GROUP_SPOT only. The spot's own color is the group's base
     * color; these are the background it fades into and its length.
     * Ambient is kept both as given and pre-scaled, for the same reason
     * the spot color is: brightness_pct can change at any time, so the
     * caller's value has to survive in order to be re-scaled. */
    uint16_t spot_size;
    uint8_t amb_r, amb_g, amb_b;
    uint8_t scaled_amb_r, scaled_amb_g, scaled_amb_b;
    uint16_t last_spot_pos;
```

- [ ] **Step 5: Declare the setter**

In `include/led_group.h`, add after the `led_group_set_blink_code()`
declaration:

```c
/* LED_GROUP_SPOT: the spot's own color is the group's base color (set with
 * led_group_set_color, so brightness_pct scales it like every other
 * state); this adds the ambient background the tail fades into, and the
 * spot's length in pixels including the head. spot_size clamps to the
 * group's indices_count, and spot_size == 0 means no spot at all - the
 * group renders pure ambient. Travel speed is period_ticks: one period is
 * one full traversal of the group. */
void led_group_set_spot(led_group_t *group, uint16_t spot_size,
                         uint8_t amb_r, uint8_t amb_g, uint8_t amb_b);
```

- [ ] **Step 6: Scale the ambient color**

In `src/led_group.c`, replace `led_group_recompute_scaled()` entirely:

```c
static void led_group_recompute_scaled(led_group_t *group)
{
    group->scaled_r = (uint8_t)((uint16_t)group->base_r * group->brightness_pct / 100);
    group->scaled_g = (uint8_t)((uint16_t)group->base_g * group->brightness_pct / 100);
    group->scaled_b = (uint8_t)((uint16_t)group->base_b * group->brightness_pct / 100);
    group->scaled_amb_r = (uint8_t)((uint16_t)group->amb_r * group->brightness_pct / 100);
    group->scaled_amb_g = (uint8_t)((uint16_t)group->amb_g * group->brightness_pct / 100);
    group->scaled_amb_b = (uint8_t)((uint16_t)group->amb_b * group->brightness_pct / 100);
}
```

Because this recomputes from `amb_r`/`amb_g`/`amb_b` rather than from the
already-scaled values, `led_group_set_brightness_pct()` needs no change
to pick up ambient scaling — and ambient inherits the non-compounding
property the spot color already has.

- [ ] **Step 7: Initialize the new fields**

In `src/led_group.c`, in `led_group_init()`, insert after the
`group->blink_code_pause_ticks = 0;` line:

```c
    group->spot_size = 0;
    group->amb_r = 0;
    group->amb_g = 0;
    group->amb_b = 0;
    group->scaled_amb_r = 0;
    group->scaled_amb_g = 0;
    group->scaled_amb_b = 0;
    group->last_spot_pos = 0;
```

- [ ] **Step 8: Implement the setter**

In `src/led_group.c`, add after `led_group_set_blink_code()`:

```c
void led_group_set_spot(led_group_t *group, uint16_t spot_size,
                         uint8_t amb_r, uint8_t amb_g, uint8_t amb_b)
{
    /* Clamp so a wrapped spot can never overlap its own tail, which would
     * otherwise write one pixel twice in a single pass with two different
     * colors. led_group_init() is a precondition of every setter, so
     * indices_count is already known here. */
    group->spot_size = spot_size > group->indices_count
                           ? group->indices_count
                           : spot_size;
    group->amb_r = amb_r;
    group->amb_g = amb_g;
    group->amb_b = amb_b;
    led_group_recompute_scaled(group);
}
```

- [ ] **Step 9: Run the tests to verify they pass**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: PASS — `1/1 Test #1: led_group_tests ... Passed`, now covering
8 assert-based cases. The 7 pre-existing cases must still pass.

- [ ] **Step 10: Commit**

```bash
git add include/led_group.h src/led_group.c tests/test_led_group.c
git commit -m "Add LED_GROUP_SPOT state value and its ambient/size fields

The spot color reuses the group's base color, so only the ambient
background and the spot length are new. Ambient is stored both as given
and pre-scaled, and is recomputed from the base in
led_group_recompute_scaled() so brightness cannot compound into it.

No rendering yet - LED_GROUP_SPOT still falls through to the default
(off) case in led_group_effective_color()."
```

---

## Task 2: Position, travel, and wrap (hard spot)

Adds the render path and the position mapping, with `spot_size = 1` so
the blend is not yet exercised. This is the task that introduces the
per-pixel branch in `led_group_update()`.

**Files:**
- Modify: `src/led_group.c` — new statics before `led_group_update()`, and `led_group_update()` at `:172-189`
- Test: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

First add this helper to `tests/test_led_group.c`, immediately after
`recorder_reset()` (around line 33). The spot path writes *every* pixel
in the group on each render, so the new tests check whole frames and this
keeps them readable:

```c
static void assert_pixel(const call_recorder_t *rec, size_t call,
                          uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    assert(call < rec->count);
    assert(rec->calls[call].index == index);
    assert(rec->calls[call].r == r);
    assert(rec->calls[call].g == g);
    assert(rec->calls[call].b == b);
}
```

Then add this test function after
`test_set_spot_stores_size_and_scales_ambient()`:

```c
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
```

Register it in `main()` after
`test_set_spot_stores_size_and_scales_ambient();`:

```c
    test_spot_travels_in_index_order_and_wraps();
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: FAIL — `Assertion failed: rec.count == 8`. `LED_GROUP_SPOT`
currently falls through to the default (off) case, so the group resolves
to black, which equals `last_r`/`last_g`/`last_b` and produces zero
writes.

- [ ] **Step 3: Add the blend and position helpers**

In `src/led_group.c`, add these two statics after
`led_group_effective_color()` and before `led_group_update()`:

```c
/* Blend fg toward bg by an integer percentage: w == 100 is pure fg,
 * w == 0 pure bg. The uint16_t intermediates keep the products in range
 * (the worst case is 255 * 100 == 25500). Callers must pass w <= 100,
 * which the weight formula in led_group_render_spot() guarantees. */
static uint8_t led_group_blend(uint8_t fg, uint8_t bg, uint8_t w)
{
    return (uint8_t)(((uint16_t)fg * w + (uint16_t)bg * (uint8_t)(100 - w)) / 100);
}

/* The head's position within the group's indices array. Phase comes off
 * the shared tick counter, like BREATHING/BLINK, so groups sharing a
 * period travel in lockstep; period_ticks is guarded against 0 the same
 * way BREATHING guards it. One period is one full traversal. */
static uint16_t led_group_spot_pos(const led_group_t *group)
{
    uint32_t period_ticks = group->period_ticks != 0 ? group->period_ticks : 1;
    uint32_t phase = s_ticks % period_ticks;

    return (uint16_t)(phase * group->indices_count / period_ticks);
}
```

- [ ] **Step 4: Add the spot render path**

In `src/led_group.c`, add this static immediately after
`led_group_spot_pos()`:

```c
/* The one state whose pixels are not all the same color, so it renders
 * per-pixel and tracks its own change detection (head position) rather
 * than the group-wide last_r/last_g/last_b. */
static void led_group_render_spot(led_group_t *group)
{
    uint16_t count = group->indices_count;
    uint16_t pos;
    uint16_t i;

    if (count == 0) {
        return;
    }

    pos = led_group_spot_pos(group);

    if (!group->dirty && pos == group->last_spot_pos) {
        return;
    }

    for (i = 0; i < count; i++) {
        /* Offset back from the head, wrapping at the end of the group:
         * 0 is the head, the tail runs to lower positions, and anything
         * past the tail is background. spot_size == 0 makes this
         * comparison false for every pixel, which is both what "no spot"
         * means and what keeps the division below unreachable. */
        uint16_t offset = (uint16_t)((pos + count - i) % count);
        uint8_t r, g, b;

        if (offset < group->spot_size) {
            /* Falls linearly from 100% at the head to 100/spot_size at
             * the last tail pixel - never to 0, so the trailing edge
             * stays visible instead of vanishing into the background. */
            uint8_t w = (uint8_t)((uint32_t)(group->spot_size - offset) * 100u
                                   / group->spot_size);
            r = led_group_blend(group->scaled_r, group->scaled_amb_r, w);
            g = led_group_blend(group->scaled_g, group->scaled_amb_g, w);
            b = led_group_blend(group->scaled_b, group->scaled_amb_b, w);
        } else {
            r = group->scaled_amb_r;
            g = group->scaled_amb_g;
            b = group->scaled_amb_b;
        }

        group->strip->write_pixel(group->indices[i], r, g, b, group->strip->ctx);
    }

    group->last_spot_pos = pos;
    group->dirty = false;
}
```

- [ ] **Step 5: Dispatch to it from led_group_update()**

In `src/led_group.c`, replace the opening of `led_group_update()` so it
branches before computing a uniform color. The rest of the function is
unchanged:

```c
void led_group_update(led_group_t *group)
{
    uint8_t r, g, b;

    if (group->state == LED_GROUP_SPOT) {
        led_group_render_spot(group);
        return;
    }

    led_group_effective_color(group, &r, &g, &b);

    if (group->dirty || r != group->last_r || g != group->last_g || b != group->last_b) {
        uint16_t i;
        for (i = 0; i < group->indices_count; i++) {
            group->strip->write_pixel(group->indices[i], r, g, b, group->strip->ctx);
        }
        group->last_r = r;
        group->last_g = g;
        group->last_b = b;
        group->dirty = false;
    }
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: PASS, 9 assert-based cases. All 7 pre-existing cases must still
pass — the uniform path is untouched.

- [ ] **Step 7: Commit**

```bash
git add src/led_group.c tests/test_led_group.c
git commit -m "Render LED_GROUP_SPOT: position, travel, and wrap

led_group_update() gains one branch: the spot renders per-pixel through
led_group_render_spot(), every other state keeps the uniform fan-out
untouched. Position is (s_ticks % period) * count / period, so one period
is one traversal and same-period groups stay in lockstep like
BREATHING/BLINK.

Change detection for this path is the head position rather than the
group-wide resolved color, since the pixels are no longer all the same
color."
```

---

## Task 3: The comet tail

Adds the tail assertions the blend code from Task 2 already satisfies:
the linear fade, the seam crossing, both degenerate sizes, and brightness
scaling across both colors. Tests only — if any of these fail, the bug is
in the Task 2 blend or weight formula.

**Files:**
- Test: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing tests**

Add these three functions after
`test_spot_travels_in_index_order_and_wraps()`:

```c
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
```

Register all three in `main()`, after
`test_spot_travels_in_index_order_and_wraps();`:

```c
    test_spot_tail_fades_linearly_into_ambient();
    test_spot_tail_spans_the_wrap_seam();
    test_spot_degenerate_sizes_and_brightness();
```

- [ ] **Step 2: Run the tests to verify they pass**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: PASS, 12 assert-based cases.

Unlike the other tasks these tests should pass immediately — the Task 2
implementation already contains the blend. They exist to pin the exact
comet values, the seam behavior, and the two degenerate sizes, none of
which Task 2's `spot_size = 1` test could reach. **If any of them fails,
do not adjust the expected values to match the code** — re-derive the
failing pixel from the two formulas in the Reference section above and
fix the weight or blend code in `src/led_group.c`.

- [ ] **Step 3: Commit**

```bash
git add tests/test_led_group.c
git commit -m "Test the comet tail, the wrap seam, and the degenerate sizes

Pins the exact per-pixel values the linear fade produces at sizes 3 and
4, the seam crossing when the head sits near the start of the group, and
the two degenerate cases: size 0 renders pure ambient, size 1 renders a
hard single-pixel spot. Also checks that brightness_pct scales the
ambient color as well as the spot color."
```

---

## Task 4: The dirty invariant

Closes the one hole the position-based change detection leaves: entering
`LED_GROUP_SPOT` while the head happens to sit at position 0.

**Files:**
- Modify: `src/led_group.c` — `led_group_set_color()`, `led_group_set_brightness_pct()`, `led_group_set_period_ticks()`, `led_group_set_blink_code()`, `led_group_set_spot()`, `led_group_set_state()`
- Test: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

Add this function after `test_spot_degenerate_sizes_and_brightness()`:

```c
static void test_entering_spot_at_position_zero_repaints(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 8);
    led_group_update(&group); /* consume the initial OFF dirty write */

    led_group_set_color(&group, 0, 255, 0);
    led_group_set_spot(&group, 1, 20, 20, 20);
    led_group_set_period_ticks(&group, 8);

    /* The spot path's change detection is the head position, and
     * last_spot_pos starts at 0 - so entering the state at position 0 is
     * exactly the case a position comparison cannot see. Without every
     * setter marking the group dirty, this renders nothing and the strip
     * stays black. */
    align_to_phase0(8);
    led_group_set_state(&group, LED_GROUP_SPOT);
    recorder_reset(&rec);

    led_group_update(&group);
    assert(rec.count == 8);
    assert_pixel(&rec, 0, 0, 0, 255, 0);
    assert_pixel(&rec, 1, 1, 20, 20, 20);
    assert_pixel(&rec, 7, 7, 20, 20, 20);
}
```

Register it in `main()` after
`test_spot_degenerate_sizes_and_brightness();`:

```c
    test_entering_spot_at_position_zero_repaints();
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: FAIL — `Assertion failed: rec.count == 8`, with `rec.count`
actually 0. `pos` is 0, `last_spot_pos` is 0, and `dirty` was cleared by
the post-init update, so `led_group_render_spot()` returns early.

- [ ] **Step 3: Mark the group dirty in every setter**

In `src/led_group.c`, add `group->dirty = true;` as the last statement of
each of these six functions. Establishing it as a flat invariant — any
mutation marks the group dirty — is what makes this robust: there is no
per-state exception to remember, and it also covers the reverse
`SPOT -> ON` transition, where `last_r`/`last_g`/`last_b` are stale
because the spot path never maintained them.

```c
void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b)
{
    group->base_r = r;
    group->base_g = g;
    group->base_b = b;
    led_group_recompute_scaled(group);
    group->dirty = true;
}

void led_group_set_brightness_pct(led_group_t *group, uint8_t pct)
{
    group->brightness_pct = pct > 100 ? 100 : pct;
    led_group_recompute_scaled(group);
    group->dirty = true;
}

void led_group_set_period_ticks(led_group_t *group, uint32_t period_ticks)
{
    group->period_ticks = period_ticks;
    group->dirty = true;
}

void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ticks)
{
    group->blink_code_count = count;
    group->blink_code_pause_ticks = pause_ticks;
    group->dirty = true;
}
```

`led_group_set_spot()` gets the same line appended after
`led_group_recompute_scaled(group);`, and `led_group_set_state()` gets it
after `group->state_entered_tick = s_ticks;`:

```c
void led_group_set_state(led_group_t *group, led_group_state_t state)
{
    group->state = state;
    group->state_entered_tick = s_ticks;
    group->dirty = true;
}
```

- [ ] **Step 4: Document the invariant on the struct field**

In `include/led_group.h`, replace the `bool dirty;` line with:

```c
    /* Set by every setter and by led_group_set_state - any mutation
     * forces the next led_group_update() to write, whatever the change
     * detection for the current state happens to compare. */
    bool dirty;
```

- [ ] **Step 5: Run the tests to verify they pass**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: PASS, 13 assert-based cases.

All 7 pre-existing cases must still pass. They do: none of them asserts a
write count across a setter call that changed nothing, which is the only
observable difference (at most one redundant rewrite of identical
values). If one of them does fail, that is real information — report it
rather than editing the assertion.

- [ ] **Step 6: Commit**

```bash
git add include/led_group.h src/led_group.c tests/test_led_group.c
git commit -m "Mark the group dirty in every setter

Only led_group_init() used to set dirty; the uniform states got away with
that because the last_r/last_g/last_b comparison catches any change to
the resolved color. The spot path compares head position instead, so
entering LED_GROUP_SPOT while the head sits at position 0 rendered
nothing and left the strip black.

A flat invariant - any mutation marks the group dirty - fixes that with
no per-state exception to remember, and also covers SPOT -> ON, where
last_r/last_g/last_b are stale because the spot path never maintained
them. Existing states see at most one redundant rewrite of identical
values."
```

---

## Task 5: Documentation and example

`CLAUDE.md` currently states "all 5 effect states complete", which this
work makes wrong, and `examples/basic_usage.c` is the driver-agnostic
demo a consuming project reads first.

**Files:**
- Modify: `CLAUDE.md` — the status line at `:3` and the state inventory in "Where things stand"
- Modify: `examples/basic_usage.c`

- [ ] **Step 1: Add the ambient color macro**

The example keeps its project colors as macros at the top. Add one for
the spot's background, after the `COLOR_ERROR` line:

```c
#define COLOR_AMBIENT 8, 8, 8
```

- [ ] **Step 2: Add a spot group to the example**

In `main()`, add the index array and group next to the two already there
(after the `error_indices` / `error_led` declarations and the
`led_group_init(&error_led, ...)` call):

```c
    static const uint16_t bar_indices[] = { 3, 4, 5, 6, 7, 8, 9, 10 };
    led_group_t bar_leds;

    led_group_init(&bar_leds, &strip, bar_indices, 8);
```

Then add the state setup after the error LED's block:

```c
    /* An 8-pixel bar with a green spot running along it, trailing a tail
     * that fades into a dim ambient background. All four calls matter:
     * the spot's own color is the group's base color, and its speed is
     * the shared period, so set_color and set_period_ticks are just as
     * required as set_spot - and are the two easiest to forget. One
     * period is one full lap, so 80 ticks over 8 pixels is 10 ticks per
     * pixel. */
    led_group_set_color(&bar_leds, COLOR_READY);
    led_group_set_spot(&bar_leds, 4, COLOR_AMBIENT);
    led_group_set_period_ticks(&bar_leds, 80);
    led_group_set_state(&bar_leds, LED_GROUP_SPOT);
```

Finally add it to the render loop, next to the two existing
`led_group_update()` calls and before `led_group_tick()`:

```c
        led_group_update(&bar_leds);
```

- [ ] **Step 3: Verify the example still builds and runs**

Run:
```bash
cmake --build build && ./build/examples/led_group_example.exe | head -40
```
Expected: builds clean. The bar group prints an 8-pixel frame (indices
3-10) in which the pixels are *not* all the same color — a bright green
head with three progressively dimmer pixels behind it and `(8, 8, 8)`
elsewhere. At 80 ticks per lap over 8 pixels the head moves every 10
ticks, and the bar reprints only on the ticks where it moves, so expect
one bar frame roughly every 10 ticks rather than one per tick.

To confirm the head actually advances, compare the first two bar frames:
```bash
./build/examples/led_group_example.exe | grep -E "pixel\[(3|4|5|6|7|8|9|10)\]" | head -16
```
Expected: the fully-saturated `(0, 255, 0)` pixel sits at a higher index
in the second frame than in the first.

- [ ] **Step 4: Update CLAUDE.md**

Make these edits:

1. The `Status:` line at the top: "all 5 effect states complete" becomes
   "all 6 effect states complete".
2. The "What this is" paragraph: the effect list
   `(off/on/breathing/blink/blink-code effects)` gains the spot.
3. The bullet in "Where things stand" that says "All 5 states
   (OFF/ON/BREATHING/BLINK/BLINK_CODE) are real": make it 6, add
   `LED_GROUP_SPOT` and `led_group_set_spot`, and note that the spot is
   the one state that renders per-pixel and therefore has its own render
   path and its own position-based change detection.
4. The same bullet's dirty-flag note: record that the dirty flag is now
   set by every setter, not just `init`, and why (a position comparison
   cannot catch `OFF -> SPOT` at position 0).
5. The test-count bullet: "7 tests total" becomes 13.
6. Add the spec and plan paths for this work next to the existing ones in
   the first two bullets.

Leave the CrowdStrike note, the reference-source section, and the
"Not yet decided" section alone, except to note that wiring the spot into
`164_traps_motor`'s `leds.c` is a follow-up in that project.

- [ ] **Step 5: Run the full suite one last time**

Run:
```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: PASS, 13 assert-based cases, `100% tests passed`.

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md examples/basic_usage.c
git commit -m "Document LED_GROUP_SPOT in CLAUDE.md and the example

basic_usage.c gains a spot group showing all four calls the state needs -
set_color for the spot, set_spot for size and ambient, set_period_ticks
for the lap time, set_state - since the two non-spot-specific ones are
the easiest to miss.

CLAUDE.md's state inventory, test count, and dirty-flag note are brought
up to date, and the new spec and plan are linked alongside the originals."
```

---

## Definition of done

- [ ] `ctest --test-dir build --output-on-failure` reports `100% tests passed`, with 13 assert-based cases: the 7 pre-existing ones plus the 6 new test functions added by Tasks 1-4.
- [ ] `cmake --build build` produces no new warnings.
- [ ] Every numeric expectation in the tests is derivable from the two formulas in the Reference section — none was adjusted to match the implementation.
- [ ] `CLAUDE.md` no longer claims 5 states.
- [ ] Wiring the state into `164_traps_motor`'s `Core/Src/leds.c` is **not** part of this plan — it is a change in the consuming project.
