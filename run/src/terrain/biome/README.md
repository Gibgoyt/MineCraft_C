# run/src/terrain/biome

**Purpose:** which of the biomes in `config/biomes/` holds any column of the world, and any voxel under it.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and the table is generated.
**Lifetime:** World: its biomes are made for one seed.
**Threads:** `terrain_biome_init` and `terrain_biome_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_biome_init`, `terrain_biome_deinit`: a pair.
- `terrain_biome_family`, `terrain_biome_family_of_class`: plan 006 phase 03's climate family of a place's numbers, and the
  family a climate class names.
- `terrain_biome_height`, `terrain_biome_climate_z`: the biome height the landform box reads (the ground less plan 007
  phase 03's plateau lift: the one place it is worked out), and the height the family is worked out at (the sea's top for oceans).
- `terrain_biome_wetness`: the wetness field biased by the yearly rain.
- `terrain_biome_distance`, `terrain_biome_choose`, `terrain_biome_caves_of`: the choice, as plain arithmetic.
- `terrain_biome_of_voxel`: the biome of a voxel in a column already worked out.
- `terrain_biome_columns_area`, `terrain_biome_column_at`, `terrain_biome_at`: the same from the world, each with the climate
  sample round the area (plan 006 phase 02's seam), or `nullptr` where there is no climate: then each 16-block tile fills
  `terrain_climate_fallback`'s for itself from the world's seed, so tests and benches need no sample.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/fields`, `terrain/hydrology` and `terrain/climate_sample` (its siblings; the last for the type it is handed), `arena`, `assert`, `types`, and the generated `tables/biomes/`.

## Choosing a column's biome

Plan 006 phase 03: **the climate decides what kind of land, and noise only adds variety inside it** (Ahmed, 2026-11-12).

1. **The family.** `terrain_climate_nearest_at` reads, at the column's corner of the 4-block lattice, the column's own climate cell (the one nearest the point after
   `terrain_climate_at`'s warp) at the column's own height (`terrain_biome_climate_z`: the ground rounded down, or the sea's
   top for ocean and deep ocean), and `terrain_biome_family` makes it plan 004 6.10's class in the same order: Ice and
   Tundra by the warmest month at the column; then dryness from the cell (a Desert or Steppe cell gives that family, as its
   rain against P_th at its own mean ground says; only water, which has no class, works P_th out at the column); then the
   tropical, temperate and cold classes by the column's coldest and warmest months and the cell's driest month, monsoon flag
   and dry summer (`TERRAIN_CLIMATE_DRY_SUMMER`, Mediterranean). So a peak can be Tundra while its valley is Temperate,
   and away from the heights a column's family is its cell's class. Families: Ice,
   Tundra, Boreal, Continental, Temperate, Mediterranean, Steppe, Desert, Savanna, Monsoon, Rainforest (`biome_family_t`).
   The nearest cell and not the blend: a blend of two classes' numbers can make a third (a Desert cell beside a Rainforest
   one blends to Savanna), and its biome then belongs to neither. The warp, held to 31 blocks here, still moves which cell
   is nearest, so family borders wobble rather than follow the 64-block grid, and a cell's centre always reads its own.
2. **Within the family**, only rows whose `families` hold it, of the `where` below, and the nearest box wins.

Hydrology says what the column is, and that says which rows of the table are looked at. What climate and height say cannot put a river on a mountain.

| Hydrology's kind | Rows with `where:` |
|---|---|
| Land, lake | `land`. A lake is land with water on it, so it keeps its land's biome |
| Ocean | `ocean` |
| Deep ocean | `deep_ocean` |
| River | `river` |
| Beach | `beach` |
| Stony shore | `stony_shore` |

Among those of the family, **the nearest box wins**. Every row is a box: a range on each of variety, wetness, continentalness, erosion and weirdness, one on the biome height (`terrain_biome_height` of the ground and the plateau's lift), and optional climate limits. For a column, with a value's gap being how far it lies outside its range, and 0 inside it, ends included:

    distance = gap(variety)^2 + gap(wetness)^2 + gap(continentalness)^2 + gap(erosion)^2 + gap(weirdness)^2
               + gap(height / 128)^2 + gap(t_warm / 10 C)^2 + gap(t_cold / 10 C)^2 + gap(rain / 1000 mm)^2

with height = `terrain_biome_height(ground, lift)`, and the last three only for a row that gives them (`t_warm`, `t_cold`, `rain_mm`), at the column's own cell. **Variety** is a
noise field of its own; **wetness** is its noise field plus half of `(rain - 1000 mm) / 1000 mm` (clamped to -1 to 1, the sum
clamped too), with the blended yearly rain, so wet places lean to the wetter members and dry ones to the drier. Neither ever
chooses between snow and jungle: they only ever see one family's members. Every family has a member for every `where`
(`compile/src/game_tables/biomes/` refuses a table that leaves a hole), so the choice never leaves the family: if no box
fits, for example Desert at 900 blocks, the nearest member wins, never another family's biome. The climate limits are
distances, not hard cuts: a hard cut let a far-off member with no limit win over the natural one just past its rain.

`lift` is the plateau under the column (plan 007 phase 03, `run/src/terrain/landform/`), carried from the density's profile
through hydrology's column. `terrain_biome_height(ground, lift)` is the **biome height**, ground less lift, how high the
ground stands above its plateau, and the one place it is worked out: the box, `terrain_biome_height_estimate`, the benches
and the tests all call it. So a plateau's top at 660 on a 650 lift counts as 10, plains or forest and not bare peaks, and a
range on a plateau counts from the plateau's top. The family, and so the climate, still read the real height
(`terrain_biome_climate_z`), so a high plateau is a cold one.

Lowest distance wins, then highest `priority`, then the first by name. So:

- **No column can be left without a biome**, and the boxes need not meet: what falls between two goes to the nearer.
- **A rare biome is a small box inside a common one, with a higher `priority`.** Inside both, both are at distance 0.
- Height is on the same footing as the climate, at 128 blocks for what is half of an axis. So a box's height matters as much as a whole climate band when the column is 30 or 40 blocks outside it.

Ridges is not an axis: it is weirdness folded, and the mountains it makes are already in `ground`.

The noise fields are read on the density's lattice, every 4 blocks, and interpolated, first in `x` and then in `y`, as hydrology does it. So what a column gets never depends on which area asked, and borders bend smoothly from block to block, without the staircase that choosing per lattice cell would leave.

## Under the ground

`underground` rows are not in that choice: they do not replace a column's biome, they hold voxels under it. A voxel at `z` belongs to the first of `biome_underground` (highest `priority` first) where:

- the column is inside the row's five ranges (variety, wetness, continentalness, erosion, weirdness). An underground row has no family: caves do not follow the surface's climate, though lush caves' wetness leans to wet land above. Nearest does not come into it, since the rest of the world is simply not underground;
- `z` is inside the row's `z` range, ends included;
- and `z <= ground - 16`, so the surface's own layers are never one.

Anywhere else, a voxel has its column's biome. `terrain_biome_column_t` keeps which rows hold the column as bits, so a voxel costs a comparison or three.

## What the table is

`config/biomes/README.md` has the fields, and `compile/src/game_tables/biomes/` makes the table.

- **Each biome's families** are in its file (plan 006 phase 03's table is in that phase file, Issues found 1). Since a value `v` has about `(v + 1) / 2` of the world below it, a box's share of a family's land can be read from its ranges. The wetness bands are those humidity had, as they go in a familiar game's published tables, as remembered and not checked; nothing was read from its files.
- **Mushroom fields are not islands.** The density has no land at `c` below about -0.46, so a far-out island cannot be chosen here: it would have to be made by the density. They take the oddest stretches of coast instead, from the land line to `c = -0.24`, where hydrology's beaches end. Stage 10.6's issue 3 says more.
- **Height is what makes the high biomes.** Slopes, groves and meadows are from 300 to 500, and bare peaks from 500 to the top of the world, since a range's height does not follow from any one axis. Woods go on up to 300, so a range has a forested foot, then high pasture, then rock, and a warm peak has snow from 800. The numbers come from the land's own shares: `bench/biome_map/` prints who holds each height.
- **Swamps go right down to the sea,** and are held to low, worn ground by `height` and `erosion`.

`tests/run/terrain/biome/` prints every biome's share of three worlds, and holds each to turning up in all three.

## What it costs

A tile's 36 lattice columns of the five fields, a hydrology tile, one read of the climate sample per lattice point a column falls on (`terrain_climate_both_at`, one warp for the blended rain and the nearest cell, at z 0; a column takes its lattice corner's and its own height's lapse and basin heat, the same numbers as a read at its height), so 16 a dense tile and not 256, and for each column one pass over the table: 53 rows of up to 9 subtractions. That is small next to the noise. Asked for columns far apart, each column has a tile to itself, which is 4 corners of everything, so a sparse map costs about 4 times a dense one per column.
