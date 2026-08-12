# led_group

Status: design phase — no code written yet.

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
- Nothing has been implemented yet — no `include/`, `src/`, `CMakeLists.txt`,
  or `library.json` exist.
- Next step: run the `writing-plans` skill against the spec to produce an
  implementation plan, then implement per that plan (probably via
  `superpowers:executing-plans` or `superpowers:subagent-driven-development`).

## Reference source (read-only, different repo)
The original implementation this library is extracted/reworked from:
`C:\Users\dans\Documents\code\biostaq_dispenser\lib\leds\lighten_object.{h,cpp}`
and `leds.{h,cpp}`. In particular, the `effect1` float breathing curve in
`lighten_object.cpp` is the source for `breathe_lut` in the spec — it needs
converting from float `0.0`-`1.0` to `uint8_t` `0`-`100` percentages.
Do not edit that repo from here; it's a separate project.

## Not yet decided / not done
- No git remote configured (local-only repo so far).
- No LICENSE or README.
- Wiring this library back into biostaq_dispenser (adding it as a
  submodule, rewriting `leds.cpp`/`leds.h` to use it) is explicitly out of
  scope for this repo per the spec — a separate follow-up task there.
