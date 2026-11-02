# run/src/terrain/hydrology

**Purpose:** where the water and the lava are. Rivers cut through the density's ground, the sea fills what lies under it, sand, gravel and clay lie on what is under water and along the shore, aquifers decide what deep caves hold, and lava lies under the whole world.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and its fields are in the bank.
**Lifetime:** World: a hydrology is made for one seed.
**Threads:** `terrain_hydrology_init` and `terrain_hydrology_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_hydrology_init`, `terrain_hydrology_deinit`: a pair.
- `terrain_hydrology_column_from`: what decides a column to what it comes to, as plain arithmetic.
- `terrain_hydrology_columns_area`, `terrain_hydrology_column_at`: what each column is, for biomes (Stage 10.6).
- `terrain_hydrology_fill_box`, `terrain_hydrology_block_at`: the blocks.
- `terrain_hydrology_flood_box`, `terrain_hydrology_fluid_at`: what a voxel holds once something has opened it, for caves (Stage 10.5).

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/climate`, `terrain/density`, `terrain/noise` and `terrain/rng` (its siblings), `arena`, `assert`, `machine`, `types`, and the generated `tables/blocks/`, for the block ids.

## A column

The climate and the ground are read on the density's own lattice, every 4 blocks, and interpolated, always in the same order, `x` and then `y`. So a column's bits never depend on which box or tile asked. With `h` the height, `s` the softness, `c` continentalness, `w` weirdness, and `width` and `shore` two 2D fields of this stage:

    wide      = (width + 1) / 2
    inner     = 0.03 + 0.04 * wide
    outer     = inner + 0.12
    river     = 1 when |w| <= inner;  0 from outer;  between, t * t * (3 - 2 * t) with t = (outer - |w|) / 0.12

    top       = h + s + 9                         above any ground this column can have
    bed       = -3 - 4 * wide
    carve_top = bed + (top - bed) * (1 - river)^2 when river > 0, else top
    ground    = min(h, carve_top)

**A river runs where weirdness is 0.** That is where `ridges` is -1, the valley floors the density's `valley` spline has already sunk and where mountains cannot be, since `peak` is 0 there. A line where a smooth field is 0 has no ends. It closes on itself or runs on until it meets the sea, so rivers do not stop in the middle of nowhere, which noise could not otherwise promise without looking far away. All rivers stand at sea level, `z = 0`, and only the ground is moved to make room.

The cut is a limit on height: **ground stays ground up to `carve_top`, and nothing above it does**. So it needs no 3D noise of its own, and it joins the unchanged ground smoothly, since `carve_top` comes down from `top`, above any ground there is. It never fills anything in, so where the sea bed is already lower than a river's bed, the river makes no difference.

| Kind | When, first match | For |
|---|---|---|
| River | `h >= 0` and `ground <= 0`: the cut has brought it to the water | Stage 10.6's rivers |
| Beach | `river < 0.5`, `c <= 0.12`, `ground` from -6 to one above `sand_top`, and not steep | Shores |
| Stony shore | The same, but 1.5 blocks of ground to the block or steeper, on the cell's four edges | Shores |
| Land | `ground >= 0` | |
| Lake | `ground < 0` with `c > -0.04`: low ground far from the sea, where water still stands at `z = 0` | Stage 10.7's lakes may add to these |
| Deep ocean | `ground < -60` | All oceans |
| Ocean | Any other `ground < 0` | All oceans |

with `sand_top = 2 + 1.5 * (shore + 1)`: from 2 to 5, so the top of the sand wanders.

## The blocks

`terrain_hydrology_fill_box` starts from `terrain_density_fill_box`. A stone voxel above `carve_top` is cut: it becomes water up to `z = 0`, and air above. The density's own water stays as it is.

Then what lies on the ground. `depth` is how many solid voxels lie between a stone voxel and the nearest open one above it, and `wet` says that open voxel is water. What a voxel becomes depends only on its column, its `z`, its `depth` and `wet`:

| Column | Where | Block |
|---|---|---|
| Ocean, deep ocean | `wet`, `z < -50`, `depth < 2` | Gravel |
| | `wet`, `z >= -30`, `shore >= 0.5`, `depth < 2` | Clay |
| | Otherwise `wet`, `z >= -50`, `depth < 3` | Sand |
| River, lake | `wet`, `shore >= 0.5`, `depth < 2` | Clay |
| | Otherwise `wet`, `depth < 3` | Gravel if `shore < -0.2`, else sand |
| Beach | `z <= sand_top`, `depth < 4` | Sand |
| | `z <= sand_top`, `depth` 4 or 5 | Sandstone |
| Stony shore | `wet`, `depth < 2` | Gravel |
| Land | `river >= 0.05`, `z <= 2`, `depth < 2` | Gravel if `shore < -0.2`, else sand: a river's banks |
| Anything else | | Stone, for Stage 10.6 to cover |

A box is worked in tiles of 16 blocks a side, lined up on the world. `depth` needs what lies over the top of the box, so the 6 layers above it are looked at too, unless the density's sums already say what they are. That is what makes a voxel the same whichever box it was in. Only the layers near the ground in a tile are walked at all.

Sand can sit on an open voxel under a thin overhang: the test holds that under 2% of the sand. Stage 11.2's falling sand deals with it when something disturbs it.

## What an opened voxel holds

This is for whatever opens the ground after the fill, as caves do, and `terrain_hydrology_flood_box` is told which voxels those are. The fill's own open voxels do not come here. For `band = ground - softness - 12`:

| Where | What it holds |
|---|---|
| `z <= -230` | Lava, always: the lava level |
| `z >= band` | The sea's rule: water up to `z = 0`, air above |
| Deeper | What its aquifer cell holds at that height |

**Aquifer cells** are cubes of 32 blocks, lined up on the world, with one site each, from 10 to 22 blocks into the cell on each axis, from a hash of the cell. A voxel belongs to the cell whose site is nearest. A cell's own numbers come from noise at its site:

    flood < 0.3                                                    dry
    otherwise: level = 40 * floor(site_z / 40) + 20 + 3 * trunc(4 * level_noise)
    holding lava if level <= -160 and lava_noise >= 0.4, else water

and it holds its fluid from its level down. Levels on steps of 3 mean neighbours often agree, and then they are one body of water.

**The 27 cells round a voxel's cell always hold its nearest site.** Its own site is at most `22 * sqrt(3) = 38.2` away, and a site two cells off at least `32 + 10 = 42`.

**Nothing placed here can flow:** no fluid beside air, no fluid over air, and no water touching lava. These rules hold that, and the test opens whole boxes to check it:

1. A voxel within 2 blocks of the plane halfway between its site and that of a cell holding something else is stone, if either holds fluid at that height. Two neighbouring voxels with different nearest sites have that plane between them, so it is under a block from each.
2. A deeper voxel beside the sea's rule, or under it, is stone where that rule says water at that height or over it. Where the rule says air, it stays open if it is air, so a cave above the sea's level can reach the surface, and is stone if it would hold fluid.
3. Water directly on the lava level, at `z = -229`, is stone.

The near-surface rule can reach no lower than `-116 - 30 - 12 = -158`, and aquifer lava no higher than -160, so the sea cannot touch lava. Whole-number sums do the plane test, with no `sqrt`: the difference of two squared distances is the distance from the plane times twice the distance between the sites.

| Field | Cell, in blocks | Octaves | Dimensions | Limit |
|---|---|---|---|---|
| `hydrology/width` | 512 | 2 | 2 | 0.42 |
| `hydrology/shore` | 48 | 2 | 2 | 0.42 |
| `hydrology/flood` | 128 | 2 | 3 | 0.42 |
| `hydrology/level` | 96 | 2 | 3 | 0.42 |
| `hydrology/lava` | 96 | 1 | 3 | 0.54 |

Each limit is about twice what that field measures: `raw_noise_measures_what_the_limits_were_chosen_for` prints them. Noise is spread as the climate spreads it. A number here changing changes every world, and `tests/run/terrain/hydrology/` pins them.

## What it costs

A lattice column costs the density's 26 octaves over again, plus continentalness and weirdness through the climate, and the 4 of the two 2D fields. The fill asks for the lattice of a 16-block tile on top of what the density asks for: the stage file has the numbers. A flood costs nothing in a tile with nothing opened. In one that has, it looks at up to 49 lattice columns and the 27 cells round each aquifer cell it enters. A voxel above all 27 levels costs one comparison.
