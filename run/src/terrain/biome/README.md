# run/src/terrain/biome

**Purpose:** which of the biomes in `config/biomes/` holds any column of the world, and any voxel under it.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and the table is generated.
**Lifetime:** World: its biomes are made for one seed.
**Threads:** `terrain_biome_init` and `terrain_biome_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_biome_init`, `terrain_biome_deinit`: a pair.
- `terrain_biome_distance`, `terrain_biome_choose`, `terrain_biome_caves_of`: the choice, as plain arithmetic.
- `terrain_biome_of_voxel`: the biome of a voxel in a column already worked out.
- `terrain_biome_columns_area`, `terrain_biome_column_at`, `terrain_biome_at`: the same from the world.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/climate` and `terrain/hydrology` (its siblings), `arena`, `assert`, `types`, and the generated `tables/biomes/`.

## Choosing a column's biome

Hydrology first says what the column is, and that says which rows of the table are looked at. What climate and height say cannot put a river on a mountain.

| Hydrology's kind | Rows with `where:` |
|---|---|
| Land, lake | `land`. A lake is land with water on it, so it keeps its land's biome |
| Ocean | `ocean` |
| Deep ocean | `deep_ocean` |
| River | `river` |
| Beach | `beach` |
| Stony shore | `stony_shore` |

Among those, **the nearest box wins**. Every row is a box: a range on each of temperature, humidity, continentalness, erosion and weirdness, and one on the ground's height. For a column, with a value's gap being how far it lies outside its range, and 0 inside it, ends included:

    distance = gap(temperature)^2 + gap(humidity)^2 + gap(continentalness)^2 + gap(erosion)^2 + gap(weirdness)^2
               + gap(ground / 128)^2

Lowest distance wins, then highest `priority`, then the first by name. So:

- **No column can be left without a biome**, and the boxes need not meet: what falls between two goes to the nearer.
- **A rare biome is a small box inside a common one, with a higher `priority`.** Inside both, both are at distance 0.
- Height is on the same footing as the climate, at 128 blocks for what is half of an axis. So a box's height matters as much as a whole climate band when the column is 30 or 40 blocks outside it.

Ridges is not an axis: it is weirdness folded, and the mountains it makes are already in `ground`.

The climate is read on the density's lattice, every 4 blocks, and interpolated, first in `x` and then in `y`, as hydrology does it. So what a column gets never depends on which area asked, and borders bend smoothly from block to block, without the staircase that choosing per lattice cell would leave.

## Under the ground

`underground` rows are not in that choice: they do not replace a column's biome, they hold voxels under it. A voxel at `z` belongs to the first of `biome_underground` (highest `priority` first) where:

- the column's climate is inside the row's five ranges. Nearest does not come into it, since the rest of the world is simply not underground;
- `z` is inside the row's `z` range, ends included;
- and `z <= ground - 16`, so the surface's own layers are never one.

Anywhere else, a voxel has its column's biome. `terrain_biome_column_t` keeps which rows hold the column as bits, so a voxel costs a comparison or three.

## What the table is

`config/biomes/README.md` has the fields, and `compile/src/game_tables/biomes/` makes the table.

- **The boxes come from the climate's own shares.** Since a value `v` has about `(v + 1) / 2` of the world below it, `temperature` from -0.45 to -0.15 is about 15% of the world. Bands of temperature and humidity go as they do in a familiar game's published tables, as remembered and not checked; nothing was read from its files.
- **Mushroom fields are not islands.** The density has no land at `c` below about -0.46, so a far-out island cannot be chosen here: it would have to be made by the density. They take the oddest stretches of coast instead, from the land line to `c = -0.24`, where hydrology's beaches end. Stage 10.6's issue 3 says more.
- **Height is what makes the high biomes.** Slopes, groves and meadows are from 300 to 500, and bare peaks from 500 to the top of the world, since a range's height does not follow from any one axis. Woods go on up to 300, so a range has a forested foot, then high pasture, then rock, and a warm peak has snow from 800. The numbers come from the land's own shares: `bench/biome_map/` prints who holds each height.
- **Swamps go right down to the sea,** and are held to low, worn ground by `height` and `erosion`.

`tests/run/terrain/biome/` prints every biome's share of three worlds, and holds each to turning up in all three.

## What it costs

A tile's 25 lattice columns of the five climate fields, a hydrology tile, and for each column one pass over the table: 53 rows of up to 6 subtractions. That is small next to the noise. Asked for columns far apart, each column has a tile to itself, which is 4 corners of everything, so a sparse map costs about 4 times a dense one per column.
