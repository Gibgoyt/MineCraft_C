# run/src/mob/skeleton

**Purpose:** what a skeleton has in mind: it finds what it is after, keeps its distance, circles, draws and shoots arrows, and leaves its drops when it dies.
**Tier:** Arena (depth 2): `mob_skeleton_start` takes one array from the arena `run/src/mob/` gives it, and nothing after that.
**Owns:** nothing. The array is borrowed from the arena.
**Lifetime:** World.
**Threads:** the sim's, as the store is.
**Public functions** (`run/include/mob/skeleton/index.h`):

- `mob_skeleton_start`, `mob_skeleton_seed`: nobody has anything in mind; what it rolls from here on.
- `mob_skeleton_tick`: one tick of every skeleton in a store.
- `mob_skeleton_drops`: what dying leaves, by `mob_drops`, for any mob's row.

**May be called by:** `run/src/mob/` for the start, and `run/src/game/` for the seed and the tick, which it calls after `mob_tick` and before `entity_tick`.
**Depends on:** `entity` (`store`, `item_drop`, `projectile`), `mob/path`, `item/stack`, `physics/collision`, `world/map`, `terrain/rng`, `arena`, `assert`, the block and mob tables, and libm.

## What it does

The numbers are the published game's (15, 16, 20 and 60 are its bow goal's and its targeting's), but for `MOB_SKELETON_NEAR`: the published one stays until 3.75 blocks, and the plan asks this one to keep its distance, so it starts backing off at 6.

| Where the target is | What it does |
|---|---|
| Nothing to be after | Stands, and about once in 120 ticks walks somewhere within 10 blocks |
| In sight, within 16 | It is engaged from now on, and its arms come up |
| Engaged, in sight, inside `NEAR` | Steps back, drifting aside, at its whole speed |
| Engaged, in sight, from `NEAR` to `FAR` | Only circles, at half speed, thinking again every 20 ticks with 30 in 100 to turn round |
| Engaged, in sight, from `FAR` to 15 | Comes on and circles, each at half speed |
| Engaged, out of sight or past 15 | Walks a path by `mob/path` to within 8 blocks, asking anew every 20 ticks |
| Past 16, or 60 ticks out of sight | Forgets |

- **A step it will not take.** Stepping back or aside uses no path, so before each one it looks a block ahead, under each corner of its box: nothing solid at its feet or head there, no water or lava, and a floor within 3 cells down, in a loaded column. Failing that it tries the other side, then each side alone, then stands. So a body with a cliff, lava or a wall behind it stays where it is and goes on shooting.
- **Sight** is the line from its eye to the target's, through `projectile_line_is_clear`: what would stop an arrow stops it, and so glass does too.
- **The bow.** While it is engaged the bow draws for 20 ticks. It looses as soon as it is drawn and the target is in sight within 15, and rests for 20. So the first shot comes a second after it engages, and then one every two seconds. A shot aims a third of the way up the target, plus 0.2 up per block away to allow for the fall, at speed 1.6, and goes astray by an inaccuracy of 6, the published one at normal difficulty.
- **The look.** It faces the target while it keeps its distance. On a path its feet go where the path goes, and `head_yaw` and `head_pitch` look at the target all the same. `aim` eases to 1 while engaged, and `entity/animation` raises its arms by that.
- **Water.** In water it swims up each tick.
- **Death.** One with no `health` left, however that came about, leaves `mob_drops`' rows for it around where it stood and is taken away. Each row is rolled for itself, with both of its rolls every time. A skeleton's are 0 to 2 bones and 0 to 2 arrows, from `config/mobs/skeleton/`.

A body waiting for its column thinks about nothing, but can still die. A slot used again starts over, by `owner`, however the last one left it: `mob/spawn`'s despawning does not tell it.

Everything rolled is from `random`, in the store's order, so one world and one set of calls gives one outcome. What goes astray is rolled in `entity/projectile`, from its own stream.
