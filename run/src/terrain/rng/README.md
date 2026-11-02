# run/src/terrain/rng

**Purpose:** seeded random streams and per-position hashes, so that everything random in a world is a pure function of its seed and a position.
**Tier:** Arena, allocating nothing: it has no memory but the eight bytes of a caller's `terrain_rng_t`.
**Owns:** nothing.
**Lifetime:** Job: a stream lives as long as the caller's local.
**Threads:** any. There is no shared state, and one `terrain_rng_t` belongs to one thread.
**Public functions:**

- `terrain_rng_mix`: SplitMix64's finalizer, a bijection on 64 bits.
- `terrain_rng_salt`: a salt from a name.
- `terrain_rng_derive`: a child seed from a seed and a salt.
- `terrain_rng_hash2`, `terrain_rng_hash3`: the hash of a position.
- `terrain_rng_start`: a stream from a seed.
- `terrain_rng_next_u64`, `terrain_rng_next_u32`, `terrain_rng_next_below`, `terrain_rng_next_f32`, `terrain_rng_next_f64`: the next number of a stream.

**May be called by:** `terrain` and its parts. `terrain/noise` uses it to derive each octave.
**Depends on:** `assert`, `types`.

## How to use it

Never carry a stream across positions: what a column gets would depend on which columns ran first, and so on the worker
count. Start one from the position instead:

    const uint64_t trees = terrain_rng_derive(terrain->seed, terrain_rng_salt("feature/trees"));
    terrain_rng_t rng = terrain_rng_start(terrain_rng_hash2(trees, x, y));

The generator is SplitMix64, and `tests/run/terrain/seed/` holds it to the published vector for seed 1234567. The hashes
are built from the same finalizer: the seed is mixed first, and each coordinate is xored in and mixed. They are pinned to
values worked out separately in Python. Changing anything here changes every world.
