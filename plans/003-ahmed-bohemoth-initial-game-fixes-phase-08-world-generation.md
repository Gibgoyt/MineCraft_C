# 003-ahmed-bohemoth-initial-game-fixes — phase 8: World generation

**Status:** not started   (updated 2026-11-04)

## Goal

A world is a large continuous landscape: mostly land, with real mountain ranges, valleys, rivers, seas that end, and caves you can find. You start on land. And there's a map tool, so the shape can be seen and tuned from there.

**Ahmed's words:** "there is just a flat land that has a single mountain", "there is a single terrain and a very very huge area of ocean created not sure if world generation is even being implemented correctly???", "have cave generation and proper world generation to generate mountains", "we can have a very large continuos world and then tune it from there".

## Is world generation implemented correctly?

**The pipeline exists and passes its tests, but its shape is tuned to produce what you saw.** This comes from reading the code and the numbers Phase 10 already measured. No map of a real world has been looked at yet, so a bug on top of this isn't ruled out. Stage 1 settles that.

| What you saw | Why, in our code |
|---|---|
| **Huge ocean** | Continentalness is spread evenly over -1 to 1, and our land starts at about -0.05 (`DENSITY_KNOTS_CONTINENTAL`, `run/src/terrain/density/index.c:31`). So **about half the world is sea by design**. Measured: 47.9%. |
| **Oceans in huge blobs** | Continentalness has a 2048-block first octave, halving each time (`run/src/terrain/climate/index.c:12`). The biggest octave dominates, so there's little to break up a coast. |
| **Flat land** | Land's base height is only 3 to 64 blocks. Measured: 34.5% of the world is at 0 to 60, which is about two thirds of all land. |
| **A single mountain** | A mountain needs three things at once: far inland, low erosion, and a high ridge. Measured: 4.13% of the world reaches 150, and 0.53% reaches 300. |
| **You start on a frozen sea** | A bug: `game_top_is_dry_land` (`run/src/game/index.c:383`) asks for a solid block, and ice is solid, so a frozen ocean counts as land. |
| **Blue blocks** | That's ice, on `frozen_ocean`. Under it is water. |
| **Caves** | Code exists in `run/src/terrain/cave/` and has tests. Spawning on the sea is one likely reason you haven't seen any, **but whether they reach the surface often enough is unmeasured**. Stage 1 checks. |

## What open-source generators do

Cloned to `/tmp/mc_c_assets/worldgen/` to read. Nothing was built or run.

| Project | Commit | Licence | What it shows |
|---|---|---|---|
| [Cubiomes](https://github.com/Cubitect/cubiomes) | `e61f905` | MIT | How Minecraft's climate noise is built |
| [Luanti](https://github.com/luanti-org/luanti) (mapgen v7, carpathian) | `9c424c4` | LGPL | A separate, original design |
| [Cuberite](https://github.com/cuberite/cuberite) | `7fd3fa5` | Apache 2.0 | Composable generators: height, biome, then finishers |

**What's useful:**

1. **Weight the middle octaves.** Cubiomes' continentalness has 9 octaves, weighted `1, 1, 2, 2, 2, 1, 1, 1, 1` (`biomenoise.c:879`), so its middle scales count double. Ours has 6, each half the last. **This is the most likely fix for the blobs:** it breaks coasts into bays and islands while keeping continents large.
2. **Smaller features.** Luanti's v7 terrain uses spreads of 500 to 1000 blocks. Its carpathian hills are 257 and ridges 743. Our erosion is 1536 and continentalness 2048.
3. **Separate mountain noise.** Luanti's v7 has its own 3D `np_mountain` over a `np_mount_height` map, with spreads of 250 to 1000. So ranges appear on their own, without needing three gates to line up.
4. **Two-layer caves.** Luanti carves tunnels from two noises (spreads 61 and 67) plus large caverns (384 by 128), which is close to what we have.
5. **Minecraft's land line.** The [Minecraft Wiki](https://minecraft.wiki/w/World_generation) puts ocean below -0.19 and coast up to -0.11. Its noise isn't spread evenly like ours, **so those numbers can't simply be copied**. They do show the idea of setting the coast well below the midpoint.

**Rules for using these:** we learn the technique and write our own code. No LGPL code is copied. Cubiomes reproduces Mojang's generator, so it's used to understand the method, not as a source of tables. No Mojang code or data.

## Proposed targets

**A starting point, for Ahmed to tune using the map.** Ahmed has decided the world goes up to +1250 (phase 11, built first). These bands were written for the old limit of 500, so **stage 1 resets the two "Reaching" rows and adds higher bands** once the map shows what the new height can do.

| Measure | Today | Target |
|---|---|---|
| Sea | 47.9% | About 25–30%, in seas that end |
| Land at 0 to 60 | 34.5% | About 30% |
| Reaching 150 | 4.13% | About 15–20%, in ranges |
| Reaching 300 | 0.53% | About 3–5% |
| Typical distance to a mountain | Unmeasured | Under about 1000 blocks |

## Todo

**Stage 1: see the world.** Nothing else starts until this exists.

- [ ] Add `bench/world_map/`, needing no display and no card: a PNG top-down map of about 16 km by 16 km, coloured by height, with sea and ice marked, plus a side-on slice that shows caves
- [ ] Have it print the table above for any seed, plus cave openings per square km, and the distance from spawn to land and to a mountain
- [ ] Run it on 8 seeds, including Ahmed's `2198175988018515243`, and record whether anything is really broken. If so, fix that first and log it here

**Stage 2: start on land.**

- [ ] Fix `game_top_is_dry_land` so ice isn't land, with a test. Search further than today's `GAME_SPAWN_RINGS`, which reach only about 1 km, and handle a seed with no land nearby

**Stage 3: continents.**

- [ ] Add per-octave weights to `terrain_noise_shape_t` in `run/include/terrain/noise/index.h`, in scalar and NASM, and use them for continentalness and erosion
- [ ] Move the land line in `DENSITY_KNOTS_CONTINENTAL` to reach the sea target, and retune `run/src/terrain/hydrology/` so rivers and coasts still join up

**Stage 4: mountains and valleys.**

- [ ] Raise inland land heights and hills, and add mountain ranges that don't need all three gates to line up
- [ ] Check with the map that ranges run on for a distance, and aren't lone peaks

**Stage 5: caves.**

- [ ] Using the numbers from stage 1, tune `run/src/terrain/cave/` so openings are easy to find, and the deep levels down to -250 have something to explore

**Stage 6: biomes follow.**

- [ ] Recheck `run/src/terrain/biome/`, which reads the same fields, and that the biome coverage test still finds every biome

**Stage 7: finish.**

- [ ] Add a generator version to the world's save, so that a world from an older shape is refused with a clear message, not stitched to a different landscape
- [ ] Update the pinned digests and golden pictures, each with a line saying why
- [ ] Check terrain speed with `bench_terrain`, as more octaves cost time. The plan 001 target is 1000 columns a second, and it's already only just met

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map /tmp/mc_c_maps 2198175988018515243
    tools/test-or-build.sh timeout 1200 ctest --preset release -R '^run_terrain_.*$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed looks at the maps and says if it looks right, then:

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

**This changes every world.** Saves hold a seed plus only what changed, so old worlds would come back as different land. Fine for today's test worlds. It's why stage 7 records a generator version.

**Build this before phase 2 onward?** Not needed for them to work, but it's the biggest thing wrong with the game right now.

**Open for Ahmed:** are the targets the right kind of world? The map is there so that you can decide by looking.
