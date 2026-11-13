# run/src/terrain/landform

**Purpose:** the land's large shapes, kilometres across, that the density stands its height on: plateaus in two tiers, their escarpments facing the sea, and their gentle back slopes inland (plan 007 phase 03); the ruggedness that picks how sharp a region's mountains are, the knob field and the summit field (plan 007 phase 04); and the basin field, where dry basins sink below the sea's level (plan 007 phase 05). The large fields are read on a lattice of nodes 32 blocks apart and blended, so they cost about one octave a lattice column; the two fine ones at each column the density asks for.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and its fields are in the bank.
**Lifetime:** World: a landform is made for one seed.
**Threads:** `terrain_landform_init` and `terrain_landform_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_landform_init`, `terrain_landform_deinit`: a pair.
- `terrain_landform_nodes`: the uplift, the ruggedness and the basin field at lattice nodes.
- `terrain_landform_uplift_points`: the uplift at columns anywhere, blended from the nodes round each; `terrain_landform_lattice_points` the same and the ruggedness `G` and the basin field `B`, blended the same way.
- `terrain_landform_knobs_points`: the knob field `K` and a knob's size `sigma` at columns anywhere.
- `terrain_landform_peak_points`: the summit field at columns anywhere.
- `terrain_landform_shape_from`: from the blended uplift and broad continentalness to the plateau and its face, as plain arithmetic.

**May be called by:** `terrain` and its parts: `terrain/density` calls it for every profile batch.
**Depends on:** `terrain/noise` (for the fields), `terrain/density` (for its spline: `terrain_density_spline_build`, `_at` and `_slope_at` only), `arena`, `assert`, `types`.

## The plateau

With `U` the uplift field, spread to -1 to 1, read at its node's point moved by the edge warp, and `b` broad continentalness (`run/src/terrain/fields/`):

    L       = clamp(U - 0.3 + 0.25 * (b - 0.1) - 0.6 * (1 - smoothstep(b; -0.7, 0.2)), -1, 1)    the plateau input
    k       = 1 - 0.8 * smoothstep(b; 0.2, 0.5)            the back edge's stretch, from 1 to 0.2
    L'      = L + (1 - k) * min(0.53 - L, 0.375)   where L < 0.53, else L
    plateau = plateau_spline(L')                           from 0 to 1, times 650 blocks
    face    = min(1, plateau_spline'(L') * k * 4e-4 * 650 / 2)

`plateau_spline` goes through these knots (master 3.4), with the density's spline: Fritsch and Carlson's slopes, so it never passes its knots and a level stays level.

| L' | -1 | 0 | 0.10 | 0.16 | 0.19 | 0.21 | 0.40 | 0.48 | 0.53 | 0.56 | 1 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| plateau | 0 | 0 | 0.04 | 0.14 | 0.40 | 0.45 | 0.48 | 0.55 | 0.95 | 1 | 1 |
| blocks | 0 | 0 | 26 | 91 | 260 | 293 | 312 | 358 | 618 | 650 | 650 |

**Tuned in plan 007 phase 03** against the master's section 4 on 8 seeds (its Issues found). The spread uplift is nearly
even from -1 to 1, so the offset of -0.3 holds tier 2 to about a tenth of the land. The **coast gate**, 0.6 at the sea
falling to 0 at `b = 0.2`, keeps plateaus off the sea bed and back from the coast; it is wide because `b` is rough (its
finest octave has a cell of 768), and a narrow gate stood walls in the sea. The stretch draws tier 2's back edge 5 times
wider (from 0.53 down to `L' = 0.455`), and below that only lifts, by at most 0.3 in `L`, so far inland the low ground is
not all raised to tier 1, as the master's unbounded stretch did.

So a column climbing out of the coastal plain meets a pediment at about 5:1, tier 1's face at about 1:2, a rounded lip, tier 1's top dipping gently inland, tier 2's foot, its face, a lip, and tier 2's level top. **Which edge faces the sea** needs no gradient: `L` carries `b`, so an edge near the coast, where `b` is low, is where a plateau rises from the plain, and keeps `k = 1`; an edge far inland, where `b` is high, is where the uplift fades, and is drawn up to 5 times wider. `face` is the plateau's slope over 2 at the typical gradient of `L`, a constant, so it too needs no neighbour; the density adds half of it to softness.

## The fields

| Field | Cell, in blocks | Octaves | Persistence | Limit | Read at |
|---|---|---|---|---|---|
| `landform/uplift` | 24576 | 3 | 0.5 | 0.35 | each node, at the point the warp moves it to |
| `landform/edge_x`, `landform/edge_y` | 2048 | 2 | 0.5 | 0.39 | each node; each moves the point up to 700 blocks |
| `landform/rugged` | 3072 | 2 | 0.5 | 0.39 | each node, where it is: `G = 0.5 + 0.5 * spread`, from 0 to 1 |
| `relief/knobs` | 640 (master 3.7: 320; see the density README) | 2 | 0.5 | 0.39 | each land column: `K = spread`; `sigma = 0.5 + 0.5 * spread(second octave alone, 0.17)` |
| `landform/peak` | 48 | 2 | 0.5 | 0.39 | each column high on a range (envelope over 0.6): `spread`, whose ridged `1 - |n|` the density uses |
| `landform/basin` | 8192 | 2 | 0.5 | 0.39 | each node, where it is: `B = spread`. Dry basins sink where it, less the density's gates, is over 0.86 (plan 007 phase 05: the density README's "Dry basins") |

A limit is about twice the field's measured standard deviation: `tests/run/terrain/landform/` prints them. A node costs 11 octaves. `sigma` is the knob field's second octave, what is left when `terrain_noise_sample_points_split` takes the first away, so it costs no more noise. What the density makes of `G`, `K`, `sigma` and the summit field is in `run/src/terrain/density/README.md`.

## The lattice

A node is the point `(32 i, 32 j)`, lined up on the world, and its uplift is a pure function of `(i, j)` and the seed. A column takes the four nodes round it, `i = floor(x / 32)` and `i + 1`, and the same in `y`, and blends them in float64, x first and then y:

    t_x = (x - 32 i) / 32;  south = v00 + t_x * (v10 - v00);  north = v01 + t_x * (v11 - v01);  v = south + t_y * (north - south)

A fraction of 0 takes the node it sits on as it is, and does not read the next one: a rule of the column alone, so a box that ends on a node line does not pay for a row of nodes it multiplies by 0. **So a column's bits do not depend on which box, tile or batch it came in**, which `tests/run/terrain/landform/` checks.

Within one call the nodes are found once each, through a small table. A chunk's box is 16-aligned, so its lattice columns lie in one 32-block cell: 4 nodes, 28 octaves for its 25 lattice columns. A scattered single column costs up to 4 nodes.

## What changes every world

Any knot, field shape, limit, the weights and the lattice's size. `tests/run/terrain/density/` and the other terrain tests pin the land.
