# Repository structure

A map of the entire codebase, so you can find anything by reasoning about where it must live instead of grepping.

**Status:** written before any code exists. This is the target layout. Directories appear as their stages land, and
Stage 18.3 audits this file against the real tree. Part names below `run/src/<system>/` may be refined by their stage,
and the stage updates this file when that happens.

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
| `tests/` | Tests mirroring source paths. No server, no display | `ctest` |
| `bench/` | Benchmarks and recorded results | On demand |
| `cmake/` | Build logic | Configure time |
| `tools/` | `test-or-build.sh` mutex | Wraps heavy commands |
| `plans/` | Plans, this file, the coding standard | Docs |
| `research/` | Source research and speed notes | Docs |
| `build/` | Generated and compiled output. **Never committed** | Build time |

Root files: `CMakeLists.txt`, `CMakePresets.json`, `.gitignore`, `.clang-format`.

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

Headers mirror source paths exactly. Include roots: `run/include`, `compile/include`, `cuda/include`,
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
| `config/recipes/<recipe>/index.yaml` | Crafting and smelting | `compile/src/game_tables/` |
| `config/biomes/<biome>/index.yaml` | Climate range, surface blocks, features | `compile/src/game_tables/` |
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
| `game_tables` | `config/{blocks,items,recipes,biomes,mobs}` → C tables in `build/<preset>/generated/tables/`, compiled into `mc_run` and included as `"tables/blocks/index.h"`, run by `cmake/generated.cmake`. Parts so far: `blocks` (07.1), `biomes` (10.6), `items` (13.1), `recipes` (13.2); the others arrive with their stages | 07.1 |

---

## 6. `run/`: the game

Layout: `run/src/<system>/<part>/index.c`, `run/include/<system>/<part>/index.h`, `README.md`.

### Foundation

| System | Parts | Job | Stage |
|---|---|---|---|
| `main` | `cpu_check` | Entry point, `app_t`, CPUID check against the build's flags, init and deinit order | 03.1 |
| `types` | | The only home of `float32_t`, `float64_t` | 03.1 |
| | `texture_blob` | The texture blob's format, in the one place both `compile/` and `run/` may include | 06.4 |
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

### World

| System | Parts | Job | Stage |
|---|---|---|---|
| `world` | (the Owner) | `world_init`, `world_deinit`, `world_reset`: the two regions and arenas its parts borrow. `world_open`, `world_close`: the map, edit log and snapshots of one world (07.3) | 07.2 |
| | `section column` | Palette storage, coordinates, `z` range checks | 07.2 |
| | `map edit` | Column map, streaming, edit log, snapshots | 07.3 |
| | `light` | Sky and block light | 11.1 |
| | `tick` | Fluids, falling blocks, random ticks | 11.2 |
| `terrain` | (the Owner) | `terrain_init`, `terrain_deinit`: one seed, and the region and arena its parts borrow | 10.1 |
| | `rng noise` | Seeded streams and position hashes; fractal fields sampled in bulk on `asm/src/noise` | 10.1 |
| | `climate` | Continentalness, erosion, ridges, temperature, humidity, weirdness | 10.2 |
| | `density` | Height splines and 3D terrain shape, `z ∈ [-250, 500]`; bedrock at `-250`, water up to `0` | 10.3 |
| | `hydrology` | Sea level 0, rivers, oceans, beaches, aquifers, the lava level under the world at `-230` | 10.4 |
| | `cave ore` | Cheese, spaghetti and noodle caves cut from any box; deepslate by depth, and the table of rock and ore veins by depth | 10.5 |
| | `biome surface` | All Overworld biomes, chosen from the climate by `config/biomes/`; what each lays on the ground and lines its caves with | 10.6 |
| | `feature` | Trees, plants, lakes, ice and snow, cross-chunk placement. `terrain_generate_box` in the Owner runs every stage | 10.7 |

### Gameplay

