# led_group Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the `led_group` C library exactly as specified in `docs/superpowers/specs/2026-08-12-led-group-design.md` — a dependency-free, driver-agnostic library for controlling independent named LED groups (OFF/ON/BREATHING/BLINK/BLINK_CODE) — with a full assert-based CTest suite.

**Architecture:** Single header (`include/led_group.h`) + single source file (`src/led_group.c`) implementing the `led_group_t` state machine described in the spec, built as a static library via CMake (`add_subdirectory`-friendly) and described to PlatformIO via `library.json`. Tests are plain C (`assert()`), compiled into one executable and registered with CTest, using a recording test double for `led_group_write_pixel_fn` instead of any real LED driver.

**Tech Stack:** C99, CMake 3.15+, CTest. No external test framework, no dynamic allocation, no floating point (per spec non-goals). Verified locally with gcc (MSYS2/MinGW, `gcc.exe` 15.2.0) + `mingw32-make` + CMake 4.4.0, generator `MinGW Makefiles`.

---

## Before you start

Read `docs/superpowers/specs/2026-08-12-led-group-design.md` in full — every function signature, struct field, and effect formula in this plan is taken directly from it. This plan does not repeat the spec's reasoning, only its concrete implementation.

## File Structure

```
led_group/
  include/
    led_group.h          # public API + led_group_t/led_strip_t structs (Task 1)
  src/
    led_group.c           # implementation, including breathe_lut (Tasks 1, 2-6)
  tests/
    CMakeLists.txt         # test executable + ctest registration (Task 1)
    test_led_group.c       # all test cases, one binary (Tasks 2-6)
  examples/
    CMakeLists.txt          # example executable (Task 8)
    basic_usage.c            # consumer-style wiring demo (Task 8)
  CMakeLists.txt            # top-level: add_library(led_group), optional tests (Task 1)
  library.json               # PlatformIO metadata (Task 1)
  .gitignore                  # ignore build/ (Task 1)
```

Everything lives in two implementation files because the spec's entire data model is one struct and one enum — splitting further (e.g. a separate file per effect) would fragment a single cohesive state machine for no benefit, and would fight the `update()` dispatch that reads all effect branches together.

---

### Task 1: Project scaffolding

**Files:**
- Create: `include/led_group.h`
- Create: `src/led_group.c`
- Create: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `library.json`
- Create: `.gitignore`

- [ ] **Step 1: Create the public header**

Create `include/led_group.h`:

```c
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

void led_group_init(led_group_t *group, const led_strip_t *strip,
                     const uint16_t *indices, uint16_t indices_count);

void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b);
void led_group_set_brightness_pct(led_group_t *group, uint8_t pct);
void led_group_set_period_ms(led_group_t *group, uint32_t period_ms);
void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ms);

void led_group_set_state(led_group_t *group, led_group_state_t state, uint32_t now_ms);
led_group_state_t led_group_get_state(const led_group_t *group);

void led_group_update(led_group_t *group, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* LED_GROUP_H */
```

- [ ] **Step 2: Create an empty (but linkable) source stub**

Create `src/led_group.c`:

```c
#include "led_group.h"

void led_group_init(led_group_t *group, const led_strip_t *strip,
                     const uint16_t *indices, uint16_t indices_count)
{
    (void)group;
    (void)strip;
    (void)indices;
    (void)indices_count;
}

void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b)
{
    (void)group;
    (void)r;
    (void)g;
    (void)b;
}

void led_group_set_brightness_pct(led_group_t *group, uint8_t pct)
{
    (void)group;
    (void)pct;
}

void led_group_set_period_ms(led_group_t *group, uint32_t period_ms)
{
    (void)group;
    (void)period_ms;
}

void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ms)
{
    (void)group;
    (void)count;
    (void)pause_ms;
}

void led_group_set_state(led_group_t *group, led_group_state_t state, uint32_t now_ms)
{
    (void)group;
    (void)state;
    (void)now_ms;
}

led_group_state_t led_group_get_state(const led_group_t *group)
{
    (void)group;
    return LED_GROUP_OFF;
}

void led_group_update(led_group_t *group, uint32_t now_ms)
{
    (void)group;
    (void)now_ms;
}
```

This is a deliberate do-nothing stub so the project compiles from commit one. Tasks 2-6 fill in real behavior function by function via TDD.

