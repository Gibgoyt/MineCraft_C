# run/src/player/movement

**Purpose:** a body's movement, one tick at a time: walk, sprint, sneak, jump, swim, climb and fall.
**Tier:** Arena by depth. It allocates nothing: everything is in the `player_movement_t` it is given, and each move uses `physics`'s own memory.
**Owns:** nothing.
**Lifetime:** The caller's.
**Threads:** One at a time per body, the one holding the map, which is `sim`.
**Public functions** (`run/include/player/movement/index.h`):

- `player_movement_start`: a body at rest at a place.
- `player_movement_box`: its box at a place, for a height.
- `player_movement_tick`: one tick from an intent.

**May be called by:** `run/src/player/`, and tests.
**Depends on:** `physics`, `world/map`, `world/column`, the generated `tables/blocks`, libm.

Nothing here reads a clock or a random number. The same world and the same intents give the same bytes, and the tests hold it to that.

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

The body is 0.6 wide and 1.8 tall, 1.5 crouched, with its eyes at 1.62 and 1.27. It stays crouched where there is no room to stand up.

## The modes, in the order they are decided

1. **Waiting:** the column its feet are in is not loaded. It stays exactly where it is, and all its speed is gone.
2. **Lava, then water:** any cell its box overlaps. Slow to start and stop, with slow sinking. Space rises, sneak sinks. Swimming into a bank whose top is less than a step above hops out.
3. **Climb:** a ladder or a vine in the cell of its feet, which is where a body stands to use one. Sideways speed and falling are limited. Space or pressing against a wall goes up, and sneak holds still. The climbing is not a flag in `config/blocks/`, since these two blocks are all that have it: lily pads are the same `thin` shape and nobody climbs one.
4. **Ground or air:** whether the last move came down on something.

`fall_distance` adds up while falling, and goes to `landed_fall` for the one tick it lands. Fluids and climbing reset it. Stage 13.4's fall damage reads it. `eyes_under_water` is for its drowning.