| System | Parts | Job | Stage |
|---|---|---|---|
| `game` | | **Owner of World, Tick and Frame arenas**; the loop connecting `sim`, `gpu`, `main` | 08.4, extended later |
| `physics` | `collision raycast` | Swept AABB; DDA block targeting | 12.1, 12.2 |
| `player` | `movement camera` | Walk, jump, swim, look | 12.1, 12.2 |
| | `interaction` | **Mining, drops, placing** | 12.3 |
| | `survival` | Health, hunger, death, respawn | 13.4 |
| | `combat` | Attacking | 15.7 |
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
| | `hud` | Crosshair, hotbar, hearts, drumsticks, debug overlay with FPS and per-stage times | 14.2 |
| | `screen_inventory screen_crafting screen_furnace screen_chest` | Game screens. `screen_inventory` also holds what all four share: the ring from `main` to `sim`, the views back, the item pictures and the window, and the other three include it, as its README allows | 14.3 |
| | `menu` | Main menu, world select, pause | 14.4 |
| | `screen_settings` | **Render distance and all options** | 14.5 |
| `settings` | `persist` | Stored options with our own runtime parser | 14.5 |
| `save` | `world` | Seed + changed-section deltas, crash-safe | 16.1 |
| | `state autosave` | Player, entities, time; background saves | 16.2 |

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
| `embed_assets` | `incbin` of generated texture blobs | 06.4 |

C calls these through prototypes in `run/include/simd/<name>/index.h`.

---

## 9. `textures/`

Layout: `textures/<GROUP>/<NAME>/`.

