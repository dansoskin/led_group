# led_object: reusable addressable-LED effect library

## Origin

Extracted from `LightenObject` in the biostaq_dispenser project
(`lib/leds/lighten_object.{h,cpp}` + `lib/leds/leds.{h,cpp}`). That code is a
C++ class tied to `Arduino.h`, `FastLED.h` (`CRGB`), and Arduino `String`, and
mixes two concerns: (1) a per-object color/brightness + effect state machine,
and (2) project-specific wiring (which physical strip indices belong to which
named LED, which driver writes them, which colors mean what to this device).

This library extracts only concern (1) into a small, dependency-free C
library that can be added as a git submodule to different projects (Arduino/
PlatformIO and plain CMake/bare-metal embedded, both C and C++ callers).
Concern (2) stays in each consuming project, the same way `leds.cpp` does
today.

## Goals

- Independently control multiple named groups of LEDs on the same
  addressable strip (e.g. "the 2 LEDs over a button" vs "a status LED"
  elsewhere on the same strip), each with its own color, brightness, and
  effect state, ticking independently.
- Effects: solid on, off, breathing (smooth pulse), blinking, and a
  "blink code" pattern (blink N times, pause, repeat) for signalling error
  codes.
- Zero dependencies beyond the C standard library (`<stdint.h>`,
  `<stddef.h>`, `<stdbool.h>`). No Arduino, no FastLED/NeoPixel, no
  dynamic allocation, no floating point.
- Usable from both C and C++ (header wrapped in `extern "C"`), and buildable
  both as a PlatformIO library and via plain CMake (e.g. STM32CubeMX
  projects).
- Fast: O(1) work per object per `update()` call, integer-only math, and a
  dirty-check so a physical pixel write only happens when the computed color
  actually changed.

## Non-goals

- Does not know about any specific LED driver (FastLED, Adafruit_NeoPixel,
  raw SPI/PWM, ...). The consuming project supplies a callback.
- Does not own "which project-specific name maps to which physical
  indices" or any color/meaning constants (`COLOR_BATTERY_LOW`, etc.) —
  that stays in project glue code.
- Does not read the clock itself (no `millis()`/`HAL_GetTick()` call inside
  the library) — the caller passes the current tick into every call.
- Multi-group blink-code sequences (e.g. distinct multi-digit codes like
  "2 blinks, pause, 3 blinks, long pause") are out of scope for v1 — only a
  single repeating "N blinks then pause" pattern is supported.

## Package layout

```
led_object/
  include/
    led_object.h
  src/
    led_object.c
  CMakeLists.txt      # add_library(led_object) for CMake/bare-metal projects
  library.json        # PlatformIO metadata; no framework/platform lock
```

`include/` + `src/` is auto-discovered by PlatformIO and is also the natural
layout for `add_subdirectory()` in CMake. The header uses
`#ifdef __cplusplus / extern "C" { ... } #endif` so `.cpp` files can include
it directly.

## Data model

```c
typedef enum {
    LED_OBJ_OFF,
    LED_OBJ_ON,
    LED_OBJ_BREATHING,
    LED_OBJ_BLINK,
    LED_OBJ_BLINK_CODE,
} led_obj_state_t;

typedef void (*led_obj_write_pixel_fn)(uint16_t index, uint8_t r, uint8_t g,
                                        uint8_t b, void *ctx);

// One instance per physical strip/driver. Shared by every led_obj_t that
// lives on that strip - the write function is a property of the driver,
// not of any individual logical LED object.
typedef struct {
    led_obj_write_pixel_fn write_pixel;
    void *ctx;
} led_strip_t;

typedef struct {
    const led_strip_t *strip;
    const uint16_t *indices;
    uint16_t indices_count;

    uint8_t base_r, base_g, base_b;
    uint8_t brightness_pct;        // 0-100, independent of base_r/g/b

    led_obj_state_t state;
    uint32_t state_entered_ms;
    uint32_t period_ms;            // full on+off cycle (BLINK, BLINK_CODE);
                                    // full breathe cycle (BREATHING)
    uint8_t blink_code_count;      // BLINK_CODE only: blinks per burst
    uint32_t blink_code_pause_ms;  // BLINK_CODE only: dark time after burst

    uint8_t last_r, last_g, last_b;
    bool dirty;                    // forces a write on the first update()
} led_obj_t;
```

`base_r/g/b` and `brightness_pct` are stored independently and only
multiplied together when computing the effective color inside `update()`.
This fixes a real bug in the original `LightenObject::set_brightness_percentage`,
which re-scaled the *already-scaled* stored color on every call (repeated or
redundant brightness calls compounded and over-darkened the LED). Keeping
the two values separate makes `led_obj_set_color()` and
`led_obj_set_brightness_pct()` fully independent and order-proof.

`indices` is `uint16_t` (unsigned), not the original's signed `int16_t` — a
pixel index is never negative.

There is no name/identifier field (the original's was only ever used for a
commented-out debug `printf`); a project that wants to log something already
has the object's pointer or array index to do so.

There is no `IDLE` state — `LED_OBJ_OFF` covers "explicitly dark," and
nothing is written until `led_obj_update()` is called, so there's no need
for a separate "untouched" placeholder state.

