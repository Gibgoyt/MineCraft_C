# run/src/terrain/ore

**Purpose:** what the ground is made of, by depth. Stone turns to deepslate below `z = -112`, and a table of veins, one row per kind of vein at one range of depths, lays rock and ore into any box of voxels.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Filling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends. It takes no noise fields: veins come from streams.
**Lifetime:** World: the ores are made for one seed. The table itself is the same for every world.
**Threads:** `terrain_ore_init` and `terrain_ore_deinit` on the thread running `terrain_init`. Filling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_ore_init`, `terrain_ore_deinit`: a pair.
- `terrain_ore_table`: the table, for anything that wants to know what lies at a depth.
- `terrain_ore_chunk_veins`: how many veins a row starts in a chunk and keeps in the world, by the draws filling makes: for measuring a row's rate (plan 005 phase 11, `bench_world_map`).
- `terrain_ore_fill_box`: deepslate and veins, into a box of blocks.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/density` (its sibling, for where a column's ground is), `terrain/rng`, `arena`, `assert`, `machine`, `types`, and the generated `tables/blocks/`, for the block ids, and `tables/ores/`, the table.

## The table

The rows live in `config/ores/<ore>/index.yaml`, one file per ore or rock, and `compile/src/game_tables/ores/` generates `tables/ores/` from them (plan 005 phase 11; before that the table was in this module's `index.c`, and moving it changed no world). `config/ores/README.md` has the fields: a row's `order` is its place in the table below, `vein` is its size and `per_256_chunks` the veins tried in 256 chunks. Every number is a row's whole definition, and changing one changes every world. `tests/run/terrain/ore/` types them out again.

The depths start from what was remembered of a familiar game's published ore distributions, not checked against a source, moved to our range: its sea level goes to ours, at 0, and each block away from that becomes about 1.95, as its 384 layers are our 751. So `z = 1.95 * (y - 63)`. As the same veins now have about twice the rock to go in, a row tries about twice as many in a chunk, so that a block of rock holds as much ore as there it would. Nothing was read from that game's files, and these numbers are only a starting point.

"Tried" is the number of veins started in a 16 by 16 chunk: `0.33` means one chunk in 3.

| Row | Makes | `z` from | to | Shape | Size | Tried | Why |
|---|---|---|---|---|---|---|---|
| `ore/dirt` | dirt | -123 | 189 | uniform | 33 | 14 | Pockets of soft ground |
| `ore/gravel` | gravel | -250 | 500 | uniform | 33 | 28 | |
| `ore/granite_low` | granite | -123 | -6 | uniform | 64 | 4 | Patches in the stone |
| `ore/granite_high` | granite | 2 | 127 | uniform | 64 | 0.33 | Rare in the hills |
| `ore/diorite_low` | diorite | -123 | -6 | uniform | 64 | 4 | |
| `ore/diorite_high` | diorite | 2 | 127 | uniform | 64 | 0.33 | |
| `ore/andesite_low` | andesite | -123 | -6 | uniform | 64 | 4 | |
| `ore/andesite_high` | andesite | 2 | 127 | uniform | 64 | 0.33 | |
| `ore/tuff` | tuff | -250 | -123 | uniform | 64 | 4 | Only in the deep rock |
| `ore/gravel_peak` | gravel | 501 | 1250 | uniform | 33 | 28 | `ore/gravel` goes on, as much in a layer, above the height the world first stopped at |
| `ore/coal_high` | coal | 142 | 500 | uniform | 17 | 60 | Mountains are rich in it |
| `ore/coal_peak` | coal | 501 | 1250 | uniform | 17 | 125 | `ore/coal_high` goes on, as much in a layer |
| `ore/coal_low` | coal | -123 | 252 | triangle | 17 | 40 | Most at about 65 |
| `ore/copper` | copper | -185 | 96 | triangle | 10 | 32 | Most at about -45 |
| `ore/iron_high` | iron | 33 | 500 | triangle | 9 | 180 | High in mountains |
| `ore/iron_peak` | iron | 501 | 1250 | uniform | 9 | 60 | What `ore/iron_high` has in a layer at about 450, held to the top. Its triangle is down to nothing at 500 |
| `ore/iron_middle` | iron | -170 | -14 | triangle | 9 | 20 | Most at about -92 |
| `ore/iron_low` | iron | -250 | 18 | uniform | 4 | 20 | A little everywhere below the sea |
| `ore/gold` | gold | -250 | -60 | triangle | 9 | 8 | Most at about -155 |
| `ore/gold_deep` | gold | -250 | -218 | uniform | 9 | 0.5 | A little more at the very bottom |
| `ore/lapis_middle` | lapis | -185 | -60 | triangle | 7 | 4 | Most at about -122 |
| `ore/lapis_low` | lapis | -250 | 2 | uniform | 7 | 8 | |
| `ore/diamond_small` | diamond | -406 | -92 | triangle | 4 | 14 | The triangle's peak is at -249, the bottom of the world, and veins that land below it are lost, so diamonds grow more common with depth |
| `ore/diamond_large` | diamond | -406 | -92 | triangle | 12 | 0.22 | One big one, now and then |
| `ore/emerald` | emerald | -154 | 500 | uniform | 3 | 16 | Only where a chunk's middle column is at least 120 high: a mountain's. This module knows no biomes, and this is the part of "mountain" that needs none. Fewer tried than the remembered number gives, as our veins of 3 really come to about 3: see Stage 10.5's issue 2 |
| `ore/emerald_peak` | emerald | 501 | 1250 | uniform | 3 | 18 | `ore/emerald` goes on, as much in a layer, under the same rule |
| `ore/chromium` | chromium | -250 | -40 | uniform | 6 | 4 | Plan 005 phase 11, for stainless steel: deep only, below most iron, and laid last, so it takes only rock and moves no other row's vein. Every chunk gets exactly 4, as 1024 is a whole 4 times 256 and the whole range is in the world. A starting point Ahmed tunes in `config/ores/chromium/index.yaml` |

**Above 500 is four rows of its own.** The world first stopped at 500, and a row's number changing moves every vein of that row, so the rows that reached that height stay as they were and the `_peak` rows go on from there. No terrain reaches them yet, and a row whose reach is wholly over everything in a box that is not air is left out for that box, which moves nothing, as a vein replaces no air.

There is no redstone: `config/blocks/` has no such block. A row for one goes here when it does.

## Rock by depth

Stone turns to deepslate by height: all of it at or below `-128`, none of it from `-112`, and in between a share of `(-112 - z) / 16`, drawn voxel by voxel from `terrain_rng_hash3`. That is a pure function of the voxel, so a seam never shows between boxes. Deepslate is why a deep ore comes in its deepslate form.

This is in this part because an ore's form depends on it. `-128` is a familiar game's `y = 0`, moved as above.

## What replaces what

Rows go in the table's order, each one replacing what the last left.

| A row | Replaces | Makes |
|---|---|---|
| A rock, its two blocks the same | stone, deepslate | its block |
| An ore | stone, granite, diorite, andesite | `block` |
| An ore | deepslate, tuff | `deep_block` |

Nothing replaces air, water, lava, bedrock, dirt or gravel, or a block a surface rule has put there. So ore can be laid after caves are cut, and shows in their walls; and a vein can overlap another or be cut by the world's edge and just be smaller.

## A vein

For a row in a chunk, from `seed` (`terrain_rng_derive` of the world's seed and the row's name):

1. A stream from `terrain_rng_hash2(seed, chunk_x, chunk_y)`. One draw from 0 to 255: below `per_chunk_256 % 256` means one vein more than `per_chunk_256 / 256`.
2. For each vein, in order: a `u32` whose low 8 bits say where in the chunk (4 bits for `x`, then 4 for `y`); then `z`; then a `u64`, the vein's own seed. A uniform `z` is `z_low + next_below(range + 1)`. A triangular one is `z_low + next_below(range / 2 + 1) + next_below(range - range / 2 + 1)`. These draws are made for every vein, whichever box is being filled, so that a vein cannot depend on which box asked.
3. A vein with `z` outside the world is lost.
4. A stream from the vein's own seed: three balls of one radius. The first's middle is the vein's start plus `next_f32() - 0.5` on each axis (`x`, then `y`, then `z`). Each later one is the last one plus `(next_f32() * 2 - 1) * radius` on each axis. A voxel is in a ball if its squared distance from the middle is at most `radius * radius`, and every voxel in a ball gets the row's replacement.

The radius makes that volume add up to `size`, with balls overlapping by about half: `radius * radius * radius = size / (4 / 3 * pi * 2)`. Worked out by halving and not by libm's `cbrt`, since nothing that decides a block may lean on how some libm rounds. `tests/run/terrain/ore/` measures what comes out.

**Reach.** Nothing in a vein is further from its start than `3 * radius + 1.5` on any axis, which is 7 blocks for the largest. That is less than a chunk, so only the chunk a box is in and the ones next to it can reach it. `terrain_ore_init` asserts this.

**The same bytes from any box.** A voxel's result is the rows and veins that touch it, one after another, in the order row, then chunk from the south-west, then vein. Any box containing that voxel runs all of those, in that order, and whatever else it runs does not touch the voxel. So splitting a box in any way changes nothing, as long as the blocks start the same.

## Not done

A familiar game's ores sometimes refuse to touch air. That asks what is next to a vein, which can be outside the box, so it has no place in a pure function here. Later features can do the same from the cave mask if the difference shows.

`terrain_generate_box` in `run/src/terrain/` calls this after density and caves, for every column the world streams.

## What it costs

A row costs nothing if its depths miss the box. Otherwise it costs a few draws per vein for the 9 chunks round a chunk, whether or not they touch it, and for the few that do, up to about 60 voxels a ball. What a chunk costs is in the stage file. Finding the top of what is not air costs one pass over a box's layers of air, down to the first that is not, for each box.
