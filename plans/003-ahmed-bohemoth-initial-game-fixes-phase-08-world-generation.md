# 003-ahmed-bohemoth-initial-game-fixes — phase 8: World generation

**Status:** in progress: stage 1 is done, and stages 2 to 7 are not started   (updated 2026-11-04)

## Goal

A world is a large continuous landscape: mostly land, with real mountain ranges, valleys, rivers, seas that end, and caves you can find. You start on land. And there's a map tool, so the shape can be seen and tuned from there.

**Ahmed's words:** "there is just a flat land that has a single mountain", "there is a single terrain and a very very huge area of ocean created not sure if world generation is even being implemented correctly???", "have cave generation and proper world generation to generate mountains", "we can have a very large continuos world and then tune it from there".

## Is world generation implemented correctly?

**Stage 1 settled this: world generation has no real bug beyond its tuning, and one real bug next to it, in where you start.** Every stage runs and does what its README says. It makes a varied world with continents and seas. Its shape is tuned to produce what you saw. The bug is `game_top_is_dry_land`, which took the frozen sea for land: **on your seed, `2198175988018515243`, you start at 8, 8, 0 on ice, in a frozen sea of about 83 km^2, 376 blocks from dry land.** That is why it looked like one piece of land and a huge ocean. The bug is stage 2's. Issues 1 to 3 give the evidence.

The table below is from before the map, by reading the code, and the map confirmed it, with one change: the mountains are worse than it says. See "What the map showed".

| What you saw | Why, in our code |
|---|---|
| **Huge ocean** | Continentalness is spread evenly over -1 to 1, and our land starts at about -0.05 (`DENSITY_KNOTS_CONTINENTAL`, `run/src/terrain/density/index.c:31`). So **about half the world is sea by design**. Measured: 47.9%. |
| **Oceans in huge blobs** | Continentalness has a 2048-block first octave, halving each time (`run/src/terrain/climate/index.c:12`). The biggest octave dominates, so there's little to break up a coast. |
| **Flat land** | Land's base height is only 3 to 64 blocks. Measured: 34.5% of the world is at 0 to 60, which is about two thirds of all land. |
| **A single mountain** | A mountain needs three things at once: far inland, low erosion, and a high ridge. Measured: 4.13% of the world reaches 150, and 0.53% reaches 300. |
| **You start on a frozen sea** | A bug: `game_top_is_dry_land` (`run/src/game/index.c:383`) asks for a solid block, and ice is solid, so a frozen ocean counts as land. |
| **Blue blocks** | That's ice, on `frozen_ocean`. Under it is water. |
| **Caves** | They are there. On 8 seeds, 36 to 56 openings at least 8 deep for each km^2 of land, and 2.7 to 6.6 at least 20 deep. Spawning on the sea is the likely reason you haven't seen any. Whether that is easy to find on foot is yours to judge. |

## What the map showed

`bench_world_map` on 8 seeds, on real blocks. Each seed's own numbers are in `bench/world_map/README.md`. Phase 10's numbers came from the height sum over the whole world, and these from blocks over 16 km, so they differ a little.

| What | On 8 seeds | Means |
|---|---|---|
| Sea | 44.9 to 51.2% | As Phase 10 measured |
| Ice | 9.9 to 21.2% of the map | A fifth to a half of the size of the sea. Temperature's first octave is 4096 blocks, so frozen seas are big |
| Seas of at least 1 km^2 | 4 to 11, the largest 24 to 116 km^2 | They do end, but the largest crossed the whole map on 6 seeds of 8 |
| Land at 150 or more | 4.04 to 6.03%, in **no piece of 1 km^2 on any seed**. The largest is 0.3 to 0.6 km^2, and 0.9 to 2.2 km across | **There are no ranges.** Ground at that height is thin ridge lines winding like a maze, as a ridge is where weirdness crosses 2/3 |
| Sheer ground | 15 to 19.5% of land rises 3 blocks for 1 across | The slices show needle spires on every seed, tens of blocks wide and over a hundred tall |
| Highest ground | 452 to 474, against the limit of 1250 | Nothing yet uses the taller world |
| Typical distance to high ground | 768 to 1856 blocks, and 2368 to 4864 for the far 10% | Near enough, but it's only ridge lines |
| Caves' share of rock | About 4 to 11% in each band of height, down to -250 | The deep levels do have caves |
| Rock that floats | 742 to 994 pieces per km^2 of land, mostly a few voxels. The largest in a seed is 313 to 10734 | See issue 2 |
| Spawn | On ice on 4 seeds of 8, and 8 to 376 blocks from dry land on those | Issue 1 |

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

