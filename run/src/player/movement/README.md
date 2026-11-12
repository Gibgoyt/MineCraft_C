# run/src/player/movement

**Purpose:** a body's movement, one tick at a time: walk, sprint, sneak, jump, swim, climb and fall.
**Tier:** Arena by depth. It allocates nothing: everything is in the `player_movement_t` it is given, and each move uses `physics`'s own memory.
**Owns:** nothing.
**Lifetime:** The caller's.
**Threads:** One at a time per body, the one holding the map, which is `sim`.
**Public functions** (`run/include/player/movement/index.h`):

- `player_movement_start`: a body at rest at a place.
- `player_movement_place`: the same body put at a place at rest, keeping the player's settings: the fly speed, the speed factor
  and fly mode (plan 006 phase 01: teleports, a respawn, getting out of bed).
- `player_movement_fly_set`, `player_movement_fly_speed`, `player_movement_fly_wheel`, `player_movement_factor`: plan 006
  phase 01's fly mode (`PLAYER_MOVEMENT_MODE_FLY`), its speed on the wheel and `/speed`'s factor.
- `player_movement_box`: its box at a place, for a height.
- `player_movement_tick`: one tick from an intent.
- `player_movement_tick_wind`: the same in a wind (plan 004 phase 08); `player_movement_tick` is this with no wind, to the byte.

**May be called by:** `run/src/player/` and its parts, `run/src/mob/creeper/` for where the body stands, `bench/mobs/`, and tests.
**Depends on:** `physics`, `world/map`, `world/column`, the generated `tables/blocks`, libm.

Nothing here reads a clock or a random number. The same world and the same intents give the same bytes, and the tests hold it to that. The wind is an input like the intent, worked out by the sim from the pure climate, so the same world, intents and winds still do.

## What the numbers add up to

Per-tick numbers at 20 Hz, chosen to give these speeds, which are Minecraft's published ones. `tests/run/player/movement/` measures each.

| Mode | Measure | Value |
|---|---|---|
| Walk | Steady speed | 4.317 blocks/s |
| Sprint | Steady speed | 5.612 blocks/s, only going forward, not crouched and not in a fluid |
| Sneak | Steady speed | 1.295 blocks/s, and will not walk off an edge higher than a step |
| Jump | Height, time in the air | 1.25 blocks, 12 ticks, from the ground only |
| Sprint jump | Push | 0.2 blocks per tick more forward |
| Fall | Terminal speed | 78.4 blocks/s |
| Swim | Sinking, rising with Space | About 2 blocks/s each |
| Lava | Each tick's drag | 0.5, against water's 0.8 |
| Climb | Up, down | 2.35 blocks/s, 3 blocks/s |
| Step | Height walked up without a jump | 0.6 blocks: a slab, not a block |
| Wind (plan 004 phase 08) | Drift standing on the ground | 0.04 blocks/s per block/s of wind over 10: 2.0 in a 60 blocks/s hurricane, 0.8 in a 30 storm, none at 10 or under |
| Wind | Walking into a hurricane, with it | 2.3, 6.3 blocks/s |
| Wind | Behind a solid block within 3 upwind, at feet or head | A quarter: 0.5 blocks/s in the hurricane |
| Wind | In the air; in water, lava or climbing | A fifth of the push; none |
| Fly (plan 006 phase 01) | Speed | 10 x 1.25^(step - 1) blocks/s, steps 1 to 22 on the wheel held to 10 to 1000, 116 by default (step 0 is 12); Ctrl doubles it; it stops when let go: no gravity, no inertia, no fall, standing height |
| `/speed N` | Walking, swimming, flying | N times the push, so N times the steady speed, N from 1 to 10 (0 in a file is 1); jumping and falling are not changed |

**Fly mode.** `mode` stays `PLAYER_MOVEMENT_MODE_FLY` from tick to tick, as the touches never set it; `player_movement_fly_set`
turns it on and off, and off starts the fall at 0 from where it is. Space goes up and Shift down; blocks and unloaded columns
stop it as they stop walking. `fly_step` and `speed_factor` are in the two bytes that were padding, so a file from before reads
as walking at the default speed with a factor of 1.

The body is 0.6 wide and 1.8 tall, 1.5 crouched, with its eyes at 1.62 and 1.27. It stays crouched where there is no room to stand up.

## The modes, in the order they are decided

1. **Waiting:** the column its feet are in is not loaded. It stays exactly where it is, and all its speed is gone.
2. **Lava, then water:** any cell its box overlaps, and kelp or seagrass with water over it, which take a water cell's place in the world. Slow to start and stop, with slow sinking. Space rises, sneak sinks. Swimming into a bank whose top is less than a step above hops out.
3. **Climb:** a ladder or a vine in the cell of its feet, which is where a body stands to use one. Sideways speed and falling are limited. Space or pressing against a wall goes up, and sneak holds still. The climbing is not a flag in `config/blocks/`, since these two blocks are all that have it: lily pads are the same `thin` shape and nobody climbs one.
4. **Ground or air:** whether the last move came down on something.

**The wind** pushes only on land, before the move: a push `a` along the way it blows, added each tick before the move as walking is, settles where each move is `a / (1 - 0.546)`, so `a = D / 20 * (1 - 0.546)` blocks a tick for a drift of `D` blocks a second on the ground (plan 004 phase 08's Issues found has why that is not its file's `D / 20 * (1 - f) / f`). A jump in a gale goes under 2 blocks downwind.

`fall_distance` adds up while falling, and goes to `landed_fall` for the one tick it lands. Fluids and climbing reset it. Stage 13.4's fall damage reads it. `eyes_under_water` is for its drowning.