```
textures/
├── ATTRIBUTION.md                   every source pack, author, licence, commit; generated
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
| `tests/compile/game_tables/` | ctest `compile_game_tables`: the real block tables against what `config/blocks/` says, dirt drops dirt, and each bad file refused at its line, blocks and biomes both. `cmake/targets.cmake` compiles the generated tables into it |
| `tests/compile/{inflate,png}/fixtures/` | Committed streams and images from independent encoders, with `generate.py`, which wrote them and is not part of the build |
| `tests/run/<system>/<part>/` | Game tests |
| `tests/run/types/` | ctest `run_types`, covering all of Stage 03.1: `types`, `assert`, `log`, `main/cpu_check`, `asm/src/cpuid`, and the built program's `--report` and failure paths |
| `tests/run/thread/` | ctest `run_thread`, covering `run/src/thread/` and `run/src/thread/ring/` together |
| `tests/run/math/` | ctest `run_math_owner` for `run/src/math/` itself, named in its `test.cmake`, and `run_math_<part>` for each part |
| `tests/run/display/mode/` | ctest `run_display_mode`, covering `run/src/display/` with `device`, `mode` and `flip`, on `fixtures/fake_card.h`, a fake kernel behind `display_ops_t` |
| `tests/run/display/vt_guard/` | ctest `run_display_vt_guard`, in forked children that really die, over a fake kernel reporting down a pipe and keys fed down another. Shares `../mode/fixtures/fake_card.h` |
| `tests/run/display/headless/` | ctest `run_display_headless`, covering `run/src/display/headless/` with `png`, `script` and `demo`, and the built program from script to file. Links `compile/src/png` to decode what it writes |
| `tests/run/input/` | ctest `run_input`, covering `run/src/input/` with `ops`, `discovery` and `events`, on replay data, this machine's capability bitmaps copied from `/proc/bus/input/devices` and the fake kernel in `fixtures/`, never on a real device |
| `tests/run/world/section/` | ctest `run_world_section`: random edits against a brute-force array, each change of form, running out of room, and the bytes each form holds |
| `tests/run/world/column/` | ctest `run_world_column`: coordinates and `z` limits, random edits against a brute-force array of the whole gameplay range, and `run/src/world/` itself |
| `tests/run/world/map/` | ctest `run_world_map`: the toroidal table, streaming by distance, requests out of date, a random walk against a brute-force model, and `world_open` |
| `tests/run/world/edit/` | ctest `run_world_edit`: random edits against a brute-force array and brute-force flags, the log, each consumer's flags, and snapshots, with a real reader thread |
| `tests/run/world/light/` | ctest `run_world_light`: incremental light against a separate full recompute on random edits, columns coming and going, running out of room or memory, exact values worked out by hand, and what an opaque block's faces see |
| `tests/run/world/tick/` | ctest `run_world_sim` (named in its `test.cmake`): `run/src/world/tick/`: flow worked out by hand and against a separate flood fill, lava and water, falling blocks, leaf decay, crops, grass, columns coming and going, running out of room, the same world from the same seed, real terrain left alone, each rule's number from both sides, and light against a rebuild after real terrain has been simulated |
| `tests/run/terrain/seed/` | ctest `run_terrain_seed`: `run/src/terrain/` with `rng` and `noise`, the same bytes from 1 worker and 16, any split of the work, and values pinned from a separate program and the scalar kernel |
| `tests/run/terrain/climate/` | ctest `run_terrain_climate`: `run/src/terrain/climate/`: ranges, the spread its README promises, the same bytes from 1 worker and 16, any split of the work, and each value worked out by hand from its noise |
| `tests/run/terrain/density/` | ctest `run_terrain_density`: `run/src/terrain/density/`: the range limits on every corner of the climate and on real columns, bedrock at `-250`, water's top at `0`, values worked out by hand, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/terrain/hydrology/` | ctest `run_terrain_hydrology`: `run/src/terrain/hydrology/`: each column worked out by hand, rivers that hold water and reach the sea, oceans, beaches, aquifers that cannot flow, the lava level, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/terrain/cave/` | ctest `run_terrain_cave`: `run/src/terrain/cave/`: all three kinds and their shares, thin against wide, a solid floor, no cave reaching the sea, each voxel worked out by hand, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/terrain/biome/` | ctest `run_terrain_biome`: `run/src/terrain/biome/` and `run/src/terrain/surface/`: every biome the plan lists is in the table and turns up in real worlds, the choice and the rules worked out by hand, each biome's own surface in real columns, lined caves, the whole chain of stages, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/terrain/feature/` | ctest `run_terrain_feature`: `run/src/terrain/feature/` and `terrain_generate_box`: any split of a box, any order of chunks, the same bytes from 1 worker and 16, trees whole across chunk lines, each tree within its reach, a column's ground against the stages walked from the sky, everything standing where it may, lakes that hold their water, ice and snow |
| `tests/run/terrain/ore/` | ctest `run_terrain_ore`: `run/src/terrain/ore/`: the table against the README, each ore at its depths, deepslate variants only in deepslate, veins worked out by hand, across chunks, any split of the work, and the same bytes from 1 worker and 16 |
| `tests/run/texture/` | ctest `run_texture`, covering `run/src/texture/` with `blob` and `registry`, and `asm/src/embed_assets/`: every texture against its source files, and damage to the blob, field by field |
| `tests/run/gpu_bridge/` | ctest `run_gpu_bridge`, covering `run/src/gpu_bridge/` with `status`, and `cuda/src/device_memory/`, on the real card: real blocks and a real kernel, the budgets, stale and forged handles, a pebibyte refused, and no device in a process of its own |
| `tests/run/render/encode/` | ctest `run_render_encode`: one voxel's bytes worked out on paper, every shape of section against brute force, and real workers. No card |
| `tests/run/render/frame/` | ctest `run_render_frame`, covering `run/src/game/`, `run/src/render/frame/` and `cuda/src/{shade,post,frame}/`, on the real card: every pixel against an exact trace, the real path against the map, golden PNGs in `fixtures/`, the built program from script to file, and the real threads |
| `tests/run/physics/` | ctest `run_physics`: `run/src/physics/` with `collision`: sweeps worked out by hand, random sweeps against a march in tiny steps, every block's boxes, what is not loaded, and no heap in a move |
| `tests/run/physics/raycast/` | ctest `run_physics_raycast`: `run/src/physics/raycast/` and `run/src/player/camera/`: casts worked out by hand, random casts against a march in tiny steps, what every block is aimed at by, mouse look, `player_camera_outline_draw`'s pixels, and `run/src/game/`'s target and outline on the real card |
| `tests/cuda/entity_rays/` | ctest `render_feature_entities`: `cuda/src/entity_rays/` and `run/src/render/entities/`: rays against boxes by hand and against a march, every tile's list against every ray, the card's hits against the host's, a box's pixels against its texture, sun, shade, glass and water, the outline's edges, depth and water, the crack by its frames, golden pictures, the cost on real terrain, and a script in the built program that digs and looks |
| `tests/cuda/lod/` | ctest `render_feature_lod`: `run/src/render/lod/`, `cuda/src/lod/` and their share in `cuda/src/shade/`: levels and coarse voxels by hand, every level on the card against its samples, following the eye, rays against a march, the sun, every far pixel's colour, what stands in front and what lets it through, fog, real terrain, golden pictures, the cost at R=512, the real threads, and the built program |
| `tests/run/player/interaction/` | ctest `run_player_interaction`: `run/src/player/interaction/`: every block's table against its `config/blocks/` file, mined and rolled; mining times by hand and for every block and tool; placing; through `run/src/player/`; on the real card and threads; and a script in the built program that walks, mines to dirt and places it back |
| `tests/run/entity/item_drop/` | ctest `run_entity_drop`, named in its `test.cmake`: `run/src/entity/` with `item_drop`: falling and sliding by hand, every block's effect on an item, merging at its limits and the grid against every pair, pickup to the tick and into a real hand, the books, the same bytes, no heap, a real body, and a script in the built program stopped at each point from dirt dropped to dirt placed back |
| `tests/run/entity/store/` | ctest `run_entity_store`, named by its path: `run/src/entity/store/`, `grid` and `animation`: every array through a removal, handles against a model over 200000 steps, a body's fall, walk, steps, water, lava and ladders by hand, 64 bodies at random never inside anything, the grid against every body at every box size, the shove by hand, every rig's parts and poses by hand, the same bytes, and no heap |
| `tests/run/item/inventory/` | ctest `run_item_inventory`, named by its path: `run/src/item/` with `stack` and `inventory`: every stack rule against every item, where things go, the books, the hotbar from another thread, no heap, and a script in the built program choosing slots, picking up and placing |
| `tests/run/item/crafting/` | ctest `run_item_crafting`, named by its path: `run/src/item/` with `crafting`, `smelting` and `container`: every recipe everywhere it fits and mirrored, furnaces by the tick, chests, and what `run/src/game/` does with them on the real card and threads |
| `tests/run/item/equipment/` | ctest `run_item_equipment`, named by its path: `run/src/item/equipment/`: every tool and piece of armor against the published numbers, wear to the use that breaks it, a hoe and shears, armor on and off with the books kept, what it stops worked out by hand, 200000 steps at random, no heap, what `run/src/game/` does with all of it on the real card and threads, and the built program |
| `tests/run/player/survival/` | ctest `run_player_survival`: `run/src/player/survival/`, with what `run/src/player/` and `run/src/game/` do with it: every rule's numbers by hand and by a real body in a real world, death and coming back, the bed, the same bytes, no heap, a real game on the card, and a script in the built program |
| `tests/run/player/movement/` | ctest `run_player_movement`: `run/src/player/` with `movement`: each mode's measured speeds against the numbers in its README, the same bytes from the same intents, the threads' exchange and interpolation, and `run/src/game/`'s walking on the real card and threads, on fake input, and in the built program |
| `tests/run/game/` | ctest `run_game`: the real loop on the real card and threads through `display_frames_run`, on fake kernels, with the guardian really forked |
| `tests/run/ui/screens/` | ctest `run_ui_screens`, named by its path: `run/src/ui/` with its four `screen_*`, and what `run/src/game/` does with them: every slot's place at three scales, replays of real scripts in `fixtures/` held byte for byte to golden PNGs beside them, 20000 random clicks on each screen, closing with no room, the ring, no heap, every way a screen comes up and goes on the real card, a click over the real threads, and the built program |
| `tests/run/ui/font/` | ctest `run_ui_font`, named by its path: `run/src/ui/` with `compose`, `font` and `widget`: the font against its recipe by hand, text, fills and images against a pixel-at-a-time model on `simd_reference_blend_over` with guard pixels round the canvas, each widget on made-up input frames, every key of a US keyboard, a picture left alone by what is drawn over it, no heap in 200 frames, a sheet of all of it held to a hash set after looking at it, and the built program |
| `tests/run/ui/hud/` | ctest `run_ui_hud`: `run/src/ui/hud/`, with what `run/src/game/` does with it: where things go by hand, random states against a pixel-at-a-time model with guard pixels round the canvas, golden PNGs in `fixtures/`, no heap in 200 frames, the real game on the card with one thread and with its real threads, and a script in the built program |
| `tests/run/render/upload/` | ctest `run_render_upload`, covering `run/src/render/` with `upload`, and `cuda/src/world_buffers/`, on the real card: a real kernel's answers against the world voxel for voxel, the slab pool against a model, the budget, and a real sim thread |
| `tests/cuda/<area>/` | Headless GPU tests; golden PNGs |
| `tests/cuda/shapes/` | ctest `render_feature_shapes`, named in its `test.cmake`: each shape by hand, a block of each shape and its holes against an exact trace from all sides, the settings, the coarse pass, how many holes a pixel goes through, golden PNGs in `fixtures/`, and the cost, on the real card |
| `tests/cuda/sky/` | ctest `render_feature_sky`, named in its `test.cmake`: the day and the light by hand, the sky, fog, shadows through holes and stored light against an exact trace, a tower's shadow by hand, the settings, the clock through the real threads, golden PNGs in `fixtures/`, and the cost, on the real card |
| `tests/cuda/water/` | ctest `render_feature_water`, named in its `test.cmake`: reflection, depth tint and fog behind a surface by hand, a lake, glass and ice against an exact trace from above and from under the water, deeper being bluer, a tower in the water by hand, the settings, golden PNGs in `fixtures/`, and the cost, on the real card |
| `tests/cuda/post/` | ctest `render_feature_post`, named in its `test.cmake`: the jitter, the way back to the last frame and what AO and god rays add up to by hand, AO's rate read back from the card and against the exact integral, TAA against a picture at four times the size, ghosts and a moving eye, god rays against an exact walk, the settings, golden PNGs in `fixtures/`, and the cost, on the real card |
| `tests/cuda/traverse/` | ctest `cuda_traverse`, with an encoder of its own for `world_buffers`' format: known hits, random rays and whole frames against an exact CPU reference in `float64_t`, the coarse pass never overestimating, and Stage 01.3's worlds. Timing on request |
| `tests/asm/` | NASM kernels against scalar C references |
| `tests/asm/abi/` | ctest `asm_abi`: every kernel keeps the callee-saved registers and leaves no dirty YMM state, by machine state, not by output |
| `tests/fuzz/`, `tests/soak/` | Fuzzing and long runs (Stage 18.1) |

