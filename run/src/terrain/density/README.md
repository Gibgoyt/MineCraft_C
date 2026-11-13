# run/src/terrain/density

**Purpose:** the shape of the ground. From a column's climate it works out where the ground would be, from there a 3D density for any voxel, and from that the first blocks: bedrock, stone, water and air.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and its fields are in the bank.
**Lifetime:** World: a density is made for one seed.
**Threads:** `terrain_density_init` and `terrain_density_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_density_init`, `terrain_density_deinit`: a pair.
- `terrain_density_spline_at`, `terrain_density_profile_from`: the height splines, as plain arithmetic.
- `terrain_density_shape_at`, `terrain_density_knob_shape_at`: a range's slope-shaping curve `S_G(m)` and a knob's shape `S_K(t)`, as plain arithmetic, for tests and tools (plan 007 phase 04).
- `terrain_density_spline_slope_at`: a spline's slope at x, the derivative of the same cubic, for `terrain/landform`'s face (plan 007 phase 03).
- `terrain_density_spline_build`: the same spline on anyone's knots, with the same slopes: a public wrapper round the static builder, for the climate's knot tables (plan 004 phase 04, `run/src/climate/normals/`). No terrain number changes.
- `terrain_density_profile_area`: where the ground is, without its blocks, for hydrology and biomes.
- `terrain_density_profile_points`: the same at columns anywhere, through the very batch a box's lattice uses, so a whole-number column gets a box's bits. A wrapper over that batch only, which moves no number; the height estimate in `run/src/terrain/` is built on it.
- `terrain_density_profile_area_lift`, `terrain_density_profile_points_lift`: the same two, and each column's lift as well, for what reads relief rather than height: hydrology carries it to biomes, and the estimate blends it.
- `terrain_density_box_voxels`, `terrain_density_sample_box`, `terrain_density_fill_box`: the bulk sampling.
- `terrain_density_at`, `terrain_density_block_at`: one voxel.

**May be called by:** `terrain` and its parts; `run/src/climate/normals/` for `terrain_density_spline_build` and `terrain_density_spline_at` only.
**Depends on:** `terrain/fields`, `terrain/noise` and `terrain/landform` (its siblings, for the climate, the fields and the plateaus), `arena`, `assert`, `machine`, `types`, and the generated `tables/blocks/`, for the block ids.

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

A column has a **profile**: its `height` in blocks, its `softness`, how far either way the 3D noise may move the ground, and
its `lift`, how much of the height is the plateau under it (plan 007 phase 03). With `c` continentalness, `b` broad
continentalness, `e` erosion, `r` ridges, `hills`, `range`, `massif` and `crest` 2D fields of this stage, all from -1 to 1;
`plateau`, `face` and the ruggedness `G`, each from 0 to 1, and the knob field `K` (-1 to 1), its size `sigma` (0 to 1) and the
summit field `n` (-1 to 1), from `run/src/terrain/landform/`:

    valley_at   = valley(r)
    base        = continental(c)                 and, where that is above 0, times valley_at, less 14 * (1 - valley_at) * wetland(e)
    lift        = 650 * plateau                  the plateau, P
    body        = range_land(b) * flank(1 - |range|) * massif(massif)      how much of a range stands here, from 0 to 1
    crest_share = 1 - 0.5 * body * (1 - crest(1 - |crest|))              side valleys cut deep only high up
    m           = body * crest_share                                       the range's envelope
    S_G(m)      = (1 - G) * soft(m) + G * sharp(m)                         gentle feet, steep tops (plan 007 phase 04)
    peak        = 40 * w^3 * (1 - |n|),  w = (m - 0.6) / 0.4 where m > 0.6, else no peak
    land        = hill_land(c)
    knob        = (20 + 70 * sigma) * S_K(t) * (1 - body) * land,  t = (K - 0.55) / 0.45 where K > 0.55, else no knob
    S_K(t)      = (1 - G) * (1 - (1 - t)^2) + G * koppie(t)              a dome, or a flat-topped koppie
    hill        = land * valley_at * (6 + 24 * hill_relief(e) * (0.5 + 0.5 * G)) + hill_sea(c) * 6

    height      = base + lift + (1000 - lift) * S_G(m) + peak + knob + hill * hills
    softness    = 4 + 22 * (h + body - h * body),   h = min(1, 0.5 * face + 0.3 * G * m)

