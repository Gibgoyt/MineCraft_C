# run/src/terrain/cave

**Purpose:** cuts caves out of the ground: wide cheese caverns, long spaghetti tunnels and thin noodle passages.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends, and its fields are in the bank.
**Lifetime:** World: the caves are made for one seed.
**Threads:** `terrain_cave_init` and `terrain_cave_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_cave_init`, `terrain_cave_deinit`: a pair.
- `terrain_cave_mask_box`: what would cut each voxel of a box, by kind.
- `terrain_cave_carve_box`: cuts a box of blocks.
- `terrain_cave_cut_with_mask`: the same, from a mask already asked for, which costs nothing more.
- `terrain_cave_cut_keeping_wanted`: the same, with the mask still saying what caves wanted, so `terrain_surface_line_caves_with_mask` need not ask them again. `terrain_generate_box` uses this.
- `terrain_cave_at`: the mask at one voxel.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/density` and `terrain/noise` (its siblings, for where the ground is and for the fields), `arena`, `assert`, `machine`, `types`, and the generated `tables/blocks/`, for the block ids.

## The three kinds

| Kind | What it is | How |
|---|---|---|
| Cheese | Wide, flattish caverns | A noise field above a threshold: holes in a block of cheese |
| Spaghetti | Long winding tunnels, about 3 to 6 blocks across, close enough to join up | Where two noise fields are both near 0: each is 0 on a sheet, and two sheets cross on a line |
| Noodle | Thin winding passages, 1 to 3 blocks across | The same, on finer fields, with a smaller width |

A voxel can be in more than one, and that is how a tunnel opens into a cavern.

## The sums

All at a lattice of points, 4 blocks apart on every axis, lined up on the world: at whole multiples of 4 in `x` and `y`, and at `z = -250 + 4 * k`. `height` and `softness` are the column's, from `terrain_density_profile_area`, at that lattice column. `spread` is the climate's. `clamp` is to 0 to 1.

    cover     = height - z                          blocks of ground above this point, ignoring 3D noise
    certain   = cover - (softness + 8)              what is left once the 3D noise and its lattice have done their worst

    floor     = clamp((z + 242) / 24)
    gate_c    = floor * clamp((certain - 8) / 12)   cheese
    gate_n    = floor * clamp((certain - 6) / 12)   noodle
    gate_s    = 0                                   spaghetti, when cover < -(softness + 8 + 4): nothing but air here
              = floor * clamp((certain - 6) / 12)   when z <= 2
              = floor                               otherwise
    surface   = clamp(1 - cover / 32)               0 deep down, 1 at the ground

    cheese    = gate_c * (spread(cheese) + 0.35 * spread(region) - 0.90) - (1 - gate_c)

    needed    = -0.55 + 0.70 * surface
    open_s    = clamp((spread(sp_gate) - needed) / 0.2)
    width_s   = 0.065 + 0.065 * (0.5 + 0.5 * spread(sp_width))
    limit_s   = width_s * open_s * gate_s

    open_n    = clamp((spread(no_gate) - 0.1) / 0.2)
    limit_n   = 0.055 * open_n * gate_n

At a point whose gate is 0, a kind's noise is not sampled: `cheese` is -1 and its limit is 0, and the sums say the same.

A voxel blends each of `cheese`, `limit_s`, `a_s`, `b_s`, `limit_n`, `a_n`, `b_n` from the 8 points round it, in `float32_t`: first in `z`, then `x`, then `y`, each as `a + t * (b - a)`. Then:

    cheese > 0                                  cheese
    limit > 0 and a * a + b * b < limit * limit tube, for each of spaghetti and noodle

**A tube's fields are blended, and the tube is worked out at the voxel.** A tube can be thinner than the lattice is wide, so its own value, sampled only at the lattice, would miss it between points and leave it as a string of beads. Its two fields are smooth at this spacing, so blending them is close to exact, at any width. No square root: the fields' squares are compared directly.

| Field | Cell, `x` and `y` | Cell, `z` | Octaves | Limit |
|---|---|---|---|---|
| `cave/cheese` | 80 | 40 | 2 | 0.40 |
| `cave/cheese_region` | 256 | 128 | 1 | 0.54 |
| `cave/spaghetti_a`, `_b` | 48 | 32 | 1 | none: used raw |
| `cave/spaghetti_gate` | 128 | 128 | 1 | 0.54 |
| `cave/spaghetti_width` | 48 | 48 | 1 | 0.54 |
| `cave/noodle_a`, `_b` | 32 | 24 | 1 | none: used raw |
| `cave/noodle_gate` | 96 | 96 | 1 | 0.54 |

The `z` cells are the shorter ones so that what they make lies flatter than it stands tall. The lattice holds at least two points per cell of each one's last octave. `raw_noise_measures_what_the_limits_were_chosen_for` prints the measurements.

A number, a shape or a size changing changes every world, and `tests/run/terrain/cave/` pins them.

## What keeps the world whole

| What | Why it holds |
|---|---|
| No cave at or below `z = -242`, so stone under every cave | `floor` is 0 at `-242`, and a voxel at or below that blends only from points where it is 0 |
| No cheese or noodle opens to the sky | `gate_c` and `gate_n` leave 8 and 6 blocks of ground the 3D noise cannot have taken |
| No cave reaches the sea | `gate_s` asks the same up to `z = 2`, and every voxel up to the sea's surface, `z = 0`, blends only from points up to there. The two others always ask. Cover of at least `softness + 8 + 6` at a corner within 4 blocks means ground the sea cannot be in, and how steep the ground gets at a shore is measured by `no_cut_voxel_ever_touches_the_sea` |
| Spaghetti does open to the sky, about 1 column in 65 | Above `z = 2` nothing but `needed` holds it back, and `needed` rises near the ground, so only the strong parts of the gate reach it: those are a cave's mouths |

`z` is a level's only part in what caves want, so `terrain_cave_mask_box` says what a cave wants of any voxel, rock or not. `terrain_cave_carve_box` cuts anything but air, water, lava and bedrock.

Water and lava in a cave are Stage 10.4's. A cave cut below the sea is air here, and it has rock all round it.

## Why these numbers: a body has to be able to walk down them

Plan 003 phase 8 stage 5 tuned spaghetti with `bench/cave_walk/`, which walks a body from the sky. Tubes are what join the caverns, and whether they join up is a matter of how close they lie, which is sudden: with cells of 64 by 44, a body got to 1 to 5% of the places to stand under `z = -150`, and with 48 by 32 to 11 to 36% on the same seeds. A cell of 64 by 72, tubes that stand steeper but lie further apart, was worse. Shrinking a cell thins a tube, as its fields change faster, so `width_s` grew by the same 4/3, and it stays about as wide. Flooding matters as much: `terrain/hydrology`'s README says why fewer aquifer cells hold anything.

## What it costs

A tile of 16 blocks a side, the size of a chunk, works out each lattice level between the cave floor and a little above its highest ground, and nothing else. A level costs 2 fields at each point its gates reach, for cheese and for the gate and width of spaghetti, and 1 for a noodle's gate, in octaves 3, 2 and 1. A tube's two fields are only worked out for a pair of levels where that tube is open. Nearly every cell of a level is rejected from its corners, without looking at its voxels. What a chunk costs is in the stage file.

A voxel's bits never depend on which tile or box it came in, as the lattice is lined up on the world.
