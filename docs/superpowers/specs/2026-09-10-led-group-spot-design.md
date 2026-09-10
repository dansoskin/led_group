# led_group: LED_GROUP_SPOT (running comet spot)

Date: 2026-09-10
Status: approved, not yet implemented

## Goal

Add a sixth effect state to `led_group` in which a bright "spot" travels
along the group at a fixed tempo, trailing a tail that fades into an
ambient background color. Every other pixel in the group sits at the
ambient color.

Motivating use: the 40-LED WS2812B strip in `164_traps_motor`
(`Core/Src/leds.c`), where one group covers indices 0-39 in order.

## Why this state is structurally different

The five existing states (`OFF`, `ON`, `BREATHING`, `BLINK`,
`BLINK_CODE`) all resolve to **one uniform color for the whole group**:
`led_group_effective_color()` computes a single RGB triple and
`led_group_update()` fans it out to every index, caching it in
`last_r`/`last_g`/`last_b` as the change-detection mechanism.

A running spot is inherently **per-pixel** - different indices hold
different colors within the same tick - so it cannot reuse either the
single-color computation or the single-color dirty check. It needs its
own render path and its own change-detection. This is the only part of
the design that is not purely additive.

## Data model

### Enum

`LED_GROUP_SPOT` is appended last so the numeric values of the five
existing states stay stable for any consumer that persists or transmits
them.

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

### New led_group_t fields

```c
uint16_t spot_size;                          /* pixels in the spot, incl. head */
uint8_t  amb_r, amb_g, amb_b;                /* ambient, as given */
uint8_t  scaled_amb_r, scaled_amb_g, scaled_amb_b;  /* ambient * brightness_pct */
uint16_t last_spot_pos;                      /* change detection for the spot path */
```

The ambient color is stored twice for the same reason the spot color
already is: `brightness_pct` can change at any time, so the value as
given by the caller has to survive in order to be re-scaled. Scaling is
precomputed rather than done per pixel per tick.

`led_group_init()` initializes `spot_size = 0`, ambient `0,0,0`,
`last_spot_pos = 0`.

## API

The spot color is the group's existing base color, set through
`led_group_set_color()`, so `brightness_pct` scales it exactly as it does
in every other state. Only the ambient color and the size are new:

```c
void led_group_set_spot(led_group_t *group, uint16_t spot_size,
                        uint8_t amb_r, uint8_t amb_g, uint8_t amb_b);
```

Speed reuses `led_group_set_period_ticks()`: **the period is the time for
one full traversal of the group.** This keeps the state consistent with
`BREATHING`/`BLINK` (same shared-counter phasing, so same-period groups
stay in lockstep) and adds no new tempo concept.

Call-site shape:

```c
led_group_set_color(&g, 0, 255, 0);       /* spot */
led_group_set_spot(&g, 4, 20, 20, 20);    /* size + ambient */
led_group_set_period_ticks(&g, 200);      /* 2 s per lap at a 10 ms cadence */
led_group_set_state(&g, LED_GROUP_SPOT);
```

### Argument handling

- `spot_size` clamps to `indices_count`, so a wrapped spot can never
  overlap its own tail and no pixel is written twice in one pass.
  `led_group_init()` is already a precondition of every other setter, so
  `indices_count` is known by the time this is called.
- `spot_size == 0` means *no spot*: the group renders pure ambient. This
  is a documented meaning, and it is also what keeps the weight division
  below safe without a special-case guard elsewhere.
- `period_ticks == 0` and `indices_count == 0` are guarded the way
  `BREATHING` already guards `period_ticks`, so neither can produce a
  divide-by-zero on the target.

## Effect semantics

### Position

Phase comes off the shared tick counter, not from state entry, matching
`BREATHING`/`BLINK`:

```c
pos = (s_ticks % period_ticks) * indices_count / period_ticks;
```

`pos` is a position within the group's `indices` array, not a strip
index - the spot travels in `indices` array order. For
`164_traps_motor`'s `all_indices[i] = i` that means the spot runs 0 -> 39.

At `period_ticks = 200` over 40 pixels, each pixel holds the head for 5
ticks (50 ms at the project's 10 ms cadence).

### Comet shape and wrap

The head sits at `pos`; the tail trails to *lower* array positions and
wraps around the end of the group. For pixel `i`:

```c
offset = (pos + count - i) % count;     /* 0 == head */
```

`offset < spot_size` puts the pixel in the spot; anything else is
ambient. Within the spot the weight falls linearly from the head:

```c
w = (spot_size - offset) * 100 / spot_size;      /* percent */
```

and each channel blends spot toward ambient:

```c
out = (spot * w + ambient * (100 - w)) / 100;
```

The blend uses the brightness-scaled spot and ambient values, and
`uint16_t` intermediates so no channel product overflows.

Consequences worth stating explicitly:

- `spot_size = 4` gives weights `100, 75, 50, 25`. With spot green
  `(0,255,0)` on ambient `(20,20,20)`, offset 1 renders `(5,196,5)`.
- `spot_size = 1` gives a single pixel at weight 100 - a hard spot, no
  tail. The comet degenerates gracefully rather than dividing by zero.
- The tail's last pixel is never exactly ambient (its weight is
  `100/spot_size`, not 0), so the trailing edge stays visible.
- The spot spans the seam on wrap when the head is near the *start* of
  the group, since the tail runs to lower positions: with `count = 8` and
  `spot_size = 3`, a head at position 1 lights 1 (head), 0, and 7. A head
  at position 7 lights 7, 6, 5 and crosses nothing.

### Render path and change detection

