# LED_GROUP_ALTERNATE design

## Goal
A group shows two colors in alternating equal-size blocks along its
`indices` array, and the two colors swap places once every half period:

```
size 1:  R W R W R W   ->  W R W R W R
size 2:  R R W W R R   ->  W W R R W W
```

The swap is a hard flip (all blocks change at the same tick), not a scroll
and not a crossfade. Both colors use the same block size.

## API
One new enum value, `LED_GROUP_ALTERNATE`, appended after
`LED_GROUP_COMET_BACKWARD`. No new setter, no new struct fields - it reuses
what already exists:

| Input | Comes from |
|---|---|
| first color | `led_group_set_color()` (base color) |
| block size, second color | `led_group_set_spot(size, r, g, b)` (`spot_size`, ambient color) |
| swap timing | `led_group_set_period_ticks()` - one period = full A->B->A cycle |
| brightness | `led_group_set_brightness_pct()` - applies to both colors |

```c
led_group_set_color(&g, LED_COLOR_RED);
led_group_set_spot(&g, 2, LED_COLOR_WHITE);
led_group_set_period_ticks(&g, 100);   /* swap every 50 ticks */
led_group_set_state(&g, LED_GROUP_ALTERNATE);
```

The header comment on the enum value states that `set_spot` supplies the
block size and second color for this state.

Rejected alternatives: a dedicated `led_group_set_alternate()` setter (more
API writing the same data), and renaming `set_spot` to something generic
(breaks existing callers such as `164_traps_motor` for a naming nicety).

## Rendering
In `led_group_effective_color()`, a new case:

- `size = spot_size != 0 ? spot_size : 1`
- `period = period_ticks != 0 ? period_ticks : 1`
- `swapped = (ticks % period) >= period / 2`
- pixel `i` shows the base color when `((i / size) % 2 == 0) != swapped`,
  else the ambient color (both via their `scaled_*` brightness-applied
  values).

So in the first half of the period even blocks are base color and odd
blocks are ambient; in the second half they are reversed. Block 0 starts at
array position 0.

Phase derives from the shared tick counter, same as BLINK, so all groups
with the same period swap in lockstep and `led_group_sync_tick()` keeps
boards aligned.

## Change detection
`led_group_update()` is unchanged. Pixel 0 always changes color on a swap,
so the existing pixel-0 probe detects every swap and skips the ticks in
between. `led_group_spot_pos()` returns 0 for this state (it is not a
moving state), so the position term never forces a rewrite. The dirty flag
already covers entering the state and every setter.

Corner case: if base and ambient colors are equal, pixel 0 never changes -
but then no pixel changes, so skipping the rewrite is correct.

## Edge cases
- `spot_size == 0` (default when `set_spot` was never called): treated as
  1, so `i / size` cannot divide by zero.
- `period_ticks == 0`: guarded to 1, like every other state.
- `spot_size >= indices_count`: already clamped to `indices_count` by
  `set_spot`; the whole group is one block and simply alternates between
  the two colors.

## Testing
Assert-based tests in `tests/test_led_group.c`, expected values derived
from the formula above before running:

1. Size 2, aligned to phase 0: exact per-pixel `R R W W R R`; after half a
   period, `W W R R W W`.
2. Size 1: `R W R W ...`.
3. Size 0 behaves exactly like size 1 (no crash).
4. Writes happen only on a swap: a counting `write_pixel` sees one full
   rewrite on entry, none on the ticks between swaps, one at the swap.

## Out of scope
Different block sizes per color, scrolling/marching, crossfading, more
than two colors.
