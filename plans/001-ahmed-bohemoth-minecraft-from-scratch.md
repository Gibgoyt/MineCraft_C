# 001-ahmed-bohemoth-minecraft-from-scratch — master plan

## 0. Context

Ahmed wants a from-scratch, heavily optimized Minecraft-style game for Linux, written in C with CUDA and NASM.
There is no OpenGL, no Vulkan and no game server. It targets this exact machine first, behind clean seams so
other systems can be added later. The repo currently holds only `plans/README.md`, `plans/PROMPT.md` (the
verbatim request) and `research/001.md`, `research/002.md`. No game code exists.

**This approval covers documentation only (Phase 00).** Implementation starts only when Ahmed says go.
Phase 00 creates the numbered plan files, `plans/REPO_STRUCTURE.md`, `plans/CODING_STANDARD.md` and
`research/003-speed-research.md`, and writes no game code.

### Decisions Ahmed made on 2026-11-01

| Question | Decision |
|---|---|
| Renderer | **CUDA voxel ray casting.** No triangles or greedy meshes in the main path. |
| Display | **Hand-rolled KMS/DRM through `/dev/dri`**, run from a text console (VT). No X11, libdrm, GBM or EGL. |
| Libraries | **glibc (libc, libm, pthreads) + POSIX/Linux syscalls + CUDA runtime.** Anything else is banned. |
| Agents | **Waves of agents are allowed for this repo's work.** This overrides the global "never use subagents" rule for this project only. |
| First release | **Full Overworld survival.** Nether, End, redstone, audio and passive animals are later plans. |

### Facts verified on this machine (read-only probes, 2026-11-01)

| Fact | Evidence | Consequence |
|---|---|---|
| i9-10900K: 10 cores/20 threads, L1d 32 KiB, L2 256 KiB, L3 20 MiB, 64-byte lines | `lscpu`, `getconf` | `machine.yaml` values |
| AVX2, FMA, BMI1/2, F16C, MOVBE, POPCNT, LZCNT(abm) present; no AVX-512 | `lscpu` flags | NASM baseline; matches Ahmed's SIMD overview |
| RTX 2080 Super: 8192 MiB, compute capability 7.5, PCIe gen3 x16 max | `nvidia-smi` | `-arch=sm_75`; roughly 12 GB/s theoretical readback |
| 1080p at 120 Hz on HDMI, plus a second connected DP output | `xrandr` | Default mode; connector chosen at runtime |
| `nvidia_drm modeset=Y`; `/dev/dri/card1` has a logind ACL | `/sys/module`, `ls` | KMS is plausible, **but dumb buffers and flips on this driver are unproven** (Phase 01) |
| Ahmed is in the `input` group but **not** `video`; `/dev/fb0` is unusable | `groups`, `ls` | evdev works directly; DRM access relies on the seat ACL |
| Kernel UAPI headers present (`drm/drm.h`, `linux/input.h`) | `ls` | No libdrm needed |
| GCC 16.2 is accepted as nvcc's host; `-std=c++20` kernel compiled | Test compile; `host_config.h` | Toolchain works |
| **Nvcc objects reference `__gxx_personality_v0`, `__cxa_guard_*`, `_Unwind_Resume`** | `nm -u` on a test object | C++ runtime symbols must be removed or accepted (Stage 01.1) |
| `-Wpadded -Werror=padded` rejects both interior and trailing padding | GCC probe | Padding rule is enforceable |
| `_Float32`/`_Float64` compile under `-std=c23 -Wconversion -Werror`, pass to `sinf`, accept `1.0f32` | GCC probe | Lets us ban `float`/`double` outside one header |
| `int32_t` is `int`; `int32_t main(int32_t, char **)` compiles clean under `-Wall -Wextra -Wpedantic` | GCC probe | `main` needs no `int` exception |
| 62 GiB RAM; THP `always`; no reserved hugepages; memlock limit 8 MiB | `free`, `/sys`, `ulimit` | 2 MiB-aligned arenas; never rely on `mlock` |
| `perf`, `valgrind`, `strace` not on PATH; `gdb`, `clang-format` are | `which` | We build our own profiler; sanitizers via GCC |

### Conflicts and unclear points

1. **"Never allow padding" does not mean `packed`.** `__attribute__((packed))` allows misaligned fields, which
   is slower and breaks aligned AVX loads. Padding is written by hand, `-Werror=padded` rejects compiler padding,
   and `static_assert` checks size and offsets. This gives what was asked: every byte is visible.
2. **"C standard library only" vs CUDA.** `.cu` files are C++ to nvcc and need libcudart. CUDA was explicitly
   requested, so this is the one sanctioned exception. Whether libstdc++ can be dropped is measured in Stage 01.1.
3. **"Never write ..." was cut off** in the request. It is read as "never write SIMD intrinsics or inline assembly
   in C". **"For now let's please create a very simple ..." was also cut off**, and is read with the later
   "simple working minecraft". Ahmed can correct either.
4. **"Generating vectors to use for later" is ambiguous.** It is read as both: procedural parametric texture
   recipes, and compiled typed vector data per texture (see Phase 06).
