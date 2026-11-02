# run/src/render

**Purpose:** the CPU side of drawing the world: holds what getting sections to the card takes, and later the frame sequence.
**Tier:** Owner.
**Owns:** `owned_host` with `arena_host` over it, for the host's copy of the column table, the slab bookkeeping, and upload's jobs, staging and ring; and on the card, `owned_pool` and `owned_table`, for one world at a time. For drawing, `owned_frame_host` with `arena_frame` over it, and on the card the textures, the three tables, a frame's four targets, what goes from frame to frame and each frame's entity lists and hits, from `render_open_frame` to `render_close_frame`.
**Lifetime:** Process for the host region, World for what is in it and for the blocks on the card.
**Threads:** `main` for init and deinit; `render_open` and `render_close` from whoever owns the world (`game`, Stage 08.4), with the `gpu` thread not using it.
**Public functions** (`run/include/render/index.h`):

- `render_limits_default`: `gpu.budget` from `config/machine.yaml`, and a table for the largest render distance.
- `render_init`, `render_deinit`: reserve host memory and take nothing on the card; release it. Deinit is safe on a zeroed `render_t`.
- `render_pool_bytes_for`: a pool size for a table of this many slots.
- `render_open`, `render_close`: take and give back the blocks on the card, for one world.
- `render_open_frame`, `render_close_frame`: the same for drawing: textures, tables, a frame's targets, and the pictures.

**May be called by:** `run/src/main/`, which starts and stops it, and `game`, which opens a world.
**Depends on:** `heap`, `arena`, `log`, `machine`, `job`, `texture`, `display/flip`, `world/edit`, `gpu_bridge/device_memory`, `gpu_bridge/world_buffers`, `gpu_bridge/traverse`, `gpu_bridge/shade`, `gpu_bridge/post`, `gpu_bridge/frame`, and its parts below.

## Parts

| Part | Job |
|---|---|
| `encode` | One section to the bitmask tree, on a worker |
| `upload` | The budgeted queue from snapshots, through the workers, to the card |

| `frame` | The tables the kernels read, the free-fly camera, the pictures on their way home, and putting one on the screen |
| `entities` | The sim's boxes to the gpu thread, each frame's per-tile lists on the card, what is aimed at and mined |

`lod` arrives with 09.6.

## Why init takes nothing on the card

`render_init` runs in every program start, and device memory has no untouched-pages-are-free rule. So only `render_open` takes any, and only as much as that world asks for. Until `game` opens a world, nothing in the program does.

The host region is reserved for the largest world the limits allow, and pages cost nothing until touched: at the default limits about 400 MiB of address.
