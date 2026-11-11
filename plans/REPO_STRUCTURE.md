# Repository structure

A map of the entire codebase, so you can find anything by reasoning about where it must live instead of grepping.

**Status:** audited against the tree by Stage 18.3, 2026-11-04. A stage that adds or renames a directory updates this file.

Rules for code are in `plans/CODING_STANDARD.md`. Rules for planning are in `plans/README.md`.

---

## 1. The idea

Three rules make the tree predictable:

1. **The path says when the code runs.** `compile/` runs while building. `run/` runs in the game. `cuda/` runs on the GPU.
   `asm/` is CPU SIMD.
2. **The path says what the code may do with memory.** See section 3.
3. **Every directory holds one module:** one `index.*` and a `README.md`. No file names to remember.

```
./src/main.c                ✗ never
./run/src/main/index.c      ✓
./run/src/main/README.md    ✓
```

---

## 2. Top level

| Path | What it holds | When it runs |
|---|---|---|
| `config/` | Human-readable YAML: hardware, blocks, items, recipes, biomes, mobs | Read by `compile/` |
| `compile/` | C tools: YAML conversion, texture translation, table generation, standards lint | Build time |
| `run/` | The game and engine, in C | Run time, CPU |
| `cuda/` | Ray-casting kernels and device code | Run time, GPU |
| `asm/` | NASM batch SIMD kernels, and embedded data | Run time, CPU |
| `textures/` | Translated texture data, recipes, licences | Input to `compile/` |
| `sounds/` | Recipes and recordings for mining and placing sounds, with licences. See its `README.md` | Input to `compile/` |
| `tests/` | Tests mirroring source paths. No server, no display | `ctest` |
| `bench/` | Benchmarks and recorded results | On demand |
| `cmake/` | Build logic | Configure time |
| `tools/` | `test-or-build.sh` mutex | Wraps heavy commands |
| `plans/` | Plans, this file, the coding standard | Docs |
| `research/` | Source research and speed notes | Docs |
| `build/` | Generated and compiled output. **Never committed** | Build time |

Root files: `README.md`, `CMakeLists.txt`, `CMakePresets.json`, `.gitignore`, `.clang-format`.

---

## 3. Memory tiers: path depth is the ownership rule

Full rules are in `CODING_STANDARD.md` section 4. In short: **every heap block has one owner, traceable to `app_t` on
`main()`'s stack.**

| Depth | Path | Tier | Allocation |
|---|---|---|---|
| 0 | `run/src/main/` | **Root** | None; declares `app_t` on the stack |
| 1 | `run/src/<system>/` | **Owner** | `heap_reserve`/`heap_release`; `_init` and `_deinit` pair |
| 2 | `run/src/<system>/<part>/` | **Arena** | Only from an `arena_t *` given by its parent |
| 3+ | `run/src/<system>/<part>/<sub>/` | **Borrow** | None |

`run/src/heap/` is the only code that calls the OS allocator. `cuda/src/device_memory/` is the only code that calls
`cudaMalloc`.

**Debugging by path:**

- A leak? Look at depth 1 of that system. Nothing below it can reserve memory.
- Memory corruption in a part? Look at who passed it the pointer and length: its parent.
- Something at depth 3 allocates? That is a build failure.

### Module anatomy

```
run/src/world/                  depth 1: Owner of everything below
├── index.c                     world_init, world_deinit
├── README.md
├── section/                    depth 2: Arena tier
│   ├── index.c
│   └── README.md
└── map/
    ├── index.c
    └── README.md

run/include/world/
├── index.h                     public API of the Owner
├── section/index.h             #include "world/section/index.h"
└── map/index.h
```

Headers mirror source paths, with two kinds of exception, each saying so in its README. A header with no C behind it:
`run/include/types/`, `run/include/simd/<kernel>/` (`asm/` implements it) and `run/include/gpu_bridge/<area>/` (`cuda/`
implements it, and its `run/src/` directory holds only a README). A part declared with its Owner: `run/src/game/input/`,
`report/`, `settle/` and `tick/`, in `run/include/game/index.h`, and `run/src/save/world/record/`, in `run/include/save/world/index.h`. Include roots: `run/include`, `compile/include`, `cuda/include`,
`asm/include`, and `build/<preset>/generated`.

### Every `README.md` states

```markdown
# <path>
**Purpose:** one sentence.
**Tier:** Root | Owner | Arena | Borrow.
**Owns:** regions and arenas, or "nothing".
**Lifetime:** Process | World | Tick | Frame | Job.
**Threads:** which threads call this.
**Public functions:** list with a one-line description of each.
**May be called by:** which modules.
**Depends on:** which modules.
```

---

## 4. `config/`: what the game is, in plain text

| Path | Contents | Read by |
|---|---|---|
| `config/machine.yaml` | Platform (`posix: true`), threads, CPU, SIMD, GPU, display, memory, world constants | `compile/src/machine_config/` |
| `config/blocks/<block>/index.yaml` | Hardness, right tool, drops, shape, light, textures. `config/blocks/README.md` lists every field | `compile/src/game_tables/` |
| `config/items/<item>/index.yaml` | Stack size, durability, food, tool tier | `compile/src/game_tables/` |
| `config/tiers/<tier>/index.yaml` | A tool tier: what it harvests, how fast it mines, and its axe's and hoe's swings (plan 005 phase 6) | `compile/src/game_tables/` |
| `config/recipes/<recipe>/index.yaml` | Crafting and smelting, each at its station: the hands, the crafting table (which makes only the other stations), the woodworking and stoneworking benches, the smith table, the leather workbench, the furnace, the blast furnace and the tanning rack (plan 005 phase 6) | `compile/src/game_tables/` |
| `config/biomes/<biome>/index.yaml` | Climate range, surface blocks, features | `compile/src/game_tables/` |
| `config/ores/<ore>/index.yaml` | Where an ore or rock lies in veins: its two blocks, what it drops, and each row of veins' order, depths, size and rate (plan 005 phase 11) | `compile/src/game_tables/` |
| `config/mobs/<mob>/index.yaml` | Health, speed, drops, spawn rules, body parts | `compile/src/game_tables/` |

To see what mining dirt does, open one file:

```yaml
# config/blocks/dirt/index.yaml
name: dirt
hardness: 0.5
tool: shovel
drops: [{ item: dirt, count: 1 }]
texture: { all: blocks/dirt }
```

`machine.yaml` generates one output per consumer, in `build/<preset>/generated/machine/`: `index.h` (C), `index.hu`
(CUDA), `index.inc` (NASM), `index.cmake` (CMake). Everything agrees because everything comes from one file.
`compile/src/machine_config/report/` is a small program that prints the defines as compiled, to show a yaml change
reaching a built binary.

---

## 5. `compile/`: tools that run during the build

Layout: `compile/src/<tool>/index.c`, `compile/include/<tool>/index.h`, `README.md`. Never linked into the game.

| Tool | Job | Stage |
|---|---|---|
| `yaml` | Our own YAML-subset parser, with line-numbered errors | 02.2 |
| `machine_config` | `machine.yaml` → `.h .hu .inc .cmake` | 02.3 |
| `standards_lint` | Fails the build on any coding-standard breach. Parts: `support catalog lexer graph rules_c rules_include rules_asm rules_cmake rules_tree` | 02.4 |
| `struct_offsets` | Emits `.inc` offsets from C structs, for NASM, and the same numbers as `static_assert`s for C. `cmake/targets.cmake` lists the headers it reads | 04.3 |
| `inflate` | Our own DEFLATE decoder | 06.1 |
| `png` | Our own bounds-checked PNG decoder (packs are untrusted) | 06.1 |
| `texture_pack` | Open pack in `/tmp/mc_c_assets/` → `textures/`; `verify` checks that tree with no pack. Parts: `support mctex manifest pack output verify`. Run by hand, never by the build | 06.2 |
| `texture_vector` | Procedural recipes -> `textures/`, and vector data per texture (`.mcvec`); `check` holds the tree to `textures/vector.yaml`. Parts: `analyze palette mcvec materials recipe`. `recipes` and `check` are run by hand and by its test; 06.4's blob step runs `vectors` | 06.3 |
| `texture_blob` | Mip chains, and every texture with its vector data in one blob, in `build/<preset>/generated/textures/`, run by `cmake/generated.cmake`. Part: `mip`. The format is `run/include/types/texture_blob/` | 06.4 |
| `sound_pack` | `sounds/` → one blob in `build/<preset>/generated/sounds/`, making each sound from its recipe or recording, run by `cmake/generated.cmake`. `verify` checks the tree and `import` takes in one recording, both by hand. Parts: `mcsnd synth tree blob import`. The format is `run/include/types/sound_blob/` | 003 phase 5 |
| `game_tables` | `config/{blocks,items,recipes,biomes,mobs}` → C tables in `build/<preset>/generated/tables/`, compiled into `mc_run` and included as `"tables/blocks/index.h"`, run by `cmake/generated.cmake`. Parts: `blocks` (07.1), `biomes` (10.6), `items` (13.1), `recipes` (13.2), `mobs` (15.3) | 07.1 |

---

## 6. `run/`: the game

Layout: `run/src/<system>/<part>/index.c`, `run/include/<system>/<part>/index.h`, `README.md`.

### Foundation

| System | Parts | Job | Stage |
|---|---|---|---|
| `main` | `cpu_check` | Entry point, `app_t`, CPUID check against the build's flags, init and deinit order | 03.1 |
| `types` | | The only home of `float32_t`, `float64_t` | 03.1 |
| | `texture_blob` | The texture blob's format, in the one place both `compile/` and `run/` may include | 06.4 |
| | `sound_blob` | The sound blob's format and the sound families, in that same place, so that `compile/src/game_tables/blocks/` and `compile/src/sound_pack/` mean one family by one number | 003 phase 5 |
| `assert` | | Assertions | 03.1 |
| `log` | | Logging | 03.1 |
| `heap` | | **Only OS allocator user**; counts everything, holds to `memory.process_budget_mib` | 03.2 |
| `arena` | | Bump allocators over memory an Owner reserved and passes in | 03.2 |
| `thread` | `ring` | Pinning physical cores first; single-producer rings | 03.3 |
| `time` | | Monotonic clock | 03.3 |
| `file` | | File wrappers | 03.3 |
| `job` | | Work-stealing job system, per-thread arenas | 03.4 |
| `profiler` | | Per-thread scopes, trace dump | 17.1 |

### Math and SIMD

| System | Parts | Job | Stage |
|---|---|---|---|
| `math` | `vec2 vec3 vec4 ivec3 mat4 quat aabb ray` | Our vector library, scalar C; `math_init` checks the floating-point environment | 04.1 |
| `simd` | `cpuid` | C prototypes of the CPUID and XGETBV functions, used by `main/cpu_check` | 03.1 |
| | `copy blend bitmask noise vec_batch` | C prototypes of NASM kernels | 04.2, 04.3 |
| | `embed_assets` | C declarations of the data `asm/src/embed_assets/` links in; no kernels | 06.4 |
| | (the Owner) | `simd_init`, `simd_deinit`: scratch region and arena, and a self-check of every kernel at startup | 04.2 |
| | `reference` | Scalar C version of each kernel: what it must match in tests, and beat in `bench/simd` | 04.2 |

### Display and input

| System | Parts | Job | Stage |
|---|---|---|---|
| `display` | `device mode flip` | Raw-ioctl KMS: card, connector, mode, dumb buffers, page flip | 05.1 |
| | `vt_guard` | Console takeover; always restores, even on a crash | 05.2 |
| | `headless` | No-display backend, scripted input replay, PNG screenshots for tests and agents | 05.4 |
| | `x11` `x11/wire` | A window on the local X server: our own protocol over a Unix socket, frames in MIT-SHM. See plan 002 | plan 002 |
| | `headless/png` `headless/script` `headless/demo` | Stored-DEFLATE PNG encoder; the script language; the scene `--headless` shows without `--test-world`, which needs no card | 05.4 |
| `input` | `ops discovery events` | evdev discovery by capability, raw keyboard and mouse events, hot-plug | 05.3 |

### Rendering (CPU side)

