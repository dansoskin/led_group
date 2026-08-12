# led_group: reusable addressable-LED effect library

## Origin

Extracted from `LightenObject` in the biostaq_dispenser project
(`lib/leds/lighten_object.{h,cpp}` + `lib/leds/leds.{h,cpp}`). That code is a
C++ class tied to `Arduino.h`, `FastLED.h` (`CRGB`), and Arduino `String`, and
mixes two concerns: (1) a per-group color/brightness + effect state machine,
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
- Fast: O(1) work per group per `update()` call, integer-only math, and a
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
led_group/
  include/
    led_group.h
  src/
    led_group.c
  CMakeLists.txt      # add_library(led_group) for CMake/bare-metal projects
  library.json        # PlatformIO metadata; no framework/platform lock
```

`include/` + `src/` is auto-discovered by PlatformIO and is also the natural
layout for `add_subdirectory()` in CMake. The header uses
`#ifdef __cplusplus / extern "C" { ... } #endif` so `.cpp` files can include
it directly.

## Data model

```c
typedef enum {
    LED_GROUP_OFF,
    LED_GROUP_ON,
    LED_GROUP_BREATHING,
    LED_GROUP_BLINK,
    LED_GROUP_BLINK_CODE,
} led_group_state_t;

typedef void (*led_group_write_pixel_fn)(uint16_t index, uint8_t r, uint8_t g,
                                          uint8_t b, void *ctx);

// One instance per physical strip/driver. Shared by every led_group_t that
// lives on that strip - the write function is a property of the driver,
// not of any individual logical LED group.
typedef struct {
    led_group_write_pixel_fn write_pixel;
    void *ctx;
} led_strip_t;

typedef struct {
    const led_strip_t *strip;
    const uint16_t *indices;
    uint16_t indices_count;

    uint8_t base_r, base_g, base_b;
    uint8_t brightness_pct;        // 0-100, independent of base_r/g/b
    uint8_t scaled_r, scaled_g, scaled_b; // = base * brightness_pct / 100, cached

    led_group_state_t state;
    uint32_t state_entered_ms;
    uint32_t period_ms;            // full on+off cycle (BLINK, BLINK_CODE);
                                    // full breathe cycle (BREATHING)
    uint8_t blink_code_count;      // BLINK_CODE only: blinks per burst
    uint32_t blink_code_pause_ms;  // BLINK_CODE only: dark time after burst

    uint8_t last_r, last_g, last_b;
    bool dirty;                    // forces a write on the first update()
} led_group_t;
```

`base_r/g/b` and `brightness_pct` are stored independently and never
overwrite each other. Each of `led_group_set_color()` and
`led_group_set_brightness_pct()` recomputes `scaled_r/g/b = base *
brightness_pct / 100` once, on the spot, whenever either one changes.
`update()` then reads `scaled_r/g/b` directly instead of redoing that
multiply on every tick.

This fixes a real bug in the original `LightenObject::set_brightness_percentage`,
which re-scaled the *already-scaled* stored color on every call (repeated or
redundant brightness calls compounded and over-darkened the LED, and could
even divide by a stale zero brightness if approached by trying to "unscale"
the stored value instead of keeping the base separately). Keeping
`base_r/g/b` as the untouched source of truth and only ever computing
*forward* from it (never trying to reverse a multiply already applied)
avoids both the compounding drift and any divide-by-zero risk.

`indices` is `uint16_t` (unsigned), not the original's signed `int16_t` — a
pixel index is never negative.

There is no name/identifier field (the original's was only ever used for a
commented-out debug `printf`); a project that wants to log something already
has the group's pointer or array index to do so.

There is no `IDLE` state — `LED_GROUP_OFF` covers "explicitly dark," and
nothing is written until `led_group_update()` is called, so there's no need
for a separate "untouched" placeholder state.

## API

```c
void led_group_init(led_group_t *group, const led_strip_t *strip,
                     const uint16_t *indices, uint16_t indices_count);

void led_group_set_color(led_group_t *group, uint8_t r, uint8_t g, uint8_t b);
void led_group_set_brightness_pct(led_group_t *group, uint8_t pct);   // clamped to 100
void led_group_set_period_ms(led_group_t *group, uint32_t period_ms); // BLINK/BLINK_CODE/BREATHING
void led_group_set_blink_code(led_group_t *group, uint8_t count, uint32_t pause_ms);

void led_group_set_state(led_group_t *group, led_group_state_t state, uint32_t now_ms);
led_group_state_t led_group_get_state(const led_group_t *group);

void led_group_update(led_group_t *group, uint32_t now_ms);
```

`led_group_update()` computes the effective color for `now_ms`, and — only
if it differs from `last_r/g/b` (or `dirty` is set) — calls
`strip->write_pixel(index, r, g, b, strip->ctx)` once for every entry in
`indices`, then updates `last_r/g/b` and clears `dirty`.

## Effect semantics

