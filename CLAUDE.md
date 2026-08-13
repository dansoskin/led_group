# led_group

Status: implemented — all 5 effect states complete, tested, and reviewed.

## What this is
A small, dependency-free C library for independently controlling named
groups of LEDs on an addressable strip (off/on/breathing/blink/blink-code
effects). Extracted from `LightenObject` in the biostaq_dispenser project
so it can be reused as a git submodule across multiple embedded projects
(PlatformIO/Arduino and plain CMake).

## Where things stand
- Design spec: `docs/superpowers/specs/2026-08-12-led-group-design.md` —
  read this first. It has the full data model, API, effect semantics, and
  the reasoning behind each decision (e.g. why brightness/base color are
  stored separately, why the breathing LUT is integer not float).
- Implementation plan: `docs/superpowers/plans/2026-08-12-led-group-implementation.md`
  — the 9-task breakdown that was followed, via `subagent-driven-development`
  with a spec-compliance review and a code-quality review after every task,
  plus a final holistic review across the whole implementation.
- `include/led_group.h` + `src/led_group.c` implement the full API: `tick`,
  `init`, `set_color`, `set_brightness_pct`, `set_period_ticks`,
  `set_blink_code`, `set_state`/`get_state`, `update`. All 5 states
  (OFF/ON/BREATHING/BLINK/BLINK_CODE) are real, including the `breathe_lut`
  integer lookup table and the dirty-flag/write-only-on-change logic. The
  historical compounding-brightness bug from the original `LightenObject`
  is fixed and regression-tested.
- The library has NO clock (changed 2026-08-13 for the cannadorf_v2
  integration, replacing a briefly-lived `attach_ms_timer` design): time is
  a shared tick counter advanced by calling `led_group_tick()` once per
  rendering pass, and all durations (periods, pauses) are tick counts — at
  a 10ms cadence, 100 ticks = 1s. Effect tempo therefore tracks the call
  cadence: a stalled caller loop stretches effects rather than skipping
  ahead. BREATHING/BLINK phase off the shared counter (`ticks % period`),
  so all groups with the same period pulse in lockstep regardless of when
  each entered its state; BLINK_CODE anchors to state entry so an error
  code always plays from its first blink.
- `tests/test_led_group.c` has one assert-based test per case in the spec's
  testing plan plus sync/anchor coverage (7 tests total), run via CTest.
  The tick counter is process-global and never reset, so tests mirror it
  through an `advance()` helper and align to phase 0 before asserting exact
  LUT values.
- `examples/basic_usage.c` is a runnable, driver-agnostic demo of how a
  consuming project wires this library up (named groups, project color
  macros, a `write_pixel` callback standing in for a real driver) — mirrors
  the shape of `leds.h`/`leds.cpp` in the reference projects. Builds and
  runs on the host via CMake, no hardware/framework needed.
- `examples/fastled_arduino/fastled_arduino.ino` is the same wiring pattern
  targeting real hardware: FastLED as the driver, `setup()`/`loop()` ticking
  every 10ms like the reference projects' `all_leds_loop()`. Needs the
  FastLED library installed separately (Arduino Library Manager or
  `lib_deps = FastLED`); not part of the plain-CMake build (intentionally
  not wired into `examples/CMakeLists.txt`, since Arduino.h/FastLED.h aren't
  available there).
- The CrowdStrike Falcon Sensor false positive that was blocking execution
  of freshly-compiled binaries during initial implementation (see git
  history around 2026-08-12 if this resurfaces) cleared on its own within
  that same session — `ctest` ran live and passed 100% before the final
  merge, confirming every manual trace done while it was blocked was
  accurate. If a future session hits vanishing/`Permission denied` `.exe`s
  again, that's the same known issue, not a new code bug.
- Two minor, explicitly non-blocking follow-ups noted by the final review
  (optional, not scheduled): (1) `led_group_effective_color()` has some
  duplicated guard/phase-check logic across the BLINK/BLINK_CODE/BREATHING
  cases that could be extracted into two small static helpers; (2) the
  BLINK_CODE `cycle_ms == 0` fallback (entering that state without calling
  `set_blink_code` first) has no dedicated test, though it was traced and
  confirmed to safely stay off rather than crash or flicker.

## Reference source (read-only, different repo)
The original implementation this library is extracted/reworked from:
`C:\Users\dans\Documents\code\biostaq_dispenser\lib\leds\lighten_object.{h,cpp}`
and `leds.{h,cpp}`, and the equivalent files in `cannadorf_v2`. The `effect1`
float breathing curve in `lighten_object.cpp` was the source for
`breathe_lut` (converted from float `0.0`-`1.0` to `uint8_t` `0`-`100`
percentages, verified byte-for-byte against the original table). Do not edit
those repos from here; they're separate projects.

## Not yet decided / not done
- Remote is configured (`https://github.com/dansoskin/led_group.git`) and
  `master` is pushed and up to date.
- No LICENSE or README.
- Wiring this library back into biostaq_dispenser/cannadorf_v2 (adding it as
  a submodule, rewriting `leds.cpp`/`leds.h` to use it) is explicitly out of
  scope for this repo per the spec — a separate follow-up task in those
  repos.
