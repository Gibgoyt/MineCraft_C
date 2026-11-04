# run/src/game/spawn

**Purpose:** where a new world's body starts: the nearest dry land to the world's middle. Not in the plan's list: plan 003 phase 8 stage 2 made the search bigger, and `game/index.c` is near the size where a part must be split off.
**Tier:** Arena by depth, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept.
**Threads:** any, with its own `game_column_job_t`. `game_open` calls it on `main`, before any job runs, and `bench/world_map/` on its own main.
**Public functions** (`run/include/game/spawn/index.h`):

- `game_ground_z`: a column's highest block, of any kind.
- `game_spawn_bare_ground_z`: the same with what grows on it left out.
- `game_spawn_top_is_dry_land`: whether that block is land to start on.
- `game_spawn_may_be_dry`: whether a column's fields let it be, without making its blocks.
- `game_spawn_is_gentle`: whether the ground round a place is fit to start on, from its fields.
- `game_spawn_walk`: squares about the middle, nearest first, asking a probe.
- `game_spawn_find`: the walk, over real terrain.

**May be called by:** `run/src/game/`, and `bench/world_map/`.
**Depends on:** `game`'s header, `terrain`, `world/column`, the block table.

## What counts as land

The column's very top, made by every stage. It is land if it is solid and is none of these:

| Top | Why not |
|---|---|
| Water, a flower, grass | Not solid |
| A tree's leaves | The top of a tree, in the air |
| Ice, packed ice, blue ice | The whole of a frozen sea, and an iceberg. A cold peak's bare ice is refused with them, as there is other ground |
| Snow on any of those | As dry as what it lies on |
| Anything right on water or lava | It is floating |
| Cactus | It hurts to stand on |

## Gentle ground

A mountain range can stand at the world's middle: on the default seed, the nearest dry land to it is at z = 970. And a body should not start on a ledge. So `game_spawn_find` first looks for dry land with ground at most 120 high, differing by at most 6 blocks from the ground 8 blocks away along each axis, and by at most 14 from that 24 blocks away. This reads the fields, 9 columns of them, and that first. The fields leave out caves and ponds, so a place that passes then has its real ground held to within 6 blocks of that 4 blocks away along each axis, what grows left out, which is 5 columns' blocks at about 1.2 ms each, on few places. Only if a whole walk finds none does it take the nearest dry land of any kind.

## How the search goes

| Band | A square every | Out to | Squares | Places |
|---|---|---|---|---|
| 1 | 16 blocks | 1024 | 65, from the middle itself | 16641 |
| 2 | 64 blocks | 8192 | 112 | 64960 |
| 3 | 256 blocks | 32768 | 96 | 61824 |

That is about 65 km across. The largest sea `bench/world_map/` measured on 8 seeds, before the coasts were tuned, was 116 km^2 and crossed its 16 km map. A coarse band can step over an islet, and it then finds the next land, which is what is wanted when nothing near is.

**What it costs.** Making all of one column's blocks works out a chunk and the chunks round it, about 1.2 ms measured, so the walk asks the column's fields first and makes the blocks only where they leave a top a way to be dry. A whole walk of nothing but fields took 438 ms on the release build. The game's watchdog hears from `game_open_alive_tell` once a square. The worst square, 1024 places all needing their blocks, is about 1.2 s against its 8.

**A seed with no land in all that.** None tried has one, and one can't be made on purpose, so `tests/run/game/spawn/` gives the walk a probe that finds nothing. `game_spawn_find` returns false with the middle as the place, `game_open` warns in the log, the body starts on top of whatever is there, and the game goes on.