A range stands on the plateau and rises at most to 1000 above the base, whatever the plateau.

**Slope shaping** (master 3.6). With `m` rising steadily from a range's foot to its top, `S_G` is flat at 0 and steep at 1, so
the ground is gentle low down and steep high up. On a straight envelope 2000 blocks long the slope at `m = 0.9` is 4.4 times
the slope at `m = 0.1` on rolling ground (`G = 0`, about `m^1.6`), 11 times at `G = 0.5`, and 135 times on alpine ground (`G =
1`, about `m^3`): `feet_are_gentle_and_tops_steep` prints them. The **summit rock** `peak` (cell 48, ridged) adds arêtes and
summit cliffs above 60% of the envelope only: 5 blocks at `m = 0.8`, 40 at the top. It replaced `jagged`, 14 blocks on every
foothill.

**Knobs** (master 3.7, Ahmed's "pimples": koppies, inselbergs, domes) stand where `K` passes 0.55, about a fifth of the land,
20 to 90 blocks high, on any ground: the lowland and a plateau's top alike, as nothing ties them to height. Never on a range
(`1 - body`) or in the sea (`land`). **Hills** are gentler than before: at most 30 blocks (46 before) on a cell of 384 (192).
The **foothills** (60 blocks on ridges where erosion was low) are gone: knobs and `S_G`'s low part do their work. Softness
follows faces and rugged ranges, so a plain or a plateau top has one surface at about 4, and overhangs go to cliffs.

Each of those names is a spline through these knots, each written `x: y`:

| Spline | Knots | Why |
|---|---|---|
| `continental` | -1: -110, -0.88: -90, -0.78: -64, -0.6: -36, -0.5: -10, -0.45: 3, -0.34: 10, -0.07: 26, 0.43: 54, 1: 100 | Sea bed, a wide shelf, the coast at about `c = -0.46`, and land rising slowly inland. So about 27% of the world is under the sea: `bench/world_map/` reads 24.9 to 29.1% on 8 seeds |
| `range_land` | -1: 0, -0.38: 0, -0.1: 0.5, 0.2: 1, 1: 1 | No ranges in the open sea or right on the coast. Read from broad continentalness: see below |
| `flank` | 0: 0, 0.40: 0, 0.55: 0.06, 0.70: 0.20, 0.82: 0.42, 0.90: 0.64, 0.95: 0.82, 0.985: 0.96, 1: 1 | How much of a range stands at each nearness to its line, 1 being on the line: wide, and nearly straight, as phase 04's shaping puts the concavity on it. About 60% of the world has any, before the land and massif |
| `massif` | -1: 0.35, -0.2: 0.58, 0.4: 0.82, 0.75: 1, 1: 1 | Along a range: passes, and here and there the great heights |
| `crest` | 0: 0.5, 0.4: 0.66, 0.8: 0.94, 1: 1 | Across a range: side valleys between sharp crests, down to three quarters of its height where `body` is 1, and not at all at its foot |
| `soft` | 0: 0, 0.1: 0.025, 0.2: 0.076, 0.3: 0.146, 0.4: 0.231, 0.5: 0.33, 0.6: 0.442, 0.7: 0.565, 0.8: 0.7, 0.9: 0.845, 0.95: 0.921, 1: 1 | `m^1.6`, a rolling range. Master 3.6's knots, and tenths between them so the spline's slope stays within a few % of the power curve's (with only the master's 7 knots the slope at `m = 0.2` came out 19% low) |
| `sharp` | 0: 0, 0.1: 0.001, 0.2: 0.008, 0.3: 0.027, 0.4: 0.064, 0.5: 0.125, 0.6: 0.216, 0.7: 0.343, 0.8: 0.512, 0.9: 0.729, 0.95: 0.857, 1: 1 | `m^3`, an alpine range |
| `koppie` | 0: 0, 0.3: 0.15, 0.5: 0.75, 0.7: 0.95, 1: 1 | Steep sides and a flat top, like the Highveld's koppies |
| `valley` | -1: 0.3, -0.6: 0.65, -0.2: 1, 1: 1 | Valley floors sink towards the sea, where Stage 10.4 can put rivers |
| `wetland` | -1: 0, 0.35: 0, 0.6: 1, 1: 1 | Worn ground's valley floors go below the sea's level, which is what gives a swamp standing water far inland |
| `hill_land` | -1: 0, -0.45: 0, -0.2: 1, 1: 1 | No hills on a beach |
| `hill_sea` | -1: 1, -0.58: 1, -0.51: 0, 1: 0 | A sea bed is not quite flat, except near the coast |
| `hill_relief` | -1: 1, 0: 0.5, 0.6: 0.12, 1: 0 | Worn ground has small hills |

