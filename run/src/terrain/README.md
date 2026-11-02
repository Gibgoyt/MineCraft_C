# run/src/terrain

**Purpose:** makes a world's ground from its seed. Same seed, same bytes, for any number of workers in any order.
**Tier:** Owner.
**Owns:** `owned_tables`, a region of `TERRAIN_TABLES_BYTES`, and `arena_tables` over it, which lends everything computed once per world to the parts.
**Lifetime:** World: one `terrain_t` serves one seed. A new seed is a `terrain_deinit` and a `terrain_init`.
**Threads:** `terrain_init` and `terrain_deinit` on one thread, with no worker reading. In between it does not change, and any worker may read it.
**Public functions:**

- `terrain_init`, `terrain_deinit`: a pair.

**May be called by:** `main`, until `game` holds it (Stage 08.4).
**Depends on:** `heap`, `arena`, and its parts.

## Parts

| Part | Job | Stage |
|---|---|---|
| `rng` | Seeded streams and per-position hashes | 10.1 |
| `noise` | Fractal noise fields, sampled in bulk on the NASM kernel | 10.1 |
| `climate` | Continentalness, erosion, ridges, temperature, humidity and weirdness at any column | 10.2 |
| `density` | The ground's shape: height splines, 3D density, and bedrock, stone, water and air for any box of voxels | 10.3 |
| `cave` | Cheese, spaghetti and noodle caves cut from any box of voxels | 10.5 |
| `ore` | Deepslate by depth, and the table of rock and ore veins by depth, laid into any box of voxels | 10.5 |

A part that needs tables takes them from `arena_tables` in its `_init`, called from `terrain_init`, and adds its noise fields
to `noise_bank` there too. `TERRAIN_NOISE_FIELDS_MAX` says how many fields fit.

## The one rule that keeps worlds the same

**What a job computes for a position comes only from `terrain_t` and that position.** Nothing is written during generation,
no stream carries over from one position to the next, and nothing depends on which worker ran or what ran before.
`tests/run/terrain/seed/` holds that to 1 worker against 16, and to any order of the work.