5. **`plans/README.md` carries conventions from another project** (`npm`, migrations, `tools/test-or-build.sh`,
   `.claude/rules/...`). The README stays unedited. The mutex is implemented here for CMake, because parallel
   waves need it. `npm` and migration lines do not apply and are logged in Stage 00.1 **Issues found**.
6. **Commit attribution.** The README and global rules forbid AI trailers, so they override any harness
   attribution reminder. Commit with `git commit -F -`, verify with
   `git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'` printing nothing. Never push
   unless asked.
7. **Legal.** `research/002.md` describes copying Mojang textures and decompiling, and says this is why that game
   cannot be released. **We use neither.** No Mojang jars, no decompiled code. Open-licensed packs and our own
   generated textures only, and attribution ships in the repo. "Minecraft" is a trademark, so the binary is
   `minecraft_c` for private use and needs a rename before any public release.
8. **Sea level and height limit.** Inclusive range `z = -250 ... +500` (751 layers). Water's top block is at
   `z = 0`. Bedrock at `-250`. Top placeable block at `+500`.
9. **Greedy meshing and shadow cascades from `research/001.md` are not used in the main renderer**, because ray
   casting has no mesh and traces sun rays directly. Their goals still apply. See section 3.

---

## 1. Requirement traceability (everything Ahmed asked for)

| # | Requirement | Where |
|---|---|---|
| R1 | Own rendering engine, no OpenGL/Vulkan, Linux only | P05, P08, P09 |
| R2 | Own voxel game engine | P07, P10-P16 |
| R3 | Own texture system, generating vectors for later | P06 |
| R4 | `config/machine.yaml` drives compile-time `#define`s (threads, GPU, SIMD, POSIX) | P02 |
| R5 | C only, standard library only, no TCP or server | Section 0, CODING_STANDARD, lint |
| R6 | Own YAML conversion in `compile/{src,include}/<name>/` | P02 |
| R7 | All runtime code in `run/{src,include}/<dir>/` | REPO_STRUCTURE |
| R8 | Texture pack translation in `compile/`, outputs in `textures/<GROUP>/<dir>/` | P06 |
| R9 | Research and textures can be copied to `/tmp/` and translated at compile time | P00, P06 |
| R10 | `z = 0` sea level, `-250` bottom, `500` height limit | P07, P10 |
| R11 | **All terrains** | P10 (all Overworld biomes; other dimensions by decision are later plans) |
| R12 | Zombies, skeletons, creepers | P15 |
| R13 | Mining works and drops the right thing (dirt drops dirt), intuitive to understand | P07, P12, `config/blocks/*` |
| R14 | Every source file is `index.{c,h}` with a `README.md` in its directory | REPO_STRUCTURE, lint |
| R15 | Architecture optimized for this hardware, general purpose later, POSIX defined in yaml | P02, P03, section 2 |
| R16 | Settings include chunk render distance | P14 |
| R17 | Plan in `plans/` per README framework, `NNN-ahmed-bohemoth-*` names | Section 6, P00 |
| R18 | `plans/REPO_STRUCTURE.md` documents the entire repo thoroughly | P00.2 |
| R19 | `plans/CODING_STANDARD.md`: no `int`/`float`, exact widths, no compiler padding, gcc 16 + nvcc 13 | P00.3 |
| R20 | One-time stack→heap pointer principle; malloc/free responsibility; depth shows ownership | Section 4 |
| R21 | CUDA in `cuda/{src,include}/<dir>/index.{cu,hu}`; all CUDA headers are `.hu` | REPO_STRUCTURE |
| R22 | Own vector library | P04 |
| R23 | All CPU SIMD in NASM, built to `.o` files that the C entry point uses | P04, lint |
| R24 | CMake architecture | P02 |
| R25 | No C file over 2000 lines | Lint, build fails |
| R26 | Waves of agents, many phases and stages | Sections 6-7 |
| R27 | Pull in speed research | Section 3, `research/003-speed-research.md` |

---

## 2. Architecture

```
                    config/*.yaml, textures/*        (inputs, human-readable)
                              │
          compile/ tools (C): yaml → machine header, block tables, texture blobs
                              │  build/generated/
       ┌──────────────────────┼─────────────────────────┐
       ▼                      ▼                         ▼
  asm/ (NASM → .o)      run/ (C23, gcc 16)       cuda/ (nvcc 13, sm_75)
  batch SIMD kernels    game + engine            ray-casting kernels
       └──────────────────────┼─────────────────────────┘
                              ▼
                     build/<preset>/bin/minecraft_c
                              │
         /dev/input/event* ──►│──► /dev/dri/card1 (KMS page flip)
```

### Threads (20 logical; every number comes from `machine.yaml`)

| Thread(s) | Count | Job |
|---|---|---|
| `main` | 1 | Reads evdev, composes HUD, page flips, owns the VT guard |
| `sim` | 1 | Fixed 20 Hz game tick; fans work out to workers |
| `gpu` | 1 | Submits CUDA frames, uploads world changes, reads pixels back |
| `worker` | 16 | Job system: terrain, light, section encoding, save, mob AI batches |
| spare | 1 | Left for the OS |

Threads are pinned, physical cores first. Workers use work-stealing queues. No locks on hot paths: threads
communicate with single-producer rings. Game state published to the renderer is a snapshot, so a slow tick
never stalls a frame (fixing the single-core and short-lived-object problems described in `research/002.md`).

