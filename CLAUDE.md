# led_group

Status: implemented — all 6 effect states complete, tested, and reviewed.

## What this is
A small, dependency-free C library for independently controlling named
groups of LEDs on an addressable strip (off/on/breathing/blink/blink-code/
running-spot effects). Extracted from `LightenObject` in the biostaq_dispenser project
so it can be reused as a git submodule across multiple embedded projects
(PlatformIO/Arduino and plain CMake).

## Where things stand
- Design spec: `docs/superpowers/specs/2026-08-12-led-group-design.md` —
  read this first. It has the full data model, API, effect semantics, and
  the reasoning behind each decision (e.g. why brightness/base color are
  stored separately, why the breathing LUT is integer not float). The
  LED_GROUP_SPOT state added later has its own spec,
  `docs/superpowers/specs/2026-09-10-led-group-spot-design.md`.
- Implementation plan: `docs/superpowers/plans/2026-08-12-led-group-implementation.md`
  — the 9-task breakdown that was followed, via `subagent-driven-development`
  with a spec-compliance review and a code-quality review after every task,
  plus a final holistic review across the whole implementation.
  LED_GROUP_SPOT followed `docs/superpowers/plans/2026-09-10-led-group-spot.md`
  (5 TDD tasks, executed inline).
- `include/led_group.h` + `src/led_group.c` implement the full API: `tick`,
  `init`, `set_color`, `set_brightness_pct`, `set_period_ticks`,
  `set_blink_code`, `set_spot`, `set_state`/`get_state`, `update`. All 6
  states (OFF/ON/BREATHING/BLINK/BLINK_CODE/SPOT) are real, including the
  `breathe_lut` integer lookup table and the dirty-flag/write-only-on-change
  logic. The historical compounding-brightness bug from the original
  `LightenObject` is fixed and regression-tested.
- SPOT is the one state that is NOT a single uniform color per group: a
  bright spot travels the group in `indices` array order, wrapping at the
  end, trailing a linearly-fading tail into an ambient background. It
  therefore the reason `led_group_effective_color()` takes a pixel index:
  every state resolves through that one function, the four uniform states
  ignore the index, and `led_group_update()` calls it once per pixel.
  (Refactored 2026-09-10 from an earlier `led_group_render_spot()` that
  sat outside `effective_color` — see the spec amendment. Net effect was
  76 bytes LESS flash.) Change detection is the one place the two shapes
  still differ: a uniform state is described by pixel 0's resolved color,
  while the spot also needs its head position, since a pixel far from the
  spot holds the ambient color for many ticks while the strip is moving.
  `led_group_spot_pos()` returns 0 for every non-SPOT state, so
  `update()` folds position into the comparison unconditionally without
  ever triggering a spurious rewrite. Its color is the group's
  base color and its speed is `period_ticks` (one period = one full
  traversal), so `set_spot` only carries the ambient color and the size.
- The dirty flag is set by EVERY setter and by `led_group_set_state()`,
  not just by `init` (changed 2026-09-10). The uniform states never needed
  that — their `last_r`/`last_g`/`last_b` comparison catches any change —
  but the spot path compares head position, so entering SPOT while the
  head sits at position 0 rendered nothing and left the strip black. A
  flat "any mutation marks the group dirty" invariant fixes that with no
  per-state exception, and also covers SPOT -> ON, where
  `last_r`/`last_g`/`last_b` are stale because the spot path never
  maintains them. Cost to the uniform states is at most one redundant
  rewrite of identical values.
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
- `tests/test_led_group.c` has one assert-based test per case in the specs'
  testing plans plus sync/anchor coverage (13 tests total), run via CTest.
  The 6 spot tests pin exact per-pixel RGB values; those were derived from
  the weight/blend formulas in the spot spec before the code was run, so a
  failure there means the implementation drifted, not the expectation.
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
- That false positive DID resurface on 2026-09-10, and the shape of it is
  worth knowing: the CMake-built `build/tests/led_group_tests.exe` runs
  fine and can be relinked and rerun freely, but a freshly-linked
  `led_group_example.exe` is deleted or refuses to exec (`Permission
  denied`) within a second of linking — reproduced at three paths,
  including outside the repo, and with an ad-hoc `gcc` build linking
  `led_group.c` directly. A trivial `hello.c` binary is untouched, so it
  keys on something about this library's binaries rather than on new
  binaries generally. Workaround: verify through the test binary (which
  runs) rather than the example. The example's exact configuration was
  checked that way — a temporary assertion block in
  `tests/test_led_group.c`, run under `ctest`, then reverted.
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
- Remote is configured (`https://github.com/dansoskin/led_group.git`).
  As of 2026-09-10 the LED_GROUP_SPOT work is merged into local `master`
  (spec, plan, 5 implementation commits, and the merge), but `master` has
  NOT been pushed - it is 9 commits ahead of `origin/master`.
- No LICENSE or README.
- Wiring this library back into biostaq_dispenser/cannadorf_v2 (adding it as
  a submodule, rewriting `leds.cpp`/`leds.h` to use it) is explicitly out of
  scope for this repo per the spec — a separate follow-up task in those
  repos.
- Likewise, switching `164_traps_motor`'s `Core/Src/leds.c` from BREATHING
  to the new LED_GROUP_SPOT state is a change in that project, not here.
  Its 40-LED strip and in-order `all_indices[i] = i` are exactly the
  layout the spot was designed against.