| System | Parts | Job | Stage |
|---|---|---|---|
| `gpu_bridge` | one per CUDA area | **The C API to CUDA** (`extern "C"`) | 08.1 |
| | `status` | `gpu_status_t`, `gpu_error_t`, and reporting a failure | 08.1 |
| | `device_memory` | The C API of the device allocator. Its `run/src/` directory holds only a README: `cuda/src/device_memory/` implements it | 08.1 |
| | `world_buffers` | The C API of the section pool and column table, and the form of a section on the card. Its `run/src/` directory holds only a README: `cuda/src/world_buffers/` implements it | 08.2 |
| | `traverse` | The C API of ray traversal: cameras and hits, on the format of `world_buffers`. Its `run/src/` directory holds only a README: `cuda/src/traverse/` implements it | 08.3 |
| | `entity_rays` | The C API of entity boxes and the outline. Its `run/src/` directory holds only a README: `cuda/src/entity_rays/` implements it | 09.5 |
| | `lod` | The C API of the far field's rays. Its `run/src/` directory holds only a README: `cuda/src/lod/` implements it | 09.6 |
| | `shade post frame` | The C APIs of shading, of sky, fog and the tone map, and of one frame's kernels. Each `run/src/` directory holds only a README: `cuda/src/<area>/` implements it | 08.4 |
| `render` | (the Owner) | `render_init`, `render_deinit`: the host region; `render_open`, `render_close`: the pool and table on the card, for one world | 08.2 |
| | `encode upload` | Encode sections on workers; budgeted upload queue | 08.2 |
| | `frame` | The kernels' tables, the free-fly camera, readback, present; HUD from 14.2 | 08.4 |
| | `entities` | Boxes from the sim to the gpu thread, per-tile entity lists, what is aimed at and mined | 09.5 |
| | `lod` | Build coarse far-field grids on workers, each a level in the format of `world_buffers`, following the eye | 09.6 |
| `texture` | `blob registry` | Checks the embedded blob; finds a texture, a mip level or its vector data by name or id | 06.4 |
| `audio` | (the Owner) | `audio_init`, `audio_deinit`: a region only for a run with sound, the `audio` thread, and the ring into it; `audio_post`, `audio_volume_set`. Silence and one log line if there is no sound server, and nothing ever connects from a run with no terminal, or on a display from `:80` up | 003 phase 5 |
| | `mixer` | Up to 8 voices from the sound blob, panned, added up and turned down, to samples in memory; what a place in the world comes to for each ear | 003 phase 5 |
| | `pulse` | Our client of the desktop's sound server, over the second of the two sockets the standard allows. `pulse/wire` is its protocol as bytes | 003 phase 5 |

### World

| System | Parts | Job | Stage |
|---|---|---|---|
| `world` | (the Owner) | `world_init`, `world_deinit`, `world_reset`: the two regions and arenas its parts borrow. `world_open`, `world_close`: the map, edit log and snapshots of one world (07.3) | 07.2 |
| | `section column` | Palette storage, coordinates, `z` range checks | 07.2 |
| | `map edit` | Column map, streaming, edit log, snapshots | 07.3 |
| | `light` | Sky and block light | 11.1 |
| | `tick` | Fluids, falling blocks, random ticks | 11.2 |
| | `wall_torch` | Which block a torch on a wall hangs from, for mining, blasts and the simulation alike. Not in the plan's list | 003 phase 2 |
| | `bench` | Which block is half a bench two voxels long, and where its other half is, for placing, mining and blasts alike. Not in the plan's list | 003 phase 4 |
| | `door` | Which block is a door, where its other half is, which doors the hand opens, and the facing for its hinge, for placing, opening, mining, blasts and mobs' paths alike | 005 phase 2 |
| `terrain` | (the Owner) | `terrain_init`, `terrain_deinit`: one seed, and the region and arena its parts borrow | 10.1 |
| | `rng noise` | Seeded streams and position hashes; fractal fields sampled in bulk on `asm/src/noise` | 10.1 |
| | `fields` | Continentalness, erosion, ridges, temperature, humidity, weirdness | 10.2 |
| | `density` | Height splines and 3D terrain shape, `z ∈ [-250, 1250]`; bedrock at `-250`, water up to `0` | 10.3 |
| | `hydrology` | Sea level 0, rivers, oceans, beaches, aquifers, the lava level under the world at `-230` | 10.4 |
| | `cave ore` | Cheese, spaghetti and noodle caves cut from any box; deepslate by depth, and the table of rock and ore veins by depth, generated from `config/ores/` (plan 005 phase 11) | 10.5 |
| | `biome surface` | All Overworld biomes, chosen from the climate by `config/biomes/`; what each lays on the ground and lines its caves with | 10.6 |
| | `feature` | Trees, plants, lakes, ice and snow, cross-chunk placement. `terrain_generate_box` in the Owner runs every stage | 10.7 |

### Gameplay

