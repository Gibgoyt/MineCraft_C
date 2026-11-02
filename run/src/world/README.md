# run/src/world

**Purpose:** the blocks and light of the world in memory, and the regions all of that lives in.
**Tier:** Owner.
**Owns:** `owned_sections` with `arena_sections` over it, from which `section_pool` takes, and `owned_columns` with `arena_columns` over it, for `column_array_alloc`.
**Lifetime:** World. `world_reset` is the boundary, and today one world lasts the process, under `app_t`.
**Threads:** `main` for init and deinit. One thread at a time for the pool: see `section/README.md`.
**Public functions** (`run/include/world/index.h`):

- `world_limits_default`: the limits a real game runs with.
- `world_init`, `world_deinit`: reserve and release both regions. Deinit is safe on a zeroed `world_t`.
- `world_reset`: takes back every section and column at once, when a world closes.

**May be called by:** `run/src/main/`, which starts and stops it, and later `game`, `terrain`, `render`, `save`.
**Depends on:** `heap`, `arena`, `log`, `machine`, and its parts below.

## Parts

| Part | Job |
|---|---|
| `section` | One 16x16x16 cube: palette storage, light, and the pool their arrays come from |
| `column` | 48 sections, coordinates, and the `z` limits |

`map` and `edit` arrive with Stage 07.3, `light` with 11.1, and `tick` with 11.2.

## The limits

Both come from `config/machine.yaml`, and neither is a size: pages cost nothing until touched.

- Sections: three eighths of `memory.process_budget_mib`, 6 GiB today. That is about a million sections with a 4-bit palette and varied light.
- Columns: a square of `game.render_distance_max` in each direction, 1025 x 1025 at 1160 bytes each, 1.14 GiB.

A column table this size is touched only where a map streams columns in. Running out of section memory is not fatal: `section_block_set` and friends return false and change nothing, and the caller chooses. Running out of column memory cannot happen for a count within the limit, and past it aborts, because a count is a choice made once at init.

Under ASan, `arena_init` poisons what it covers, so the sanitize preset may touch up to 0.9 GiB of shadow at start for these two regions: an eighth of their size, by arithmetic and not measured.
