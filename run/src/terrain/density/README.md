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
- `terrain_density_box_voxels`, `terrain_density_sample_box`, `terrain_density_fill_box`: the bulk sampling.
- `terrain_density_at`, `terrain_density_block_at`: one voxel.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/climate` and `terrain/noise` (its siblings, for the climate and the fields), `arena`, `assert`, `machine`, `types`, and the generated `tables/blocks/`, for the block ids.

## The limits

The world's range, from `config/machine.yaml`, both ends included: `z = -250 ... +1250`.

| `z` | What is there | Held by |
|---|---|---|
| below -250, above 1250 | Air, always. Storage reaches `-256` and `1279`, so a box may cover whole sections | `density_block_of` |
| -250 | Bedrock, always, one layer | `density_block_of` |
| -249 to -155 | Stone, always, before caves (10.5) cut it | The sums below |
| -154 to 491 | Stone where the density is above 0 | The density |
| 492 to 1250 | Never stone. What grows on a peak (10.7) has this room | The sums below |
| up to 0, if not stone | Water, so water's top block is at `z = 0` | `density_block_of` |
| above 0, if not stone | Air | `density_block_of` |

Sea level and the limits come from `MC_WORLD_Z_*`. Rivers, aquifers and lava are `run/src/terrain/hydrology/`'s, and may turn some of this water or
air into something else. This module says only where water would be if nothing else were going on: so a pocket under a shore's
overhang below `z = 0` is water here.

## How high the ground is

A column has a **profile**: its `height` in blocks, and its `softness`, how far either way the 3D noise may move the ground.
With `c` continentalness, `e` erosion, `r` ridges, and `hills` and `jagged` two 2D fields of this stage, all from -1 to 1:

    base       = continental(c)                 and, where that is above 0, times valley(r)
    high       = inland(c) * relief(e)          how much mountain this ground allows
    mountain   = high * peak(r)                 from 0 to 1
    hill       = hill_land(c) * (6 + 30 * hill_relief(e)) + hill_sea(c) * 6

    height     = base + 320 * mountain + hill * hills + 34 * mountain * jagged
    softness   = 4 + 26 * high

Each of those names is a spline through these knots, each written `x: y`:

| Spline | Knots | Why |
|---|---|---|
| `continental` | -1: -110, -0.88: -90, -0.77: -64, -0.62: -36, -0.52: -10, -0.47: 3, -0.35: 10, -0.07: 24, 0.43: 44, 1: 64 | Sea bed, a wide shelf, the coast at about `c = -0.49`, and land rising slowly inland. So about 26% of the world is under the sea: `bench/world_map/` reads 23.8 to 30.6% on 8 seeds |
| `inland` | -1: 0, -0.52: 0, -0.35: 0.1, -0.07: 0.6, 0.21: 1, 1: 1 | No mountains in the sea or on the shore |
| `relief` | -1: 1, -0.5: 0.9, -0.2: 0.55, 0.1: 0.2, 0.4: 0.05, 0.7: 0, 1: 0 | Eroded ground is flat |
| `peak` | -1: 0, -0.5: 0.029023, 0: 0.111111, 0.25: 0.197688, 0.5: 0.343292, 0.75: 0.588169, 1: 1 | `(2^(3 * (r + 1)) - 1) / 63`: an exponential, so that a few ridges tower over the many that do not |
| `valley` | -1: 0.3, -0.6: 0.65, -0.2: 1, 1: 1 | Valley floors sink towards the sea, where Stage 10.4 can put rivers |
| `hill_land` | -1: 0, -0.47: 0, -0.22: 1, 1: 1 | No hills on a beach |
| `hill_sea` | -1: 1, -0.6: 1, -0.53: 0, 1: 0 | A sea bed is not quite flat, except near the coast |
| `hill_relief` | -1: 1, 0: 0.5, 0.6: 0.12, 1: 0 | Worn ground has small hills |

The numbers are about twice a familiar game's above and below the sea, as our range is about twice as tall.

**A spline never passes its knots.** Its pieces are cubics with Fritsch and Carlson's slopes, which stay between their two knots
and have no corner, and a last clamp takes care of rounding. So the limits can be added up by hand, which is what the header does:

    height   at most 64 + 320 + 36 + 34 = 454     at least -110 - 6 = -116
    softness from 4 to 4 + 26 = 30

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

    -116 - 30 - 8 - 1 = -155        454 + 30 + 8 = 492

`softness` is what makes it 3D. Where it is small the density falls faster with height than the noise can bring it back up, so a
plain has one surface. Where it is large, the noise wins here and there, and that is an overhang or an arch. Only the high ground
has that, and the fade means that nothing outside twice the softness pays for 3D noise at all.

| Field | Cell, in blocks | Octaves | Dimensions | Limit | Standard deviation measured |
|---|---|---|---|---|---|
| `density/hills` | 192 | 3 | 2 | 0.34 | 0.169 |
| `density/jagged` | 32 | 2 | 2 | 0.39 | 0.194 |
| `density/detail` | 64 | 3 | 3 | 0.35 | 0.177 |

The lattice holds at least two points per cell of each one's last octave. `raw_noise_measures_what_the_limits_were_chosen_for`
prints the measurements.

A knot, a shape, a limit or a size changing changes every world, and `tests/run/terrain/density/` pins them.

## What it costs

A lattice column costs the climate's warp, continentalness, erosion and ridges, 22 octaves, and hills and jagged, 5 more. Each level
within reach of the noise costs 3 octaves for each lattice column. A box is cut into tiles of 32 blocks a side, lined up on the
world, only so that its lattice fits on the stack.

A voxel's bits never depend on which tile or box it came in, as the lattice is lined up on the world. Blocks need only a sign, so
where all of a level's corners in a tile are clearly one sign, they skip the interpolating.
