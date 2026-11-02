# run/src/terrain/noise

**Purpose:** fractal noise fields sampled in bulk, on `noise3_batch8` from `asm/src/noise/`: a whole grid or list of points in blocks goes in, one float per point comes out.
**Tier:** Arena: the bank's table comes from an `arena_t *` its parent gives it. Sampling allocates nothing.
**Owns:** nothing. Its fields are in the arena `terrain_init` lends.
**Lifetime:** World: a bank is made for one seed.
**Threads:** `terrain_noise_bank_init`, `_add` and `_deinit` on the thread running `terrain_init`. Sampling from any thread at once, since a field does not change once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_noise_bank_init`, `terrain_noise_bank_deinit`: a pair.
- `terrain_noise_bank_add`: makes a field from the seed, a name and a shape.
- `terrain_noise_grid_points`: how many floats a grid fills.
- `terrain_noise_sample_grid`, `terrain_noise_sample_points`: the bulk sampling.

**May be called by:** `terrain` and its parts.
**Depends on:** `simd/noise` (the kernel, by its prototype), `terrain/rng` (its sibling, for each octave's seed and offsets), `arena`, `log`, `types`.

## What a value is

For a point `p` in blocks, with `amp`, `freq`, `off` and `seed` from the octave:

    value = sum over octaves, in order, of  amp * noise3(float32(p.x * freq.x + off.x), ..., seed)

Nothing in that depends on the batch, the order or the thread, so splitting work differently cannot change a bit. No stream
is carried across points. `tests/run/terrain/seed/` pins the result, and it passes the same pin on the debug and release
presets: see the stage file.

Each octave gets its own kernel seed and its own offset from `terrain_rng_derive(field, octave)`, where `field` comes from the
world's seed and a hash of the field's name. So adding a field, or a later stage's field being added first, moves nothing
in the fields that were there. Without the offset, every octave of lacunarity 2 would be 0 together at each whole-number
point of the first.

## How far out it is good

The kernel takes `float32_t`, and that sets a limit no wrapper can lift, at any frequency: at block `B`, a cell coordinate
can only move in steps of about `B * 2^-23` blocks.

| Block | Step of the input |
|---|---|
| 1,000,000 | about 1/8 of a block |
| 8,388,608 (2^23) | a whole block |
| Past that | neighbouring blocks start to share a value |

So terrain stays smooth to about a million blocks out, and starts to go to stairs some way past that. It stays finite and
repeatable all the way to the ends of an `int32_t`. Wrapping coordinates into a smaller range does not help, because the
kernel's lattice hash only repeats after 2^32 cells, so a wrap would leave a visible seam.