All timing is relative to `state_entered_ms`, set whenever
`led_group_set_state()` is called — so each group's phase is independent,
and two groups in `LED_GROUP_BREATHING` don't have to be in lockstep unless
the caller wants that (e.g. by calling `set_state` on both at the same
tick).

- **OFF** — effective color is always `(0, 0, 0)`.
- **ON** — effective color is the cached `scaled_r/g/b` as-is, no time
  dependence and no per-tick multiply.
- **BREATHING** — `scaled_r/g/b` scaled by a lookup table:
  `phase_ms = (now_ms - state_entered_ms) % period_ms`;
  `idx = phase_ms * LUT_SIZE / period_ms`;
  `factor = breathe_lut[idx]` (0-100, ported from the original `effect1`
  table, converted from float 0.0-1.0 to `uint8_t` 0-100).
  Final color = `scaled_r/g/b * factor / 100`.
- **BLINK** — on for the first half of `period_ms`, off for the second half:
  `on = ((now_ms - state_entered_ms) % period_ms) < period_ms / 2`.
  (Old "slow"/"fast" presets become `led_group_set_period_ms(group, 1000)` /
  `(group, 300)`.)
- **BLINK_CODE** — burst of `blink_code_count` blinks at `period_ms`,
  followed by `blink_code_pause_ms` of dark, then repeats:
  `cycle_ms = blink_code_count * period_ms + blink_code_pause_ms`;
  `t = (now_ms - state_entered_ms) % cycle_ms`;
  if `t < blink_code_count * period_ms`: blink on/off exactly as `BLINK`
  using `t % period_ms`; otherwise off (in the pause).

No floating point anywhere; `breathe_lut` is a `static const uint8_t[]`.

## Performance characteristics

- No dynamic allocation anywhere in the library — `led_group_init()` just
  stores the caller-supplied `indices` pointer/count (caller owns that
  memory for the group's lifetime), matching the original design.
- No floating point — all effect math is integer (LUT lookups, one multiply
  and one divide/modulo per active group per `update()` call).
- The brightness multiply (`base * brightness_pct / 100`) happens once,
  inside `led_group_set_color()`/`led_group_set_brightness_pct()`, cached
  into `scaled_r/g/b` — `update()` never repeats it. The only per-tick math
  is the LUT phase lookup for `BREATHING`/`BLINK`/`BLINK_CODE`; `ON`/`OFF`
  do no math at all.
- The `breathe_lut` table is `uint8_t` (0-100), not the original's `float`
  (0.0-1.0), specifically because the library must run unmodified across
  different MCUs, and float performance is not portable the way integer
  performance is: on FPU-equipped parts (ESP32 Xtensa, STM32F4/F7/H7 "F"
  variants) a float multiply is about as cheap as an integer one, but on
  FPU-less parts (STM32F0/G0, Cortex-M0/M0+) float arithmetic is emulated
  in software and can be an order of magnitude slower. The `/ 100` this
  introduces isn't a real cost either: dividing by a compile-time constant
  is folded by GCC/Clang into a multiply-and-shift at normal optimization
  levels (`-Os`/`-O2`), on every target this library targets — so it is
  not an actual division instruction at runtime, unlike the genuine
  runtime divisions by `period_ms` (a variable) already required for
  `BREATHING`/`BLINK`/`BLINK_CODE` phase math. Net effect: the integer LUT
  is at worst equal to, and on FPU-less targets substantially faster than,
  keeping the table as floats.
- `update()` is O(1) per group plus O(k) only when the color has changed,
  where k = `indices_count` (bounded by how many indices that one logical
  LED group has, typically 1-2).
- This is more than sufficient for typical embedded LED-indicator use
  (a handful of groups, update loop in the tens-to-low-hundreds of Hz) —
  no further optimization (e.g. power-of-two period constraints to avoid
  division) is needed for that profile. If a future project needs to drive
  hundreds of pixels or a much higher refresh rate, that would call for a
  different design (batched/block writes) and should be scoped separately.

## What stays in each consuming project (not in this library)

Exactly what `leds.cpp`/`leds.h` do today in biostaq_dispenser: naming
actual LED groups (e.g. `top_left_led` = indices `{3, 5}`), project-specific
color macros (`COLOR_BATTERY_GOOD`, etc.), a `led_strip_t` wired to whichever
driver is in use (FastLED, Adafruit_NeoPixel, raw SPI/PWM...), and calling
`led_group_update()` for every group once per loop tick with
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
groups sharing a `led_strip_t`, and BLINK_CODE burst+pause+repeat
boundaries.

## Migration note (biostaq_dispenser)

Out of scope for this design/repo. A follow-up task in biostaq_dispenser
would add this repo as a git submodule under `lib/`, and rewrite
`leds.cpp`/`leds.h` to wire a `led_strip_t` around `FastLED`/`CRGB` and call
`led_group_update()` from `all_leds_loop()`. Not done as part of this spec.
