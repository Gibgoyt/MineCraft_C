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
| `config/blocks/<block>/index.yaml` | Hardness, right tool, drops, shape, light, textures | `compile/src/game_tables/` |
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
| `standards_lint` | Fails the build on any coding-standard breach | 02.4 |
| `struct_offsets` | Emits `.inc` offsets from C structs, for NASM | 04.3 |
| `inflate` | Our own DEFLATE decoder | 06.1 |
| `png` | Our own bounds-checked PNG decoder (packs are untrusted) | 06.1 |
| `texture_pack` | Open pack in `/tmp/mc_c_assets/` → `textures/` | 06.2 |
| `texture_vector` | Vector data per texture; procedural recipes | 06.3 |
| `game_tables` | `config/{blocks,items,recipes,biomes,mobs}` → C tables | 07.1 |

---

## 6. `run/`: the game

Layout: `run/src/<system>/<part>/index.c`, `run/include/<system>/<part>/index.h`, `README.md`.

### Foundation

| System | Parts | Job | Stage |
|---|---|---|---|
| `main` | | Entry point, `app_t`, CPUID check, init and deinit order | 03.1 |
| `types` | | The only home of `float32_t`, `float64_t` | 03.1 |
| `assert` | | Assertions | 03.1 |
| `log` | | Logging | 03.1 |
| `heap` | | **Only OS allocator user**; counts everything | 03.2 |
| `arena` | | Bump allocators | 03.2 |
| `thread` | | Pinning, rings | 03.3 |
| `time` | | Monotonic clock | 03.3 |
| `file` | | File wrappers | 03.3 |
| `job` | | Work-stealing job system, per-thread arenas | 03.4 |
| `profiler` | | Per-thread scopes, trace dump | 17.1 |

### Math and SIMD

| System | Parts | Job | Stage |
|---|---|---|---|
| `math` | `vec2 vec3 vec4 ivec3 mat4 quat aabb ray` | Our vector library, scalar C | 04.1 |
| `simd` | `copy blend bitmask noise vec_batch` | C prototypes of NASM kernels | 04.2, 04.3 |

### Display and input

| System | Parts | Job | Stage |
|---|---|---|---|
| `display` | `device mode flip` | Raw-ioctl KMS: card, connector, mode, dumb buffers, page flip | 05.1 |
| | `vt_guard` | Console takeover; always restores, even on a crash | 05.2 |
| | `headless` | No-display backend, PNG screenshots for tests | 05.4 |
| `input` | `device keyboard mouse` | evdev discovery, raw events, hot-plug | 05.3 |

### Rendering (CPU side)

| System | Parts | Job | Stage |
|---|---|---|---|
| `gpu_bridge` | one per CUDA area | **The C API to CUDA** (`extern "C"`) | 08.1 |
| `render` | `encode upload` | Encode sections on workers; budgeted upload queue | 08.2 |
| | `frame` | Frame sequence: submit, readback, HUD, present | 08.4 |
| | `entities` | Per-tile entity lists | 09.5 |
| | `lod` | Build coarse far-field grids | 09.6 |
| `texture` | `registry` | Runtime lookup of embedded texture blobs | 06.4 |

### World

| System | Parts | Job | Stage |
|---|---|---|---|
| `world` | `section column` | Palette storage, coordinates, `z` range checks | 07.2 |
| | `map edit` | Column map, streaming, edit log, snapshots | 07.3 |
| | `light` | Sky and block light | 11.1 |
| | `tick` | Fluids, falling blocks, random ticks | 11.2 |
| `terrain` | `rng noise` | Deterministic randomness, noise | 10.1 |
| | `climate` | Continentalness, erosion, ridges, temperature, humidity | 10.2 |
| | `density` | 3D terrain shape, `z ∈ [-250, 500]` | 10.3 |
| | `hydrology` | Sea level 0, rivers, oceans, aquifers, lava | 10.4 |
| | `cave ore` | Caves and ore tables | 10.5 |
| | `biome surface` | All Overworld biomes, surface rules | 10.6 |
| | `feature` | Trees, plants, cross-chunk placement | 10.7 |

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
| `ui` | `font widget` | Bitmap font, widgets | 14.1 |
| | `hud` | Crosshair, hotbar, hearts, debug overlay | 14.2 |
| | `screen_inventory screen_crafting screen_furnace screen_chest` | Game screens | 14.3 |
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
| `traverse` | Coarse depth pass; primary rays through the bitmask tree | 08.3 |
| `shade` | Textures, light, shadows, AO, water | 08.4, 09.x |
| `post` | Sky, fog, TAA, tone map | 08.4, 09.x |
| `frame` | One frame's kernels in order, GPU timers | 08.4, 17.1 |
| `entity_rays` | Mob and item boxes | 09.5 |
| `lod` | Far-field traversal | 09.6 |

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
| `copy` | Streaming copy to scanout memory | 04.2 |
| `blend` | HUD alpha blending | 04.2 |
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
├── ATTRIBUTION.md                   every source pack, author, licence, commit
├── blocks/dirt/
│   ├── index.yaml                   source pack or procedural recipe
│   ├── index.mctex                  translated data
│   └── README.md
├── items/
├── mobs/zombie/
└── ui/
```

Groups: `blocks`, `items`, `mobs`, `ui`.

How a texture gets into the game:

```
open pack, pinned commit, cloned to /tmp/mc_c_assets/      (never committed as-is)
        │  compile/src/texture_pack  (licence check)
        ▼
textures/<GROUP>/<NAME>/                                    committed: builds need no network
        │  compile/src/texture_vector  (palette, mean color, height, normal)
        ▼
build/<preset>/generated/textures/                          blob
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
| `tests/run/<system>/<part>/` | Game tests |
| `tests/cuda/<area>/` | Headless GPU tests; golden PNGs |
| `tests/asm/` | NASM kernels against scalar C references |
| `tests/fuzz/`, `tests/soak/` | Fuzzing and long runs (Stage 18.1) |

Tests need no server and no display, and run under `timeout`.

**`bench/`**: `simd`, `terrain`, `mobs`, `world`, each `index.c` with a `README.md` and a results history.

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
| `cmake/targets.cmake` | `mc_asm`, `mc_cuda`, `mc_run`, `minecraft_c`, tools, tests |

Presets: `debug`, `sanitize`, `release`, `profile`. Output goes to `build/<preset>/`, with the program at
`build/<preset>/bin/minecraft_c`.

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

- Depth 2 parts may include their own Owner's header, and lower systems.
- Sibling parts may include one another only if their READMEs say so.
- `compile/` may not include `run/`, except for `run/include/types/`. Its tools share the type definitions.
- Nothing in `run/` includes from `compile/`, aside from generated output in `build/`.

---

## 12. Threads and where they live

| Thread | Started by | Mainly runs |
|---|---|---|
| `main` | The OS | `main`, `input`, `display`, `ui`, last step of `render/frame` |
| `sim` | `game` | `game`, `player`, `mob`, `entity`, `world/tick` |
| `gpu` | `game` | `render/frame`, `render/upload`, `gpu_bridge`, `cuda/` |
| `worker` × 16 | `job` | `terrain`, `world/light`, `render/encode`, `save`, mob AI batches |

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
| Biomes | `config/biomes/`, `run/src/terrain/biome/` |
| Zombie behaviour | `run/src/mob/zombie/` |
| Who allocates anything | `run/src/heap/`, then depth 1 of the system |
| Type rules, padding, memory law | `plans/CODING_STANDARD.md` |
| What's done and what's next | `plans/001-*-phase-*.md` |