| System | Parts | Job | Stage |
|---|---|---|---|
| `game` | | **Owner of World, Tick and Frame arenas**; the loop connecting `sim`, `gpu`, `main` | 08.4, extended later |
| | `session` | From the title screen to Quit Game: runs the menus, opens the world chosen, pauses it, closes it. Not in the plan's list: `ui` must not call `game`, and `main` may not hold this much | 14.4 |
| | `census` | Plan 004 phase 7: the weekly census run from the sim, step by step and never waiting: the weeks owed by phase 1's trigger, oldest first (fast past 52), each week's kept tiles in batches whose climate tiles are prepared first and 2 by 2 blocks on the workers, flow accumulation, the outlook and the report, and `<world>/climate/week.<W>`; the report goes to main through a triple buffer; attached at `game_open`, stepped in `game_sim_step`, told of each column. Allocates once from the World arena | Plan 004 phase 7 |
| | `weather` | Plan 004 phase 8: the sim's side of the weather the player sees and feels: `climate_weather_at` at the eye every 10 ticks, lightning by hash in the thunderstorm cells within 2000 blocks (a day's list kept), the rain top map round the eye kept from its own edit-log cursor, the wind blended between samples for the body, `--weather` kinds, and a slot a tick to the thread that draws through a triple buffer, which eases toward it and draws with it. In the World arena beside the column jobs, as `game_t` is at its size | Plan 004 phase 8 |
| | `cover` | Plan 004 phase 8: snow and ice on loaded ground from the census's week, as block edits: one idempotent rule per block column (snow over 20 mm, off at 10; ice on still water over 5 cm, 30 at sea, off at 2), every loaded column after each census and each column as it streams in. In the World arena beside the column jobs | Plan 004 phase 8 |
| | `corner` | Plan 004 phase 10: the HUD's top right corner while walking: the sim's words (the temperature at the body's height, the wind, the season label, the storm warning, worked out on a worker) and main's text for them, in the World arena beside the column jobs. Split off as `game/index.c` is at its split line | Plan 004 phase 10 |
| | `atlas` | Plan 004 phase 9: the game's side of the Atlas: opening its store with the world, what the world tells it each frame, its keys before the game screens', drawing it in place of the HUD. Split off as `game/index.c` is at its split line | Plan 004 phase 9 |
| | `report` | What the HUD shows now and the log's lines at the end of a run. Not in the plan's list: split off at Stage 14.5 for size | 14.5 |
| | `view` | What a config comes to for what is drawn, and bringing a picture up to the display's size. Not in the plan's list: `game_reconfigure` needs it, and `game` had reached its size for splitting | 14.5 |
| | `settle` | One thread, until all the eye's place wants is on the card. Not in the plan's list: split off at the Phase 14 gate, as `input` was | 14 gate |
| | `tick` | One tick of the world, `game_sim_tick`. Not in the plan's list: split off at the Phase 18 gate, at the standard's 1500 lines | 18 gate |
| | `input` | A poll of real input or a script's tick turned into flying, the body's wishes, the hotbar and the overlay's key. Not in the plan's list: split off at the Phase 14 gate, when `game` passed its size for splitting | 14 gate |
| `calendar` | | The date from the world clock: year, day of the year (whole, and continuous for the sun), week, season label, the HUD's date line, and which weeks a late census runs. Pure arithmetic with no state, on the bottom row of section 11 beside `math` and `time` | Plan 004 phases 1, 2 |
| `climate` | | Plan 004's climate, worked out top to bottom. The latitude of a place from its y alone, a triangle wave with the tropic at y = 10,000 and a pole at 38,298. The Owner of the climate cells: one region (146 MiB of address space) for the tile cells, the land-mask pages and both tables' slots, with a 64-byte `climate_t` in `app_t`; the warp fields, the tile and page jobs, the sim's scheduler, the spawn precompute and the reads (`climate_cell_get`, the warped bilinear `climate_sample_at`, from phase 4 `climate_air_at`, `climate_sea_temperature`, `climate_current_flow`, from phase 5 `climate_normals_at` and the tests' `climate_test_hooks_set`, and from phase 6 the weather systems' kind seeds and hooks and the coarse mask read anywhere: `climate_mask_page_read`, `climate_mask_point`, `climate_mask_height_at`, and from phase 7 `climate_normals_days_at`, a week of normals in one read). From phase 7 a second region (320 MiB of address) for the weather: its noises and the store of kept tiles. Reads the ground only through `terrain_height_estimate_area`. Ranked between `render` and `terrain` in section 11 | Plan 004 phases 2, 3, 4, 5, 6, 7 |
| | `sun` | Declination by day of the year, sunset hour angle and day length, and the sun's direction, elevation and azimuth at a latitude and moment, or from the world clock and a y. Pure, allocates nothing | Plan 004 phase 2 |
| | `sea` | Which water is sea (its body reaches the sea floor's depth, or is over 33.6 km²), a large lake (over 2 km²) or a small one, by a 4-connected flood fill; and Felzenszwalb and Huttenlocher's exact distance transform with the nearest cell. On grids the caller lends; knows nothing of tiles | Plan 004 phase 3 |
| | `cache` | The slots of the two toroidal tables (64 by 64 tiles, 128 by 128 mask pages): the key, the state, the reader count, the keep square, and claim, publish and evict, seq_cst on both sides | Plan 004 phase 3 |
| | `mask` | The coarse land mask: a page of 256 cell-centre heights per tile in the sparse table, and a tile's 272 by 272 window of 17 by 17 pages, building a missing page into the table or privately, never waiting | Plan 004 phase 3 |
| | `cell` | `climate_cell_t` (128 bytes), one tile's 256 cells from its window (sea and land distances and directions, κ, `z_ground`, latitude, flags), the window handed out as `climate_window_t`, the bilinear blend, and the climate's constants and ground-function type | Plan 004 phases 3, 4 |
| | `normals` | The one home of every knot table of plan 004 (`p_belt`, `T̄_lat`, `A_sea`, `A_land`, and from phase 5 the rain belts `P_belt`), each joined with the terrain's spline, built once in `climate_init` | Plan 004 phases 4, 5 |
| | `harmonic` | A yearly cycle as an average plus one yearly wave, and from phase 5 two: the 12-day least-squares fits and evaluating a stored cycle. Allocates nothing | Plan 004 phases 4, 5 |
| | `air` | A tile's ocean currents (warm, cold, neutral by latitude and coast side, with strength and flow), swing and lag, heat-low pressure, and wind (belts by the wind latitude, the pressure wind turned 70°, steering by slopes, altitude), fitted from 12 days; the pieces with their worked examples. Pure, from the window; a window sea cell's current is public for phase 5 | Plan 004 phases 4, 5 |
| | `heat` | Temperature (master plan 6.6) as a pure formula of the yearly mean at sea level, height (12 °C per 1000 blocks), the basin heat below sea level on land, and phase 4's swing and lag; the daily range, the snow share, and a tile's `t_mean`. Allocates nothing | Plan 004 phase 5 |
| | `rain` | Rain (6.7): the belts at the wind latitude, the upwind walk (16 steps reading the window's kinds and currents, then on to 8 km reading heights only), moisture, the orographic factor and rain shadow, on 12 days fitted to two yearly waves into `rain_mm_week`; the tests' hooks type. Pure, from the window | Plan 004 phase 5 |
| | `season` | Hurricane season (6.8) packed as quarter days with the hurricane-coast flag, the monsoon flag and monsoon today, the snow balance height by a degree-day model and bisection, and the simplified Köppen classes (6.10). Pure, from the window | Plan 004 phase 5 |
| | `layer` | Climate map layers to XRGB8888 pixels: sea distance, continentality, sea and lakes, latitude lines; from phase 4 currents, pressure and wind, each for a day; from phase 5 temperature (day, mean, warmest, coldest), rain (year, day), hurricane season, monsoon, climate class and snow balance. The Atlas and the map tools draw with it; from phase 6 five "now" layers from a day list of weather systems: pressure now with H and L, the systems with their tracks and fronts, rain radar, thunderstorm cells and wind now, systems over unbuilt land drawn as outlines | Plan 004 phases 3, 4, 5, 6 |
| | `systems` | Weather systems (plan 004 phase 6, master plan 7.2 to 7.4): hurricanes, lows with cold fronts, highs and thunderstorm cells, spawned by a hash per spawn square and day as Poisson counts thinned to each kind's rate by place and season, steered by rules reading only latitude, the season and the coarse mask, and listed per day near a place in a fixed order, so the same bytes on 1 worker or 16. Every constant of the phase, the tuned rates marked. Allocates the list, its scratch and a mask page cache from the arena it is given | Plan 004 phase 6 |
| | `systems/field` | A day list's pressure (bells), wind (7.3's profile circling by hemisphere and mirror, reversed for highs), rain (eyewall and spiral bands, a low's shield and front, cells), fronts, the high's cut, `f_sys`, the day's share and a place's wetness. Allocates nothing | Plan 004 phase 6 |
| | `systems/name` | Hurricane names from syllables by a system's name seed, and "Hurricane" or "Tropical storm" titles in 24 bytes. Allocates nothing | Plan 004 phase 6 |
| | `weather` | Plan 004 phase 7 (master plan 7.4, 7.5): each day's weather at any place, pure (normals, the day's weather systems through views of a day list, three noises scaled to a measured spread of 1: spells, rain amount, rain hours), the day's kind, `climate_weather_at` for any moment; the store of kept tiles (generated land and a 2-tile drainage margin, `climate_week_tile_t`, saved), spin-up, a census week for a block of tiles, and flow accumulation over every kept cell. Allocates from the week arena and a worker's Job arena | Plan 004 phase 7 |
| | `weather/week` | `climate_day_t`, `climate_week_t` (64 bytes, saved), the day-kind table in its order, and master plan 7.5's weekly update (melt, snow, evaporation, the soil bucket, runoff, ice day by day) on plain numbers. Allocates nothing | Plan 004 phase 7 |
| | `weather/flow` | D8 flow accumulation over the caller's cells (fixed neighbour order, height then y then x) and the flood index `Q / max(20, 3 Qbar)`. Allocates nothing | Plan 004 phase 7 |
| | `outlook` | Plan 004 phase 7 (master plan 8): chances for the next 7 days, in two halves: what is known at the census moment into a plain input (systems alive then and their steered tracks, normals, the unborn systems' expected count), and the chances from that input alone, with a Gaussian forecast error drawn by hash; the footprint form and the exact disk chance. From phase 10 the per-system chance at any moment, the known list and the unborn rate sums at any instants, for the storm warning | Plan 004 phase 7 |
| | `label` | Plan 004 phase 10 (master plan 4.1): the season at a place on a day from plain numbers: the hemisphere's season in thirds, wet or dry between the tropics, polar night and midnight sun, hurricane season and monsoon, and its text joined by middle dots. Allocates nothing | Plan 004 phase 10 |
| | `warning` | Plan 004 phase 10 (master plan 9.1): the HUD's storm warning, over 50% within the next day: each known storm's best outlook chance at now and every quarter day to a day ahead, the unborn arrivals, and the rule (named, hurricane likely, storm). Allocates from the arena it is given | Plan 004 phase 10 |
| `physics` | `collision raycast` | Swept AABB; DDA block targeting | 12.1, 12.2 |
| `player` | `movement camera` | Walk, jump, swim, look | 12.1, 12.2 |
| | `combat` | The attack: aim, swing, strength | 15.7 |
| | `interaction` | **Mining, drops, placing** | 12.3 |
| | `survival` | Health, hunger, death, respawn | 13.4 |
| `item` | `stack inventory` | Stacks, hotbar, 36 slots | 13.1 |
| | `crafting smelting container` | Recipes, furnace, chest | 13.2 |
| | `equipment` | Tools, durability, armor | 13.3 |
| `entity` | `item_drop` | Dropped items | 12.4 |
| | `store grid animation` | SoA storage, spatial grid, body parts | 15.1 |
| | `projectile` | Arrows | 15.5 |
| | `explosion` | Ray-based explosions | 15.6 |
| `mob` | `path spawn combat` | Pathfinding, spawning, damage | 15.2, 15.3, 15.7 |
| | `zombie skeleton creeper` | One directory per mob | 15.4-15.6 |

### UI, settings, saves

| System | Parts | Job | Stage |
|---|---|---|---|
| `ui` | (the Owner) | `ui_init`, `ui_deinit`: the region and arenas its parts borrow; `ui_frame_begin`, `ui_frame_begin_over`, `ui_frame_end`: a frame on a canvas | 14.1 |
| | `compose` | Canvas, clipping, images and nine parts: every pixel through `asm/src/blend`. Not in the plan's list: `font` and `widget` both draw through it, and sit beside it | 14.1 |
| | `font widget` | Bitmap font, widgets | 14.1 |
| | `hud` | Crosshair, hotbar, hearts, drumsticks, bubbles of air, coordinates, debug overlay with FPS, per-stage times and coordinates; the top right corner: date, season, climate line with its wind arrow, storm warnings, and a slot kept for a settlement's name | 14.2, plan 004 phases 1 and 10 |
| | `screen_inventory screen_crafting screen_furnace screen_chest` | Game screens. `screen_inventory` also holds what all four share: the ring from `main` to `sim`, the views back, the item pictures and the window, and the other three include it, as its README allows | 14.3 |
| | `menu` | Main menu, world create and select, pause, loading | 14.4 |
| | `menu/catalog` | Worlds on disk: each one's `world.meta`, what a typed name and seed turn into | 14.4 |
| | `screen_settings` | **Render distance and all options** | 14.5 |
| | `screen_census` | The weekly census screen over the stopped world: phase 1's week, date and Continue; from phase 7 the census's report (`ui_census_report_t`, defined here as `ui` may not include `game`): where you are last week, events within render distance, the outlook table and the named systems, a progress bar while it works, Continue greyed until the file is written or failed | Plan 004 phases 1, 7 |
| | `atlas` | Plan 004 phase 9: the Atlas on `M`, a full-screen map of every generated column with ungenerated land grey, while the game runs: the store of column tops (160 tiles of 512 by 512 in memory, 8 levels, a ticket lock), its files under `<world>/atlas/`, the view, the keys, the drawing job in 16 bands and the saving job, and each frame's title, legend, day slider, readout and player mark. Its memory is the region `ui_t` reserves | Plan 004 phase 9 |
| | `atlas/tile` | The Atlas's pixel (RGB565 and height), a column's top, a column into its tile, halving into a mip, the `.tile` file's encode and decode. Borrow tier | Plan 004 phase 9 |
| | `atlas/draw` | The Atlas's drawing tools: colour scales, hill shading, marching squares, lines, circles, discs, arrows, forecast cones, the over-grey rule, legends. Borrow tier | Plan 004 phase 9 |
| | `atlas/layer` | The Atlas's layer registry (26 rows) and every layer's bands and overlay, through `climate/layer` where it already draws a field; a ground from the terrain's estimate for the map tool and tests. Borrow tier | Plan 004 phase 9 |
| `settings` | (the Owner) | `settings_init`, `settings_deinit`, `settings_load`, `settings_save`: the values, and where they are kept | 14.5 |
| | `persist` | Their ranges and defaults, and the file, with our own runtime parser | 14.5 |
| `save` | (the Owner) | `save_init`, `save_deinit`, `save_open`, `save_close`: the region and arena its parts borrow, and the one world open | 16.1 |
| | `world` | Seed + changed-section deltas, with versioned headers, replaced through a crash-safe rename | 16.1 |
| | `world/record` | One section's record, to and from a delta file. Split off at the Phase 18 gate, at the standard's 1500 lines | 18 gate |
| | `names` | A table's names as a list on disk, and what a saved list's numbers mean in this build, so that a new block or item ends no world | Plan 003 phase 1 |
| | `state` | Player, inventory, entities, mobs, time, the calendar, chests and furnaces, in a file that goes with one save of the blocks | 16.2, plan 004 phase 1 |
| | `state/translate` | A state file from other tables rewritten in memory to this build's, by name, as it is opened | Plan 003 phase 1 |
| | `autosave` | Every 30 s of world time: the sim copies out, a worker writes; the only writer while a game runs | 16.2 |
| | `climate` | Plan 004 phase 7: the weekly state in its own file, `<world>/climate/week.<W>`: a checksummed header and every kept tile in order, written whole or not at all, read back only if every check holds (a forged number with a good hash is refused), the newest two kept. Allocates nothing | Plan 004 phase 7 |

---

## 7. `cuda/`: GPU code

Layout: `cuda/src/<dir>/index.cu`, `cuda/include/<dir>/index.hu`, `README.md`. **All CUDA headers are `.hu`.**

| Directory | Job | Stage |
|---|---|---|
| `device_memory` | **Only `cudaMalloc`/`cudaFree` user**; device allocator | 08.1 |
| `world_buffers` | Section pool, column table on device | 08.2 |
| `traverse` | Coarse depth pass; primary rays through the bitmask tree, which `world_buffers` holds; the shape table of boxes | 08.3, 08.4, 09.1 |
| `shade` | Textures, holes in them, light, shadows, AO, water | 08.4, 09.x |
| `post` | Sky, fog, god rays' colour, TAA, tone map | 08.4, 09.x |
| `frame` | One frame's kernels in order, GPU timers | 08.4, 17.1 |
| `entity_rays` | Mob and item boxes against a frame's rays; the outline of what is aimed at, on the finished picture | 09.5 |
| `lod` | Far-field traversal: rays from the finest coarse grid out, past what full detail holds, with a sun ray for each surface | 09.6 |

### The C ↔ CUDA boundary

```
run/src/render/frame/index.c
    │  #include "gpu_bridge/frame/index.h"        C only
    ▼
run/include/gpu_bridge/frame/index.h              extern "C", shared structs, static_asserts
    ▲
    │  #include                                   C++ under nvcc
cuda/include/frame/index.hu                       adds CUDA-only parts
    ▲
cuda/src/frame/index.cu                           implements the functions
```

C never includes a `.hu`.

---

## 8. `asm/`: CPU SIMD

Layout: `asm/src/<dir>/index.asm`, `asm/include/<dir>/index.inc`, `README.md`. Compiled by NASM to `.o`.

| Directory | Job | Stage |
|---|---|---|
| `cpuid` | The CPUID and XGETBV instructions, since inline asm is banned, and the CPU guard that runs before anything built with `-march`; not SIMD | 03.1 |
| `conventions` | `asm/include/` only: register rules, `YMM_BYTES`, and `REQUIRE_CPU`, which fails the build if `cpu.simd` turns off what a kernel uses | 04.2 |
| `copy` | Streaming copy to scanout memory | 04.2 |
| `blend` | HUD alpha blending, called from `run/src/ui/compose/` | 04.2 |
| `bitmask` | Popcount and mask kernels | 04.2 |
| `noise` | 8-wide gradient noise | 04.3 |
| `vec_batch` | Batch vector transforms | 04.3 |
| `embed_assets` | `incbin` of the generated texture and sound blobs | 06.4, 003 phase 5 |

C calls these through prototypes in `run/include/simd/<name>/index.h`.

---

## 9. `textures/`

Layout: `textures/<GROUP>/<NAME>/`.

```
textures/
├── ATTRIBUTION.md                   every source pack, author, licence, commit; generated, but for its last section
├── README.md                        how this directory works, and how to re-import
├── import.yaml                      the pinned pack, and what becomes of each of its files
├── vector.yaml                      what pixels cannot say (light, blending), and every texture the game needs
├── blocks/dirt/
│   ├── index.yaml                   source pack or procedural recipe
│   ├── index.mctex                  translated data
│   └── README.md
├── items/
├── mobs/zombie/
└── ui/
```

Groups: `blocks`, `items`, `mobs`, `ui`, each with its own `README.md`. `index.mctex` is defined in `compile/src/texture_pack/mctex/`.

How a texture gets into the game:

```
open pack, pinned commit, cloned to /tmp/mc_c_assets/      (never committed as-is)
        │  compile/src/texture_pack  (pin and licence check), run by hand, never by the build
        ▼
textures/<GROUP>/<NAME>/                                    committed: builds need no network
        │  compile/src/texture_vector  (palette, mean color, height, normal)
        │  compile/src/texture_blob  (mip chains, and all of it in one file)
        ▼
build/<preset>/generated/textures/index.mctb                blob
        │  asm/src/embed_assets  (incbin)
        ▼
linked into minecraft_c, read through run/src/texture/
```

**No Mojang textures and no decompiled code.** Open-licensed packs and our own recipes only.

### `sounds/`

`sounds/<family>/<hit|break|place>_<n>/` holds `index.yaml`, `README.md`, and `index.mcsnd` if it's a recording. All 90 are recordings today. A recipe, the fallback, is made into samples while building. Either way a build needs no network and no `ffmpeg`. `compile/src/sound_pack` → `build/<preset>/generated/sounds/index.mcsb` → `asm/src/embed_assets` → linked into `minecraft_c`. Recordings must be `CC0-1.0` and credited in `sounds/ATTRIBUTION.md`. **No Minecraft sounds.**

---

## 10. `tests/`, `bench/`, `tools/`, `cmake/`

**`tests/`** mirrors source: `tests/run/world/section/index.c` tests `run/src/world/section/`.

| Path | Purpose |
|---|---|
| `tests/harness/` | Minimal test framework (Stage 02.4) |
| `tests/compile/<tool>/` | Tool tests |
| `tests/compile/standards_lint/` | ctest `standards_lint_fixtures`: one failing fixture per banned construct, under `fixtures/{lint,callgraph,compiler}/` |
| `tests/compile/standards_lint/harness/` | Tests the harness itself |
| `tests/compile/texture_pack/` | ctest `compile_texture_pack`: builds its own small pack at run time, and checks the committed `textures/` with no pack |
| `tests/compile/texture_vector/` | ctest `compile_texture_vector`: vector data worked by hand, every recipe operation, bad input, and the committed `textures/` |
| `tests/compile/texture_blob/` | ctest `compile_texture_blob`: the mip rules by hand-worked images, the blob's layout, and bad input |
| `tests/compile/sound_pack/` | ctest `compile_sound_pack`: recipes by length, ends, peak and repeatability, the committed `sounds/` and its blob, a recipe standing in for a recording, damage to a blob, each mistake in a tree, and importing a recording |
| `tests/run/audio/` | ctest `run_audio`: the whole of sound, thread and all, to a stand-in or a file, and the program mining with its sound in a file. `fixtures/` is the stand-in, a server over two pipes that keeps every sample in memory. `run_audio_mixer` under `mixer/` checks samples in memory, `run_audio_pulse` under `pulse/` the client, and `run_audio_pulse_wire` under `pulse/wire/` the bytes. None can be heard |
| `tests/run/audio/sound_blob/` | ctest `run_audio_sound_blob`: the blob linked in is the one the build made, and every block that can be mined finds a sound of each kind |
| `tests/compile/game_tables/` | ctest `compile_game_tables`: the real block tables against what `config/blocks/` says, dirt drops dirt, and each bad file refused at its line, blocks and biomes both. `cmake/targets.cmake` compiles the generated tables into it |
| `tests/compile/{inflate,png}/fixtures/` | Committed streams and images from independent encoders, with `generate.py`, which wrote them and is not part of the build |
| `tests/run/<system>/<part>/` | Game tests |
| `tests/run/types/` | ctest `run_types`, covering all of Stage 03.1: `types`, `assert`, `log`, `main/cpu_check`, `asm/src/cpuid`, and the built program's `--report` and failure paths |
| `tests/run/thread/` | ctest `run_thread`, covering `run/src/thread/` and `run/src/thread/ring/` together |
| `tests/run/math/` | ctest `run_math_owner` for `run/src/math/` itself, named in its `test.cmake`, and `run_math_<part>` for each part |
| `tests/run/display/mode/` | ctest `run_display_mode`, covering `run/src/display/` with `device`, `mode` and `flip`, on `fixtures/fake_card.h`, a fake kernel behind `display_ops_t` |
| `tests/run/display/vt_guard/` | ctest `run_display_vt_guard`, in forked children that really die, over a fake kernel reporting down a pipe and keys fed down another. Shares `../mode/fixtures/fake_card.h` |
| `tests/run/display/x11/` | ctest `run_display_x11`, covering `run/src/display/x11/wire/`: the protocol as bytes, with no server |
| `tests/run/display/headless/` | ctest `run_display_headless`, covering `run/src/display/headless/` with `png`, `script` and `demo`, and the built program from script to file. Links `compile/src/png` to decode what it writes |
| `tests/run/input/` | ctest `run_input`, covering `run/src/input/` with `ops`, `discovery` and `events`, on replay data, this machine's capability bitmaps copied from `/proc/bus/input/devices` and the fake kernel in `fixtures/`, never on a real device |
| `tests/run/world/section/` | ctest `run_world_section`: random edits against a brute-force array, each change of form, running out of room, and the bytes each form holds |
| `tests/run/world/column/` | ctest `run_world_column`: coordinates and `z` limits, random edits against a brute-force array of the whole gameplay range, and `run/src/world/` itself |
| `tests/run/world/map/` | ctest `run_world_map`: the toroidal table, streaming by distance, requests out of date, a random walk against a brute-force model, and `world_open` |
| `tests/run/world/edit/` | ctest `run_world_edit`: random edits against a brute-force array and brute-force flags, the log, each consumer's flags, and snapshots, with a real reader thread |
| `tests/run/world/light/` | ctest `run_world_light`: incremental light against a separate full recompute on random edits, columns coming and going, running out of room or memory, exact values worked out by hand, and what an opaque block's faces see |
| `tests/run/world/tick/` | ctest `run_world_sim` (named in its `test.cmake`): `run/src/world/tick/`: flow worked out by hand and against a separate flood fill, lava and water, falling blocks, leaf decay, crops, grass, columns coming and going, running out of room, the same world from the same seed, real terrain left alone, each rule's number from both sides, and light against a rebuild after real terrain has been simulated |
| `tests/run/calendar/` | ctest `run_calendar`: `run/src/calendar/`, with no card: the worked examples, the day starting at 06:00, years rolling over with weeks carrying on, every week 7 days, every season boundary north and south, the date line, the census plan, the continuous year position, and the same dates from 16 threads |
| `tests/run/climate/` | ctest `run_climate`: `run/src/climate/`, with no card: latitude at the equator, tropic, polar circle and poles, the worked examples, the fold past a pole, the period far out, the even rise, and the same bits from 16 threads |
| `tests/run/climate/sun/` | ctest `run_climate_sun`: `run/src/climate/sun/`, with no card: declination at the equinoxes and solstices, day length at 60 degrees, 12-hour equinox days, polar day and night, the south, the unit vector, every worked example of the phase 02 file, today's fixed sun as a special case, the sun from the clock, and the same bits from 16 threads |
| `tests/run/climate/sea/` | ctest `run_climate_sea`: `run/src/climate/sea/` on synthetic grids: the worked example's components, deep water making a body sea, the sea and lake thresholds, a one-cell channel against a corner, and the distance transform against a brute-force search on synthetic coasts, with guard words |
| `tests/run/climate/cache/` | ctest `run_climate_cache`: `run/src/climate/cache/`: slots modulo the side, the keep square, replacement, a reader stopping an eviction, and 8 readers against a replacing thread 100,000 times on real threads; by hand under `tsan` too |
| `tests/run/climate/mask/` | ctest `run_climate_mask`: `run/src/climate/mask/`: pages at the cell centres, a real page equal to the height estimate bit for bit, a window as its 17 by 17 pages, and a page someone else holds copied privately |
| `tests/run/climate/cell/` | ctest `run_climate_cell`: the cells through `run/src/climate/` on real workers: the worked examples, a straight coast, the flags, `z_ground`, exact latitude, the blend, the warp's reach, a tile the same whatever was built first, 1 and 16 workers the same bytes held to a pinned digest, negative coordinates, reads false until built, and the column gate with the scheduler |
| `tests/run/climate/layer/` | ctest `run_climate_layer`: `run/src/climate/layer/`: each layer's colours, the latitude lines on their rows, grey where unbuilt, phase 4's current arrows by kind and flow, pressure lines on multiples of 4 hPa and wind arrows downwind, phase 5's ten layers' colours, the day moving only the day layers, grey where unbuilt, and the layers' hashes on a real seed (printed until Ahmed has seen the pictures), and from phase 6 the now layers' colours on a hand-made day list, systems over unbuilt tiles as outlines only, and the now layers on a real seed held to themselves |
| `tests/run/climate/normals/` | ctest `run_climate_normals`: `run/src/climate/normals/`: every knot the master plan's, the terrain's spline between knots, 90° past 90°, and the same bytes from an arena |
| `tests/run/climate/harmonic/` | ctest `run_climate_harmonic`: `run/src/climate/harmonic/`: the fit days, a half-year step's worked example, a pure wave back exactly, a constant, and the heat-low worked example; from phase 5 two pure waves back exactly, the worked example's rain within 1.8 mm, and the two-wave fit's first wave the one-wave fit's |
| `tests/run/climate/air/` | ctest `run_climate_air`: `run/src/climate/air/` and `climate_air_at`: the mirror sign, the belt wind's worked examples, trades, westerlies and polar easterlies, the calms, no jumps, the 70° turn, spin round heat lows on real, southern and mirrored islands, steering never faster, altitude, currents on synthetic continents at 30° S, 50° N (and mirrored), 8° N and 70° N, the fade offshore, land's current, the sea temperature, heat lows, the 20° N coast's monsoon question, a real tile's stored pressure, and the four-cell blend |
| `tests/run/climate/fixtures/` | Not a test: `ground.h`, synthetic grounds (a plane, a ramp, all land, all sea, a straight coast, a sea in one corner, lakes, an island anywhere; from phase 4 a square continent, an east-west coast, open sea and a ridge; from phase 5 Ahmed's South Africa sketch, a peaked ridge, level land at any height and a high east-west coast) shared by the climate's tests |
| `tests/run/climate/heat/` | ctest `run_climate_heat`: `run/src/climate/heat/` and `climate_normals_at`, with no card: 6.6's three worked examples, 12 °C colder per 1000 blocks, the daily range and the snow share, the south half a year from the north, the read equal to the formula at cell centres and the blend between them, and `t_mean` stored at sea level |
| `tests/run/climate/rain/` | ctest `run_climate_rain`: `run/src/climate/rain/`, with no card: the belt spline, the upwind walk's step count in the window and past it, moisture and the orographic factor, Ahmed's South Africa sketch with the master plan's assumptions and with the real belt, the real year printed, a ridge's rain shadow, and rain never negative on a real seed |
| `tests/run/climate/season/` | ctest `run_climate_season`: `run/src/climate/season/`, with no card: hurricane seasons at 15° N with and without a warm current and their packing, the 5° to 25° band, a southern season wrapping, the hurricane coast within 3 km, the monsoon flag and monsoon today, the snow balance height's worked examples, the 14 class cases, and the 45° N peak (Tundra) and the South Africa plateau (Desert) through tiles |
| `tests/run/climate/systems/` | ctest `run_climate_systems`: `run/src/climate/systems/` and its parts, with no card: Poisson counts by the inverse CDF on 10^6 real hashes, the forced season's hurricanes, the track-time sea, hurricanes only in season and never within 5°, the turn poleward and east, weakening over land and cool sea, the spin round lows and highs north, south and mirrored, the hurricane's worked wind and pressure, fronts toward the equator, names, a track the same whichever day is asked first, 1 and 16 workers the same lists held to a pinned digest, negative coordinates against a list built by hand, no track past a pole, lists never overflowing, the mask's point read, cells' rates, start hours and motion, and a fifth of the rate under a high's centre over 1000 days |
| `tests/run/climate/systems/rain/` | ctest `run_climate_systems_rain` (`test.cmake`: 3600 s): master plan 7.4's calibration, 8 seeds at 0° to 65° N, a 4 by 4 km box of 16 test cells each, 10 years (30 where the cells' sampling noise is over 5%), the expected rain within 15% of the normal at every cell. Run on release |
| `tests/run/climate/weather/` | ctest `run_climate_weather` (`test.cmake`: 1800 s): `run/src/climate/weather/` and `week/`, no card: the noises' measured spread, the spring melt by the master plan's numbers, ice by day, 100,000 weeks in the bucket conserving water, the day-kind table, a day as a pure function, spells, heat waves, hourly rain adding up to the day, snow through the year at 45° and 75° on synthetic ground |
| `tests/run/climate/label/` | ctest `run_climate_label`: `run/src/climate/label/`, no card: every day of the year north and south against 4.1's table, the worked examples, wet and dry with 23.5 tropical, polar night and midnight sun, the second labels, and the text into every buffer size with a guard byte |
| `tests/run/climate/warning/` | ctest `run_climate_warning` (`test.cmake`: 900 s): `run/src/climate/warning/`, no card: 51% warns and 49% and 50% do not, two at 30% and unborn arrivals make "hurricane likely", storms only when no hurricane warns, Hurricane Tavela through the outlook's per-system chance, the outlook's own chance at whole days, a real storm low through `climate_warning_at` against the test's own hand, 1 and 16 workers the same warnings on a real seed (random places and along that low's path), and the slowest of 100 warnings under 20 ms |
| `tests/run/climate/constants/` | ctest `run_climate_constants` (plan 004 phase 11): every climate constant of the phase 11 file's table pinned to its value, the derived ones (the pole's y, the period, the polar circle, every part's year, week and hour) against their sources, every knot of every knot table, and the literal sweep of `run/src/calendar/` and `run/src/climate/` for a number with no name, with the sweep itself tried on a sample |
| `tests/run/climate/outlook/` | ctest `run_climate_outlook` (`test.cmake`: 3600 s): `run/src/climate/outlook/`: Hurricane Tavela, background rain and "at least one" by hand, the forecast error, the outlook from a copy of its input with the climate gone, the same outlook from a new process, never reading the future over a year of censuses, and the calibration over 10 years at 32 places on 2 real seeds. Run the calibration on release |
| `tests/run/terrain/seed/` | ctest `run_terrain_seed`: `run/src/terrain/` with `rng` and `noise`, the same bytes from 1 worker and 16, any split of the work, and values pinned from a separate program and the scalar kernel |
| `tests/run/terrain/fields/` | ctest `run_terrain_fields`: `run/src/terrain/fields/`: ranges, the spread its README promises, the same bytes from 1 worker and 16, any split of the work, and each value worked out by hand from its noise |
| `tests/run/terrain/density/` | ctest `run_terrain_density`: `run/src/terrain/density/`: the range limits on every corner of the climate and on real columns, bedrock at `-250`, water's top at `0`, values worked out by hand, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/terrain/hydrology/` | ctest `run_terrain_hydrology`: `run/src/terrain/hydrology/`: each column worked out by hand, rivers that hold water and reach the sea, oceans, beaches, aquifers that cannot flow, the lava level, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/terrain/cave/walk/` | ctest `run_terrain_cave_walk`: `bench/cave_walk/` finds and walks a way into a cave and down to `z = -150` on the default seed, and the built game walks the first by the tool's script and ends where the tool's body did |
| `tests/run/terrain/cave/` | ctest `run_terrain_cave`: `run/src/terrain/cave/`: all three kinds and their shares, thin against wide, a solid floor, no cave reaching the sea, each voxel worked out by hand, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/terrain/biome/` | ctest `run_terrain_biome`: `run/src/terrain/biome/` and `run/src/terrain/surface/`: every biome the plan lists is in the table and turns up in real worlds, the choice and the rules worked out by hand, each biome's own surface in real columns, lined caves, the whole chain of stages, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/terrain/feature/own/` | ctest `run_terrain_feature_own`: a chunk's tops read from its own box are what the walk down finds, and `terrain_generate_box` gives the same bytes as every stage by hand with features finding their own, on 3 seeds, lakes included |
| `tests/run/terrain/feature/` | ctest `run_terrain_feature`: `run/src/terrain/feature/` and `terrain_generate_box`: any split of a box, any order of chunks, the same bytes from 1 worker and 16, trees whole across chunk lines, each tree within its reach, a column's ground against the stages walked from the sky, everything standing where it may, lakes that hold their water, ice and snow |
| `tests/run/terrain/ore/` | ctest `run_terrain_ore`: `run/src/terrain/ore/`: the table against the README, each ore at its depths, deepslate variants only in deepslate, veins worked out by hand, across chunks, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/texture/` | ctest `run_texture`, covering `run/src/texture/` with `blob` and `registry`, and `asm/src/embed_assets/`: every texture against its source files, and damage to the blob, field by field |
| `tests/run/gpu_bridge/` | ctest `run_gpu_bridge`, covering `run/src/gpu_bridge/` with `status`, and `cuda/src/device_memory/`, on the real card: real blocks and a real kernel, the budgets, stale and forged handles, a pebibyte refused, and no device in a process of its own |
| `tests/run/render/encode/` | ctest `run_render_encode`: one voxel's bytes worked out on paper, every shape of section against brute force, and real workers. No card |
| `tests/run/render/frame/` | ctest `run_render_frame`, covering `run/src/game/`, `run/src/render/frame/` and `cuda/src/{shade,post,frame}/`, on the real card: every pixel against an exact trace, the real path against the map, golden PNGs in `fixtures/`, the built program from script to file, and the real threads |
| `tests/run/profiler/` | ctest `run_profiler`: `run/src/profiler/`: self time by hand on a clock it is told, the ring, scopes that do not balance, real threads, the trace's every number read back, a full disk, and `--profile` on the real program and the real threads, with `cuda/src/frame/`'s kernel timers against what the frame says it took |
| `tests/run/physics/` | ctest `run_physics`: `run/src/physics/` with `collision`: sweeps worked out by hand, random sweeps against a march in tiny steps, every block's boxes, what is not loaded, and no heap in a move |
| `tests/run/physics/raycast/` | ctest `run_physics_raycast`: `run/src/physics/raycast/` and `run/src/player/camera/`: casts worked out by hand, random casts against a march in tiny steps, what every block is aimed at by, mouse look, `player_camera_outline_draw`'s pixels, and `run/src/game/`'s target and outline on the real card |
| `tests/cuda/entity_rays/` | ctest `render_feature_entities`: `cuda/src/entity_rays/` and `run/src/render/entities/`: rays against boxes by hand and against a march, every tile's list against every ray, the card's hits against the host's, a box's pixels against its texture, sun, shade, glass and water, the outline's edges, depth and water, the crack by its frames, golden pictures, the cost on real terrain, and a script in the built program that digs and looks |
| `tests/cuda/lod/` | ctest `render_feature_lod`: `run/src/render/lod/`, `cuda/src/lod/` and their share in `cuda/src/shade/`: levels and coarse voxels by hand, every level on the card against its samples, following the eye, rays against a march, the sun, every far pixel's colour, what stands in front and what lets it through, fog, real terrain, golden pictures, the cost at R=512, the real threads, and the built program |
| `tests/run/player/felling/` | ctest `run_player_felling`: `run/src/player/felling/`, with the hands and the leaf sweep: every kind of tree the generator draws comes down whole with a drop for every log, the limit measured against the biggest, what it must leave, its pace, and an axe running out, and `fixtures/fell.script`, a proof by eye in the built program, run by hand |
| `tests/run/player/interaction/wall_torch/` | ctest `run_player_interaction_wall_torch`: a torch on each face aimed at, what breaks it and what it drops, through the real raycast, and a real picture of all four |
| `tests/run/player/interaction/door/` | ctest `run_player_interaction_door`: doors: the three blocks, the hinge, placing both halves each way and refusing, opening once a press, an iron door shut, breaking either half for one door, and an open door saved and opened again. `fixtures/door.script` is the proof in the built program, run by `run_playthrough` |
| `tests/run/player/interaction/bench/` | ctest `run_player_interaction_bench`: a bench two voxels long: placed along x, refused by a block, the player or a column not loaded, and mining either half taking both and dropping exactly one |
| `tests/run/player/interaction/` | ctest `run_player_interaction`: `run/src/player/interaction/`: every block's table against its `config/blocks/` file, mined and rolled; mining times by hand and for every block and tool; placing; through `run/src/player/`; on the real card and threads; and a script in the built program that walks, mines to dirt and places it back |
| `tests/run/entity/item_drop/` | ctest `run_entity_drop`, named in its `test.cmake`: `run/src/entity/` with `item_drop`: falling and sliding by hand, every block's effect on an item, merging at its limits and the grid against every pair, pickup to the tick and into a real hand, the books, the same bytes, no heap, a real body, and a script in the built program stopped at each point from dirt dropped to dirt placed back |
| `tests/run/mob/path/` | ctest `run_mob_path`, named by its path: `run/src/mob/` and `run/src/mob/path/`: every kind of move and its cost by hand, the bounds, against a separate shortest-path search over random ground, the cache against a fresh search through random edits, the same bytes on the workers as without, a tick with every worker busy, real bodies walking what it finds through real physics, and no heap |
| `tests/run/mob/zombie/` | ctest `run_mob_zombie`, named by its path: `run/src/mob/zombie/`: sight, noticing, the chase round real obstacles through real physics, giving up, each edge of a blow, what it drops by the numbers, the same bytes, no heap, and the program itself at midnight |
| `tests/run/mob/spawn/` | ctest `run_mob_spawn`, named by its path: `run/src/mob/spawn/` and `tables/mobs/`: each rule by hand at its edge, on a real world with real light, a night's spawning, the cap, every despawn rule, the same bytes, and no heap |
| `tests/run/mob/animal/` | ctest `run_mob_animal`, named by its path: `run/src/mob/passive/` and the animal rules in `run/src/mob/spawn/`: male and female bodies, daylight herds under their own cap, wandering, keeping to the herd and running when hit (plan 005 phase 4) |
| `tests/run/mob/animal/proof/` | ctest `run_mob_animal_proof`: a real game on the card, with pictures: a herd of each animal, a sheep shorn, a cow milked, a bed of wool slept in (plan 005 phase 4) |
| `tests/run/entity/store/` | ctest `run_entity_store`, named by its path: `run/src/entity/store/`, `grid` and `animation`: every array through a removal, handles against a model over 200000 steps, a body's fall, walk, steps, water, lava and ladders by hand, 64 bodies at random never inside anything, the grid against every body at every box size, the shove by hand, every rig's parts and poses by hand, the same bytes, and no heap |
| `tests/run/item/inventory/` | ctest `run_item_inventory`, named by its path: `run/src/item/` with `stack` and `inventory`: every stack rule against every item, where things go, the books, the hotbar from another thread, no heap, and a script in the built program choosing slots, picking up and placing |
| `tests/run/item/crafting/` | ctest `run_item_crafting`, named by its path: `run/src/item/` with `crafting`, `smelting` and `container`: every recipe everywhere it fits and mirrored, furnaces by the tick, chests, and what `run/src/game/` does with them on the real card and threads |
| `tests/run/item/equipment/` | ctest `run_item_equipment`, named by its path: `run/src/item/equipment/`: every tool and piece of armor against the published numbers, wear to the use that breaks it, a hoe and shears, armor on and off with the books kept, what it stops worked out by hand, 200000 steps at random, no heap, what `run/src/game/` does with all of it on the real card and threads, and the built program |
| `tests/run/item/crafting/copper/` | ctest `run_item_crafting_copper`, named by its path: copper armour's four recipes, as patterns written by hand, at every height on a table, one ingot short, one ingot of iron, and from smelting to a whole set worn |
| `tests/run/item/crafting/stairs/` | ctest `run_item_crafting_stairs`, named by its path: the stairs of all eight woods: each makes its own on a table and no mix of planks makes any, each block and item says what oak's do in its own wood's texture, and the card draws each in the colour of its planks |
| `tests/run/item/crafting/station/` | ctest `run_item_crafting_station`: where each recipe is made: wood on a bench and nothing on a table, the rest the other way, and everything of wood alone on a bench but for what it takes to get one |
| `tests/run/item/equipment/copper/` | ctest `run_item_equipment_copper`, named by its path: copper armour's stats against plan 003 phase 6, each piece in its own slot only, its points, what a set stops, and each piece wearing out on exactly its last use |
| `tests/run/mob/sight/` | ctest `run_mob_sight`: a zombie, a skeleton and a creeper in a real game, through `run/src/game/` and on the card: each is on the picture, in its own skin |
| `tests/run/mob/skeleton/` | ctest `run_mob_skeleton`, named by its path: `run/src/mob/skeleton/` and `run/src/entity/projectile/`: keeping its distance, arrows by hand, its drops, and one in a real game |
| `tests/run/mob/creeper/` | ctest `run_mob_creeper`, named by its path: `run/src/mob/creeper/` and `run/src/entity/explosion/`: its approach and fuse, a blast's rays and sums by hand, its drops |
| `tests/run/mob/combat/` | ctest `run_mob_combat`, named by its path: `run/src/mob/combat/` and `run/src/player/combat/`: each number by hand, the aim and what shields it, the button, the ten ticks after a hit, pushes by hand and on real bodies, falls and lava, each kind dying with its drops, the same bytes, no heap, and a real game on the card with a sword in hand |
| `tests/run/player/survival/` | ctest `run_player_survival`: `run/src/player/survival/`, with what `run/src/player/` and `run/src/game/` do with it: every rule's numbers by hand and by a real body in a real world, death and coming back, the bed, the same bytes, no heap, a real game on the card, and a script in the built program |
| `tests/run/player/movement/` | ctest `run_player_movement`: `run/src/player/` with `movement`: each mode's measured speeds against the numbers in its README, the wind's drift, shelter and none in fluids or climbing (plan 004 phase 8, `fixtures/wind.h`), the same bytes from the same intents, the threads' exchange and interpolation, and `run/src/game/`'s walking on the real card and threads, on fake input, and in the built program |
| `tests/run/game/` | ctest `run_game`: the real loop on the real card and threads through `display_frames_run`, on fake kernels, with the guardian really forked |
| `tests/run/game/census/` | ctest `run_game_census`: plan 004 phase 01's weekly census in a real game on the card: plain ticks, screens on and Continue, the day cycle off, a night slept through, a 100-day jump on the real threads, saves mid-week and with the screen up, the date line in the real game, and `fixtures/week.script` through the session's headless scene | From plan 004 phase 7 (`test.cmake`: 1800 s, given the built program): the worked D8 grid and the flood index by hand, then with no card the census on a synthetic sloped coast (`fixtures/weather.h`): 1 and 16 workers the same store and file, 14 weeks at once the same as one by one, 8 fast weeks then 52 full against 60 full, 20 censuses replayed the same, a hurricane landfall flooding downstream, and the program with `--climate-jump 100` through the menus to the census and Continue (`fixtures/jump.script`)
| `tests/run/game/sun/` | ctest `run_game_sun`: plan 004 phase 02 on the real game and card: the picture's sun at the eye's latitude, a lit midnight sky past the polar circle, a bed at 60 degrees north on a midsummer midnight, mobs at noon at 80 degrees north in midwinter, the polar season labels, and `--day-of-year` on the built program |
| `tests/run/game/weather/` | ctest `run_game_weather`: plan 004 phase 8's sim side: lightning the same on replay and as often as its rate says, a bolt's joints, the forced kinds, and on the card the slots a real game sends: none with no climate, the rain top following edits, forced thunder every 2 s and a hurricane's wind |
| `tests/run/game/cover/` | ctest `run_game_cover`: plan 004 phase 8's snow and ice cover on small worlds with no card: lakes freezing and thawing between the lines, flowing water and the sea, the ice through a save and load, a column streamed in, snow on and off, never on leaves, ice or lava, a roof, the layer replaced by a placed block, no flicker at the edge, and the time over 4225 columns |
| `tests/run/game/spawn/` | ctest `run_game_spawn`: `run/src/game/spawn/`, with no display and no card: what counts as land, the search's walk on made-up land and on none, and real seeds starting on real land |
| `tests/run/ui/screens/` | ctest `run_ui_screens`, named by its path: `run/src/ui/` with its four `screen_*`, and what `run/src/game/` does with them: every slot's place at three scales, replays of real scripts in `fixtures/` held byte for byte to golden PNGs beside them, 20000 random clicks on each screen, closing with no room, the ring, no heap, every way a screen comes up and goes on the real card, a click over the real threads, and the built program |
| `tests/run/ui/census/` | ctest `run_ui_census`: plan 004 phase 7's census screen with a report, no card: golden pictures at 640 by 480 and 1280 by 720 (every event, 8 systems, the failed line) and of the working screen in `fixtures/`, the layout by hand at scales 1, 2 and 3, Continue and Esc greyed until written, the compass, no heap in 200 frames |
| `tests/run/ui/menu/` | ctest `run_ui_menu`: `run/src/ui/menu/` with `catalog`, and `run/src/game/session/`: what is typed against published vectors and by hand, worlds on disk with what is wrong with them, each screen on made-up input by numbers worked out by hand, every size, no heap, pictures held to hashes set after looking at them, the built program all the way round under `--headless` and its refusals, and the whole round on fake kernels with the real threads and a real guardian |
| `tests/run/save/world/` | ctest `run_save_world`: `run/src/save/` with `world`: each record and a manifest by hand, reloads against a brute-force model over many saves and on real terrain with real light, a bit flipped at every byte of a manifest, children killed at every step of a save and at random moments, readers on threads, a disk that refuses, the game on the real card, and the built program digging, reopened, and refused another seed |
| `tests/run/save/state/` | ctest `run_save_state`: `run/src/save/state/` and `autosave/` on a small world: a file's bytes by hand, every part reloaded byte for byte and going on the same, flipped bits and forged numbers refused, a stuck write the sim does not wait for, a disk that refuses, children killed at every step of a save and at random moments with blocks and state checked to be one save, and what a save takes from the sim |
| `tests/run/save/world/names/` | ctest `run_save_world_names`: a world saved under another list of blocks: one added at either end, one gone, each voxel compared by name, nothing on disk changed by opening, and an old-format or forged list refused |
| `tests/run/save/state/names/` | ctest `run_save_state_names`: a state saved under other tables: every stack, drop, block number and body compared by name, what has gone taken out and the file still believed, recipes worked out again, and an old-format file refused |
| `tests/run/save/state/calendar/` | ctest `run_save_state_calendar`: the calendar section of plan 004 phase 01: it reloads exactly, a version 3 file without one opens with the caller's new calendar and saves as version 4, and calendars that cannot be true are refused |
| `tests/run/save/state/game/` | ctest `run_save_state_game`: frames of the real threaded game on the card with and without saves, that game reloaded byte for byte, a column that will not fit, and the built program killed in play and run on |
| `tests/run/save/climate/` | ctest `run_save_climate`: `run/src/save/climate/` with the census on a synthetic coast: 50 random tiles back byte for byte, padding zero; a bit flipped at every byte, another seed, another version, soil 151 and a NaN with good hashes, each refused and left alone; a world reopened mid-week the same bytes; a crash after the file, and before it, rerunning to the same bytes; a world from before at spin-up, one cell equal to its spin-up and seven days by hand |
| `tests/run/save/power/` | ctest `run_save_power`: every file system call of a world being made and saved recorded, then each thing a power cut could leave of it, by what `fsync` promises, opened and checked as one whole save no older than the last one reported |
| `tests/run/failure/` | ctest `run_failure`: what happens when things go away. A real fault on the real card under a real game, and a hang; the kernel's own `ENOSPC` on a small `tmpfs` in a user namespace, with no card and under a game; keyboards, mice and the display pulled out, and a VT switch while a world opens and before the console is taken, on fake kernels. What the tests share is in `fixtures/rig.h` |
| `tests/run/settings/` | ctest `run_settings`: `run/src/settings/` with `persist`, `run/src/ui/screen_settings/` and `run/src/game/view/`: every setting saved and read back, bad files, each slider and box on made-up input, flips on a fake kernel, workers, and scripts in the built program whose pictures and files prove render distance live, without a restart, and kept |
| `tests/run/settings/session/` | ctest `run_settings_session`: `game_reconfigure`, `game_view_refresh` and the Settings screen through `run/src/game/session/`: render distance both ways and everything else in an open world on the card, with one thread and with its real threads, and a whole session on fake devices |
| `tests/run/ui/font/` | ctest `run_ui_font`, named by its path: `run/src/ui/` with `compose`, `font` and `widget`: the font against its recipe by hand, text, fills and images against a pixel-at-a-time model on `simd_reference_blend_over` with guard pixels round the canvas, each widget on made-up input frames, every key of a US keyboard, a picture left alone by what is drawn over it, no heap in 200 frames, a sheet of all of it held to a hash set after looking at it, and the built program |
| `tests/run/playthrough/` | ctest `run_playthrough`: the whole game in the built program from scripts of keys and mouse, through its menus: a first day on seed 36 with its pickaxe made on a bench, stairs made on a bench on the same seed that is then broken by one half, a bench that gives its grid back, stands through a save and goes by its west half, and a night on seed 2, each saved and opened again |
| `tests/run/ui/hud/` | ctest `run_ui_hud`: `run/src/ui/hud/`, with what `run/src/game/` does with it: where things go by hand, random states against a pixel-at-a-time model with guard pixels round the canvas, golden PNGs in `fixtures/`, no heap in 200 frames, the real game on the card with one thread and with its real threads, and a script in the built program |
| `tests/run/ui/hud/corner/` | ctest `run_ui_hud_corner` (`test.cmake`: 900 s): plan 004 phase 10's top right corner, `run/src/ui/hud/` and `run/src/game/corner/`: the words by hand, where the panels sit at three sizes, empty lines and the settlement's slot, below a notice and clear of the coordinates and F3, the arrow by hand, goldens in `../fixtures/` (`corner_*.png`, and off is `date_640x480.png` and `coordinates_640x480.png`), and on the card a real game with a climate: hurricane season on the date line, the temperature at the body's height against the test's own hand and 6.0 colder 500 up, the settings turning each line off, no heap in 200 frames |
| `tests/run/ui/atlas/` | ctest `run_ui_atlas`: plan 004 phase 9's Atlas: tiles, columns' tops, halving and files by hand and against models; the store's merge, reopen, mips after a save, the lock under a 5 s race, and dropped writes; the layer table, hill shading, marching squares and scales by their worked examples; golden pictures of all 26 layers at two zooms held to hashes in `fixtures/goldens.txt`; outlines only over ungenerated land; the Atlas's rows as `climate_layer_draw`'s bytes; any split into bands and workers the same bytes; pans; the redraw policy, the keys and the readout; and no heap |
| `tests/run/ui/atlas/game/` | ctest `run_ui_atlas_game`: the Atlas in real games on the card: in a played world through the session the game ticks on, unpaused, and the body stands for the 100 ticks it is up, and on the real threads columns stream in behind it; Esc closes it and not the world (the pause screen on a second Esc, and a test world's run going on); a played world's tiles there after a reopen; and the built program opening it with `m` and closing it with `escape` |
| `tests/run/render/upload/` | ctest `run_render_upload`, covering `run/src/render/` with `upload`, and `cuda/src/world_buffers/`, on the real card: a real kernel's answers against the world voxel for voxel, the slab pool against a model, the budget, and a real sim thread |
| `tests/cuda/<area>/` | Headless GPU tests; golden PNGs |
| `tests/cuda/shapes/` | ctest `render_feature_shapes`, named in its `test.cmake`: each shape by hand, a block of each shape and its holes against an exact trace from all sides, the settings, the coarse pass, how many holes a pixel goes through, golden PNGs in `fixtures/`, and the cost, on the real card |
| `tests/cuda/sky/` | ctest `render_feature_sky`, named in its `test.cmake`: the day and the light by hand, the sky, fog, shadows through holes and stored light against an exact trace, a tower's shadow by hand, the settings, the clock through the real threads, the fog from high over the ground, golden PNGs in `fixtures/`, and the cost, on the real card. `fixtures/{rig,program,from_above}.h` hold the rig, the built program's test and the fog from above, apart from `index.c` to keep it under 1500 lines |
| `tests/cuda/under_water/` | ctest `render_feature_under_water`, named in its `test.cmake`: what eyes under water see, by hand, on a sea of its own, and from a real body in the built program with golden PNGs and a script in `fixtures/`, and the cost, on the real card |
| `tests/cuda/water/` | ctest `render_feature_water`, named in its `test.cmake`: reflection, depth tint and fog behind a surface by hand, a lake, glass and ice against an exact trace from above and from under the water, deeper being bluer, a tower in the water by hand, the settings, golden PNGs in `fixtures/`, and the cost, on the real card |
| `tests/cuda/weather/` | ctest `render_feature_weather`, named in its `test.cmake`: plan 004 phase 8's weather in the renderer: a clear sky to the bit, drops, the wind's slant, the roof, walls and the bolt by hand on the host with the card's own lines, visibility 124, 117 and 496, golden PNGs of each weather out of a doorway in `fixtures/`, the roof and a flash in the real renderer, and `--weather` in the built program |
| `tests/cuda/post/` | ctest `render_feature_post`, named in its `test.cmake`: the jitter, the way back to the last frame and what AO and god rays add up to by hand, AO's rate read back from the card and against the exact integral, TAA against a picture at four times the size, ghosts and a moving eye, god rays against an exact walk, the settings, golden PNGs in `fixtures/`, and the cost, on the real card |
| `tests/cuda/traverse/` | ctest `cuda_traverse`, with an encoder of its own for `world_buffers`' format: known hits, random rays and whole frames against an exact CPU reference in `float64_t`, the coarse pass never overestimating, and Stage 01.3's worlds. Timing on request |
| `tests/asm/` | NASM kernels against scalar C references: `tests/asm/<kernel>/` is ctest `asm_<kernel>` |
| `tests/asm/simd/` | ctest `asm_simd`: the `simd` Owner, its region and its self-check |
| `tests/asm/conventions/` | ctest `asm_conventions`: `asm/include/conventions/` and the offsets from `struct_offsets` |
| `tests/asm/support/` | Not a test: guard-page buffers and a repeatable random source, shared by those above. A header and a README |
| `tests/asm/abi/` | ctest `asm_abi`: every kernel keeps the callee-saved registers and leaves no dirty YMM state, by machine state, not by output |
| `tests/fuzz/` | Coverage-guided fuzzers of our own, sanitize preset only: `support` (the fuzzer), `yaml`, `png`, `save`, each also a 20 s ctest `fuzz_<name>`, and `planted`, ctest `fuzz_planted`, that it finds what is there (Stage 18.1) |
| `tests/soak/` | The program `soak`, of every preset, run by hand for hours: the threaded game and the built program with `--headless`, again and again on the same worlds (Stage 18.1) |

Tests need no server and no display, and run under `timeout`.

**`bench/`**: `simd`, `terrain`, `mobs`, `world`, `world_map`, `biome_map`, `cave_walk`, `climate`, `atlas`, each `index.c` with a `README.md` and a results history. `terrain/tour` is the phase gate's biome tour: a picture of every biome, with no display and no card. `world_map` shows a seed's whole world: a 16 km map, slices and a table of what it measures, with no display and no card. `biome_map` lays the biomes over the same 16 km and says who holds each height, with no display and no card. `climate` times a climate tile cold and warm on one worker (target under 100 ms) and the spawn precompute on 16, with `--pictures` draws the climate layers of a seed's 16 km to PNGs (phase 03's four, phase 04's currents, pressure and wind on four days, and phase 05's ten, the day ones for each day of `--days`, one a month by default), and with `--synthetic` phase 04's two synthetic continents and phase 05's South Africa sketch, with no display and no card; it also times each part of a tile on one thread; from plan 004 phase 6, `--systems` times the list of weather systems near a point (target under 2 ms, warm) and `--season` writes the gate's frames of a hurricane season with `systems.txt`. `climate/census` (plan 004 phase 7) times the census of 100 km² of generated land on all workers, the median of 5 (target under 250 ms), with each stage and what the jobs spent, and adds to its `history.tsv`. `climate/survey` (plan 004 phase 11, `bench_climate_survey`) surveys a pole-to-pole strip 16 km wide on plan 003's 8 seeds through the climate functions alone: climate class shares and where the biome disagrees, hurricanes a season per warm coast square and coast with landfalls, thunderstorm days a year by class, the outlook's calibration in tenths, long-run rain against normals and the climate's memory per km², as Markdown tables that are the same bytes on 1 worker or 16, adding a line to its `history.tsv` with `--record`. `world` holds the standard worlds and camera paths, and adds to `bench/world/history.tsv`, a line per row per recorded run. `atlas` (plan 004 phase 9) times opening the Atlas over 100 km² of generated land (target under 100 ms), each layer's picture at 1920 by 1080 on 16 workers (under 60 ms), main's frame with it up (under 2 ms), a column's write and its top; and `climate --pictures <directory> <seed> --layer <key|all> [--zoom <n>] [--flat]` draws every Atlas layer through the Atlas's own code, checking the climate fields against `climate_layer_draw` with `--flat`. `world_map/atlas` (`bench_world_map_atlas <directory> [seed] [--layer <key|all>] [--zoom <0 to 5>] [--flat]`) draws them over a seed's real column tops, made with the game's own `terrain_generate_box` and `ui_atlas_column_top` into an Atlas store, the same check with `--flat`.

**`tools/test-or-build.sh`**: `flock` around heavy commands, so parallel agents never run builds at the same time:

```bash
tools/test-or-build.sh cmake --build --preset debug
```

**`cmake/`**:

| File | Job |
|---|---|
| `cmake/machine.cmake` | Builds the machine tool at configure time; loads generated values |
| `cmake/flags.cmake` | Flags per language and preset |
| `cmake/generated.cmake` | Custom commands for tables and textures |
| `cmake/targets.cmake` | `mc_asm`, `mc_cuda`, `mc_run`, `minecraft_c`, tools, tests, benchmarks; all found by path. Also runs `struct_offsets`, which `mc_asm` waits for |
| `cmake/hardening.cmake` | The targets of `tests/fuzz/` and `tests/soak/`, which discovery leaves out |
| `cmake/check_link.cmake` | Fails the build if `minecraft_c` needs a library the standard does not allow, or has an executable stack |
| `cmake/standards_lint.cmake` | Runs the lint before anything else compiles, runs the recursion check on every preset, and registers ctests `standards_lint` and `clang_format` |
| `cmake/check_format.cmake` | Run by ctest `clang_format`: fails if any C or CUDA file does not match `.clang-format` |
| `cmake/sanitize/` | ASan options CUDA needs, linked only in the `sanitize` preset |

`cmake/machine.cmake` and `cmake/generated.cmake` are included if present, so the stages that write them do not edit
the root `CMakeLists.txt`.

Presets: `debug`, `sanitize`, `tsan`, `release`, `profile`. Output goes to `build/<preset>/`, with the program at
`build/<preset>/bin/minecraft_c`, tools at `bin/tools/<name>`, tests at `bin/tests/<name>`, benchmarks at
`bin/bench_<name>`, and generated files under `build/<preset>/generated/`.

**Discovery.** There is no source list to edit. A glob with `CONFIGURE_DEPENDS` finds new directories at build time:

| Path | Becomes |
|---|---|
| `run/src/**/index.c` | Object library `mc_run`. Tests link all of it except `run/src/main/` |
| `asm/src/**/index.asm` | Object library `mc_asm` |
| `cuda/src/**/index.cu` | Object library `mc_cuda` |
| `compile/src/**/index.c` with `int32_t main(` at line start | Tool target `mc_tool_<path>` |
| `compile/src/**/index.c` without one | Object library `mc_compile_lib`, linked into every tool and `tests/compile/` test |
| `tests/harness/index.c` | Linked into every test. Include root `tests/`: `#include "harness/index.h"` |
| `bench/**/index.c` | Target `mc_bench_<name>` and program `bin/bench_<name>`, the path with `/` as `_`, linked like a game test. Not a ctest |
| `tests/**/index.c` | Target `mc_test_<name>` and ctest `<name>`, the path with `/` as `_`: `tests/run/world/section/` is `run_world_section` |

Under `tests/`, nothing in `harness/`, `fuzz/`, `soak/` or any `fixtures/` directory is registered as a test by discovery. `cmake/hardening.cmake` registers the fuzzers itself.

A `test.cmake` next to a test's `index.c` may set `MC_TEST_NAME`, `MC_TEST_ARGS`, `MC_TEST_LIBS` and `MC_TEST_TIMEOUT`,
and may call `add_test` for more runs. A step that has to run before anything compiles, such as the lint, is added with
`add_dependencies(mc_prebuild <target>)`.

**`.claude/rules/test-or-build-mutex.md`** documents the mutex (Stage 02.1), since `plans/README.md` refers to it.

---

## 11. Dependency direction

Lower layers never include upper layers. `standards_lint` checks this from include paths.

```
main
 └─ game
     ├─ ui, save, settings
     ├─ mob, player
     ├─ entity, item, physics
     ├─ render ─► gpu_bridge ─► (cuda/)
     ├─ climate (reads terrain only through its height estimate)
     ├─ terrain, world
     ├─ display, input, texture, audio
     └─ job, thread, file, time, math, calendar, simd, profiler ─► (asm/)
         └─ arena, heap, log, assert, types
```

- `main` calls `job` directly too: `job_system_t` is a field of `app_t`, and `game` borrows it.
- Depth 2 parts may include their own Owner's header, and lower systems.
- Sibling parts may include one another only if their READMEs say so.
- `compile/` may not include `run/`, except for `run/include/types/`. Its tools share the type definitions.
- Nothing in `run/` includes from `compile/`, aside from generated output in `build/`.

---

## 12. Threads and where they live

| Thread | Started by | Mainly runs |
|---|---|---|
| `main` | The OS | `main`, `input`, `display`, `ui`, last step of `render/frame` |
| `sim` | `game` | `game`, `player`, `mob`, `entity`, `world/tick`, `world/light` |
| `gpu` | `game` | `render/frame`, `render/upload`, `gpu_bridge`, `cuda/` |
| `worker` × 16 | `job` | `terrain`, `render/encode`, `save`, mob AI batches |
| `audio` | `audio` | `audio`, on the CPU `threads.audio` in `config/machine.yaml` gives it, after the workers |

---

## 13. How to add something

### A new module

1. Choose the depth by what it must do with memory: section 3.
2. Create `run/src/<system>/<part>/index.c`, `run/include/<system>/<part>/index.h`, and `README.md`.
3. Have its parent call `<part>_init(self, arena)` from the parent's `_init`, and its `_deinit` in reverse order.
4. Add `tests/run/<system>/<part>/index.c`. CMake finds new directories by itself.
5. Add a line to this file.

### A new block

1. Create `config/blocks/<name>/index.yaml`.
2. Create `textures/blocks/<name>/`, from a pack or a recipe.
3. Build. Tables regenerate, and the block exists, with drops. No C changes unless it has new behaviour.

### A new mob

1. Create `config/mobs/<name>/index.yaml` and `textures/mobs/<name>/`.
2. Create `run/src/mob/<name>/` for its behaviour, and its header.
3. Add `tests/run/mob/<name>/`.

### A new NASM kernel

1. Create `asm/src/<name>/index.asm` and `asm/include/<name>/index.inc`.
2. Add a prototype at `run/include/simd/<name>/index.h`.
3. Add a test against a scalar C reference, and a benchmark in `bench/simd/` that shows a gain.

### A new CUDA kernel

1. Add the C API to `run/include/gpu_bridge/<area>/index.h`.
2. Create `cuda/src/<dir>/index.cu` and `cuda/include/<dir>/index.hu`.
3. Add a headless test in `tests/cuda/<area>/`.

### A file is nearing 1500 lines

Split it into sub-directories. Remember that going deeper changes the tier: code that moves to depth 3 may no longer allocate.

---

## 14. Where is...?

| Looking for | Go to |
|---|---|
| The entry point | `run/src/main/index.c` |
| Thread counts, SIMD flags, GPU settings | `config/machine.yaml` |
| What a block drops | `config/blocks/<block>/index.yaml` |
| How mining works | `run/src/player/interaction/` |
| How breaking a log brings down the whole tree | `run/src/player/felling/` |
| Height limit and sea level | `config/machine.yaml`, `world:` |
| Render distance setting | `run/src/ui/screen_settings/`, `run/src/settings/` |
| The date and the census | `run/src/calendar/`, the world clock and the census in `run/src/game/README.md`, the screen in `run/src/ui/screen_census/` |
| Latitude and where the sun is | `run/src/climate/` (latitude from y), `run/src/climate/sun/` (declination, day length, direction); the picture's sun in `run/src/game/README.md` and `render_frame_sun_set` in `run/src/render/frame/` |
| Climate cells: sea distance, continentality, sea and lakes | `run/src/climate/` (the tile cache, jobs and reads) with `sea/`, `cache/`, `mask/`, `cell/` and `layer/`; the ground it reads is `terrain_height_estimate` in `run/src/terrain/`; the column gate and the spawn precompute in `run/src/game/` |
| Ocean currents, pressure belts, heat lows and winds | `run/src/climate/air/` (one tile's, from its window), `run/src/climate/normals/` (the knot tables), `run/src/climate/harmonic/` (the yearly wave), read with `climate_air_at` in `run/src/climate/`, drawn by `run/src/climate/layer/` |
| Temperature, rain, hurricane seasons, monsoons, snow line and climate classes | `run/src/climate/heat/`, `run/src/climate/rain/` and `run/src/climate/season/` (one tile's, from its window), read with `climate_normals_at` in `run/src/climate/`, drawn by `run/src/climate/layer/` |
| Weather systems: hurricanes, lows and fronts, highs, thunderstorm cells, their pressure, wind and rain, and names | `run/src/climate/systems/` (spawning, tracks, the day list), `run/src/climate/systems/field/`, `run/src/climate/systems/name/`, drawn by the now layers of `run/src/climate/layer/`; the season's frames by `bench/climate --season` |
| Every number the climate uses, where it lives, and what was measured with them | The constant table and **Results** of `plans/004-ahmed-bohemoth-climate-system-phase-11-tuning-and-cost.md`; pinned by `tests/run/climate/constants/`; measured by `bench/climate/survey` |
| Daily weather, the weekly state (snow, soil water, runoff, ice, floods), the census and the outlook | `run/src/climate/weather/` (with `week/` and `flow/`), `run/src/climate/outlook/`, the census in `run/src/game/census/`, its file in `run/src/save/climate/`, its screen in `run/src/ui/screen_census/`; `--climate-jump` in `run/src/main/`; the cost in `bench/climate/census` |
| Weather the player sees and feels: rain, snow, lightning, cloud and fog, wind on the body, snow and ice on the ground | The sim's side in `run/src/game/weather/`, the drops, bolt and weather fog in `run/include/gpu_bridge/post/` and `cuda/src/post/`, the sky, light and fog in `run/src/render/frame/`, the wind in `run/src/player/movement/`, cover in `run/src/game/cover/`, the thin snow block in `config/blocks/snow_layer/`; `--weather` and `--no-weather` in `run/src/main/`; the cost in `bench/world` (the `-rain`, `-snow`, `-thunder` and `-fog` rows) |
| The Atlas on `M`: the map, its layers, its tiles on disk | `run/src/ui/atlas/` (with `tile/`, `draw/`, `layer/`), the game's side in `run/src/game/atlas/`, its memory in `run/src/ui/index.c`; the cost in `bench/atlas`, every layer to PNG by `bench/climate --pictures ... --layer all` and over real land by `bench/world_map/atlas` |
| What is on disk for a world, and what a crash does to it | `run/src/save/world/README.md` |
| What a save holds besides blocks, and how the two stay one save | `run/src/save/state/README.md` |
| When saves happen, and what they cost a frame | `run/src/save/autosave/README.md` |
| How a pixel is made | `cuda/src/frame/`, then `traverse`, `shade`, `post` |
| How pixels reach the monitor | `run/src/render/frame/`, then `run/src/display/flip/` |
| Why the console came back after a crash | `run/src/display/vt_guard/` |
| Terrain shape | `run/src/terrain/density/` |
| Rivers, oceans, beaches, aquifers, lava | `run/src/terrain/hydrology/` |
| Biomes | `config/biomes/`, `run/src/terrain/biome/` |
| Where ores spawn, and how often | `config/ores/`, `run/src/terrain/ore/` |
| What a biome lays on the ground | `config/biomes/`, `run/src/terrain/surface/` |
| What grows on it, and lakes | `config/biomes/`, `run/src/terrain/feature/` |
| Every stage of terrain for one box | `terrain_generate_box` in `run/src/terrain/` |
| Zombie behaviour | `run/src/mob/zombie/` |
| Who allocates anything | `run/src/heap/`, then depth 1 of the system |
| Type rules, padding, memory law | `plans/CODING_STANDARD.md` |
| Whose textures these are, and what shipping them owes | `textures/ATTRIBUTION.md` |
| How another platform or a CPU-only renderer would come in | Section 15 of this file |
| What's done and what's next | `plans/001-*-phase-*.md` |

---

## 15. Portability seams

The game targets one machine: Linux, x86-64 with AVX2, one NVIDIA card through CUDA, the screen through KMS. These are the places another platform would come in. **None has been built for a second platform, so none has been proven.** What follows was read from the tree and, where it says measured, run, on 2026-11-04 (Stage 18.4).

Two things to know first:

- **There is no runtime choice of platform.** `main` picks KMS or `headless` from the command line, by calling one or the other. The yaml keys that name a platform choose nothing: see 15.1.
- **`display_ops_t` and `input_ops_t` are for tests, not for other systems.** They stand in for Linux's own calls, `ioctl_call` with its request numbers included. Another OS does not fill in these tables. It goes in above them.

### 15.1 `config/machine.yaml`

**Cut at:** one file, read by a fixed path in `cmake/machine.cmake`, generating `MC_*` for C, CUDA, NASM and CMake. It holds no selector for a second machine: a port edits this file or adds one.

| Keys | What they do today |
|---|---|
| `cpu.arch_flag`, `cpu.simd.*` | Real. `-march` and `-mtune`, `main/cpu_check`, `asm/src/cpuid`, `REQUIRE_CPU` |
| `threads.*`, `memory.*` | Real. Pinning, workers, the heap's budget. But see `threads.gpu` below |
| `display.device`, `connector`, `width`, `height`, `refresh_hz` | Real. `display_config_from_machine` |
| `gpu.architecture`, `gpu.budget.*` | Real. `CMAKE_CUDA_ARCHITECTURES`, and `gpu_bridge_limits_default` |
| `gpu.enabled` | Guards one call, `gpu_bridge_init` in `run/src/main/`. **It does not give a game without a card:** see 15.5 |
| `threads.gpu` | `0` is refused while `gpu.enabled` is true, as `game` starts its gpu thread whenever a world opens. With `gpu.enabled: false` it passes, and no world opens anyway (15.5) |
| `platform.posix` | Printed by `--report`. Nothing branches on it |
| `platform.os`, `gpu.api`, `display.backend` | No consumer in `run/`, `cuda/`, `asm/` or `cmake/`. One word allowed each, and any other is refused at configure (measured): `platform.os must be one of: linux` |

**To add a platform:** add the word to its row in `machine_keys` in `compile/src/machine_config/`, which also gives it a define such as `MC_DISPLAY_BACKEND_KMS`, then make something read that define. Today nothing does.

### 15.2 `run/src/display`

**Cut at:** the picture. Whoever draws sees only a `display_buffer_t` (`run/include/display/flip/`): XRGB8888, rows `pitch` bytes apart, written and never read. The same type goes to the drawer under both backends.

| Backend | Starts with | The drawer plugs in through |
|---|---|---|
| KMS | `display_init` | `display_frame_source_t`, called by `display_frames_run` once per frame |
| `headless` | `display_offscreen_init` | `display_headless_scene_t`, called by `display_headless_run` once per tick |

**To add a backend:** follow `headless`, the one precedent. Add `run/src/display/<backend>/` with a struct of its own beside `display_t` (which is all KMS), give it a loop calling a frame source with a `display_buffer_t` over memory it can show, and add the `main` paths that start it. `display/vt_guard`, the refusals and the guardian process are about taking a Linux console from a display server, and stay with KMS.

**What leaks through:**

- `display_buffer_t` carries KMS's `handle` and `fb_id`. `headless` leaves them zero, and a new backend can too.
- `main` and `game/session` name `display_t` and `display_frames_run` directly. There is no table of backends, so a new one adds a branch in both.
- `asm/src/copy` is written for scanout memory: non-temporal stores and a fence. `headless` shows it works on ordinary memory. Whether it is still the fast choice on a backend's own memory has not been measured.

### 15.3 `run/src/input`

**Cut at:** one frame of input. `input_poll` returns an `input_frame_t`: what is held, pressed, released and repeated, and mouse and wheel counts. `input_kinds_now` says which kinds of device are there. Nothing in `game` or `ui` reads a device. Under `headless` a script stands in, through `game_input_from_script` in `run/src/game/input/`.

**To add a platform:** keep those two functions and `input_frame_t`, and replace what is behind them. `discovery`, `events` and `ops` are evdev and inotify throughout. `input_init` takes a directory of device nodes, so its signature changes too.

**What leaks through:**

- **The key codes are Linux's.** `INPUT_KEY_CODES` is `KEY_CNT` from `<linux/input-event-codes.h>`, and `game`, `game/session`, `ui`, `ui/widget`, `display/headless/script` and their tests say `KEY_W` and `BTN_LEFT`. The cheapest port translates native keys to these codes and carries its own copy of the numbers.
- `mouse_dx` and `mouse_dy` are raw counts with no acceleration. A system that only reports a pointer position has to match that, or mouse look changes.
- The emergency exit in `display/vt_guard` opens keyboards for itself through `display_ops_t`. It does not use this system.

### 15.4 `run/src/gpu_bridge`

**Cut at:** the C declarations in `run/include/gpu_bridge/<area>/index.h`. Nothing in `run/` includes a CUDA header or calls a CUDA function. `run/src/gpu_bridge/` has C for the Owner and `status`; everything else is implemented in `cuda/src/<area>/`. So another GPU API, or the CPU, would supply these same declarations, and `render` and `game` should not have to change. That has not been tried.

| Area | What an implementation must provide |
|---|---|
| `device_memory` | Blocks by pool, with budgets, explicit write, read and zero, and the leak check |
| `world_buffers` | The section pool and column table. The format is in this header, and `render/encode` writes to it on the CPU |
| `traverse`, `entity_rays`, `lod` | Rays: the coarse pass, primary rays, boxes and the outline, the far field |
| `shade`, `post` | Textures, light, shadows, water; sky, fog, TAA, the tone map to XRGB8888 |
| `frame` | One frame's kernels in order, their timings, and the picture copied to host memory |

**What leaks through:**

- **The build needs CUDA whatever the yaml says.** `project()` names `CUDA` as a language, `mc_cuda` is always compiled, and `mc_link_game` always links `cudart_static`. `gpu.architecture` must be `sm_<number>`. This is the first thing a port has to change.
- `frame_host_pin` exists because a pinned copy is faster on this card. Elsewhere it may do nothing, but it must still be there.
- `main` starts the bridge before pinning itself, so that CUDA's threads do not inherit one CPU. Another API may not care, but the order is fixed in `main`.
- Other headers borrow the bridge's types, not its code: `player/camera` from `traverse`, `settings/persist` from `shade`. They stay as they are as long as the headers do.

### 15.5 Future work: a CPU-only render backend

**Status: recorded, not started, not scheduled.** There is no CPU-only way to draw a world today, and no stage in plan 001 builds one.

**What `gpu.enabled: false` does now.** Measured on a debug build with only that key changed:

| Check | Result |
|---|---|
| The build | Still compiles every CUDA kernel and links `cudart_static`. `project()` still requires nvcc |
| `--report` | `MC_GPU_ENABLED=0`, exit 0 |
| `--headless` (the demo scene, no world) | Runs, exit 0 |
| `--headless --test-world` | Exit 5: `gpu: allocating from a closed device_memory_t: invalid argument` |

So the switch skips starting the card and nothing replaces it. A world fails on its first allocation, cleanly and with no leak.

**The shape it would take**, from the seam in 15.4. This is a sketch, not a design anyone has tested:

1. **Make CUDA optional in the build.** `gpu.enabled`, or a new `gpu.api` word, decides whether `CUDA` is a language, whether `mc_cuda` exists and whether `cudart_static` is linked. `tests/cuda/` needs the same decision.
2. **Implement every area in 15.4 in C**, keeping the headers. `run/src/gpu_bridge/<area>/` holds only a README today, and discovery would pick up an `index.c` there. The tiers hold: the Owner `gpu_bridge` reserves from `run/src/heap/`, under `memory.process_budget_mib`, and its parts take arenas. `device_memory` turns into host regions, and `frame_host_pin` does nothing.
3. **Run the kernels on `job`'s workers**, a tile of the picture at a time. The gpu thread can stay as the one that drives a frame and waits. Terrain and encoding use the same workers, so a frame competes with them.
4. **Any SIMD goes in `asm/`, as everywhere.** C is built with `-fno-tree-vectorize`, so a wide ray loop means new NASM kernels, each with a scalar reference and a benchmark that shows a gain.

**What can be built on:** the `.cu` sources, which keep to a C-like subset; the bitmask tree's format, which is plain data in `gpu_bridge/world_buffers`; and the exact `float64_t` trace in `tests/cuda/traverse/`, which is a test's reference on a dense array and not a renderer, but is what a new traversal should be held to.

**How to know it is right:** the tests under `tests/cuda/` and `tests/run/render/frame/` that hold the card to an exact trace and to golden PNGs run against it. Whether they match byte for byte, or the goldens need a tolerance across `float32_t` on two kinds of hardware, is an open question.

**What is not known:** what it costs. No CPU frame has been measured, so no frame rate or usable render distance is promised here. A first measurement belongs in `bench/world/`, on its standard worlds and camera paths.

### 15.6 What has no seam

These call Linux or the CPU directly, with nothing to swap. A port to another OS or CPU changes them in place.

| Where | What it assumes |
|---|---|
| `run/src/heap/` | `mmap`, `madvise` for huge pages |
| `run/src/thread/`, `run/src/job/` | `pthread_setaffinity_np`, `sched.h`, the futex syscall |
| `run/src/file/`, `run/src/time/`, `run/src/log/` | POSIX file calls with `fsync` and rename, `CLOCK_MONOTONIC` |
| `run/src/save/`, `run/src/ui/menu/catalog/` | `dirent.h`, `flock`, and what `fsync` and rename promise on Linux, which `run_save_power` tests against |
| `asm/` | x86-64, NASM, the System V calling convention. `run/src/simd/reference/` has a scalar C version of `copy`, `blend`, `bitmask`, `noise` and `vec_batch`, which a CPU without these instructions could start from. `cpuid` and `embed_assets` have none |
| `cmake/flags.cmake`, `cmake/check_link.cmake` | GCC's flags, `readelf`, and `ld-linux-x86-64.so.2` in the allowed list |
| `static_assert`s on struct sizes | 64-bit pointers: `sizeof(input_ops_t) == 48`, and many like it |
