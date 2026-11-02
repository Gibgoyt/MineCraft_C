# bench/terrain/tour

**Purpose:** the phase gate's biome tour: a picture of every biome for a person to look at, with no display and no card, so it is safe to run whenever. Writes `<biome>.png` for each, and `all.png`, a sheet of them all.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers, released before it exits.
**Lifetime:** Process.
**Threads:** main, and 16 job workers.
**Public functions:** `main`. Run it as `tools/test-or-build.sh timeout 600 build/release/bin/bench_terrain_tour <directory> [seed]`. Exit status 0, 1 if a biome was not found or a picture could not be written, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `terrain`, `job`, `thread`, `heap`, and the generated `tables/biomes/` and `tables/blocks/`.

Each picture is the map of 128 by 128 blocks round the first column found of that biome, from above, at 2 pixels a block, with below it a slice across the middle of that map, 64 blocks tall, with upward on the page upward in the world. The search for each goes over 49 km, sampled every 64 blocks, and the tool prints the place of each biome.

**This is not the game's renderer.** Colours are this program's table, by block, typed by hand: it shows shapes, sizes and neighbours. A map is shaded by its slope. An underground biome's map shows the ground over it, and its slice is what is worth looking at.

`all.png` goes across then down, in the order the tool prints, at 1 pixel a block.