## API

```c
void led_obj_init(led_obj_t *obj, const led_strip_t *strip,
                   const uint16_t *indices, uint16_t indices_count);

void led_obj_set_color(led_obj_t *obj, uint8_t r, uint8_t g, uint8_t b);
void led_obj_set_brightness_pct(led_obj_t *obj, uint8_t pct);   // clamped to 100
void led_obj_set_period_ms(led_obj_t *obj, uint32_t period_ms); // BLINK/BLINK_CODE/BREATHING
void led_obj_set_blink_code(led_obj_t *obj, uint8_t count, uint32_t pause_ms);

void led_obj_set_state(led_obj_t *obj, led_obj_state_t state, uint32_t now_ms);
led_obj_state_t led_obj_get_state(const led_obj_t *obj);

void led_obj_update(led_obj_t *obj, uint32_t now_ms);
```

`led_obj_update()` computes the effective color for `now_ms`, and — only if
it differs from `last_r/g/b` (or `dirty` is set) — calls
`strip->write_pixel(index, r, g, b, strip->ctx)` once for every entry in
`indices`, then updates `last_r/g/b` and clears `dirty`.

## Effect semantics

All timing is relative to `state_entered_ms`, set whenever
`led_obj_set_state()` is called — so each object's phase is independent, and
two objects in `LED_OBJ_BREATHING` don't have to be in lockstep unless the
caller wants that (e.g. by calling `set_state` on both at the same tick).

- **OFF** — effective color is always `(0, 0, 0)`.
- **ON** — effective color is `base * brightness_pct / 100`, no time
  dependence.
- **BREATHING** — same effective base color, scaled by a lookup table:
  `phase_ms = (now_ms - state_entered_ms) % period_ms`;
  `idx = phase_ms * LUT_SIZE / period_ms`;
  `factor = breathe_lut[idx]` (0-100, ported from the original `effect1`
  table, converted from float 0.0-1.0 to `uint8_t` 0-100).
  Final color = `base * brightness_pct / 100 * factor / 100`.
- **BLINK** — on for the first half of `period_ms`, off for the second half:
  `on = ((now_ms - state_entered_ms) % period_ms) < period_ms / 2`.
  (Old "slow"/"fast" presets become `led_obj_set_period_ms(obj, 1000)` /
  `(obj, 300)`.)
- **BLINK_CODE** — burst of `blink_code_count` blinks at `period_ms`,
  followed by `blink_code_pause_ms` of dark, then repeats:
  `cycle_ms = blink_code_count * period_ms + blink_code_pause_ms`;
  `t = (now_ms - state_entered_ms) % cycle_ms`;
  if `t < blink_code_count * period_ms`: blink on/off exactly as `BLINK`
  using `t % period_ms`; otherwise off (in the pause).

No floating point anywhere; `breathe_lut` is a `static const uint8_t[]`.

## Performance characteristics

- No dynamic allocation anywhere in the library — `led_obj_init()` just
  stores the caller-supplied `indices` pointer/count (caller owns that
  memory for the object's lifetime), matching the original design.
- No floating point — all effect math is integer (LUT lookups, one multiply
  and one divide/modulo per active object per `update()` call).
- `update()` is O(1) per object plus O(k) only when the color has changed,
  where k = `indices_count` (bounded by how many indices that one logical
  LED group has, typically 1-2).
- This is more than sufficient for typical embedded LED-indicator use
  (a handful of objects, update loop in the tens-to-low-hundreds of Hz) —
  no further optimization (e.g. power-of-two period constraints to avoid
  division) is needed for that profile. If a future project needs to drive
  hundreds of pixels or a much higher refresh rate, that would call for a
  different design (batched/block writes) and should be scoped separately.

## What stays in each consuming project (not in this library)

Exactly what `leds.cpp`/`leds.h` do today in biostaq_dispenser: naming
actual LED groups (e.g. `top_left_led` = indices `{3, 5}`), project-specific
color macros (`COLOR_BATTERY_GOOD`, etc.), a `led_strip_t` wired to whichever
driver is in use (FastLED, Adafruit_NeoPixel, raw SPI/PWM...), and calling
`led_obj_update()` for every object once per loop tick with
`millis()`/`HAL_GetTick()`.

## Testing plan

Plain assert-based C unit tests (no external test framework, consistent
with the "no dependencies beyond the C standard library" goal), run via
CMake + CTest. Since the library never reads the clock itself, tests drive
it by passing explicit `now_ms` values and asserting on the sequence of
`write_pixel` callback invocations (a test double records calls into a
buffer) — no hardware or timing flakiness involved. Cases to cover: OFF/ON
solid color, brightness/color independence (the bug fix), BREATHING LUT
phase wraparound, BLINK on/off timing and independent phase across two
objects sharing a `led_strip_t`, and BLINK_CODE burst+pause+repeat
boundaries.

## Migration note (biostaq_dispenser)

Out of scope for this design/repo. A follow-up task in biostaq_dispenser
would add this repo as a git submodule under `lib/`, and rewrite
`leds.cpp`/`leds.h` to wire a `led_strip_t` around `FastLED`/`CRGB` and call
`led_obj_update()` from `all_leds_loop()`. Not done as part of this spec.