**A spline never passes its knots.** Its pieces are cubics with Fritsch and Carlson's slopes, which stay between their two knots
and have no corner, and a last clamp takes care of rounding. So the limits can be added up by hand, which is what the header does:

    height   at most 100 + (lift + 1000 - lift) + 40 + 30 = 1170    at least -110 - 6 = -116
    softness from 4 to 4 + 22 = 26

A knob does not add to the top, as `1 - body` takes it away where a range stands, and on a plateau with no range it adds 90 to
650: `100 + 650 + 90 + 30 = 870`. `TERRAIN_DENSITY_HEIGHT_MAX` stays 1190, the sum before plan 007, so the world's top, the
stone limit and the room for trees do not move. The limit is as high as it can be, as `terrain/index.h` holds `1190 + 26 + 8`
plus the tallest tree to the world's top.

## How a range is made

**A range does not need three things to line up.** Before plan 003 phase 8 stage 4, a mountain took far inland, low erosion and a high ridge at once, and ridges is folded from weirdness, whose cell is 512: so high ground came as thin mazes and needles, with nothing at 150 or more of even 1 km^2.

**A range is the line where a smooth field is 0.** `range` has a cell of 8192 blocks (4096 before plan 007 phase 03). A line where such a field is 0 has no ends: it closes on itself or runs on, so it goes on for many kilometres. As the field is spread evenly, `1 - |range|` is too, so `flank` can be read as shares of the world. That makes the ranges common and far apart by the field's own scale, about 8 km, curving less, so a range runs 15 to 40 km. Nearness 0.40 is `|range| = 0.6`, so a belt reaches about 2.4 km either side of its line.

**Slope is what sets the sizes.** Height from a field changes by its size over its cell. 1000 blocks over a fine field is a wall, so `body` takes only broad fields, and the finer ones carry only small heights: `crest`, cell 896, a quarter of the height at most, and only high up; the summit rock, cell 48, 40 blocks, only above 60% of the envelope.

**Broad continentalness.** `c` has octaves down to 48 blocks, which make a coast's bays. Taking a range down to the sea on `c` gave walls: on Ahmed's seed, 5 blocks up for each across, 500 tall, standing in the sea. So `range_land` reads `b`, made of only `c`'s first 3 octaves, down to a cell of 768, for no more noise. A range can then stand out into the sea as a headland, which takes a little of the sea's share.

**Valleys.** Between ranges, by `flank`. In them, by `crest`. And the valley floors of `r`, which rivers follow: hills sink with those floors, so a river can run on through them.

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
| `density/hills` | 384 | 3 | 2 | 0.34 | 0.169 |
| `density/detail` | 64 | 3 | 3 | 0.35 | 0.177 |
| `density/range` | 8192, each octave 0.45 of the last | 3 | 2 | 0.36 | 0.182 |
| `density/massif` | 4096 | 2 | 2 | 0.39 | 0.208 |
| `density/crest` | 896 | 3 | 2 | 0.35 | 0.170 |

The lattice holds at least two points per cell of each one's last octave. `raw_noise_measures_what_the_limits_were_chosen_for`
prints the measurements.

A knot, a shape, a limit or a size changing changes every world, and `tests/run/terrain/density/` pins them.

## What it costs

A lattice column costs the climate's warp, continentalness, erosion and ridges, 22 octaves, and hills and range, 6 more, and the landform's nodes, about 1 more in a box (`run/src/terrain/landform/`). Massif and crest, 5 octaves, are only sampled for columns with some range; the knob field, 2 octaves, only on land; the summit field, 2, only where the envelope passes 0.6. Anywhere else each multiplies 0, so skipping it gives the same bits. Each level
within reach of the noise costs 3 octaves for each lattice column. A box is cut into tiles of 32 blocks a side, lined up on the
world, only so that its lattice fits on the stack.

A voxel's bits never depend on which tile or box it came in, as the lattice is lined up on the world. Blocks need only a sign, so
where all of a level's corners in a tile are clearly one sign, they skip the interpolating.