### World format

- Axes: **x east, y north, z up.** Right-handed. `Z_MIN = -250`, `Z_SEA = 0`, `Z_MAX = 500`.
- **Chunk = 16×16 column** (so "render distance in chunks" means what players expect), split into **16³
  sections**. Exactly 48 section slots per column cover `z ∈ [-256, 512)`, with gameplay clamped to the real range.
- CPU section: `uint16_t` block state with a palette, `uint8_t` sky/block light. All-air or uniform sections
  store no array.
- GPU section: a three-level bitmask tree of `uint64_t`. Section mask → 4³ brick masks → voxels, using `tzcnt`-style
  skips. Material and light are stored only for occupied voxels.
- World map on GPU: toroidal column table with per-column max height, so sky rays exit early.

### Render path (each frame)

1. **Coarse depth pass:** low-resolution conservative cone trace gives each tile a safe ray start distance
   (Laine & Karras "beam optimization").
2. **Primary rays:** hierarchical DDA (Amanatides & Woo) through the bitmask tree. Non-cube blocks use a shape
   table of boxes. Mobs and items are boxes taken from a per-tile entity list.
3. **Shading:** texture lookup with distance-chosen mip, face shade, stored light, one sun shadow ray, short AO
   rays at reduced rate with temporal reuse.
4. **Transparency:** water and glass continue the ray; water gets simple reflection and depth tint.
5. **Far field (LOD):** coarser clipmap voxel grids beyond the full-detail radius.
6. **Post:** sky, fog, optional god rays, TAA, tone map to XRGB8888.
7. **Present:** copy to pinned host memory → CPU composes HUD with NASM blend → copy into dumb buffer → page flip.

### Performance targets (goals, measured and recorded; not promises)

| Metric | Target | Notes |
|---|---|---|
| Frame rate at 1080p, render distance 32, all effects | 120 FPS (8.33 ms) | 248.8M primary rays/s before secondary rays |
| Render distance setting | 2 to 512 chunks | Default 32; full detail to a configurable radius, LOD beyond |
| Terrain | ≥ 1000 columns/s on 16 workers | Set by benchmark in P10 |
| Sim tick at 100 mobs | ≤ 5 ms of the 50 ms budget | |
| Allocations during play | 0 calls to the OS heap in hot paths | Counted by the heap layer |
| VRAM | ≤ 6 GiB voxels, 256 MiB textures, 256 MiB frame buffers | Rough estimate: ~100 MiB at R=32, ~1.6 GiB at R=128, assuming 6 sections/column at 4 KiB |

---

## 3. Speed research and how each idea is used

Full write-up goes to `research/003-speed-research.md`, marking each claim as *sourced* or *engineering judgment*.

| Idea | Source | Our use |
|---|---|---|
| Store blocks as data, not objects | research/001 | Sections are flat arrays with palettes |
| Avoid short-lived objects | research/002 | No heap in hot paths; arenas only (section 4) |
| Spread simulation over all cores | research/002 | Job system, pinned threads |
| GPU does visibility | research/002; Nvidium (mesh-shader culling) | Ray casting makes visibility implicit per pixel |
| Grid traversal | Amanatides & Woo 1987 | Core ray loop and player block targeting |
| Beam optimization, voxel ray casting on GPU | Laine & Karras 2010 (60.9-122M rays/s on hardware of that era) | Coarse pass; numbers are old, so we measure in P01 |
| Software graphics pipelines on GPU | Laine & Karras 2011 (2-8x slower than hardware) | Why we did not choose rasterizing |
| Bitwise 64-bit masks | binary-greedy-meshing (50-200 µs per chunk) | Same bit tricks used for tree build and occupancy, not for meshes |
| Cubic sections | research/001 | 16³ sections with empty-section skip |
| LOD, one block per 8 m³ and coarser | research/001; Distant Horizons | Clipmap far field |
| Generate less, offload generation | research/001; C2ME (generation on other threads) | Worker terrain with priority by distance |
| Same seed gives same world; save only changes | research/001 | Deterministic terrain; delta saves in P16 |
| Layered noise; exponential mountains | research/001 | Fractal noise and height splines |
| Continentalness, erosion, peaks/valleys, 3D density, aquifers | Minecraft Wiki | Our own implementation of the concepts, in P10 |
| Physics and AI optimization with no behaviour change | Lithium | Cached neighbour lookups, batched AI in P15 |
| Lower memory use | FerriteCore | Palette and interned state tables |
| Compact vertices, multi-draw, cave culling | Sodium | **Not applicable** with no triangles; details were not confirmed by my searches |
| AO, god rays, water, sky, fog, shadows, TAA | research/001, research/002 | P09, through rays instead of Unity's pipeline |