- [ ] **Step 3: Create the top-level CMakeLists.txt**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.15)
project(led_group C)

add_library(led_group STATIC src/led_group.c)
target_include_directories(led_group PUBLIC include)
target_compile_features(led_group PUBLIC c_std_99)

option(LED_GROUP_BUILD_TESTS "Build led_group unit tests" ON)

if(LED_GROUP_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()
```

The `CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR` guard means tests only build when `led_group` is the top-level CMake project, not when a consuming project pulls it in via `add_subdirectory()` (matching the spec's plain-CMake/STM32CubeMX use case).

- [ ] **Step 4: Create tests/CMakeLists.txt**

Create `tests/CMakeLists.txt`:

```cmake
add_executable(led_group_tests test_led_group.c)
target_link_libraries(led_group_tests PRIVATE led_group)
add_test(NAME led_group_tests COMMAND led_group_tests)
```

- [ ] **Step 5: Create a placeholder test file so the build works**

Create `tests/test_led_group.c`:

```c
#include "led_group.h"

int main(void)
{
    return 0;
}
```

- [ ] **Step 6: Create library.json (PlatformIO metadata)**

Create `library.json`:

```json
{
  "name": "led_group",
  "version": "0.1.0",
  "description": "Dependency-free C library for independently controlling named groups of LEDs on an addressable strip (off/on/breathing/blink/blink-code effects).",
  "keywords": ["led", "neopixel", "fastled", "effects"],
  "repository": {
    "type": "git",
    "url": "https://github.com/dansoskin/led_group.git"
  },
  "frameworks": "*",
  "platforms": "*",
  "build": {
    "flags": ["-Iinclude"]
  }
}
```

- [ ] **Step 7: Create .gitignore**

Create `.gitignore`:

```
build/
```

- [ ] **Step 8: Verify the empty project builds and the placeholder test runs**

Run:
```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: build succeeds, `ctest` reports `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 9: Commit**

```bash
git add include/led_group.h src/led_group.c CMakeLists.txt tests/CMakeLists.txt tests/test_led_group.c library.json .gitignore
git commit -m "chore: scaffold led_group project structure"
```

---

### Task 2: OFF state, init, and the dirty-write-once mechanism

This is the first real behavior: `led_group_init()` establishes safe defaults, and `led_group_update()` only calls `write_pixel` when the computed color changed (or on the very first call, via `dirty`).

**Files:**
- Modify: `src/led_group.c`
- Modify: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

Replace the contents of `tests/test_led_group.c` with:

```c
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

int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: FAIL (test binary asserts because the stub `led_group_update` never calls `write_pixel`, so `rec.count == 1` fails after the first `led_group_update`).

- [ ] **Step 3: Implement `led_group_init` and the OFF branch of `led_group_update`**

Replace the contents of `src/led_group.c` with:

```c
#include "led_group.h"

static void led_group_recompute_scaled(led_group_t *group)
{
    group->scaled_r = (uint8_t)((uint16_t)group->base_r * group->brightness_pct / 100);
    group->scaled_g = (uint8_t)((uint16_t)group->base_g * group->brightness_pct / 100);
    group->scaled_b = (uint8_t)((uint16_t)group->base_b * group->brightness_pct / 100);
}

void led_group_init(led_group_t *group, const led_strip_t *strip,
                     const uint16_t *indices, uint16_t indices_count)
{
    group->strip = strip;
    group->indices = indices;
    group->indices_count = indices_count;

    group->base_r = 0;
    group->base_g = 0;
    group->base_b = 0;
    group->brightness_pct = 100;
    group->scaled_r = 0;
    group->scaled_g = 0;
    group->scaled_b = 0;

    group->state = LED_GROUP_OFF;
    group->state_entered_ms = 0;
    group->period_ms = 1000;
    group->blink_code_count = 0;
    group->blink_code_pause_ms = 0;

    group->last_r = 0;
    group->last_g = 0;
    group->last_b = 0;
    group->dirty = true;
}

void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b)
{
    (void)group;
    (void)r;
    (void)g;
    (void)b;
}

void led_group_set_brightness_pct(led_group_t *group, uint8_t pct)
{
    (void)group;
    (void)pct;
}

void led_group_set_period_ms(led_group_t *group, uint32_t period_ms)
{
    (void)group;
    (void)period_ms;
}

void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ms)
{
    (void)group;
    (void)count;
    (void)pause_ms;
}

void led_group_set_state(led_group_t *group, led_group_state_t state, uint32_t now_ms)
{
    (void)group;
    (void)state;
    (void)now_ms;
}

led_group_state_t led_group_get_state(const led_group_t *group)
{
    return group->state;
}

static void led_group_effective_color(const led_group_t *group, uint32_t now_ms,
                                       uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    (void)now_ms;
    switch (group->state) {
    case LED_GROUP_OFF:
    default:
        *out_r = 0;
        *out_g = 0;
        *out_b = 0;
        break;
    }
}

void led_group_update(led_group_t *group, uint32_t now_ms)
{
    uint8_t r, g, b;
    led_group_effective_color(group, now_ms, &r, &g, &b);

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

Note `led_group_recompute_scaled` is written now but unused until Task 4 (`set_color`/`set_brightness_pct`) — it will trigger an "unused function" warning until then; that's expected and resolves itself in Task 4.

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 5: Commit**

```bash
git add src/led_group.c tests/test_led_group.c
git commit -m "feat: implement led_group_init and OFF state with dirty-write-once update"
```

---

### Task 3: ON state, set_color, set_state, and multi-index fan-out

**Files:**
- Modify: `src/led_group.c`
- Modify: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

Add this test function to `tests/test_led_group.c`, above `int main(void)`:

```c
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
```

Update `main()` to call it:

```c
int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    test_on_writes_scaled_color_and_fans_out_to_all_indices();
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: FAIL (`set_color`/`set_state` are still no-ops, and `LED_GROUP_ON` isn't handled in `led_group_effective_color`, so the color asserts fail).

- [ ] **Step 3: Implement `set_color`, `set_state`, and the ON branch**

In `src/led_group.c`, replace the `led_group_set_color` stub:

```c
void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b)
{
    group->base_r = r;
    group->base_g = g;
    group->base_b = b;
    led_group_recompute_scaled(group);
}
```

Replace the `led_group_set_state` stub:

```c
void led_group_set_state(led_group_t *group, led_group_state_t state, uint32_t now_ms)
{
    group->state = state;
    group->state_entered_ms = now_ms;
}
```

Replace the `led_group_effective_color` switch statement's default-only body with an ON case added:

```c
static void led_group_effective_color(const led_group_t *group, uint32_t now_ms,
                                       uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    (void)now_ms;
    switch (group->state) {
    case LED_GROUP_ON:
        *out_r = group->scaled_r;
        *out_g = group->scaled_g;
        *out_b = group->scaled_b;
        break;

    case LED_GROUP_OFF:
    default:
        *out_r = 0;
        *out_g = 0;
        *out_b = 0;
        break;
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 5: Commit**

```bash
git add src/led_group.c tests/test_led_group.c
git commit -m "feat: implement ON state, set_color, and set_state"
```

---

### Task 4: Brightness/color independence (the compounding-brightness bug fix)

This is the regression test for the real bug described in the spec: the original `set_brightness_percentage()` re-scaled its own already-scaled stored color, so repeated or redundant brightness calls compounded and over-darkened the LED. `led_group` fixes this by keeping `base_r/g/b` untouched and always recomputing `scaled_r/g/b` fresh from the base.

**Files:**
- Modify: `src/led_group.c`
- Modify: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

Add this test function to `tests/test_led_group.c`, above `int main(void)`:

```c
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
```

Update `main()` to call it:

```c
int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    test_on_writes_scaled_color_and_fans_out_to_all_indices();
    test_brightness_does_not_compound_on_repeated_calls();
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: FAIL (`led_group_set_brightness_pct` is still a no-op, so `scaled_r/g/b` stay at 0).

- [ ] **Step 3: Implement `led_group_set_brightness_pct`**

In `src/led_group.c`, replace the stub:

```c
void led_group_set_brightness_pct(led_group_t *group, uint8_t pct)
{
    group->brightness_pct = pct > 100 ? 100 : pct;
    led_group_recompute_scaled(group);
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 5: Commit**

```bash
git add src/led_group.c tests/test_led_group.c
git commit -m "feat: implement led_group_set_brightness_pct without compounding scaled color"
```

---

### Task 5: BREATHING effect and the breathe_lut table

The LUT below is the original `effect1` float table from `biostaq_dispenser/lib/leds/lighten_object.cpp` (also used verbatim in `cannadorf_v2`), converted from `float 0.0-1.0` to `uint8_t 0-100` by multiplying by 100 (every source value has exactly two decimal digits, so this conversion is exact, not rounded).

**Files:**
- Modify: `src/led_group.c`
- Modify: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

Add this test function to `tests/test_led_group.c`, above `int main(void)`:

```c
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
```

Update `main()` to call it:

```c
int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    test_on_writes_scaled_color_and_fans_out_to_all_indices();
    test_brightness_does_not_compound_on_repeated_calls();
    test_breathing_follows_lut_and_wraps_at_period_boundary();
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: FAIL (`LED_GROUP_BREATHING` isn't handled yet, `set_period_ms` is a no-op, so effective color stays black).

- [ ] **Step 3: Implement the breathe_lut table, set_period_ms, and the BREATHING branch**

In `src/led_group.c`, add the LUT near the top, right after the `#include "led_group.h"` line:

```c
#define BREATHE_LUT_SIZE 200

/* Ported from biostaq_dispenser/cannadorf_v2's `effect1` float table
 * (0.0-1.0), converted to uint8_t percentages (0-100). See
 * docs/superpowers/specs/2026-08-12-led-group-design.md for why this is an
 * integer LUT rather than float. */
static const uint8_t breathe_lut[BREATHE_LUT_SIZE] = {
    4,5,5,5,6,6,6,7,7,7,8,8,9,9,10,10,11,11,12,13,13,14,15,16,16,17,18,19,20,20,21,22,23,24,25,26,28,29,30,31,32,33,35,36,
    37,38,40,41,43,44,45,47,48,50,51,53,54,56,57,59,60,62,63,65,66,68,69,70,72,73,75,76,77,79,80,81,83,84,85,86,87,88,89,90,91,92,93,94,
    95,95,96,97,97,97,98,98,99,99,99,99,99,99,99,99,99,98,98,97,97,97,96,95,95,94,93,92,91,90,89,88,87,86,85,84,83,81,80,79,77,76,75,73,
    72,70,69,68,66,65,63,62,60,59,57,56,54,53,51,50,48,47,45,44,43,41,40,38,37,36,35,33,32,31,30,29,28,26,25,24,23,22,21,20,20,19,18,17,
    16,16,15,14,13,13,12,11,11,10,10,9,9,8,8,7,7,7,6,6,6,5,5,5
};
```

Replace the `led_group_set_period_ms` stub:

```c
void led_group_set_period_ms(led_group_t *group, uint32_t period_ms)
{
    group->period_ms = period_ms;
}
```

Add a BREATHING case to `led_group_effective_color` (it now needs `now_ms`, so drop the `(void)now_ms;` line):

```c
static void led_group_effective_color(const led_group_t *group, uint32_t now_ms,
                                       uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    switch (group->state) {
    case LED_GROUP_ON:
        *out_r = group->scaled_r;
        *out_g = group->scaled_g;
        *out_b = group->scaled_b;
        break;

    case LED_GROUP_BREATHING: {
        /* period_ms is a public setter's input; guard the mod/div below
         * against a caller passing 0, which would otherwise be a
         * divide-by-zero crash on every embedded target this runs on. */
        uint32_t period_ms = group->period_ms != 0 ? group->period_ms : 1;
        uint32_t phase_ms = (now_ms - group->state_entered_ms) % period_ms;
        uint32_t idx = phase_ms * BREATHE_LUT_SIZE / period_ms;
        uint8_t factor = breathe_lut[idx];
        *out_r = (uint8_t)((uint16_t)group->scaled_r * factor / 100);
        *out_g = (uint8_t)((uint16_t)group->scaled_g * factor / 100);
        *out_b = (uint8_t)((uint16_t)group->scaled_b * factor / 100);
        break;
    }

    case LED_GROUP_OFF:
    default:
        *out_r = 0;
        *out_g = 0;
        *out_b = 0;
        break;
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 5: Commit**

```bash
git add src/led_group.c tests/test_led_group.c
git commit -m "feat: implement BREATHING state with integer breathe_lut"
```

---

### Task 6: BLINK effect with independent per-group phase

**Files:**
- Modify: `src/led_group.c`
- Modify: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

Add this test function to `tests/test_led_group.c`, above `int main(void)`. This is the spec's "independent phase across two groups sharing a `led_strip_t`" test case: both groups enter `BLINK` with the same `period_ms` but at different `now_ms`, so at a shared instant one can be on while the other is off.

```c
static void test_blink_phase_is_independent_per_group(void)
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
    led_group_set_period_ms(&group_a, 1000);
    led_group_set_period_ms(&group_b, 1000);

    led_group_set_state(&group_a, LED_GROUP_BLINK, 0);   /* phase starts at 0 */
    led_group_set_state(&group_b, LED_GROUP_BLINK, 500); /* phase starts 500ms later */
    recorder_reset(&rec);

    /* now_ms = 600: A's phase is 600 (off half), B's phase is 100 (on half). */
    led_group_update(&group_a, 600);
    led_group_update(&group_b, 600);
    assert(rec.count == 2);
    assert(rec.calls[0].index == 0 && rec.calls[0].r == 0);   /* A off */
    assert(rec.calls[1].index == 1 && rec.calls[1].r == 50);  /* B on */

    /* now_ms = 1100: A's phase is 100 (on half), B's phase is 600 (off half)
     * - the two groups have swapped, proving they're not in lockstep. */
    led_group_update(&group_a, 1100);
    led_group_update(&group_b, 1100);
    assert(rec.count == 4);
    assert(rec.calls[2].index == 0 && rec.calls[2].r == 50);  /* A on */
    assert(rec.calls[3].index == 1 && rec.calls[3].r == 0);   /* B off */
}
```

Update `main()` to call it:

```c
int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    test_on_writes_scaled_color_and_fans_out_to_all_indices();
    test_brightness_does_not_compound_on_repeated_calls();
    test_breathing_follows_lut_and_wraps_at_period_boundary();
    test_blink_phase_is_independent_per_group();
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: FAIL (`LED_GROUP_BLINK` isn't handled, effective color stays black for both groups).

- [ ] **Step 3: Implement the BLINK branch**

In `src/led_group.c`, add a BLINK case to `led_group_effective_color`, next to the BREATHING case:

```c
    case LED_GROUP_BLINK: {
        uint32_t period_ms = group->period_ms != 0 ? group->period_ms : 1;
        bool on = ((now_ms - group->state_entered_ms) % period_ms) < period_ms / 2;
        if (on) {
            *out_r = group->scaled_r;
            *out_g = group->scaled_g;
            *out_b = group->scaled_b;
        } else {
            *out_r = 0;
            *out_g = 0;
            *out_b = 0;
        }
        break;
    }
```

(Insert it as another `case` inside the same `switch (group->state)` block used in Task 5, alongside `LED_GROUP_ON` and `LED_GROUP_BREATHING`.)

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 5: Commit**

```bash
git add src/led_group.c tests/test_led_group.c
git commit -m "feat: implement BLINK state with per-group phase"
```

---

### Task 7: BLINK_CODE effect (burst + pause + repeat)

**Files:**
- Modify: `src/led_group.c`
- Modify: `tests/test_led_group.c`

- [ ] **Step 1: Write the failing test**

Add this test function to `tests/test_led_group.c`, above `int main(void)`. Uses `blink_code_count = 3`, `period_ms = 100` (so each on/off half is 50ms), `blink_code_pause_ms = 400` — burst is `3 * 100 = 300ms`, full cycle is `300 + 400 = 700ms`.

```c
static void test_blink_code_bursts_pauses_and_repeats(void)
{
    call_recorder_t rec;
    recorder_reset(&rec);
    led_strip_t strip = { recording_write_pixel, &rec };
    static const uint16_t indices[] = { 0 };
    led_group_t group;

    led_group_init(&group, &strip, indices, 1);
    led_group_set_color(&group, 80, 80, 80);
    led_group_set_period_ms(&group, 100);
    led_group_set_blink_code(&group, 3, 400);
    led_group_set_state(&group, LED_GROUP_BLINK_CODE, 0);
    recorder_reset(&rec);

    led_group_update(&group, 0);   /* t=0: burst, phase 0 < 50 -> on */
    assert(rec.count == 1 && rec.calls[0].r == 80);

    led_group_update(&group, 60);  /* t=60: burst, phase 60 -> off */
    assert(rec.count == 2 && rec.calls[1].r == 0);

    led_group_update(&group, 110); /* t=110: burst, phase 10 -> on (2nd blink) */
    assert(rec.count == 3 && rec.calls[2].r == 80);

    led_group_update(&group, 310); /* t=310: past burst_ms=300 -> pause, off */
    assert(rec.count == 4 && rec.calls[3].r == 0);

    led_group_update(&group, 650); /* t=650: still in pause -> off, no new write */
    assert(rec.count == 4);

    led_group_update(&group, 700); /* t=700 wraps to t=0 of next cycle -> on again */
    assert(rec.count == 5 && rec.calls[4].r == 80);
}
```

Update `main()` to call it:

```c
int main(void)
{
    test_off_writes_black_once_via_dirty_flag();
    test_on_writes_scaled_color_and_fans_out_to_all_indices();
    test_brightness_does_not_compound_on_repeated_calls();
    test_breathing_follows_lut_and_wraps_at_period_boundary();
    test_blink_phase_is_independent_per_group();
    test_blink_code_bursts_pauses_and_repeats();
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: FAIL (`LED_GROUP_BLINK_CODE` and `set_blink_code` aren't implemented yet).

- [ ] **Step 3: Implement `set_blink_code` and the BLINK_CODE branch**

In `src/led_group.c`, replace the `led_group_set_blink_code` stub:

```c
void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ms)
{
    group->blink_code_count = count;
    group->blink_code_pause_ms = pause_ms;
}
```

Add a BLINK_CODE case to `led_group_effective_color`, next to BLINK:

```c
    case LED_GROUP_BLINK_CODE: {
        uint32_t period_ms = group->period_ms != 0 ? group->period_ms : 1;
        uint32_t burst_ms = (uint32_t)group->blink_code_count * period_ms;
        uint32_t cycle_ms = burst_ms + group->blink_code_pause_ms;
        /* Both blink_code_count and blink_code_pause_ms default to 0, so a
         * caller that forgets to call set_blink_code() before entering this
         * state would otherwise hit cycle_ms == 0 here - guard the same way
         * period_ms is guarded above. */
        if (cycle_ms == 0) {
            cycle_ms = 1;
        }
        uint32_t t = (now_ms - group->state_entered_ms) % cycle_ms;
        if (t < burst_ms) {
            uint32_t phase = t % period_ms;
            bool on = phase < period_ms / 2;
            if (on) {
                *out_r = group->scaled_r;
                *out_g = group->scaled_g;
                *out_b = group->scaled_b;
            } else {
                *out_r = 0;
                *out_g = 0;
                *out_b = 0;
            }
        } else {
            *out_r = 0;
            *out_g = 0;
            *out_b = 0;
        }
        break;
    }
```

- [ ] **Step 4: Run the test to verify it passes**

Run:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 5: Commit**

```bash
git add src/led_group.c tests/test_led_group.c
git commit -m "feat: implement BLINK_CODE state (burst/pause/repeat)"
```

---

### Task 8: Example usage (consumer-style wiring demo)

This is the piece future projects (e.g. a `biostaq_dispenser`/`cannadorf_v2`-style
migration) will actually copy from. It mirrors the shape of `leds.h`/`leds.cpp`
in both reference repos — named groups, project-specific color macros, a
`led_strip_t` wired to a driver, and a loop calling `led_group_update()` — but
since the library itself must stay dependency-free, the "driver" here is a
`printf`-based stand-in instead of FastLED/Adafruit_NeoPixel. A real project
swaps `console_write_pixel` for a callback that writes to its actual strip;
nothing else about the wiring changes.

**Files:**
- Create: `examples/basic_usage.c`
- Create: `examples/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create the example source file**

Create `examples/basic_usage.c`:

```c
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

    /* Status LED breathes green to show the device is idle and ready. */
    led_group_set_color(&status_led, COLOR_READY);
    led_group_set_brightness_pct(&status_led, 50);
    led_group_set_period_ms(&status_led, 2000);
    led_group_set_state(&status_led, LED_GROUP_BREATHING, 0);

    /* Error LED signals "error code 2": 2 blinks, then a long pause, on repeat. */
    led_group_set_color(&error_led, COLOR_ERROR);
    led_group_set_period_ms(&error_led, 300);
    led_group_set_blink_code(&error_led, 2, 1000);
    led_group_set_state(&error_led, LED_GROUP_BLINK_CODE, 0);

    /* The library never reads the clock itself - the caller (here, a fixed
     * fake step standing in for millis()/HAL_GetTick()) drives every tick. */
    for (uint32_t now_ms = 0; now_ms <= 1000; now_ms += 100) {
        printf("-- now_ms = %u --\n", now_ms);
        led_group_update(&status_led, now_ms);
        led_group_update(&error_led, now_ms);
    }

    return 0;
}
```

- [ ] **Step 2: Create examples/CMakeLists.txt**

Create `examples/CMakeLists.txt`:

```cmake
add_executable(led_group_example basic_usage.c)
target_link_libraries(led_group_example PRIVATE led_group)
```

- [ ] **Step 3: Wire the example into the top-level build**

In `CMakeLists.txt`, add an `LED_GROUP_BUILD_EXAMPLES` option next to the
existing `LED_GROUP_BUILD_TESTS` one, so the full file reads:

```cmake
cmake_minimum_required(VERSION 3.15)
project(led_group C)

add_library(led_group STATIC src/led_group.c)
target_include_directories(led_group PUBLIC include)
target_compile_features(led_group PUBLIC c_std_99)

option(LED_GROUP_BUILD_TESTS "Build led_group unit tests" ON)
option(LED_GROUP_BUILD_EXAMPLES "Build led_group examples" ON)

if(LED_GROUP_BUILD_TESTS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LED_GROUP_BUILD_EXAMPLES AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(examples)
endif()
```

Same reasoning as the tests guard: examples only build when `led_group` is
the top-level CMake project, not when a consuming project pulls it in via
`add_subdirectory()`.

- [ ] **Step 4: Build and run the example**

Run:
```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
build/examples/led_group_example.exe
```
Expected: exits with code 0, and prints 11 blocks (one per `now_ms` step from
0 to 1000 in steps of 100) each starting with `-- now_ms = N --`, followed by
one `pixel[0] = (...)` line for the status LED and (only on steps where the
color actually changed, thanks to the dirty-check) `pixel[1] = (...)` /
`pixel[2] = (...)` lines for the error LED. No crash, no assertion failures.

- [ ] **Step 5: Confirm the existing test suite still passes**

Run:
```bash
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1` (the example doesn't
touch `src/led_group.c` or `tests/`, this just confirms the CMakeLists.txt
edit didn't break anything).

- [ ] **Step 6: Commit**

```bash
git add examples/basic_usage.c examples/CMakeLists.txt CMakeLists.txt
git commit -m "docs: add example demonstrating consumer-style led_group wiring"
```

---

### Task 9: Final verification and push

**Files:** none (verification only)

- [ ] **Step 1: Clean rebuild from scratch**

Run:
```bash
rm -rf build
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```
Expected: builds with no errors. If any `-Wall`-style warnings appear about unused variables/functions, there should be none left at this point — every function defined in `src/led_group.c` is now used by at least one real (non-stub) code path.

- [ ] **Step 2: Run the full test suite**

Run:
```bash
ctest --test-dir build --output-on-failure
```
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 3: Confirm the public header matches the spec exactly**

Open `include/led_group.h` side-by-side with the "Data model" and "API" sections of `docs/superpowers/specs/2026-08-12-led-group-design.md` and confirm every struct field, enum value, and function signature matches. This is a manual read-through, not a script — the spec is the acceptance criteria for the public surface.

- [ ] **Step 4: Push to origin**

```bash
git push origin master
```

- [ ] **Step 5: Verify the push**

Run:
```bash
git log origin/master --oneline -10
```
Expected: shows all commits from Tasks 1-7 (scaffolding through BLINK_CODE) present on `origin/master`.

---

## Explicitly out of scope for this plan

Per the spec's own non-goals and "Migration note": no LED driver integration (FastLED/NeoPixel), no README/LICENSE, and no changes to `biostaq_dispenser` or `cannadorf_v2`. Those remain separate follow-up tasks.
