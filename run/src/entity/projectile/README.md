# run/src/entity/projectile

**Purpose:** arrows: what leaves a bow, how it flies, what it hits and what becomes of it.
**Tier:** Arena (depth 2): `projectile_store_start` takes one array from the arena `run/src/entity/` gives it, and nothing after that.
**Owns:** nothing. The array is borrowed from the arena.
**Lifetime:** World.
**Threads:** the sim's, as the store and the grid are.
**Public functions** (`run/include/entity/projectile/index.h`):

- `projectile_store_start`: empty, from a seed and an arena.
- `projectile_fire`: one arrow, turned at random by how inaccurate the shot is.
- `projectile_tick`: one tick of every arrow. `run/src/game/` calls it after `entity_tick`, with the player's box.
- `projectile_knockback`: the published sum, for what is hit.
- `projectile_line_is_clear`: whether anything solid is between two places, by the same walk through the cells an arrow makes.

**May be called by:** `run/src/entity/` for the start, `run/src/mob/` to fire and to look, and `run/src/game/` for the tick and for drawing.
**Depends on:** `entity/store`, `entity/grid`, `physics/collision`, `world/map`, `terrain/rng`, `arena`, `assert`, the block tables and libm.

## How one flies

Each tick, in this order, as the published game does it:

1. In a column not loaded, or about to enter one, it waits and does not age. In lava it burns: that one is our own choice, and items do the same.
2. Whichever comes first along the segment to where its velocity takes it: the target's box, a body's box, or a block's box. The target's and the bodies' count as `PROJECTILE_BODY_MARGIN` wider than they are, and a block's as it is.
3. If nothing: it moves, the velocity is multiplied by 0.99, or 0.6 if it started the tick in water, and 0.05 comes off its rise.

It is a point on a segment and not a box moved by `physics`, because `collision_sweep` slides along what it meets: that is right for a body, and here the first contact is the whole answer. The cells are walked in order, so it cannot pass through anything however fast it goes. A block with no collision box, such as a plant or water, does not stop it. A shooter is never hit by its own arrow.

| What it hits | What follows |
|---|---|
| Target or body | The hit is `ceil(speed * 2)`: 4 half hearts at the 1.6 a skeleton shoots at. A body loses that from `health` and is knocked back here, by the published sum; a target is told through `projectile_hurt_t`, and chooses. The arrow is gone either way |
| Block | It stays `PROJECTILE_BACK_OFF` short of the face, with no velocity, for `PROJECTILE_STUCK_TICKS`. If its block goes, it falls again. Nothing picks it up: the published game keeps a mob's arrows from being picked up too |
| Nothing | Past `PROJECTILE_FLIGHT_TICKS` it is gone, which is a bound of our own |

A body at no health is not there to hit. What takes it away is its own mind's business: see `run/src/mob/skeleton/` and Stage 15.7.

A full store loses what has lain longest, and failing that what has flown longest, and `lost_count` says so. `fired_count` is always the sum of `body_hits`, `target_hits`, `expired_count`, `burned_count`, `lost_count` and `count`. `stuck_count` counts something else: how many times one stuck.
