# run/src/terrain/density

**Purpose:** the shape of the ground. From a column's climate it works out where the ground would be, from there a 3D density for any voxel, and from that the first blocks: bedrock, stone, water and air.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and its fields are in the bank.
**Lifetime:** World: a density is made for one seed.
**Threads:** `terrain_density_init` and `terrain_density_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_density_init`, `terrain_density_deinit`: a pair.
- `terrain_density_spline_at`, `terrain_density_profile_from`: the height splines, as plain arithmetic.
- `terrain_density_profile_area`: where the ground is, without its blocks, for hydrology and biomes.
- `terrain_density_profile_points`: the same at columns anywhere, through the very batch a box's lattice uses, so a whole-number column gets a box's bits. A wrapper over that batch only, which moves no number; the height estimate in `run/src/terrain/` is built on it.
- `terrain_density_box_voxels`, `terrain_density_sample_box`, `terrain_density_fill_box`: the bulk sampling.
- `terrain_density_at`, `terrain_density_block_at`: one voxel.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/fields` and `terrain/noise` (its siblings, for the climate and the fields), `arena`, `assert`, `machine`, `types`, and the generated `tables/blocks/`, for the block ids.

## The limits

The world's range, from `config/machine.yaml`, both ends included: `z = -250 ... +1250`.

| `z` | What is there | Held by |
|---|---|---|
| below -250, above 1250 | Air, always. Storage reaches `-256` and `1279`, so a box may cover whole sections | `density_block_of` |
| -250 | Bedrock, always, one layer | `density_block_of` |
| -249 to -151 | Stone, always, before caves (10.5) cut it | The sums below |
| -150 to 1223 | Stone where the density is above 0 | The density |
| 1224 to 1250 | Never stone. What grows on a peak (10.7) has this room | The sums below |
| up to 0, if not stone | Water, so water's top block is at `z = 0` | `density_block_of` |
| above 0, if not stone | Air | `density_block_of` |

Sea level and the limits come from `MC_WORLD_Z_*`. Rivers, aquifers and lava are `run/src/terrain/hydrology/`'s, and may turn some of this water or
air into something else. This module says only where water would be if nothing else were going on: so a pocket under a shore's
overhang below `z = 0` is water here.

## How high the ground is

A column has a **profile**: its `height` in blocks, and its `softness`, how far either way the 3D noise may move the ground.
With `c` continentalness, `b` broad continentalness, `e` erosion, `r` ridges, and `hills`, `jagged`, `range`, `massif` and `crest` five 2D fields of this stage, all from -1 to 1:

    valley_at  = valley(r)
    base       = continental(c)                 and, where that is above 0, times valley_at, less 14 * (1 - valley_at) * wetland(e)
    body       = range_land(b) * belt(1 - |range|) * massif(massif)      how much of a range stands here, from 0 to 1
    rise       = body * crest(1 - |crest|)
    high       = inland(c) * relief(e)
    foothill   = (1 - body) * high * peak(r)
    hill       = hill_land(c) * valley_at * (8 + 38 * hill_relief(e)) + hill_sea(c) * 6

    height     = base + 1030 * rise + 60 * foothill + hill * hills + 14 * body * jagged
    softness   = 4 + 22 * (high + body - high * body)

Each of those names is a spline through these knots, each written `x: y`:

| Spline | Knots | Why |
|---|---|---|
| `continental` | -1: -110, -0.88: -90, -0.78: -64, -0.6: -36, -0.5: -10, -0.45: 3, -0.34: 10, -0.07: 26, 0.43: 54, 1: 100 | Sea bed, a wide shelf, the coast at about `c = -0.46`, and land rising slowly inland. So about 27% of the world is under the sea: `bench/world_map/` reads 24.9 to 29.1% on 8 seeds |
| `range_land` | -1: 0, -0.38: 0, -0.1: 0.5, 0.2: 1, 1: 1 | No ranges in the open sea or right on the coast. Read from broad continentalness: see below |
| `belt` | 0: 0, 0.52: 0, 0.68: 0.32, 0.82: 0.68, 0.94: 1, 1: 1 | How much of a range stands at each nearness to its line, 1 being on the line. About 48% of the world has any |
| `massif` | -1: 0.35, -0.2: 0.58, 0.4: 0.82, 0.75: 1, 1: 1 | Along a range: passes, and here and there the great heights |
| `crest` | 0: 0.5, 0.4: 0.66, 0.8: 0.94, 1: 1 | Across a range: side valleys down to half its height, between sharp crests |
| `inland` | -1: 0, -0.5: 0, -0.36: 0.12, -0.1: 0.6, 0.12: 1, 1: 1 | No foothills in the sea or on the shore |
| `relief` | -1: 1, -0.5: 0.9, -0.2: 0.55, 0.1: 0.2, 0.4: 0.05, 0.7: 0, 1: 0 | Eroded ground is flat |
| `peak` | -1: 0, -0.5: 0.08, 0: 0.3, 0.5: 0.65, 1: 1 | Foothills on the ridges. It tops out gently, as ridges is the steepest field there is |
| `valley` | -1: 0.3, -0.6: 0.65, -0.2: 1, 1: 1 | Valley floors sink towards the sea, where Stage 10.4 can put rivers |
| `wetland` | -1: 0, 0.35: 0, 0.6: 1, 1: 1 | Worn ground's valley floors go below the sea's level, which is what gives a swamp standing water far inland |
| `hill_land` | -1: 0, -0.45: 0, -0.2: 1, 1: 1 | No hills on a beach |
| `hill_sea` | -1: 1, -0.58: 1, -0.51: 0, 1: 0 | A sea bed is not quite flat, except near the coast |
| `hill_relief` | -1: 1, 0: 0.5, 0.6: 0.12, 1: 0 | Worn ground has small hills |

**A spline never passes its knots.** Its pieces are cubics with Fritsch and Carlson's slopes, which stay between their two knots
and have no corner, and a last clamp takes care of rounding. So the limits can be added up by hand, which is what the header does:

    height   at most 100 + 1030 + 46 + 14 = 1190    at least -110 - 6 = -116
    softness from 4 to 4 + 22 = 26

A foothill does not add to the 1190, as `1 - body` takes it away where a range stands. The limit is as high as it can be, as `terrain/index.h` holds `1190 + 26 + 8` plus the tallest tree to the world's top.

## How a range is made

**A range does not need three things to line up.** Before plan 003 phase 8 stage 4, a mountain took far inland, low erosion and a high ridge at once, and ridges is folded from weirdness, whose cell is 512: so high ground came as thin mazes and needles, with nothing at 150 or more of even 1 km^2.

**A range is the line where a smooth field is 0.** `range` has a cell of 4096 blocks. A line where such a field is 0 has no ends: it closes on itself or runs on, so it goes on for many kilometres. As the field is spread evenly, `1 - |range|` is too, so `belt` can be read as shares of the world. That makes the ranges common and far apart by the field's own scale, about 4 km.

**Slope is what sets the sizes.** Height from a field changes by its size over its cell. 1030 blocks over a fine field is a wall, so `body` takes only broad fields, and the finer ones carry only small heights: `crest`, cell 896, half the height at most; `jagged`, cell 32, 14 blocks; foothills, through ridges, 60. Those were 320 and 34 on the needles before.

**Broad continentalness.** `c` has octaves down to 48 blocks, which make a coast's bays. Taking a range down to the sea on `c` gave walls: on Ahmed's seed, 5 blocks up for each across, 500 tall, standing in the sea. So `range_land` reads `b`, made of only `c`'s first 3 octaves, down to a cell of 768, for no more noise. A range can then stand out into the sea as a headland, which takes a little of the sea's share.

**Valleys.** Between ranges, by `belt`. In them, by `crest`. And the valley floors of `r`, which rivers follow: hills sink with those floors, so a river can run on through them.

**The numbers are a first guess for Ahmed to tune**, by `bench/world_map/`. Its table has what they give.

## What the density is

Worked out at a lattice of points, 4 blocks apart in `x` and `y` and 8 in `z`, all lined up on the world: at whole multiples of 4,
and at `z = -250 + 8 * k`. For a lattice point:

    depth   = (height - z) / softness
    fade    = 1 when |depth| <= 1;  2 - |depth| when up to 2;  0 from 2
    density = depth + fade * spread(detail(x, y, z))

Any voxel's density is that, interpolated between the 8 points round it, in float32_t: first in `z`, then `x`, then `y`, each as
`a + t * (b - a)`. Ground is a density above 0.

`spread` is what the climate uses, `u * (1.5 - 0.5 * u * u)` of `u = clamp(raw / limit, -1, 1)`, so the noise is never past 1 either
way. That has a useful result. Beyond `|depth| = 1`, what is added is smaller than the value it is added to, so the sign cannot
change: **no ground above `height + softness`, and none missing below `height - softness`**. Interpolation adds at most a level of
the lattice, which gives the table's `-155` and `492`:

    -116 - 26 - 8 - 1 = -151        1190 + 26 + 8 = 1224

`softness` is what makes it 3D. Where it is small the density falls faster with height than the noise can bring it back up, so a
plain has one surface. Where it is large, the noise wins here and there, and that is an overhang or an arch. Only the high ground
has that, and the fade means that nothing outside twice the softness pays for 3D noise at all.

| Field | Cell, in blocks | Octaves | Dimensions | Limit | Standard deviation measured |
|---|---|---|---|---|---|
| `density/hills` | 192 | 3 | 2 | 0.34 | 0.169 |
| `density/jagged` | 32 | 2 | 2 | 0.39 | 0.194 |
| `density/detail` | 64 | 3 | 3 | 0.35 | 0.177 |
| `density/range` | 4096, each octave 0.45 of the last | 3 | 2 | 0.36 | 0.182 |
| `density/massif` | 2048 | 2 | 2 | 0.39 | 0.208 |
| `density/crest` | 896 | 3 | 2 | 0.35 | 0.170 |

The lattice holds at least two points per cell of each one's last octave. `raw_noise_measures_what_the_limits_were_chosen_for`
prints the measurements.

A knot, a shape, a limit or a size changing changes every world, and `tests/run/terrain/density/` pins them.

## What it costs

A lattice column costs the climate's warp, continentalness, erosion and ridges, 22 octaves, and hills, jagged and range, 8 more. Massif and crest, 5 octaves, are only sampled for columns with some range, as anywhere else they multiply 0, so skipping them gives the same bits. Each level
within reach of the noise costs 3 octaves for each lattice column. A box is cut into tiles of 32 blocks a side, lined up on the
world, only so that its lattice fits on the stack.

A voxel's bits never depend on which tile or box it came in, as the lattice is lined up on the world. Blocks need only a sign, so
where all of a level's corners in a tile are clearly one sign, they skip the interpolating.