Tests need no server and no display, and run under `timeout`.

**`bench/`**: `simd`, `terrain`, `mobs`, `world`, each `index.c` with a `README.md` and a results history. `terrain/tour` is the phase gate's biome tour: a picture of every biome, with no display and no card.

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
| `cmake/check_link.cmake` | Fails the build if `minecraft_c` needs a library the standard does not allow, or has an executable stack |
| `cmake/standards_lint.cmake` | Runs the lint before anything else compiles, runs the recursion check on every preset, and registers ctests `standards_lint` and `clang_format` |
| `cmake/check_format.cmake` | Run by ctest `clang_format`: fails if any C or CUDA file does not match `.clang-format` |
| `cmake/sanitize/` | ASan options CUDA needs, linked only in the `sanitize` preset |

`cmake/machine.cmake` and `cmake/generated.cmake` are included if present, so the stages that write them do not edit
the root `CMakeLists.txt`.

Presets: `debug`, `sanitize`, `release`, `profile`. Output goes to `build/<preset>/`, with the program at
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

Under `tests/`, nothing in `harness/`, `fuzz/`, `soak/` or any `fixtures/` directory is registered as a test.

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
     ├─ terrain, world
     ├─ display, input, texture
     └─ job, thread, file, time, math, simd ─► (asm/)
         └─ arena, heap, log, assert, types
