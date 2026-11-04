# run/src/terrain/surface

**Purpose:** what each biome lays on the ground, and what it lines its caves with.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and its fields are in the bank.
**Lifetime:** World: a surface is made for one seed.
**Threads:** `terrain_surface_init` and `terrain_surface_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_surface_init`, `terrain_surface_deinit`: a pair.
- `terrain_surface_block_from`, `terrain_surface_cave_block_from`: the rules, as plain arithmetic.
- `terrain_surface_fill_box`: covers the ground of a box.
- `terrain_surface_tile_read`, `terrain_surface_fill_tile_box`: the same fill, for what asks for one chunk in slices, reading what does not change with the layers once.
- `terrain_surface_line_caves_box`: lines the caves of a box. `terrain_surface_line_caves_with_mask`: the same blocks, from a mask of that box, which asks the caves only for the layers just past its ends. On a tall column of a range the second look at the caves took a third of its time, so `terrain_generate_box` uses this.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/biome`, `terrain/cave`, `terrain/density`, `terrain/hydrology`, `terrain/noise` and `terrain/rng` (its siblings), `arena`, `assert`, `machine`, `types`, and the generated `tables/biomes/` and `tables/blocks/`.

## The order

Each of these takes what the one before left, in the same box:

| Step | Call | Why it comes here |
|---|---|---|
| 1 | `terrain_hydrology_fill_box` | Stone, water, air, and sand, gravel and clay |
| 2 | `terrain_surface_fill_box` | Needs the ground with nothing cut from it, so the top of the ground stays where it is, whatever a cave does under it. It would otherwise lay grass on cave floors |
| 3 | `terrain_cave_carve_box`, `terrain_hydrology_flood_box` | Cuts through what the surface laid, and fills what it opened |
| 4 | `terrain_surface_line_caves_box` | Needs to know the caves. Reads only `terrain_cave_mask_box`, so lining does not depend on what any cave held |
| 5 | `terrain_ore_fill_box` | Rock and ore replace stone and deepslate only, so they leave everything the surface laid, and stay out of the lining |
| 6 | `terrain_feature_fill_box` | Stage 10.7: what grows on it. `terrain_generate_box` runs all six |

## The rules

For each voxel of solid ground, with `depth` how many solid voxels lie between it and the nearest open one above, and `wet` saying that open voxel is water. Air, water and lava are open. **Each rule looks at the voxel's biome, which is its column's, and the first that fits is taken:**

| When | The voxel becomes |
|---|---|
| `wet`, `depth < wet_depth` | `wet_top`. The one rule that may replace what hydrology laid: a mangrove's mud goes over the sand |
| `wet` | What it was |
| Not stone | What it was: hydrology's sand, gravel, clay and sandstone stay, as does the bedrock |
| `steepness >= steep.from`, `depth <= filler` | `steep.block`: a slope too steep to hold soil |
| `depth = 0`, `z >= high.from` | The patch this column is in, or `high.block` |
| `depth = 0`, `z >= bands.from` | The band for this layer |
| `depth = 0` | The patch this column is in, or `top` |
| `z >= bands.from`, `depth < bands.depth` | The band for this layer |
| `z >= bands.from` | Stone |
| `depth <= filler` | `filler` |
| `depth <= filler + under_depth` | `under` |
| Otherwise | Stone |

with

    filler = 0                                                when filler_depth is 0
             filler_depth + min(3, trunc(2 * (depth_noise + 1)))   otherwise

so a column's soil is up to 3 layers thicker than the file says, as the noise goes. A **patch** is a row `from`, `to` of `patches`: the first whose range holds the column's `patch` noise, from `from` up to but not including `to`, where a `to` of 1 includes 1. The patch and depth noise belong to the column, so they make shapes on the ground, not specks.

**The most any rule reaches is 16 layers,** `BIOME_LAYERS_MAX`, which the tool holds every file to. So a voxel that has no open voxel within 16 above it changes for no biome, and that is what lets a box know what is in it with no more than the 16 layers over its top.

**Steepness** is the steepest of the four edges of the column's lattice cell, in blocks per block, as hydrology's stony shore has it.

**The bands** are what give the badlands their stripes: 256 layers of terracotta, with 70 runs of six colours from 1 to 3 layers thick laid over at places drawn from the seed. They repeat every 256 layers. A voxel reads the layer `z + round(6 * wander)`, for `wander` a broad field, so a stripe goes up and down by up to 6 blocks over a hundred or two.

## Under the ground

A voxel is lined when it is stone, the biome at that voxel is an underground one, and a cave wants the voxel over it (it is a floor) or under it (a ceiling). A floor takes the patch this column is in, or `floor`; a ceiling takes `ceiling`. A voxel with a cave on both sides is a floor.

"A cave wants" means `terrain_cave_mask_box`, not what is there: so a stone wall the flood left, for keeping water and lava apart, can be lined too, which nobody can see, and in exchange no block depends on what is above the box.

## Boxes

A box is worked in tiles of 16 blocks a side, lined up on the world, and the voxels are a pure function of the voxel however a box is cut, on any thread, in any order.

Before reading any noise, the lattice round a tile says whether its voxels can be near the top of the ground at all, and whether anything over the box can be ground. The bounds come from the same sums as the density's own: no ground above `height + softness + 9` and none missing below `height - softness - 9`, for the highest and lowest **corner of the cells the tile touches**, and not for the column, as a corner on a steep slope can be far over its neighbour. A river's `carve_top` lowers the second, and it is the column's own number. When the bounds cannot say, the 16 layers over the box are filled by `terrain_hydrology_fill_box` to look at.

Lining cuts its tile into slices of 16 layers lined up on the world, and asks for the caves only where a column holds an underground biome at those heights.

## The fields

| Field | Cell, in blocks | Octaves | Dimensions | Limit |
|---|---|---|---|---|
| `surface/depth` | 32 | 2 | 2 | 0.42 |
| `surface/patch` | 24 | 2 | 2 | 0.42 |
| `surface/wander` | 192 | 2 | 2 | 0.42 |

Noise is spread as hydrology spreads it, and the limit is the one it measured for the same shape. A number here changing changes every world, and `tests/run/terrain/biome/` pins them.

## What it does not do

- **No eroded badlands hoodoos.** The ground's shape comes from the density, and this only colours it. Eroded badlands differ from badlands by their bands reaching lower and their barer top.
- **No sculk in the deep dark:** `config/blocks/` has no such block. It has cobbled deepslate and tuff on its floors until it does.
- **No ice on water, and no snow over what is not its own top.** Those are `run/src/terrain/feature/`'s. `snow_block` is the only snow there is, and it lies on the coldest ground from here.

## What it costs

A tile that is near the ground: the biome's columns, a hydrology tile for the bounds and steepness, 3 fields of 2 octaves on 256 columns, and up to one fill of 16 layers. A tile well away from the ground stops after the first two. Lining a tile with no underground biome at those heights costs its columns. `tests/run/terrain/biome/` prints columns a second, through every stage.
