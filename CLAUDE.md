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
- `include/led_group.h` + `src/led_group.c` implement the full API: `init`,
  `set_color`, `set_brightness_pct`, `set_period_ms`, `set_blink_code`,
  `set_state`/`get_state`, `update`. All 5 states (OFF/ON/BREATHING/BLINK/
  BLINK_CODE) are real, including the `breathe_lut` integer lookup table and
  the dirty-flag/write-only-on-change logic. The historical compounding-
  brightness bug from the original `LightenObject` is fixed and regression-
  tested.
- `tests/test_led_group.c` has one assert-based test per case in the spec's
  testing plan (6 tests total), run via CTest.
- `examples/basic_usage.c` is a runnable, driver-agnostic demo of how a
  consuming project wires this library up (named groups, project color
  macros, a `write_pixel` callback standing in for a real driver) — mirrors
  the shape of `leds.h`/`leds.cpp` in the reference projects.
- Known environmental issue on the machine this was built on: a CrowdStrike
  Falcon Sensor false positive blocks/deletes execution of any freshly-
  compiled binary that links `src/led_group.c` (compilation is unaffected,
  only running the resulting `.exe` is blocked). Every task's test/example
  behavior was therefore verified by rigorous manual trace against the code
  rather than a live `ctest` run, cross-checked by an independent reviewer
  redoing each trace from scratch. Re-run `ctest --test-dir build
  --output-on-failure` for real once that's resolved (on this machine or a
  different one) to get a live pass/fail confirmation.
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
  `master` is pushed. This implementation was done on a feature branch in an
  isolated worktree — merge it back via `finishing-a-development-branch`.
- No LICENSE or README.
- Wiring this library back into biostaq_dispenser/cannadorf_v2 (adding it as
  a submodule, rewriting `leds.cpp`/`leds.h` to use it) is explicitly out of
  scope for this repo per the spec — a separate follow-up task in those
  repos.
