# bench/biome_map

**Purpose:** shows which biome holds what ground in a seed's world: a map of about 16 km by 16 km and a table, so `config/biomes/` can be tuned against the terrain as it is. It needs no display and no card, so it is safe to run whenever.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers, released before it exits, and one `terrain_t` a seed.
**Lifetime:** Process.
**Threads:** main, and 16 job workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 280 build/release/bin/bench_biome_map <directory> [seed...]`. A seed is decimal, or hex with `0x`. The default seed is `TERRAIN_SEED_DEFAULT`. Exit status 0, 1 if a picture could not be written or a seed could not start, or 2 if the program could not start.
**May be called by:** people.
**Depends on:** `terrain`, `game` for where the world's middle is, `display/headless/png`, `job`, `thread`, `heap`, and the generated `tables/biomes/`.

## What it writes

`<seed>_biomes.png`: 2048 by 2048, a column every 8 blocks, the same area and grid as `bench_world_map`'s `<seed>_map.png`, so one can be held over the other. Each biome has its colour, printed in the table, and is lit from the upper left so the land's shape shows. Magenta is mushroom fields, so they can be found. **This is not the game's renderer.** Colours are this program's, typed by hand.

## How it knows

A pixel is `terrain_biome_columns_area`, the game's own choice, with its `ground`, which is the fields' height and not real blocks. So a cave's mouth, a pond or a tree does not show here, and a biome's height is what the table's boxes are matched against. It takes about 2 s a seed.

## The table

| Column | What it says |
|---|---|
| map, of land | The biome's share of the map, and of all that is not sea |
| low, typ, high | Its ground: what 10%, 50% and 90% of its columns are at or under, in bins of 4 blocks |
| sea typ, sea far | How far from the sea half and 90% of its columns are, in blocks, by two sweeps that read up to 8% long, and only over this map |

Then, for each height band of `where: land`, its share of the land, the share of it in a biome with at least 1 try of a tree per chunk, and the 4 commonest biomes.

## Results

Plan 003 phase 8 stage 6's before and after are in that phase file.