**Sources:**
[Amanatides & Woo](https://www.eecs.yorku.ca/~amana/research/grid.pdf) ·
[Efficient Sparse Voxel Octrees](https://www.nvidia.com/docs/IO/88889/laine2010i3d_paper.pdf) ·
[Software Rasterization on GPUs](https://research.nvidia.com/publication/2011-08_high-performance-software-rasterization-gpus) ·
[binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing) ·
[Nvidium](https://modrinth.com/project/uvBS5xOp) ·
[Sodium culling issue](https://github.com/CaffeineMC/sodium/issues/333) ·
[Performance mod overview](https://maketecheasier.com/best-performance-enhancing-mods-for-minecraft) ·
[Terrain](https://minecraft.wiki/w/Terrain) ·
[Custom world generation](https://minecraft.wiki/w/Tutorial%3ACustom_world_generation) ·
[Pixel Perfection (CC-BY-SA-4.0)](https://github.com/minetest-texture-packs/Pixel-Perfection)

---

## 4. The memory law (normative summary; full text in CODING_STANDARD.md)

**The One-Owner Principle: every heap block has exactly one owning pointer, and that pointer can be traced by
struct fields back to a single `app_t` on `main()`'s stack.** No owning globals, no owning pointers in locals
that outlive their function.

The path of a file tells you what it may do with memory:

| Depth | Path | Tier | May do |
|---|---|---|---|
| 0 | `run/src/main/` | **Root** | Declares `app_t app` on the stack; calls each system's `_init`/`_deinit` in order and reverse order |
| 1 | `run/src/<system>/` | **Owner** | Only tier that calls `heap_reserve`/`heap_release`; owns its regions and arenas |
| 2 | `run/src/<system>/<part>/` | **Arena** | Receives an `arena_t *` from its parent; allocates from it; never frees individually, only resets |
| 3+ | `run/src/<system>/<part>/<sub>/` | **Borrow** | Allocates nothing; works on pointer + length given by callers, plus stack locals |

- **`run/src/heap/` is the only code touching the OS allocator.** `malloc`, `calloc`, `realloc`, `free`,
  `mmap`, `munmap` and `alloca` are banned everywhere else. VLAs and recursion are banned too.
- **Whoever initializes, deinitializes**, in the same `index.c`, as a pair.
- **Naming shows ownership:** `owned_` holds the block and must release it, `borrowed_` must not outlive its
  owner, `arena_` is a bump allocator, `handoff_` is an explicit transfer through a queue.
- **Lifetimes:** Process, World, Tick, Frame, Job. Each has its own arena, reset at its own boundary.
- **Threads:** each has a Job arena. Pointers crossing threads only go through named rings.
- **GPU memory** obeys the same law: `cudaMalloc`/`cudaFree` only in `cuda/src/device_memory/index.cu`.
- **`compile/` tools** follow the same tiers with a single process arena.

---

## 5. Repository layout (summary; every directory is documented in REPO_STRUCTURE.md)

```
config/machine.yaml                     hardware and compile-time switches
config/blocks/<block>/index.yaml        hardness, tool, drops, textures (dirt drops dirt, readable here)
config/{items,recipes,biomes,mobs}/<name>/index.yaml
compile/{src,include}/<tool>/index.{c,h}   yaml, machine_config, inflate, png, texture_pack,
                                           texture_vector, game_tables, struct_offsets, standards_lint
run/{src,include}/<system>/...             main, types, assert, log, heap, arena, thread, job, time, file,
                                           math/*, simd/*, display/*, input/*, gpu_bridge/*, render/*,
                                           texture/*, world/*, terrain/*, physics/*, player/*, item/*,
                                           entity/*, mob/*, ui/*, settings/*, save/*, game/*
cuda/{src,include}/<dir>/index.{cu,hu}     device_memory, world_buffers, traverse, shade, entity_rays,
                                           lod, post, frame
asm/{src,include}/<dir>/index.{asm,inc}    copy, blend, noise, bitmask, vec_batch, embed_assets
textures/<GROUP>/<dir>/                    blocks/, items/, mobs/, ui/, each with index.yaml + README.md
tests/<mirror of source path>/index.c      no server, no display, bounded by timeouts
bench/<name>/index.c                       benchmarks with recorded results
cmake/                                     toolchain, flags, generated code, targets, presets
tools/test-or-build.sh                     flock mutex around heavy commands
plans/  research/
```

Every source directory holds exactly one `index.*` plus a `README.md` (purpose, tier, what it owns, public
functions, what may call it). `run/src/main/{index.c,README.md}` is the entry point, as requested. Headers mirror
at `run/include/<same path>/index.h`, included as `#include "world/section/index.h"`.

C ↔ CUDA boundary: `run/include/gpu_bridge/*/index.h` is the C-only `extern "C"` API. `cuda/include/*/index.hu`
includes it and adds CUDA-only parts. Shared structs have `static_assert`s compiled on both sides.

### `config/machine.yaml` (initial content, verified values)

```yaml
platform: { os: linux, posix: true }
toolchain: { c_standard: c23, gcc_min: 16, nvcc_min: "13.4", nasm_min: "3.02", cmake_min: "4.4" }
cpu:
  model: i9-10900K
  arch_flag: skylake
  logical_threads: 20
  physical_cores: 10
  cache_line_bytes: 64
  l1d_kib: 32
  l2_kib: 256
  l3_mib: 20
  simd: { sse42: true, avx: true, avx2: true, fma3: true, bmi1: true, bmi2: true, popcnt: true, lzcnt: true, avx512: false }
threads: { main: 1, sim: 1, gpu: 1, workers: 16, reserved_os: 1, pin_physical_first: true }
gpu:
  enabled: true
  api: cuda
  model: RTX 2080 Super
  architecture: sm_75
  vram_mib: 8192
  budget: { voxels_mib: 6144, textures_mib: 256, frames_mib: 256 }
display: { backend: kms, device: /dev/dri/card1, connector: auto, width: 1920, height: 1080, refresh_hz: 120 }
memory: { huge_page_bytes: 2097152, process_budget_mib: 16384 }
game: { render_distance_default: 32, render_distance_max: 512, tick_hz: 20 }
world: { z_min: -250, z_sea: 0, z_max: 500, chunk_size: 16, section_size: 16 }
```

The tool emits `build/generated/machine/index.{h,hu,inc,cmake}`, so C, CUDA, NASM and CMake share one source.
Example defines: `MC_PLATFORM_POSIX`, `MC_THREADS_WORKERS`, `MC_CPU_HAS_AVX2`, `MC_GPU_ENABLED`.

### Build design

- `project(minecraft_c LANGUAGES C ASM_NASM CUDA)`, CMake 4.4, `CMakePresets.json` with `debug`, `sanitize`,
  `release`, `profile`.
- **Configure step** compiles the yaml and machine tools via `try_compile`/`execute_process`, because compiler
  flags depend on `machine.yaml`. `CMAKE_CONFIGURE_DEPENDS` re-runs it when the yaml changes.
- **Build step** uses `add_custom_command` for tables and texture blobs, and NASM `incbin` embeds them in a `.o`.
- Targets: `mc_asm`, `mc_cuda`, `mc_run` are object libraries that link into `minecraft_c`.
- C flags: `-std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion -Wshadow -Wvla
  -Wpadded -Werror -march=<arch_flag> -mtune=<arch_flag>`, and `-fno-tree-vectorize -fno-tree-slp-vectorize`
  so all deliberate SIMD stays in `asm/`. Release adds `-O3 -flto`. No `-ffast-math`, for deterministic worlds.
- `standards_lint` runs before compile and fails the build on any standard breach.

---

## 6. Plan files

Follows `plans/README.md`. NNN is `001`, author `ahmed`, round `bohemoth`, plan name `minecraft-from-scratch`.

```
plans/001-ahmed-bohemoth-minecraft-from-scratch.md                                 this plan, byte for byte
plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-05-display-input.md          phase gate and stage index
plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-05-stage-2-vt-guard.md       stage with todo list
```

Phase numbers are zero-padded so they sort. Each stage has its own file, so parallel agents never edit the same
checklist. Every file uses the README template (Status, Goal, Todo, Verification, Commits, Issues found).

Each stage's Verification uses `tools/test-or-build.sh ...`. From Phase 02 onward, unwrapped heavy commands are
bugs. Before the wrapper exists (Stage 02.1), this is logged as an accepted exception.

---

## 7. Agent rules

A **wave** is a set of stages that run in parallel because their paths do not overlap.

- Each agent gets one stage, works in its own git worktree, and may edit only the paths its stage owns plus its
  own stage file. Public headers that others depend on are merged first.
- Prompts must repeat: read `plans/README.md`, `CODING_STANDARD.md`, `REPO_STRUCTURE.md`; tick todos as work
  lands; log issues in full; **no AI trailers**; **never push**; **never leave a process running**.
- The main thread reviews, merges, runs the phase gate, and only then starts the next wave.
- Agents **cannot verify on KMS**, because the desktop owns the display. They use the headless backend, and
  screenshots go to PNG for visual checks. KMS gates are run by Ahmed.
- Any process a test starts runs under `timeout`, and agents confirm it is gone with `pgrep`. The game is never
  left running. At each user gate, the exact commands are provided for Ahmed to run.

---

## 8. Phases

Wave order: **W0** P00 → **W1** P01 → **W2** P02 → **W3** P03 → **W4** P04, P05, P06 → **W5** P07 →
**W6** P08, P10 → **W7** P09, P11, P12 → **W8** P13, P14, P15 → **W9** P16 → **W10** P17, P18.

### Phase 00: Documentation (W0) — this approval
| Stage | Output | Proof |
|---|---|---|
| 00.1 setup-plan-files (dedicated agent; main thread waits) | Verbatim plan copy and all phase and stage files | `cmp` against `~/.claude/plans/` source; file count matches this section |
| 00.2 repo-structure | `plans/REPO_STRUCTURE.md`: every directory, tier, ownership, dependency rules, how to add a module | Every section 5 path appears |
| 00.3 coding-standard | `plans/CODING_STANDARD.md`: section 4 in full, all type rules below, NASM and CUDA ABI rules | Every R19-R25 rule appears |
| 00.4 speed-research | `research/003-speed-research.md` | Section 3 with sources |
**Gate:** Ahmed reads the docs and says go. **Nothing below starts without it.**

CODING_STANDARD must define at least:
- **Banned:** `int`, `short`, `long`, `unsigned`, `signed`, `float`, `double`, `size_t` in our interfaces, bitfields,
  `packed`, intrinsics, inline asm, `immintrin.h`, VLAs, recursion, `goto` except cleanup.
- **Allowed:** `int8_t...int64_t`, `uint8_t...uint64_t`, `uintptr_t`, `float32_t`, `float64_t` (typedefs of `_Float32`,
  `_Float64`; the single typedef site is `run/include/types/index.h`, with CUDA mapping to `float`/`double` if Stage
  01.1 shows it necessary), `bool` for locals and returns (`uint8_t` in structs), `char` only for text at libc boundaries.
- **Layout:** field order largest-first, `_padN` fields written out, `static_assert` on `sizeof`, `alignof` and shared
  `offsetof`. `alignas(64)` on structs written across threads.
- **File size:** hard limit 2000 lines for `.c .h .cu .hu .asm .inc`; split at 1500; exceeded means a build failure.
- **NASM:** SysV ABI, `default rel`, `vzeroupper` before `ret`, `.note.GNU-stack`, batch APIs only. A single
  vector add in assembly is slower than scalar C, since a call costs more than the work. The C vector library stays
  scalar. Bulk arrays go to `asm/`.
- **CUDA:** `.cu` restricted to a C-like subset. No exceptions, RTTI, `new`/`delete` or STL. Everything crossing the
  boundary is C-compatible with `extern "C"`.

### Phase 01: Feasibility spikes (W1) — risks that could overturn this plan
Throwaway code under `/tmp/`; only results stay.
| Stage | Question | Pass condition |
|---|---|---|
| 01.1 toolchain-link | CMake with C + NASM + CUDA linking through `main.c`? Do `-fno-exceptions -fno-rtti -fno-threadsafe-statics` remove C++ symbols? Static or shared cudart? | Binary runs a kernel; exact link list recorded in the standard |
| 01.2 kms-flip (**Ahmed runs on a VT**) | Dumb buffers, modeset, page-flip events on `nvidia-drm`, with X on another VT? If not, with X stopped by Ahmed? | Solid-color flips at 120 Hz under `timeout 10`, console restored |
| 01.3 ray-throughput | Bitmask DDA on a synthetic R=32 world at 1080p, headless | Primary + sun rays ≤ 8.33 ms, or recorded fallback |
| 01.4 copy-to-scanout | Readback and dumb-buffer write cost | Total ≤ 2 ms |
**Failure routes (Ahmed decides each):** 01.2 fails → X11 raw protocol is the only known alternative. 01.3 misses →
internal render scale with TAA upsampling, or a lower FPS target.

### Phase 02: Build skeleton (W2)
| Stage | Owns | Delivers | Proof |
|---|---|---|---|
| 02.1 | `CMakeLists.txt`, `cmake/`, `tools/`, `.gitignore`, `.claude/rules/` | Presets, mutex, three languages, hello binary | Configure and build on all presets |
| 02.2 | `compile/*/yaml/` | Own YAML subset parser with line-numbered errors | Parser tests, bad-input tests |
| 02.3 | `compile/*/machine_config/`, `config/machine.yaml` | `.h .hu .inc .cmake` generation | Changing a yaml value changes the binary's reported define |
| 02.4 | `compile/*/standards_lint/`, `tests/harness/` | Lint and test harness | Each banned construct has a failing fixture |
**Gate:** `ctest` green; lint rejects a 2001-line file, `int`, padding, and a missing README.

### Phase 03: Core runtime (W3)
| Stage | Owns | Delivers |
|---|---|---|
| 03.1 | `run/*/types`, `assert`, `log`, `main` | Sanctioned types, CPUID check against build flags, entry point |
| 03.2 | `run/*/heap`, `arena` | Reserve/release (2 MiB aligned, `MADV_HUGEPAGE`), bump arenas, live counters |
| 03.3 | `run/*/thread`, `time`, `file` | Pinning, rings, monotonic clock, file wrappers |
| 03.4 | `run/*/job` | Work-stealing job system with per-thread Job arenas |
**Gate:** sanitize preset clean; job stress test on all workers; heap counters zero at exit.

### Phase 04: Math and SIMD (W4)
| Stage | Owns | Delivers |
|---|---|---|
| 04.1 | `run/*/math/*` | vec2/3/4, ivec3, mat4, quat, aabb, ray, all scalar C |
| 04.2 | `asm/*/{copy,blend,bitmask}`, `run/*/simd/*` | NASM conventions, streaming copy, alpha blend, popcount and mask kernels with C prototypes |
| 04.3 | `asm/*/{noise,vec_batch}`, `bench/simd` | 8-wide gradient noise, batch transforms, generated struct offsets |
**Gate:** each NASM kernel matches a scalar C reference in tests; benchmarks prove the gain, or the kernel is removed.

### Phase 05: Display and input (W4)
| Stage | Owns | Delivers | Proof |
|---|---|---|---|
| 05.1 | `run/*/display/device`, `mode`, `flip` | Raw-ioctl card open, connector and mode pick, dumb buffers, page flip | Ahmed: test pattern on a VT |
| 05.2 | `run/*/display/vt_guard` | KD_GRAPHICS, K_OFF, VT_PROCESS switching; **console always restored** on exit, crash signals and watchdog; emergency key combo | Ahmed: deliberate crash returns to a working console |
| 05.3 | `run/*/input/*` | evdev discovery by capability, raw mouse and keyboard, hot-plug | Replay tests, Ahmed live |
| 05.4 | `run/*/display/headless` | No-display backend, PNG screenshot writer (stored deflate) | Agents can inspect output |
**Gate:** Ahmed sees the pattern, moves a cursor square, switches VT and back, and the console survives a forced crash.
Running from inside X fails fast with a clear message.

### Phase 06: Textures (W4)
| Stage | Owns | Delivers |
|---|---|---|
| 06.1 | `compile/*/{inflate,png}` | Own bounds-checked decoder (packs are untrusted input) |
| 06.2 | `compile/*/texture_pack`, `textures/**` | Import from a pinned open pack cloned to `/tmp/mc_c_assets/`, licence check, `ATTRIBUTION.md`, committed outputs so builds need no network |
| 06.3 | `compile/*/texture_vector` | **Vector data per texture:** palette, mean color for LOD, derived height and normal, emissive and alpha class. **Procedural recipes** in `index.yaml` for textures with no pack source |
| 06.4 | `asm/*/embed_assets`, `run/*/texture` | Mip chains, blob embedded through `incbin`, runtime lookup |
Default pack: Pixel Perfection (repo states CC-BY-SA-4.0; ShareAlike applies to derivatives). Licence file and per-pack coverage
are verified in 06.2. VoxeLibre media is licensed file by file and credits some Faithful-derived art, so it is **not**
bulk-imported. Missing mob textures come from our recipes.

### Phase 07: World data (W5)
| Stage | Owns | Delivers |
|---|---|---|
| 07.1 | `compile/*/game_tables`, `config/blocks/*` | YAML → block tables, with hardness, tool, drops, shape, light, texture. About 120 Overworld blocks |
| 07.2 | `run/*/world/section`, `column` | Palette storage, uniform-section shortcut, coordinate helpers, range clamps |
| 07.3 | `run/*/world/map`, `edit` | Toroidal column map, streaming by distance, edit log, dirty flags, snapshots |
**Gate:** random-edit tests vs a brute-force array; `z=-251` and `z=501` rejected; memory per section measured.

### Phase 08: CUDA renderer core (W6)
| Stage | Owns | Delivers |
|---|---|---|
| 08.1 | `cuda/*/device_memory`, `run/*/gpu_bridge` | C ABI, device allocator, error handling |
| 08.2 | `cuda/*/world_buffers`, `run/*/render/encode`, `upload` | Worker section encoding, budgeted upload queue, slab pool |
| 08.3 | `cuda/*/traverse` | Coarse pass and primary rays |
| 08.4 | `cuda/*/shade`, `post`, `frame`, `run/*/render/frame` | Textures, face shade, tone map, readback, present; free-fly camera |
**Gate (first picture):** Ahmed flies a test world on KMS at R=32; golden headless PNGs match; FPS recorded.

### Phase 09: Renderer features (W7)
| Stage | Delivers |
|---|---|
| 09.1 | Non-cube block shapes (slabs, stairs, plants, torches), alpha |
| 09.2 | Sky, day cycle, fog, sun shadows, stored light |
| 09.3 | Water and glass transparency, reflection, depth tint |
| 09.4 | AO with temporal reuse, TAA, optional god rays |
| 09.5 | Entity boxes and per-tile lists, block outline, breaking overlay |
| 09.6 | LOD clipmaps to R=512, with worker-built coarse grids |
**Gate:** each effect has a setting, a measured cost, and a golden image.

### Phase 10: Terrain — all Overworld terrains (W6)
| Stage | Delivers |
|---|---|
| 10.1 | Seeded RNG, per-position hashing, NASM noise integration, fixed-seed determinism across worker counts |
| 10.2 | Climate fields: continentalness, erosion, ridges, temperature, humidity, weirdness |
| 10.3 | 3D density with height splines rescaled to `z ∈ [-250, 500]`; bedrock; sea level 0 |
| 10.4 | Rivers, oceans, beaches, aquifers, lava level |
| 10.5 | Caves (cheese, spaghetti, noodle), ore tables by depth |
| 10.6 | Biome table and surface rules: plains, sunflower plains, all forests, taigas, snowy biomes, ice spikes, swamp, mangrove, desert, savanna, windswept, badlands, jungles, meadow, cherry grove, grove, slopes, all peaks, shores, rivers, all oceans, mushroom fields, dripstone, lush caves, deep dark |
| 10.7 | Features: all tree types, grass, flowers, cactus, sugar cane, ice, snow, lakes, cross-chunk placement as a pure function of seed |
**Gate:** the same seed gives byte-identical columns with 1 and 16 workers; a biome-coverage test finds every biome; bench recorded; Ahmed visually reviews a biome tour.
Structures such as villages and strongholds are a later plan.

### Phase 11: Light and block simulation (W7)
| Stage | Delivers |
|---|---|
| 11.1 | Sky and block light flood fill across sections, incremental updates on edit |
| 11.2 | Water and lava flow, falling sand and gravel, leaf decay, crop and grass spread on random ticks |
**Gate:** incremental light matches full recompute on random edits.

### Phase 12: Player and mining (W7) — **first playable**
| Stage | Delivers |
|---|---|
| 12.1 | Swept AABB collision, walk, sprint, sneak, jump, swim, climb, fall; fixed tick with render interpolation |
| 12.2 | Camera and mouse look, DDA block targeting with outline |
| 12.3 | Timed mining by hardness and tool, drops spawned from block tables, block placing |
| 12.4 | Dropped-item entity, physics, merge, pickup |
**Gate:** Ahmed spawns, walks, **mines dirt and picks up dirt**, and places it back. Tests cover drop tables for every block.

### Phase 13: Items and survival (W8)
| Stage | Delivers |
|---|---|
| 13.1 | Item tables, stacks, hotbar, 36-slot inventory |
| 13.2 | 2×2 and 3×3 crafting, full basic recipe set, furnace, chest |
| 13.3 | Tools with durability, armor |
| 13.4 | Health, hunger, food, damage, death and respawn, bed |

### Phase 14: UI and settings (W8)
| Stage | Delivers |
|---|---|
| 14.1 | Bitmap font, widgets, NASM-blended composition |
| 14.2 | HUD: crosshair, hotbar, hearts, hunger, debug overlay with FPS and per-stage times |
| 14.3 | Inventory, crafting, furnace, chest screens |
| 14.4 | Main menu, world create and select, pause |
| 14.5 | **Settings:** render distance (2-512 chunks), full-detail radius, FOV, sensitivity, vsync/uncapped, each graphics effect, render scale, worker count (up to the compile-time maximum); saved by our own runtime parser in `run/*/settings`, since `compile/` is not linked at runtime |
**Gate:** changing render distance in the menu takes effect without restarting, and persists.

### Phase 15: Mobs (W8)
| Stage | Delivers |
|---|---|
| 15.1 | SoA entity store, spatial grid, shared AABB physics, part-based animation data |
| 15.2 | Bounded A* pathfinding, batched on workers, with path caching |
| 15.3 | Spawning by light level, surface and distance; despawn rules |
| 15.4 | **Zombie:** chase, melee, drops |
| 15.5 | **Skeleton:** keep distance, arrows as projectiles, drops |
| 15.6 | **Creeper:** approach, fuse, ray-based explosion breaking blocks and dealing damage, drops |
| 15.7 | Player attack, knockback, mob damage and death |
**Gate:** bench with 100 mobs meets target; Ahmed survives (or doesn't) a night.

### Phase 16: Saving (W9)
| Stage | Delivers |
|---|---|
| 16.1 | World files from seed + changed-section deltas (research/001), versioned header, crash-safe rename |
| 16.2 | Player, inventory, entity and time persistence; background autosave with no frame hitch |
**Gate:** kill during save loses at most one autosave interval; reload matches byte for byte.

### Phase 17: Performance hardening (W10)
| Stage | Delivers |
|---|---|
| 17.1 | Built-in profiler: per-thread scopes, kernel timers, trace dump |
| 17.2 | Standard benchmark worlds and paths; results history in `bench/` |
| 17.3 | Pass over top costs: memory layout, ray coherence, tile order, thread placement, upload budget |
**Gate:** section 2 target table filled in with measured numbers, misses stated plainly.

### Phase 18: Release hardening (W10)
| Stage | Delivers |
|---|---|
| 18.1 | Sanitizer and fuzz runs (YAML, PNG, saves); multi-hour soak |
| 18.2 | Failure handling: GPU loss, device unplug, disk full, VT switch during load |
| 18.3 | Audit: every README accurate, REPO_STRUCTURE matches tree, all todos truthful, licences complete |
| 18.4 | Portability seams documented for later platforms; CPU-only render backend recorded as future work |
**Gate:** Ahmed plays a full session and signs off.

### Later plans (out of scope here)
Nether, End, redstone, structures, passive animals, audio, multiplayer, a CPU render backend, other operating systems.

---

## 9. Risks

| Risk | Likelihood | Handling |
|---|---|---|
| `nvidia-drm` dumb buffers or flips misbehave | Medium | Stage 01.2 before any code depends on them |
| A crash leaves the console unusable | Medium | VT guard, watchdog, `timeout` in early runs; recover with SSH or a reboot if needed |
| 120 FPS unreachable with all effects | Medium | Stage 01.3, render scale, per-effect settings |
| Only Ahmed can test KMS | Certain | Headless backend, explicit user gates |
| CUDA pulls in libstdc++ | Medium | Measured in 01.1, recorded as a documented exception |
| Texture pack lacks mob art or licence is unclear | Medium | Procedural recipes, licence check before import |
| Parallel agents collide | Medium | Per-stage path ownership, worktrees, headers merged first |
| Scope is very large | High | First picture at P08, first playable at P12; every gate is a usable stopping point |

---

## 10. Verification

**Phase 00 (this approval):**
```bash
cmp ~/.claude/plans/pasted-content-id-a5ab-ahmed-admin-mine-serialized-fiddle.md \
    plans/001-ahmed-bohemoth-minecraft-from-scratch.md
ls plans/ | grep -c '^001-ahmed-bohemoth-'
grep -l 'not started' plans/001-*-phase-* | wc -l
git log -1 --format=%B | grep -iE 'co-authored|anthropic|claude|opus|sonnet'   # must print nothing
```
Then a manual check that every R1-R27 row maps to real content in the documents.

**Whole project, final path:**
```bash
cmake --preset release
tools/test-or-build.sh cmake --build --preset release
tools/test-or-build.sh ctest --preset release
# Then Ahmed, on a text console (Ctrl+Alt+F3, log in):
./build/release/bin/minecraft_c
```
Ahmed always starts the game. Agents only run bounded headless tests and confirm no process remains:
`pgrep -a minecraft_c` prints nothing.