`led_group_update()` gains a single branch: `LED_GROUP_SPOT` dispatches
to a new `led_group_render_spot()`, and every other state keeps today's
`led_group_effective_color()` + uniform fan-out unchanged.

`led_group_render_spot()` makes one pass over `indices`, computing each
pixel's color from `offset` as above - one modulo per pixel, negligible
on the STM32G0. It writes only when `dirty || pos != last_spot_pos`, then
stores `pos` in `last_spot_pos` and clears `dirty`.

`last_r`/`last_g`/`last_b` are not maintained by the spot path; the dirty
invariant below is what covers transitions in and out of it.

**Amended 2026-09-10, after implementation.** The separate
`led_group_render_spot()` was folded into `led_group_effective_color()`,
which now takes a pixel index; every state resolves through that one
function and `led_group_update()` calls it per pixel. The four uniform
states ignore the index. This was a deliberate consistency choice, and it
cost nothing: flash went DOWN 76 bytes, because removing the separate
render function saved more than the two callers grew.

Two things had to adapt. First, `led_group_spot_pos()` now returns 0 for
every non-SPOT state — without that, folding the head position into
`update()`'s change detection made uniform states rewrite spuriously
whenever the notional position ticked over, which the pre-existing
`LED_GROUP_ON` test caught. Second, `update()` guards
`indices_count == 0` and returns before the probe call, which is what
keeps the SPOT case's `% count` safe. `last_r`/`last_g`/`last_b` now
hold pixel 0's color for every state, spot included.

**Amended again 2026-09-10: symmetric profile and direction control.**
The original bright-head/linearly-fading-tail comet was replaced by a
profile that is brightest in the MIDDLE of the spot and fades
symmetrically toward both ends, and a `led_group_set_spot_direction()`
setter (`led_group_spot_dir_t`: FORWARD = 0, REVERSE) was added.

Because the profile is symmetric, direction only mirrors the position -
`pos = count - 1 - pos` - and never touches the weights. That keeps the
motion continuous across the seam.

The weight math works in DOUBLED distances, because an even spot_size has
its middle between two pixels and integer halves would lose it. For a
pixel at `offset` within the spot: `span2 = spot_size - 1`,
`d2 = |2*offset - span2|`, `ring = (d2 + 1) / 2`,
`levels = (spot_size + 1) / 2`, and
`w = (spot_size / 2 + 1 - ring) * 100 / levels`. An even size plateaus
its peak across the middle two pixels so the peak still reaches the full
spot color. Sample profiles: size 3 gives 50/100/50, size 5 gives
33/66/100/66/33, size 6 gives 33/66/100/100/66/33.

This was verified exhaustively for sizes 1-200: the peak is always
exactly 100, the ends never reach 0, and every profile is symmetric. The
upper bound is not cosmetic - `led_group_blend()` computes `100 - w` in
unsigned arithmetic, so a weight above 100 would underflow to a huge
value and corrupt the channel.

The runtime cost is that `led_group_spot_pos()` is evaluated once per
pixel rather than once per pass, so a 40-LED render pass does roughly 128
software divides on the Cortex-M0+ instead of 48. Since the position only
changes every few ticks, most passes take the cheap early-return path
(about 5 divides), so the average lands near a quarter of one percent of
a 64 MHz core at a 10 ms cadence. If that ever matters, hoisting `pos`
out of the per-pixel call is the fix.

## Behavior change to existing code: the dirty invariant

**Every setter and `led_group_set_state()` will set `dirty = true`.**

Today only `led_group_init()` sets `dirty`, and the uniform states get
away with that because the `last_r`/`last_g`/`last_b` comparison in
`led_group_update()` catches any change to the resolved color.

The spot path compares *position* instead, which that comparison cannot
cover. Concretely, an `OFF -> SPOT` transition while `pos == 0` would
find `pos == last_spot_pos` with `dirty == false`, render nothing, and
leave the strip black. Making "any mutation marks the group dirty" a flat
invariant fixes this with no exceptions to remember, and is more robust
than a spot-specific special case.

Effect on the existing states: at most one redundant rewrite of identical
values after a setter call that changed nothing. No visual or timing
difference.

## Testing plan

Extending `tests/test_led_group.c`, which is assert-based and run via
CTest. Tests use an 8-pixel group, well inside `MAX_RECORDED_CALLS` (64),
and align to phase 0 through the existing `align_to_phase0()` helper
before asserting exact values.

The comet weights are integer percentages, so every expected RGB value is
exact - no approximate comparisons.

1. **Comet profile.** Head at full spot color, tail weights falling
   linearly, ambient on every pixel outside the spot.
2. **Travel and wrap.** Position advances with ticks, wraps at the end of
   the group, and the spot spans the seam (head at 1 lights 1, 0, 7).
3. **Degenerate sizes.** `spot_size = 0` renders pure ambient;
   `spot_size = 1` renders a hard single-pixel spot with no tail.
4. **Brightness scaling.** `brightness_pct` scales the spot *and* the
   ambient color, and does not compound across repeated calls.
5. **Dirty invariant regression.** `OFF -> SPOT` repaints on the very
   next `led_group_update()` even when `pos == last_spot_pos == 0`.

## Out of scope

- Bounce/ping-pong travel. Wrap-around only; a direction or mode flag is
  not added until something needs it.
- Multiple simultaneous spots on one group. Two overlapping groups on the
  same strip already approximate this if it is ever wanted.
- A configurable fade curve. The linear ramp is the whole tail profile;
  a LUT-based curve is not added speculatively.
- Rewiring `164_traps_motor`'s `leds.c` onto the new state. That is a
  change in the consuming project, not in this library.
