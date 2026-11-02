# run/src/world

**Purpose:** the blocks and light of the world in memory, and the regions all of that lives in.
**Tier:** Owner.
**Owns:** `owned_sections` with `arena_sections` over it, from which `section_pool` takes, and `owned_columns` with `arena_columns` over it, for the column table and what `world_open` puts beside it: `map`, `edit`, `snapshot` and `light`, all in the `world_t`.
**Lifetime:** World. `world_reset` is the boundary, and today one world lasts the process, under `app_t`.
**Threads:** `main` for init and deinit. One thread at a time for the pool: see `section/README.md`.
**Public functions** (`run/include/world/index.h`):

- `world_limits_default`: the limits a real game runs with.
- `world_init`, `world_deinit`: reserve and release both regions. Deinit is safe on a zeroed `world_t`.
- `world_reset`: takes back every section and column at once, with no world open.
- `world_open`, `world_close`, `world_open_bytes`: start and end the map, the edit log, the snapshots, light and ticks, for one world. `world_close` resets. `game` calls these (Stage 08.4); today only tests do.

**May be called by:** `run/src/main/`, which starts and stops it, and later `game`, `terrain`, `render`, `save`.
**Depends on:** `heap`, `arena`, `log`, `machine`, and its parts below.

## Parts

| Part | Job |
|---|---|
| `section` | One 16x16x16 cube: palette storage, light, and the pool their arrays come from |
| `column` | 48 sections, coordinates, and the `z` limits |
| `map` | The toroidal table of columns, and what streams in and out by distance |
| `edit` | Every change once a column is in: the edit log, dirty flags, and the snapshots the renderer reads |
| `light` | Sky and block light: flood fill across sections and columns, incremental on every edit |
| `tick` | The block simulation: fluids, falling blocks, leaf decay, crops and grass |

`tick` arrives with 11.2.

## The limits

Both come from `config/machine.yaml`, and neither is a size: pages cost nothing until touched.

- Sections: three eighths of `memory.process_budget_mib`, 6 GiB today. That is about a million sections with a 4-bit palette and varied light.
- Columns: a square of `game.render_distance_max` in each direction, 1025 x 1025 slots at 1436 bytes each (1160 for the column, 276 for the map's, the log's, the snapshots', light's and ticks' records of it), 1.31 GiB, and 77.1 MiB besides for the log, for three views of 1024 sections and for light's queues.

A column table this size is touched only where a map streams columns in. The records beside it are touched in proportion to the radius given to `world_open`, not to this limit. Running out of section memory is not fatal: `section_block_set` and friends return false and change nothing, and the caller chooses. A table too large for the limit is refused by `world_open`, with nothing changed.

Under ASan, `arena_init` poisons what it covers, so the sanitize preset may touch up to 0.9 GiB of shadow at start for these two regions: an eighth of their size, by arithmetic and not measured.
