# bench/cave_walk

**Purpose:** answers the question a map cannot: can a body on foot find a seed's caves and go down them? It measures that on real blocks, and walks a real body from where a game starts into a cave. It needs no display and no card, so it is safe to run whenever. Plan 003 phase 8 stage 5 tunes the caves from this.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers, one `world_t` and one `physics_t` in it, released before it exits, and one `terrain_t` a seed.
**Lifetime:** Process.
**Threads:** main, and 16 job workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 600 build/release/bin/bench_cave_walk [--route-only] <directory> [seed...]`. `--route-only` is the walks alone, with no areas measured. The default seed is `TERRAIN_SEED_DEFAULT`. A seed is decimal, or hex with `0x`. Exit status 0, 1 if a seed has no way in or no way down near where a body starts, the body could not walk one or a file could not be written, or 2 if it could not start. ctest `run_terrain_cave_walk`, in `tests/run/terrain/cave/walk/`, runs it on the default seed.
**May be called by:** people, and that test.
**Depends on:** `terrain`, `game` for its spawn search, `player/movement` and `player/camera` for the body, `physics`, `world`, `display/headless/png`, `job`, `thread`, `heap`, `time`, and the generated `tables/blocks/`.

## What a body can do, here

A place is the voxel of a body's feet, with something solid under it, and no solid block or fluid in it or the one above. From a place, one step to a side goes:

- **along**, or **down** at most 3 blocks, the most that does no harm (`player_survival_fall_damage`), or
- **up** 1 block, by a jump, if there is room over its head.

Nothing is mined, no fluid is swum, and no block is placed. It is a floor on what a person can do, since a person can do all of those. Going back up is not asked for: a way that drops 3 blocks counts as a way in, and may be no way out without a block or a pick.

A place is **in a cave** if at least 8 blocks of ground stand over it in its column, reading the column's top with trees and the like left out. So the foot of an overhang would count too. This is a measure over many places: the walk's own pictures say what one place really is.

## What it measures

9 areas of 512 by 512 blocks, 4096 apart, round the world's middle, with every stage. From every place open to the sky, it walks everywhere it can inside that area, keeping for each place walked to which column of the surface it came in by. So what an area's edge cuts off is left out, and every number here reads low for a long way down.

| Row | What it counts |
|---|---|
| ways/km2 | Ways in: touching surface columns from which a body gets to a place at least 20 under its ground. Per km^2 of dry land |
| typical, far | How far dry land is from the nearest way in, straight, in blocks, for half of it and for 90%. -1 if that much of the land has none in its area. The bins are 8 blocks, by two sweeps that read up to 8% long |
| within128 | The share of dry land with a way in within 128 blocks |
| low-typ, low128 | The same two, of land under 150 high, where a game starts |
| one row per height band | Of all the places to stand in caves in that band, the share walked to from the sky. A seed's own lines add what caves cut there comes to: air, water, lava, or stone again, which is the flood's walls |
| lowest, deepest | The lowest z walked to, and the most ground over any place walked to |
| route, ticks | The fewest steps from where a game starts a body to a place 24 under its ground, and how many ticks the body took |
| deep, ticks | The same, to such a place at `z = -150` or lower |

## The walks

`game_spawn_find` says where a body starts, as it does for the game. A square of 1024 blocks round it is made, and the fewest steps found from there to a place 24 under its ground, `cave`, then to such a place at `z = -150` or lower, `deep`. If either fails, so does the seed. When a walk finds no way, it says how many places it got to and the lowest.

The columns round a route go into a real `world_t`. A real body then follows it: `player_movement_tick`, turning by whole counts of the mouse through `player_camera_look`, holding forward, and holding jump only when the next place is higher. It gives up after 200 ticks with no progress. It starts on the game's own yaw, typed here as `WALK_GAME_START_YAW`.

What it did is written as a script for `--headless`, `<seed>_cave.script` and `<seed>_deep.script`, setting off at tick 100. So the same walk can go through the game itself, with its threads, mobs and renderer, into screenshots:

    tools/test-or-build.sh timeout 280 build/release/bin/minecraft_c --headless --test-world --walk --render-distance 8 \
        --script <dir>/<seed>_cave.script --screenshot-dir <dir>/shots --width 960 --height 540

The program's last lines say where the body ended, which can be held to this tool's. `--test-world` runs the default seed, so only that seed's scripts apply to it. **Nothing can hurt this tool's body, and the game's can be hurt.** On the default seed, the game's body on the deep walk was shot dead by skeletons on the way down, came back at the start and walked the rest of the script on the surface. There is no flag to keep mobs out, as `--no-entities` only stops drawing them.

| File | What |
|---|---|
| `<seed>_cave_unrolled.png`, `<seed>_deep_unrolled.png` | The rock cut along the route and laid flat, a column a pixel, with the body in magenta. Sky pale blue, rock grey, cave near black, water blue, lava orange, what grows green |
| `<seed>_cave_mouth.png`, `<seed>_deep_mouth.png` | A straight slice 256 wide through where the route goes under, the same colours |
| `<seed>_cave.script`, `<seed>_deep.script` | The walks, for `--headless` |

**This is not the game's renderer.** Colours are this program's, typed by hand.

## Results

### 2026-11-04, plan 003 phase 8 stage 5

Release build, 8 seeds, exit status 0 for both runs. About 33 s a seed. "Before" is the caves and the flood as stage 4 left them. The share columns are of places to stand in caves in that band, walked to from the sky inside a 512-block area, so they read low.

| Seed | Ways/km^2 | Typical | Far 10% | Within 128 | Under -150 | -150 to -51 | -50 to -1 | Lowest | Route | Deep route |
|---|---|---|---|---|---|---|---|---|---|---|
| Before: 2198175988018515243 | 157.2 | 88 | 312 | 67% | 0.0% | 0.6% | 7.6% | -70 | 593 | not asked |
| Before: 12636146071196537873 | 256.4 | 88 | 256 | 66% | 0.0% | 0.0% | 0.2% | -22 | 143 | not asked |
| Before: 5569068473609503046 (the default) | 382.0 | 56 | 128 | 91% | 0.0% | 1.4% | 3.8% | -94 | 134 | not asked |
| Before: 1 | 327.0 | 64 | 160 | 85% | 0.0% | 1.0% | 2.9% | -108 | 334 | not asked |
| Before: 42 | 442.9 | 64 | 184 | 80% | 0.0% | 0.8% | 1.9% | -121 | 65 | not asked |
| Before: 20261104 | 358.5 | 56 | 128 | 91% | 0.0% | 0.1% | 1.5% | -87 | 385 | not asked |
| Before: 9999999999999 | 444.9 | 56 | 144 | 88% | 0.0% | 0.6% | 3.0% | -170 | 124 | not asked |
| Before: 18446744073709551557 | 233.9 | 72 | 168 | 81% | 0.1% | 5.1% | 18.2% | -183 | 103 | not asked |
| After: 2198175988018515243 | 722.8 | 56 | 168 | 85% | 11.5% | 17.2% | 26.1% | -229 | 454 | 1139 |
| After: 12636146071196537873 | 1018.5 | 48 | 128 | 91% | 15.0% | 19.6% | 36.6% | -229 | 118 | 927 |
| After: 5569068473609503046 (the default) | 1259.6 | 32 | 88 | 97% | 36.0% | 49.4% | 47.0% | -229 | 120 | 783 |
| After: 1 | 1073.8 | 40 | 104 | 95% | 34.0% | 37.4% | 43.6% | -229 | 250 | 1074 |
| After: 42 | 1718.1 | 32 | 88 | 97% | 39.1% | 43.9% | 53.0% | -229 | 83 | 667 |
| After: 20261104 | 1167.3 | 32 | 88 | 96% | 21.3% | 31.1% | 34.6% | -229 | 259 | 797 |
| After: 9999999999999 | 1476.3 | 32 | 88 | 96% | 31.0% | 34.8% | 41.8% | -229 | 80 | 618 |
| After: 18446744073709551557 | 812.2 | 40 | 104 | 94% | 34.5% | 39.3% | 50.0% | -229 | 83 | 771 |

The lowest is -229 on all 8 as lava lies from -230 down, and this tool does not walk into it.