```

- `main` calls `job` directly too: `job_system_t` is a field of `app_t`, and `game` will borrow it.
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
| Height limit and sea level | `config/machine.yaml`, `world:` |
| Render distance setting | `run/src/ui/screen_settings/`, `run/src/settings/` |
| How a pixel is made | `cuda/src/frame/`, then `traverse`, `shade`, `post` |
| How pixels reach the monitor | `run/src/render/frame/`, then `run/src/display/flip/` |
| Why the console came back after a crash | `run/src/display/vt_guard/` |
| Terrain shape | `run/src/terrain/density/` |
| Rivers, oceans, beaches, aquifers, lava | `run/src/terrain/hydrology/` |
| Biomes | `config/biomes/`, `run/src/terrain/biome/` |
| What a biome lays on the ground | `config/biomes/`, `run/src/terrain/surface/` |
| What grows on it, and lakes | `config/biomes/`, `run/src/terrain/feature/` |
| Every stage of terrain for one box | `terrain_generate_box` in `run/src/terrain/` |
| Zombie behaviour | `run/src/mob/zombie/` |
| Who allocates anything | `run/src/heap/`, then depth 1 of the system |
| Type rules, padding, memory law | `plans/CODING_STANDARD.md` |
| What's done and what's next | `plans/001-*-phase-*.md` |