**A starting point, for Ahmed to tune using the map.** The height bands are reset for the limit of +1250, which is built (phase 11). Today's world stops at 474, so the map cannot show what the new height can do: **the rows from 500 up are a first guess, to change once stage 4 has made such mountains.** The old rows' targets, 15–20% reaching 150 and 3–5% reaching 300, were for a limit of 500. "Today" is the range over the 8 seeds, of the whole map, from `bench_world_map`.

| Measure | Today | Target |
|---|---|---|
| Sea | 44.9–51.2% | About 25–30%, in seas that end |
| Land at 0 to 60 | 26.7–31.3% | About 30% |
| Reaching 150 | 4.04–6.03% | About 20–25%, in ranges |
| Reaching 300 | 0.50–0.86% | About 10–12% |
| Reaching 500 | 0 | About 4–6% |
| Reaching 750 | 0 | About 1.5–2.5% |
| Reaching 1000 | 0 | About 0.2–0.5% |
| Highest ground | 452–474 | About 1100–1200, so that no peak is cut flat at 1250 |
| Pieces of 1 km^2 or more at 150 or over | 0 | Several on any map, with the largest at least 5 km across (today's largest of any size: 0.9–2.2 km) |
| Typical distance to high ground | 768–1856 blocks | Under about 1000 blocks, to a piece of a real range |

## Todo

**Stage 1: see the world.** Nothing else starts until this exists.

- [x] Add `bench/world_map/`, needing no display and no card: a PNG top-down map of about 16 km by 16 km, coloured by height, with sea and ice marked, plus a side-on slice that shows caves
- [x] Have it print the table above for any seed, plus cave openings per square km, and the distance from spawn to land and to a mountain
- [x] Run it on 8 seeds, including Ahmed's `2198175988018515243`, and record whether anything is really broken. If so, fix that first and log it here
- [x] Reset the height bands in the targets for +1250

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
    tools/test-or-build.sh timeout 600 build/release/bin/bench_world_map /tmp/mc_c_maps 2198175988018515243 12636146071196537873 0x4D494E4543524146 1 42 20261104 9999999999999 18446744073709551557
    tools/test-or-build.sh timeout 1200 ctest --preset release -R '^run_terrain_.*$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed looks at the maps and says if it looks right, then:

    ./build/release/bin/minecraft_c --windowed

Run for stage 1: release build with lint, exit 0. `bench_world_map` on the 8 seeds above, exit 0 in under 590 s, with 0 columns differing of 1024 on each. Run again, Ahmed's seed gave the same bytes for its map and its slice (`cmp`). `ctest -R '^(run_terrain_.*|run_game.*|clang_format|standards_lint.*)$'`, 12 of 12, on the code as it stands, which changes no terrain. All 8 maps and 5 slices were read by eye. `pgrep -a minecraft_c` and `pgrep -af "$PWD/build"` print nothing. Not run: `--windowed`, and whether the maps look right to Ahmed, which are his.

## Commits

Appended as they land:

- `8759778` Add bench_world_map, a 16 km map, side-on slices and a table of any seed's real world with no display and no card, and let it ask the game's own spawn search where a body starts.
- `df4c419` Say in the repository's structure what bench/world_map is.

## Issues found

**This changes every world.** Saves hold a seed plus only what changed, so old worlds would come back as different land. Fine for today's test worlds. It's why stage 7 records a generator version.

**Build this before phase 2 onward?** Not needed for them to work, but it's the biggest thing wrong with the game right now.

**Open for Ahmed:** are the targets the right kind of world? The map is there so that you can decide by looking.

**1. A real bug, stage 2's and not fixed here: a frozen sea counts as dry land.** Expected: a body starts on land. Actual: `game_spawn_find`, asked by `bench_world_map`, puts it on `ice` on 4 of 8 seeds, Ahmed's among them, at 8, 8, 0, 376 blocks from dry land. On the other 3 it is in the search's first ring, with dry land 8 to 11 blocks away, as the search stops at the first ice. Left for stage 2, which owns the fix, with the tool's `spawn on` and `to land` columns to check it with.

**2. No other real bug found. What was checked, and what is left to tuning.** Expected: a bug on top of the tuning might show. Actual, on 8 seeds:

- Every seed makes a different world, and by eye no seam, tile, mirror or repeat shows on any map or slice read. Flipping a seed's top bit gives another world.
- The same seed gives the same bytes.
- Each column read for the map agrees with `terrain_generate_box`, 1024 a seed: 0 differed.
- Hydrology's kind agrees with the blocks: nothing called ocean is dry, and 1.3 to 1.8% called something else is sea at sea level, nearly all of it beach, at the shore's edge.
- 50 surface biomes appear on every seed.

Left to tuning, and not bugs, as each is what its README says it does:

- **Thin mazes of ridges, and needles.** Stage 4. Height follows `ridges`, which has a crease at its top and is steep by design. A slice of Ahmed's seed showed a 1-wide run of rock, about 15 tall, floating over a needle's tip, at x = -2228, y = -64, z about 230 to 245. That slice was from an earlier run, as the slice's place has since moved. Raising mountains without changing this makes it worse.
- **Rock that floats.** 4013 to 16700 voxels in the 64 tiles, most in pieces of a few. On Ahmed's seed it is mostly stone, dirt and dripstone, and 43% of it is above z = 50, 2144 voxels of that in one piece. Whether caves or the 3D noise cut each piece free was not separated. The density's README says the noise makes overhangs and arches, and nothing promises they join up. Whether this is acceptable is for stages 4 and 5, which should watch this number.
- **Rivers as sheer cuts.** A river's `carve_top` falls quickly to the bed, and the slices show a plateau cut to sea level with near-upright walls. Rivers also run in closed loops, as lines where weirdness is 0. Stage 3 retunes hydrology.
- **Square water in caves.** Aquifer cells show as straight walls of stone in some caves. The README says that is how two bodies of fluid are kept apart. Stage 5 can look at it.

**3. How far "no real bug" goes.** It covers what this tool looks at, on these seeds: shapes, kinds, connectivity and spawn. It is a reading of pictures and numbers, and does not prove the absence of a bug. It does not look at feature placement, which the tool leaves out of the map, and has its own tests.

**4. The tool's map could not use every stage.** Expected: `terrain_generate_box` on a narrow band of each column. Actual: one seed did not finish in `timeout 590`, using 155 minutes of CPU, since `terrain_feature_fill_box` works out a chunk and those round it for each one-column box. Done: the map runs the stages up to caves, and takes the ice from the biome's `ice` rate. It takes about 18 s, and 1024 columns a seed are still held to every stage. So **the map does not show ponds, frozen ponds, icebergs or ice spikes,** 2 to 8 columns in 1024. The cave tiles and slices do use every stage.

**5. The tool's first table was wrong, and reading a map caught it.** Expected: ice on the map is frozen water. Actual: a cold peak's own surface of ice and packed ice was painted as sea ice, in hatching on a mountain, and counted as rivers and lakes, which read 6.9% on Ahmed's seed where it is 5.6%, and left it out of the height bands, 5.05% against 6.03% at 150. Done: ice is frozen water only with water under it, or at the sea's level. All the numbers here are from after that.

**6. For whoever raises the mountains.** `bench/terrain/tour/`'s `TOUR_LAYERS` is 751, and stops at z = 500. `TERRAIN_DENSITY_HEIGHT_MAX` has to rise, as phase 11's issue 11 says. The terrain tests keep 751 layers, phase 11's issue 5. What taller mountains cost in frame rate and terrain speed is unmeasured, and phase 11 left that to this phase.

**Wiring outside this stage's files.** `game_ground_z` and `game_spawn_find` in `run/src/game/index.c` are no longer static, and are declared in `run/include/game/index.h`. `GAME_WORLD_MIDDLE` names the 8 the game started from. `game_spawn_find` takes a `nullptr` game, for the tool. This way the tool reports where the game truly starts, and stage 2's fix will show in it. No behaviour of the game changes.
