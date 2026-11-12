# 009-ahmed-bohemoth-game-engine-optimizations — master plan

**Status:** draft for approval (2026-11-12). Research and plans only: on approval, `plans/README.md` applies. Phase files 00 to 08 exist beside this file (section 11). Phase 00 is done and the rest are `not started`. **No code is written until Ahmed says go.** Nothing is pushed.

## 0. Context

### Ahmed's words, 2026-11-12

- "Please look at ~/Pictures/5.png"
- "Please read ./research/001.md"
- "Look at the pause menu"
- ""Full detail" should never ever be a thing! if there are is a 2x2x1 group of blocks, they should be rendered only as triangle lines of the larger rectangle that it has created as a result of being one shape!!"
- "this should be for all blocks, even blocks that are close to us!! resolution drops from really close to us!! at all times 16 blocks in all directions **VIEWABLE** from the user must be rendered properly! and further optimization as we move away!!"
- "Please only extremely thoroughly research this! research rendering optimization on top of engines like unreal engine and godot! we are only writing these extreme optimizations to ./plans/009-ahmed-bohemoth-game-engine-optimizations.md please"
- "config settings should be read at compile time from a yaml file so that we can very very easily change things up! and fine tune what we need to!!! I think maybe at ./compile/config/<FILEPATH> yaml files! this way in the future we can very easily change numbers to get the sweet spot of exactly what we need!!"

The passage of `research/001.md` he pointed at (lines 98 to 125) is the Unity video's level-of-detail and greedy meshing section: "farther chunks could get away with generating one block per 8 cub m. Even farther chunks shall have progressively lower resolution, eventually reaching the single block per chunk", and "adjacent squares can be combined into larger rectangles to reduce triangle count".

### What the picture shows

`~/Pictures/5.png`: 1920 × 1080, night, on a mountainside looking up along the slope. The near right third of the picture is a dark tree (leaves). The top quarter is sky. The rest is rock and grass slope running away from the eye at a low angle, out to a ridge several hundred blocks off. In this game z is height, so `x -385 y 347 z 2` is 2 blocks above sea level, at the foot of the slope. `sun -13.1` means the sun is 13.1° below the horizon, so the light is the moon, 13.1° above the opposite horizon (`run/src/render/frame/README.md`, "The day").

| F3 line | Value | What it is (read from the code) |
|---|---|---|
| FPS | **31.9** | Pictures main took in the last second (`game_fps_note`) |
| coarse | 1.67 ms | The beam pass: one safe start distance per 8 × 8 tile (`traverse_coarse_kernel`, `cuda/src/traverse/index.cu:207`) |
| primary | **10.41 ms** | Primary rays, entity rays **and the far field's rays** (`cuda/src/frame/index.cu:5-15`; the far field is counted as primary, `cuda/src/frame/README.md`) |
| shade | **14.62 ms** | Water, surfaces with their alpha layers, sun (here moon) rays, AO and fresh AO, all one number |
| post | 1.40 ms | Post, TAA, outline |
| readback | 1.34 ms | The synchronous copy of the 8.3 MB picture home |
| gpu total | **29.44 ms** | The sum, on the card's clock, last picture only |
| sim tick | 3.21 ms | |
| hud | 1.05 ms | |
| present | 0.42 ms | |
| mobs 4, drops 1, uploads 0, far jobs 0 | | Nothing streaming: the world is settled |

His settings (`settings.yaml`): render distance **64**, full detail **32**, FOV 70° vertical, render scale 100 %, vsync on, every effect on except god rays, far field on.

### What "Full detail" means in this engine today

This engine draws no triangles. It is CUDA voxel ray casting (plan 001, section 0, Ahmed's decision of 2026-11-01): one ray per pixel walks a three-level bitmask tree with Amanatides and Woo's grid stepping, and the first voxel it meets is the pixel. There is no mesh, no greedy mesher and no triangle count anywhere in the program.

"Full detail" is one number, in chunks, and it decides **three things at once**:

1. **What is held block by block.** `game_view_work_out` (`run/src/game/view/index.c:38-51`) clamps it to the table, and `world_map_set_view_radius` (`run/src/game/index.c:412`, `:856`) makes the map generate, light and upload every column within it at 1 voxel = 1 block. That goes to the card's column table and slab pool (`run/include/gpu_bridge/world_buffers/index.h`: a column of 96 sections, each 16³, each a 64-bit mask of 4³ bricks, each a 64-bit mask of voxels). The table and pool are sized once, when a world opens (`game_config_t.table_chunks`, `run/src/game/README.md`), which is why the slider says `(new world)` past it.
2. **Where the far field starts.** `render_lod_plan(full_detail, render_distance)` (`run/src/render/lod/index.c:61-84`) keeps only the coarse levels whose reach is past full detail. A level is a world of the same format whose voxel is 2<sup>shift</sup> blocks each way, built on workers by sampling terrain (`run/src/render/lod/README.md`). At his 32 / 64:
   - blocks out to 32 × 16 = **512 blocks**;
   - shift 1 (2-block voxels) is dropped: its reach, (16 − 2) × 16 × 2 = 448, is under 512;
   - **shift 2 (4-block voxels)**, radius 16 columns, reach (16 − 2) × 16 × 4 = 896 blocks;
   - **shift 3 (8-block voxels)**, radius ⌊1024 / 128⌋ + 3 = 11 columns, reach (11 − 2) × 16 × 8 = 1152 ≥ 1024, and the plan stops.
3. **How rays hand over.** A primary ray walks the block world first (`traverse_trace_stopping`, `cuda/include/traverse/index.hu:305-455`). A ray that hits nothing, or that shade will look past (water, glass, leaves), then goes to the far field (`lod_past_kernel` and `lod_frame_kernel`, `cuda/src/lod/index.cu:305`, `:362`). The far ray starts **at the eye** and, inside full detail, passes by each coarse hit that lands in a column full detail holds, one 16-block column at a time, with up to `LOD_SKIPS_MAX` = 32 fresh traversals per level (`lod_trace` and `lod_full_detail_at`, `cuda/src/lod/index.cu:79-155`).

So "full detail" is a hard ring: inside 512 blocks every block is a voxel, whatever its size on the screen; outside it, 4-block voxels at once. That is exactly what Ahmed objects to.

**How big a block is on his screen.** At 1080 pixels high and 70° vertical, one pixel is θ = 2 tan(35°) / 1080 = **1.297 × 10⁻³ rad**, so a block d blocks away covers 1 / (d θ) = **771 / d pixels** across:

| Distance (blocks) | 16 | 64 | 128 | 256 | 512 | 1024 |
|---|---|---|---|---|---|---|
| One block on screen (px) | 48 | 12 | 6.0 | 3.0 | 1.5 | 0.75 |

Today the ring puts full blocks at 512 (1.5 px each) and then 4-block voxels right after it, whose surface can be up to 2 blocks off: **3 px of error at 513 blocks, against 0 at 511.** The ring is too fine inside and too coarse just outside, at the same time.

### Why the frame is 29 ms

Ranked by what it can explain. **Proven** means from the code with arithmetic; **suspected** means phase 01 must measure it.

1. **The card runs at about a third of its clock (measured by plan 008).** `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md` section 0, cause C1: the laptop's card is held to 50 W of 200, at 570 to 675 MHz of 2100, with "SW Power Cap" and "SW Thermal Slowdown" active. So 29.44 ms on this card is roughly **8.5 to 10 ms on a cool card.** Code cannot lift that cap; every target here is set on the card as it is.
2. **Primary, 10.41 ms (suspected: grazing rays, far rays, leaves).**
   - **Grazing rays along the slope.** A ray that skims a surface at a low angle crosses bricks that hold voxels, and inside such a brick the loop steps **one voxel at a time** (`shift = 0` at `index.hu:418`), through air as well as rock. Empty bricks step by 4 and empty sections by 16, but a slope seen side-on is a long band of half-full bricks. The 64-tree write-up below found exactly this: "grazing rays, especially on flat surfaces not aligned to cell boundaries, are the main cost."
   - **The far field at render distance 64.** The top quarter of the picture is sky: every sky pixel sends a far ray that starts at the eye and passes by full detail's columns. Plan 005 phase 8 measured a far field at 64 adding 2.9 ms (plains) to 12 ms (water) to the primary pass on the bench, even after far rays were limited to where shade will look.
   - **Leaves on the right.** Each leaf pixel asks `lod_past_kernel` whether shade's ray gets out of full detail, a `traverse_trace_stopping` through the canopy, leaf voxel by leaf voxel (plan 005 phase 8: about 2.5 ms on `jungle/orbit`).
3. **Shade, 14.62 ms (suspected: the moon's shadow rays, alpha layers, AO).**
   - **Moon rays at 13° elevation, uncapped.** Every surface facing the moon sends one ray toward it (`shade_sun_seen`, `cuda/src/shade/index.cu:28-100`). It has no length limit and walks block voxels all the way to the edge of full detail. At 13° elevation over a slope it grazes the terrain for hundreds of blocks, through up to `SHADE_SUN_LAYERS` = 8 surfaces of leaves. The moon's direct light is dim in absolute terms (0.05 to 0.10, `render_sky_fill`, `run/src/render/frame/index.c:387-389`), but it is still more than the night's ambient (0.03 to 0.07), so its shadows do show and cannot simply be dropped.
   - **Leaves.** A ray landing on a leaf texel of alpha 0 starts a fresh trace from that hole, up to `SHADE_ALPHA_LAYERS` = 16 surfaces (`run/include/gpu_bridge/shade/index.h:35`). A third of this picture is leaves.
   - **AO**: one ray per pixel one frame in four, plus fresh rays for newly seen pixels (plan 008 C3 covers the turning case).
4. **Coarse, 1.67 ms (proven small).** Tiles looking along the slope stop early, as the stretch's box holds the ground.
5. **Post 1.40, readback 1.34 (plan 008 phase 03 overlaps the readback).**

Ruled out: triangle count, draw calls, overdraw and frustum culling. None exists in a ray caster: a ray only goes where the camera looks, and stops at the first opaque voxel, so a buried block costs nothing.

### Where this plan sits

- **Plan 008** (smooth frames when turning) is planned and not built. It owns:
  - the instruments: per-kernel clocks inside shade, the frame log, the card's MHz, the shake bench, `--at X Y Z --seed S`;
  - pacing (008 phase 02);
  - the second stream for readback (008 phase 03);
  - corner AO and the world-space AO cache (008 phase 04);
  - adaptive resolution with a bilinear upscale (008 phase 05);
  - a far-field turn cache (008 phase 06).

  This plan **reuses all of that and builds none of it again**. It changes **what a ray has to step through**: levels, boxes, secondary rays, temporal upscaling. Plan 008's Q9 left temporal upsampling "as a later plan"; it is this plan's phase 07. Order: 009's code phases rebase onto whatever of 008 has landed, and 009 phase 01 waits for 008 phase 01's instruments, or adds only its own counters (phase 00 records the order).
- **Plan 006** (biomes follow the climate) is being built in another worktree and edits `run/src/game/index.c` and F3 (its phase 01). 009 touches `run/src/game/view/`, `run/src/game/index.c` (map radius) and the Settings screen in phase 04, so phase 04 rebases onto 006 phase 01.
- Plans 006 and 008 are not edited by this plan.

---

## 1. Names used in this plan

| Name | Meaning |
|---|---|
| **Level k** | A grid whose voxel is s<sub>k</sub> = 2<sup>k</sup> blocks each way. Level 0 is the blocks themselves |
| **The pyramid** | Levels 0 to K, each centred on the eye, all in the one `world_buffers` format, each built from the one below it where that is loaded, and from terrain where it is not |
| **Error ε<sub>k</sub>** | How far a surface at level k can be from the true surface: s<sub>k</sub> / 2 blocks, by the half rule of `render_lod_cells_from_column` |
| **Pixel error E** | The most pixels a coarser level may move a surface on screen. A tuning number (section 6) |
| **Band k** | The distances along a ray where level k is used: from d<sub>k</sub> to d<sub>k+1</sub> |
| **The near guarantee** | Ahmed's rule: every surface within 16 blocks of the eye, in any direction, that a pixel sees is drawn from level 0 |
| **Steps** | Iterations of the ray loop. A ray caster's cost is pixels × steps, not triangles |
| **Tuning YAML** | `compile/config/<path>/index.yaml`, turned into `MC_TUNE_*` defines at build time |

---

## 2. Where I disagree, or read Ahmed's words a particular way

### 2.1 "Triangle lines of the larger rectangle": a ray caster has no triangles, but it has the same idea

Greedy meshing merges coplanar faces so the rasterizer draws fewer triangles. Here nothing is rasterized. **A ray stops at the first solid voxel it meets**, so merging four solid blocks into one box does not make a ray hitting them any cheaper: it was one test either way.

What does cost in a ray caster is **the cells a ray steps through before it hits**, and **the extra rays** (shadow, AO, alpha, far). So the faithful translation of Ahmed's 2×2×1 rule is:

- **Merge what a ray steps through.** Empty space merged into larger cells lets a ray cross it in one step: 2×2×2 coalescing and per-brick distances (phase 05). A body of identical see-through voxels, such as a lake of water, merged into one box lets the water walk cross it in one step instead of cell by cell (phase 05). This is where "one shape instead of many" pays here.
- **Merge blocks into coarser voxels by distance** (the pyramid, phases 03 and 04). That is the level-of-detail half of `research/001.md`: "one block per 8 cub m … progressively lower resolution". A coarser voxel is literally the 2×2×2 group "rendered as one shape".

Phase 01 measures steps, so this claim is checked, not assumed.

### 2.2 "Resolution drops from really close": yes, by a rule, and the rule says how close

Unreal's Nanite replaces a cluster with a coarser one only when the change is under a pixel. Godot's mesh LOD threshold is 1 pixel by default. Both stop "close" from meaning "visibly wrong". This plan does the same, with E in the tuning YAML.

A level k is allowed from distance d<sub>k</sub> = ε<sub>k</sub> / (E θ) = s<sub>k</sub> / (2 E θ) = **385.6 s<sub>k</sub> / E blocks**, and never inside 16 blocks:

| E (px) | Level 1 (2 blocks) from | Level 2 (4) from | Level 3 (8) from | Level 4 (16) from |
|---|---|---|---|---|
| 1 | 771 | 1542 | 3085 | 6170 |
| **2 (proposed default)** | **386** | **771** | **1542** | 3085 |
| 4 | 193 | 386 | 771 | 1542 |
| 8 | 96 | 193 | 386 | 771 |
| Today, as a ring | 512 jumps straight to 4 blocks (3 px), and 8 blocks at 896 (3.4 px) | | | |

**The honest consequence:** at 1080p and 70°, a block is still 1.5 px wide at 512 blocks. So a 1-pixel rule keeps blocks out to 771, further than today. "Resolution drops from really close" holds only with E of 4 to 8 px, where 2-block voxels start at 96 to 193 blocks and are visibly coarser there. So E is a number Ahmed tunes by eye in the YAML. The default is 2 px, under the TAA's blur and close to what Nanite accepts, and every row of the table is one edit away.

The levels' distances change with FOV and resolution, because θ does: a narrow FOV zooms in, so finer levels go further. They are worked out every frame from the camera, not stored.

### 2.3 Distance-based levels alone are not the big win at his settings

At E = 2 px the bands inside render distance 64 (1024 blocks) are level 0 to 386, level 1 to 771, level 2 to 1024. That is little coarser than today's ring and finer past 512. The speed comes from five things together, each measured in phase 01 before it is built:

1. **No pass-by.** One pyramid with hand-over by distance removes the far field's 32-traversal pass-by of full detail's columns.
2. **Coarser steps for grazing rays** past 386 blocks.
3. **Merged boxes** for empty space and water.
4. **Secondary rays by level:** a shadow ray far from its surface needs no block-level detail (section 4.5).
5. **Temporal upscaling:** fewer rays per picture, the same 1080p picture after accumulation.

### 2.4 "Full detail" goes away as a setting; a block radius stays as an outcome

Ahmed: "Full detail" should never ever be a thing. **The slider, the settings key, `--full-detail` and the word all go.**

But the game still needs *some* blocks held at level 0: the sim, mining, collision and the near guarantee all need real blocks. So the map's load radius stays, as **a number worked out from the pixel error, the FOV and the resolution**: r<sub>0</sub> = max(16, d<sub>1</sub> + margin). It is never a thing a person sets. At E = 2, FOV 70°, 1080p, that is 386 blocks + 2 chunks of margin = **27 chunks**, against 32 today. Render distance stays as the one distance setting.

### 2.5 "16 blocks in all directions VIEWABLE … rendered properly", read precisely

Every pixel whose surface lies within 16 blocks of the eye is drawn from level 0, bit for bit as today's renderer draws it. That covers primary rays and what shade's own ray sees through water and glass. It is a sphere, not a cube, so it holds in every direction, up and down too.

A test renders each picture twice, with the pyramid and with level 0 everywhere, and requires every pixel whose reference hit is under 16 blocks to be byte-identical (section 8). "Properly" beyond 16 blocks means within E pixels.

### 2.6 Stay a ray caster

**Recommendation: stay a pure CUDA ray caster.** Reasons, for this codebase:

- The coding standard bans OpenGL, Vulkan and OptiX (`plans/CODING_STANDARD.md` section 1). So the card's hardware rasterizer, its RT cores, DLSS (which needs DirectX or Vulkan through NGX) and hardware variable-rate shading are all out of reach. CUDA has no rasterizer.
- A software rasterizer on the GPU runs 2 to 8 times slower than the hardware one (Laine & Karras 2011). Nanite's software path wins only for triangles smaller than a few pixels.
- A hybrid would hold two representations of the world, greedy meshes for near and voxels for rays, and still need rays for shadows, AO, water and the far field. That means double the memory, two upload paths, and seams between the two kinds of picture.
- A ray caster's cost is pixels × steps and **does not grow with triangle count**, which is the thing greedy meshing fights. Its level of detail cuts steps, which is what this plan does.
- What a hybrid buys, cheap primary visibility, the coarse pass already gives most of (Laine & Karras's beam optimisation: 97.4 to 123.8 Mrays/s on their city scene), and phase 05 sharpens it.

**Researched and left out:** splatting far voxels with a CUDA software splatter (Nanite's 64-bit `atomicMin` visibility buffer applied to level ≥ 3 voxels of 2 px or less). It might beat rays crossing far grids. But it needs its own culling, leaves holes at grazing angles, and still needs rays for shadows. Section 10, out of scope; a later plan if phase 08 leaves the far field the top cost.

### 2.7 `compile/config/` against the repository's rule

`plans/REPO_STRUCTURE.md` section 2 says `config/` holds YAML and `compile/` holds C tools. Ahmed's path puts YAML under `compile/`. There are two readings:

- **A, Ahmed's path (default):** `compile/config/<path>/index.yaml`. The name says "compile-time configuration", and that is the point. The lint does not object: a directory with no code has no module rules (`compile/src/standards_lint/rules_tree/index.c`, `tree_check_directory` returns when `code_count == 0`). REPO_STRUCTURE gains a row.
- **B:** `config/tuning/<path>/index.yaml`, beside `config/machine.yaml`.

Default **A**, with the REPO_STRUCTURE row, asked as Q1.

---

## 3. Research: what engines and papers do, and what each means here

Every row is marked **sourced** (a source I read, cited in section 13) or **judgment** (my reasoning, to be measured). "Here" says what it becomes in this engine.

### 3.1 Unreal Engine 5

| Technique | What it does | Numbers | Here |
|---|---|---|---|
| **Nanite cluster DAG** | Meshes are cut into ~128-triangle clusters. Groups of clusters are simplified with their borders locked and split again, building a DAG. At run time a GPU pass picks the cut where the parent's error is over the threshold and the cluster's is under it | Error threshold "less than a pixel"; with TAA, popping under 1 px is not noticed. Software raster for small triangles about 3× faster than hardware on average | **Sourced.** The pyramid with E in pixels, and the per-ray choice `parent error > E ≥ level error`, are this rule for voxels. Borders locked becomes levels built from the level below, so they nest (phase 03) |
| **Nanite two-pass HZB occlusion** | Test against last frame's depth pyramid, draw, build the new one, re-test what was culled | Boxes tested at the mip where they fill ≤ 16 px. Newly exposed areas get no culling until the next frame | **Sourced.** A ray caster needs no occlusion culling: a ray stops at the first hit. The analogue that matters is the coarse pass (a conservative start distance per tile), which phase 05 runs on the pyramid's coarse levels |
| **Lumen** | Short screen-space traces first. Then per-mesh distance fields near, a coarse **global distance field in clipmaps** far, then a surface cache for lighting | "a coarser, scene-wide version … when it travels far from its origin" | **Sourced.** The same shape as section 4.5: a secondary ray starts fine and moves to coarse levels as it travels. Lumen's global SDF clipmaps around the camera are the pyramid's levels |
| **Virtual shadow maps** | 16k × 16k virtual shadow maps in 128 × 128 pages. Clipmaps for directional lights. Only invalidated pages are redrawn | 16k virtual, clipmap level per range | **Sourced.** No shadow maps here (one ray per surface). Kept: resolution by distance (shadow rays by level, phase 06) and caching what did not change (plan 008's AO cache; a shadow cache is Q8) |
| **TSR / TAAU** | Render below output resolution with jitter, and accumulate at output resolution over frames | UE5's default. "Screen percentage lower than 50 % only for > 4K or dynamic resolution" | **Sourced.** Phase 07: 75 % per axis internally (56 % of the rays), accumulated at 1080p, on top of plan 008 phase 05's resolution ladder |

### 3.2 Godot 4

| Technique | What it does | Numbers | Here |
|---|---|---|---|
| **Mesh LOD** | LODs made by meshoptimizer on import. Chosen at run time by a **screen-space metric**, from the AABB's nearest point, with FOV and resolution counted | `mesh_lod_threshold` default **1 pixel**, "perceptually lossless"; `lod_bias` per node | **Sourced.** E is that threshold. A per-material bias is Q6 (leaves and water may want a different E) |
| **Visibility ranges (HLOD)** | Swap groups of objects for one stand-in by distance, with hysteresis, or an alpha / dithered fade | Fade forces transparency; dither is the cheap way | **Sourced.** The pyramid's band edges are dithered per pixel and per frame, so TAA blends them (section 4.4) instead of a visible ring |
| **Occlusion culling** | CPU raster (Embree) of baked occluders into a small buffer; AABBs tested | Best indoors and on the mobile renderer | **Sourced.** Not needed by a ray caster. Underground costs nothing already |
| **SDFGI** | Signed distance field cascades that follow the camera; more cascades = further and dearer, dearer still when moving fast | "Cascade 0 distance", "max distance" | **Sourced.** Another precedent for nested coarse grids around the eye, tuned by a few distances: what the tuning YAML exposes |

### 3.3 Minecraft renderers

| Technique | What it does | Numbers | Here |
|---|---|---|---|
| **Greedy meshing, binary greedy meshing** (`research/001.md`, cgerikj) | Merge coplanar faces of one block type into rectangles, with 64-bit column masks | 50 to 200 µs per chunk; about 4× faster with masks (`research/003-speed-research.md`) | **Sourced.** No meshes here. The mask tricks carry over: 2×2×2 coalescing tests a 64-bit brick mask against `0x00330033` (phase 05) |
| **Distant Horizons** | Snapshots chunks as they leave range into simplified LOD stand-ins, a quadtree of detail levels by distance, generated coarse first and sharpened later. Culls LOD faces covered by other LODs | Release notes only, no published metric | **Sourced.** Today's far field already does the coarsest-first fill (`run/src/render/lod/README.md`, "Order"). New: levels built from the loaded world, so edits show at once (phase 03) |
| **Voxy** | LOD sections voxelized into its own format, streamed in a spiral, from the world or the seed | Needs OpenGL 4.6 | **Sourced.** Spiral nearest-first is what the far field does. Nothing new to take |
| **Sodium, Nvidium** | Chunk-graph occlusion and compact vertices; Nvidium culls on the GPU with mesh shaders | (`research/003-speed-research.md`) | **Sourced.** Not applicable: no triangles and no graphics pipeline |
| **Transvoxel** (Lengyel) | Transition cells between LODs of smooth marching-cubes terrain | 512 cases in 73 classes | **Sourced.** For smooth surfaces only. Cubes have no seams to stitch; the seam here is a band edge, handled by dithering and nesting |

### 3.4 Voxel ray casting

| Technique | What it does | Numbers | Here |
|---|---|---|---|
| **Amanatides & Woo 1987** | Grid stepping: two compares and one add per step | | **Sourced.** The loop in `traverse_trace_stopping` |
| **Laine & Karras, ESVO (2010)** | Sparse voxel octree with contours; a **beam pre-pass** gives each block of rays a safe start | City 2048 × 1536: 97.4 → 123.8 Mrays/s with the beam pass; 60.9 Mrays/s primary on a 2.7 GB scene | **Sourced.** The coarse pass exists. Phase 05 lets it step coarse levels far away |
| **Kämpe, Sintorn, Assarsson, SVDAG (2013)** | Merge identical subtrees of an SVO into a DAG | Node count down 1 to 3 orders of magnitude; Epic Citadel 128K³, 19 G voxels in 945 MB against 5.1 GB for the SVO without pointers | **Sourced.** Memory, not steps. VRAM is not this plan's limit (2.5 of 8 GiB at most in `bench_world`). Not taken |
| **Aokana (2025)** | Many shallow SVDAG chunks, LOD by `size × StreamingFactor − distance`, 8 × 8 tiles against chunk bounds, two-pass Hi-Z, visibility buffer | 64K world (~10 G voxels) at about 6 ms a frame on an RTX 3060 Ti; 2 to 4× faster than HashDAG past 32K; about 5 % of the scene in VRAM | **Sourced.** Confirms the shape: shallow trees (ours is 3 levels), 8 × 8 tiles (ours), distance-chosen LOD. Its LOD rule is distance-only; ours is in pixels so FOV and resolution count |
| **Sparse 64-trees** (dubiousconst282, 2024) | 4³ branching with 64-bit child masks, the same as this engine's bricks. Ancestor memo, **2³ coalescing**, octant mirroring | Ancestor memo ~2× (16903 → 8896 cycles a ray); **2³ coalescing 21 %**; mirroring 10 %; unaligned / anisotropic skipping only 2 to 5 %; **grazing rays are the main cost** | **Sourced.** Phase 05 tries 2³ coalescing first (its best cheap win). Mirroring is a candidate. The 2 to 5 % for anisotropic skipping is why per-brick distances are an experiment, kept only if they measure |
| **Brickmaps** (van Wingerden 2015) | Layered grids of bricks, raw and editable, streamed, basic LOD | | **Sourced.** This engine is a brickmap already (column → section → brick → voxel). Nothing to change in kind |
| **Teardown** | Each object's box rasterized, a voxel DDA inside it with **mip levels as a dense octree**. World occupancy packs 2×2×2 voxels per byte, 3 mips. Ambient rays step mips coarser as they travel; sun rays adapt the mip ("hits reduce the mip, misses increase it") | 1252 × 128 × 1252 bytes for 2504 × 256 × 2504 voxels; ambient rays end at 24 units | **Sourced.** The two most useful ideas for phase 06: secondary rays move to coarser occupancy as they travel, and "miss → go coarser, hit → go finer" |
| **Chebyshev distance maps** (Deakin & Knackstedt 2020) | Per cell, the Chebyshev distance to the nearest occupied cell; anisotropic variants per direction | Called the fastest known empty-space skipping in a later comparison (35 % ahead of the alternative) | **Sourced.** Phase 05 experiment: one byte per brick, distance in bricks to the nearest occupied brick |
| **Merging runs into boxes for rays** | The ray tracer's greedy meshing: maximal empty or uniform boxes, so a ray leaves a box in one step | | **Judgment.** Phase 05's uniform-water bricks, and the distance field, which approximates maximal empty boxes cheaply |
| **Cone / beam tracing; mip-mapped occupancy** | A ray's footprint grows with distance; sample coarser data as it does (GigaVoxels, Teardown) | | **Sourced** idea, **judgment** use. The pyramid is mip-mapped occupancy; section 4.5 gives the cone rule for shadows |

### 3.5 Temporal and resolution techniques

| Technique | Numbers | Here |
|---|---|---|
| **TAAU, TSR, FSR 2** (FSR 2 is MIT on GPUOpen, HLSL; it needs this frame's and the last frame's data) | UE5 default; FSR 2 open | **Sourced.** Phase 07 writes its own CUDA temporal upscaler from the published ideas: jitter, reprojection, neighbourhood clamp, upsample at output resolution. No library is linked (coding standard section 1) |
| **Checkerboard rendering** (El Mansouri, *Rendering Rainbow Six Siege*, GDC 2016) | Half the pixels a frame; reprojection with neighbourhood min/max clamp | **Sourced.** A phase 07 experiment against TAAU at 70 %. Kept only if better per ms |
| **Variable rate / adaptive shading** (NVIDIA Turing VRS; Adaptive Shading claims up to 15 %) | Hardware VRS exists only through graphics APIs | **Sourced.** Out of reach in CUDA as hardware. A software form (one shade per 2 × 2 on flat, low-contrast tiles) is Q7, after phase 07 |
| **Async readback** | | Plan 008 phase 03 owns it |
| **Dynamic resolution** | | Plan 008 phase 05 owns it; phase 07 replaces its bilinear upscale with temporal |

### 3.6 What the research does not settle

- No source measures **a 16³ / 4³ brickmap with a pixel-error pyramid** against a hard ring. The gains in section 5 are judgment, from step counts, and phase 01 measures the real distribution.
- Nobody publishes the image cost of a given E for blocky terrain. Ahmed judges E by eye at the phase 04 gate, with the reference-image metric of section 8 beside him.

---

## 4. Design

### 4.1 Measure first (phase 01)

Nothing is built until the causes in section 0 have numbers on his card, at his spot:

- **His view as a bench row.** Through plan 008 phase 01's `--at X Y Z --seed S`, add a yaw / pitch and a sun elevation, so `x -385 y 347 z 2`, night, sun −13.1°, render distance 64 is a settled row. Its seed is Ahmed's, Q2.
- **Steps by distance band.** Each primary hit already carries its step count (`TRAVERSE_INFO_STEPS`). A bench-only kernel histograms steps per pixel into bands 0–16, 16–64, 64–128, 128–256, 256–512 and 512–1024, plus far-field levels. The same for shadow, AO and alpha rays, behind a counters pointer that is null in play.
- **A reference mode.** Level 0 out to render distance (full detail = render distance), for image comparison, offline only.
- **One experiment per cause:**
  - shadows off;
  - far field off;
  - alpha off;
  - AO off;
  - render scale 75 %;
  - **full detail 16, 24, 32, 48 at render distance 64**: today's settings already show what moving level 0's edge is worth;
  - a step heatmap PNG of his view.

### 4.2 Compile-time tuning YAML (phase 02)

**Layout** (Q1 default A):

```
compile/config/README.md                    what lives here, the schema, the workflow
compile/config/render/README.md
compile/config/render/lod/index.yaml        pixel error, near guarantee, band dither, level count, build margins
compile/config/render/traverse/index.yaml   coarse-pass stretches, coalescing, distance-field cap
compile/config/render/shade/index.yaml      AO turns, rays, samples, reach; alpha and sun layers; shadow cone; dim-light cut
compile/config/render/temporal/index.yaml   TAA clamp and blend; upscale ladder; jitter length
compile/config/render/budget/index.yaml     upload and far-field bytes a frame, far-field jobs
```

**Each key describes itself**, so adding a number means editing YAML and using the define, and nothing else:

```yaml
# compile/config/render/lod/index.yaml
pixel_error:
  value: 2.0
  type: f32
  unit: pixels
  min: 0.25
  max: 16.0
  why: "A coarser level is used only where it moves a surface by at most this many pixels on screen."
near_guarantee:
  value: 16
  type: u32
  unit: blocks
  min: 16
  max: 256
  why: "Ahmed, 2026-11-12: every surface seen within this many blocks is drawn from the blocks themselves."
band_dither:
  value: 0.08
  type: f32
  unit: ratio
  min: 0.0
  max: 0.25
  why: "A band edge moves by up to this share of its distance per pixel and frame, so TAA blends it."
```

**The tool:** `compile/src/tuning/` (a new Owner, `index.c` with `main`, README, `compile/include/tuning/index.h`), using `compile/src/yaml/`.

- **Input:** every `compile/config/**/index.yaml`.
- **Output:** for each, `build/<preset>/generated/tuning/<path>/index.h` (C, and the `gpu_bridge` headers nvcc reads too) and `index.hu` (CUDA-only code), as `machine_config` writes both.
- **Defines:** `MC_TUNE_<PATH>_<KEY>`, so `render/lod` `pixel_error` is `MC_TUNE_RENDER_LOD_PIXEL_ERROR` and greps find it. A `u32` is written `16u`, an `f32` `2.0F` (the exact decimal the YAML says, via `yaml_get_decimal`), a `bool` `1` or `0`. A unit goes in a comment beside it.
- **`MC_TUNE_HASH`:** FNV-1a of every key and value in a fixed order, in `generated/tuning/index.h`. `--report` prints it with every value, and `bench_world`'s history gains a `tuning` column, so every recorded number says which tuning it ran with.

**It refuses, with `path:line:column: message`, and writes nothing:**

- a field other than `value type unit min max why`, or a missing one;
- a type, or a unit, not on its list;
- a value outside `min`..`max`;
- an `f32` with no decimal point;
- an empty `why`;
- a key that is not lower snake case;
- two files making the same define.

**Rules across keys** live where the meaning is, as `static_assert` in the consuming header. For example, `static_assert(MC_TUNE_RENDER_LOD_NEAR_GUARANTEE >= 16u)`, Ahmed's floor, beside the YAML's `min: 16`, so a typo in either is caught. A `tests/compile/tuning/` check fails if a define is never used outside `generated/`.

**When it runs:** at build time, as `game_tables` does (`cmake/generated.cmake`), not at configure time. Nothing in CMake reads these numbers. So:

    edit compile/config/render/lod/index.yaml
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --menu        # Ahmed runs it himself

A header whose content did not change is not rewritten, so only what includes a changed file rebuilds.

**Beside `config/machine.yaml`, not in it.** `machine.yaml` is facts about the machine, read at configure time because flags depend on it. Tuning is choices, read at build time. `game.render_distance_default` and `_max` are choices that live in `machine.yaml` today; moving them is Q3, default "leave them".

**What moves in phase 02, with every golden picture and bench hash unchanged:**

- `RENDER_LOD_*`: radius, margin, jobs, features shift max, pool bytes, frame bytes;
- `LOD_SKIPS_MAX`;
- `SHADE_ALPHA_LAYERS`, `SHADE_SUN_LAYERS`;
- `SHADE_AO_TURNS`, `SHADE_AO_FRESH_RAYS`, `SHADE_AO_SAMPLES_MAX`, `SHADE_AO_REACH`;
- `SHADE_SHAFT_STEPS`, `SHADE_WATER_SEEN_BLOCKS`;
- the coarse pass's stretch lengths;
- TAA's 1.25 clamp, its 0.1 blend and `POST_TAA_STILL`;
- the upload budget.

**What does not move:** a number that shapes a data format or a struct: tile size, brick and section sizes, `WORLD_BUFFERS_*`, struct-sized arrays. The rule, in the README: *a number goes in tuning YAML if changing it changes speed or look but no format and no struct.*

### 4.3 One pyramid of levels (phase 03)

Today's far field is levels built only from **terrain samples**, and full detail is a separate world. Phase 03 makes them one pyramid, without yet changing what is drawn:

- **Levels 1 to K in the same `world_buffers` format**, each a clipmap centred on the eye (the far field's tables already scroll this way). Level k covers at least its band's far edge plus the margin. **Level 1 is new** for the near region, since today's plan drops it under full detail.
- **Built from the level below where that is loaded.** A level-k voxel is the 2×2×2 group of level k−1 voxels "as one shape" (Ahmed's words, made literal), by the same half rule `render_lod_cells_from_column` uses for terrain. **Where:** on workers, from the map's own sections (which hold every edit), through the far field's existing job, encode and upload path (`run/src/render/lod/`), at a priority above far columns. A column whose sections change is queued again at once. So **an edit reaches every level within about 100 ms**, where today a far column waits for the save capture, up to a second. A card kernel was considered: it would need slabs allocated on the card, which the host-side pool (`world_buffers_t`) does not allow. It stays a fallback only if workers miss 100 ms. Outside level 0's radius, levels are still built from terrain samples on workers, as today.
- **The reduction, worked through:**
  - **Occupancy.** A level-1 voxel covers 2×2×2 blocks. Its block counts if at least half of what it covers is blocks, by the half rule: "a voxel is a block if at least half of what the sample has in its height is".
  - **Material.** The most common material among the counted voxels; ties go to the higher voxel, so a grass top stays grass.
  - **Light.** The brightest stored light of its counted voxels, as faces show the brighter side (`cuda/src/shade/README.md`, "The light on a surface").
  - A uniform section reduces to a uniform section, and an empty one to nothing.
- **Cost, worked:** a section is 4096 voxels. Its level-1 child gets 512 of them, 1/8 of the work and bytes, and level k+1 gets 1/8 of level k again. Reduction runs on columns whose sections changed, so it adds at most about 1/7 to what upload already moves (1/8 + 1/64 + …), and its worker time is bit operations over sections the map already holds. Measured in phase 03.

### 4.4 Continuous LOD replaces Full detail (phase 04)

- **Each ray picks its level by distance, worked out from the camera every frame:** band k is d<sub>k</sub> ≤ t < d<sub>k+1</sub> with d<sub>k</sub> = max(near guarantee, s<sub>k</sub> / (2 E θ)) for k ≥ 1, and d<sub>0</sub> = 0. θ comes from `half_fov_tangent` and the picture height (render scale counted), so plan 008's resolution ladder makes levels coarser exactly as far as its pixels are bigger, and never inside 16 blocks.
- **One loop walks the pyramid.** A ray starts in level 0 at the coarse pass's distance. At d<sub>k+1</sub> it moves to level k+1, with `resume` set from that t: the far field's existing hand-over between levels (`lod_trace`, `t_from = t_exit − LOD_HANDOVER_BACK`). The pass-by of full detail's columns (`lod_full_detail_at`, up to 32 traversals a level) is gone: a level is used only past its band edge, so there is nothing to pass by.
- **No ring:**
  - The band edge is dithered per pixel and per frame by up to `band_dither` of its distance. TAA blends the two levels across the band, which is Godot's dithered visibility-range fade and Unreal's dithered LOD transition.
  - Levels nest, as each is built from the one below, so a coarse surface lies within ε<sub>k</sub> of the fine one.
  - A grazing ray that the finer level let through can meet the coarser one's surface at once on hand-over. The hand-over keeps the existing back-off, and phase 04's tests hold the seam: no pixel at a band edge is further from the reference than ε<sub>k+1</sub> projected.
- **The near guarantee in code:** `d_1 >= MC_TUNE_RENDER_LOD_NEAR_GUARANTEE` by construction, and a test (section 8).
- **What the game holds at level 0:** r<sub>0</sub> = d<sub>1</sub> + `block_margin_chunks` (YAML), worked out by `game_view_work_out` from E, FOV, resolution and the table. The map's view radius is set from it. The table is sized at world open for the widest r<sub>0</sub> the FOV range allows, up to `block_radius_max_chunks` (YAML, default 48, today's `SETTINGS_FULL_DETAIL_MAX`). A narrow FOV past that cap gets d<sub>1</sub> held at the cap, and F3 says so (Q4).
- **What goes:**
  - the Full detail slider and its tooltip (`run/src/ui/screen_settings/index.c:41`, `:121`, `:135-136`; `UI_SCREEN_SETTINGS_EXPLAIN_FULL_DETAIL`);
  - `settings_values_t.full_detail` and its row (`run/src/settings/persist/index.c:27`, `:72`); an old `settings.yaml` with `full_detail:` is read with one log line and the key ignored;
  - `--full-detail` (`run/src/main/index.c:60`, `:293`);
  - `RENDER_LOD_FULL_DETAIL_DEFAULT`;
  - the word "full detail" from every README.

  The Settings screen goes from six sliders to five.

### 4.5 Secondary rays by level (phase 06)

A secondary ray needs only the detail its effect can show:

- **Shadow (sun and moon) rays, by the light's cone.** The sun and moon are each about 0.53° across, so a shadow cast from L blocks away has a penumbra L × tan(0.53°) = **0.0093 L blocks** wide. An occluder detail smaller than that cannot show in a real shadow. So a shadow ray may step level k once it has travelled L<sub>k</sub> = ε<sub>k</sub> / (0.0093 × `shadow_cone_scale`) blocks from its surface: **level 1 from 108 blocks, level 2 from 216, level 3 from 432.** It always starts at level 0, so a near tree's shadow at your feet is exact. This is Teardown's ambient and sun tracing ("misses increase the mip"), made conservative by the cone.
- **Also by the receiver's distance:** a surface at distance d needs no shadow detail finer than its own band. So a shadow ray from a level-k surface starts at level k (judgment; tested against the reference).
- **Dim-light cut.** When the direct light could change a lit pixel by under `shadow_skip_below` (YAML, default 2 / 255 after tone map, worked out per frame by `render_sky_fill`), no shadow ray is sent. **This does not cover 5.png:** the moon's direct light (0.05 to 0.10) is above the night's ambient, and at the dark end of sRGB a change of 2 / 255 is a direct light of only 6 × 10⁻⁴. It fires only where the light fades through the horizon (within about 7° of it, `fade` in `render_sky_fill`) and under heavy overcast. The moon's shadow rays at his spot are made cheaper by the cone rule above, not cut (Q5).
- **Alpha layers by distance.** Past `alpha_far_from_blocks`, where a leaf texel is under a pixel (771 / 16 = **48 blocks** at 1080p and 70°), a canopy pixel goes through at most `alpha_layers_far` holes and then takes the texture's mean colour, which the code already does at layer 16 ("what far leaves look like anyway", `cuda/src/shade/README.md`).
- **AO** stays as it is (reach 3 blocks; plan 008 phase 04 owns its fresh rays).
- **Far-field sun rays** are coarse already (`lod_sun_reaches`).

### 4.6 Merged boxes for empty space and water (phase 05)

Each item is an experiment, kept only if phase 01's step counts and the card clock say it pays:

1. **2³ coalescing.** Inside an occupied brick, before stepping one voxel, test the 2×2×2 cube holding it: `(brick_mask >> base) & 0x00330033ull` equal to 0 means step 2. The 64-tree write-up measured **21 %** fewer cycles per ray. No format change.
2. **Per-brick Chebyshev distance.** One byte per brick: the distance in bricks to the nearest occupied brick, within the section and its neighbours, capped at `distance_cap` (YAML). An empty brick then steps `d × 4` voxels in any direction. That is 64 bytes a section, written by `run/src/render/encode/` and recomputed for the neighbours of an edited section. It is the merged empty box in one byte. The write-up found unaligned skipping worth 2 to 5 % for primary rays, but secondary rays (shadows, AO, which start next to surfaces) may gain more. Measured, then kept or dropped.
3. **Uniform bricks for water.** A 64-bit mask per section of bricks that are full and all one block. The water walk (`shade_water_through`, cell by cell; plan 005 phase 8: 6 to 10 ms on `deep_ocean/orbit` at render distance 48) then steps 4 at a time through such a brick, and 16 through a uniform section. This is the "one shape" rule for see-through runs. It adds 8 bytes to `world_buffers_section_t`, a format change, so phase 05 bumps the section format and re-encodes on load (sections are rebuilt from the world on every load, so nothing on disk changes).
4. **The coarse pass on the pyramid.** Far stretches test level-k occupancy, which is 8<sup>k</sup> times fewer reads per box. Every tile's distance must stay conservative: the existing exact-trace test holds.
5. **Octant mirroring**: 10 % in the 64-tree write-up. Tried last; dropped if under 3 % here.

### 4.7 Temporal upscaling (phase 07)

On top of plan 008 phase 05's ladder, its history indexed by the previous width, and its upscale kernel:

- **Render at 75 % per axis** (1440 × 810, 56 % of the rays) and accumulate at 1920 × 1080. Each output pixel gathers the internal samples near it with Catmull-Rom weights, reprojects history along its unjittered ray (`post_reproject`), clamps to the neighbourhood (the existing 1.25 σ), and mixes.
- **Jitter** over 16 Halton points instead of 8, so every output pixel is covered within 16 frames at 75 %.
- **A still camera converges to the full-resolution picture.** Its test: after 32 still frames, the mean absolute difference from a 100 % picture is ≤ 1 / 255 and the 99.9th percentile ≤ 8 / 255.
- **Weather drops and the outline** stay after the history, as now.
- **Checkerboard** is an experiment beside it.
- **The setting:** `render_scale_percent` becomes the internal scale. Its default stays 100 until Ahmed has seen 75 (Q9). The ladder from plan 008 then uses temporal and not bilinear.

---

## 5. What each step should buy, worked

All of this is **judgment**, to be replaced by phase 01's numbers.

**Steps of one grazing ray from 100 to 1024 blocks** (worst case: it hugs the slope, crossing about 1.5 cells per block of length at level 0):

| | Level 0 cells | Level 1 | Level 2 | Level 3 | Total | Against today |
|---|---|---|---|---|---|---|
| Today (ring at 512, far field from eye) | 100–512: 618 | | 512–896: 144 | 896–1024: 24 | 786, **plus the pass-by's re-traversals** | |
| E = 2 | 100–386: 429 | 386–771: 289 | 771–1024: 95 | | 813, **no pass-by** | about equal, minus the pass-by |
| E = 4 | 100–193: 140 | 193–386: 145 | 386–771: 145 | 771–1024: 47 | 477 | **−39 %** |
| E = 8 | 96–193: 146 | 193–386: 72 | 386–771: 72 | 771–1024: 24 | about 314 | **−60 %** |

**For his frame on his card** (29.44 ms; ranges are judgment):

| Pass | Today | After phases 04 to 06 | After phase 07 (56 % of the rays) |
|---|---|---|---|
| Primary (with the far field) | 10.41 | 6 to 8 (no pass-by, coarse steps, coalescing) | 3.4 to 4.5 |
| Shade | 14.62 | 7 to 10 (shadow cone, alpha far) | 4 to 5.6 |
| Coarse | 1.67 | 1.2 | 0.7 |
| Post + upscale | 1.40 | 1.40 | 1.6 |
| Readback | 1.34 | (plan 008 phase 03: off the critical path) | |
| **Card total** | **29.44** | **15.6 to 20.6** | **9.7 to 12.4** |

So the target in section 7 (p99 ≤ 16.7 ms on the capped card at his spot) needs phase 07, or E above 2, or both. That is why E and the internal scale are tuning numbers.

---

## 6. The tuning numbers this plan adds

Phase 02 makes the files; later phases add keys as they need them. Defaults are the plan's; Ahmed tunes them.

| File | Key | Default | Unit | Range | Phase |
|---|---|---|---|---|---|
| `render/lod` | `pixel_error` | 2.0 | pixels | 0.25 – 16 | 04 |
| `render/lod` | `near_guarantee` | 16 | blocks | 16 – 256 | 04 |
| `render/lod` | `band_dither` | 0.08 | ratio | 0 – 0.25 | 04 |
| `render/lod` | `level_count` | 6 | count | 2 – 8 | 03 |
| `render/lod` | `block_margin_chunks` | 2 | chunks | 1 – 8 | 04 |
| `render/lod` | `block_radius_max_chunks` | 48 | chunks | 8 – 64 | 04 |
| `render/lod` | `features_shift_max` | 2 | levels | 0 – 4 | 02 (moved) |
| `render/traverse` | `coalesce_2` | true | bool | | 05 |
| `render/traverse` | `distance_cap` | 3 | bricks | 0 – 7 | 05 |
| `render/traverse` | `coarse_long_stretch`, `coarse_short_stretch` | today's | blocks | | 02 (moved) |
| `render/shade` | `shadow_cone_scale` | 1.0 | ratio | 0.25 – 8 | 06 |
| `render/shade` | `shadow_skip_below` | 0.0078 (2 / 255) | ratio | 0 – 0.05 | 06 |
| `render/shade` | `alpha_far_from_blocks` | 48 | blocks | 16 – 512 | 06 |
| `render/shade` | `alpha_layers_far` | 4 | count | 1 – 16 | 06 |
| `render/shade` | `ao_turns`, `ao_fresh_rays`, `ao_samples_max`, `ao_reach`, `alpha_layers`, `sun_layers` | today's | | | 02 (moved) |
| `render/temporal` | `taa_clamp_sigma`, `taa_blend` | 1.25, 0.1 | | | 02 (moved) |
| `render/temporal` | `upscale_jitter_points` | 16 | count | 8 – 32 | 07 |
| `render/budget` | `upload_bytes_per_frame`, `lod_frame_bytes`, `lod_jobs` | today's | | | 02 (moved) |

---

## 7. Targets

"His spot" is `x -385 y 347 z 2`, looking up the slope as in 5.png, night with the sun at −13.1°, render distance 64, 1920 × 1080, every effect he plays with. "Capped" is the card as plan 008 measured it, with the clock recorded beside every number (008's instruments).

| Measure | How | Today | Target |
|---|---|---|---|
| Card time at his spot, capped, p99 over 240 settled frames | Phase 01 row | 29.44 (one frame) | **≤ 16.7 ms** |
| The same, mean | Same | Phase 01 | ≤ 14 ms |
| Stretch: the same on a cool card (if Q5 of plan 008 lifts the cap once) | Same | Phase 01 | ≤ 8.33 ms; recorded, not required |
| **Near guarantee** | Pixels whose reference hit is < 16 blocks, against level 0 everywhere | n/a | **Byte-identical, every pixel, every test row** |
| Error past 16 blocks | Share of pixels whose surface depth differs from the reference by more than E px projected, or whose block differs past the near guarantee | n/a | ≤ 1 % of pixels at E = 2, settled rows |
| Mean primary steps a pixel at his spot | Phase 01 histogram | Phase 01 | −40 % |
| Shadow-ray steps at his spot | Phase 01 counters | Phase 01 | −50 % |
| Water walk on `deep_ocean/orbit` at 64 | `bench_world`, primary + shade | Plan 005 phase 8: 13.73 ms primary | −50 % of the walk |
| `bench_world` rows at render distance 32 | Full run, like with like | `history.tsv` | None slower by more than 3 % |
| The Settings screen | Golden pictures | 6 sliders | 5, no "Full detail" anywhere |
| VRAM | `bench_world` | 1.8 GiB voxels at 32 | No more than today at 64 |
| World fill-in at render distance 64 | `run_render_distance_live` | 7.66 s (plan 005 phase 8) | No worse |
| An edit's reach to every level | Test | Up to 1 s (save capture) | ≤ 100 ms |
| Tuning edit to new numbers | Edit YAML, one build | n/a | Only dependents rebuild; `--report` shows the new `MC_TUNE_HASH` |

These are first targets; Ahmed changes any at the phase 01 gate (Q10).

## 8. Tests the plan relies on

- **Tuning tool** (`tests/compile/tuning/`, no card):
  - each refusal at its line;
  - exact decimals out;
  - an unchanged file is not rewritten;
  - the hash changes with one value;
  - every define is used.
- **Moving constants changes nothing:** after phase 02, every golden picture and every settled `bench_world` hash is the same bytes.
- **The reduction** (`tests/run/render/lod/`, host and card): worked on paper.
  - A 2×2×2 of stone → stone.
  - Four of eight blocks → a block (the half rule).
  - Three of eight → air.
  - A grass-topped column → a grass top.
  - Light → the brightest.
  - Uniform → uniform.
  - The card's reduction equals the host's to the byte.
- **The near guarantee** (`render_feature_lod`): for each of 6 places (plains, jungle, peaks, ocean, cave, his spot), render with the pyramid and with level 0 to render distance. Every pixel with a reference hit under 16 blocks must be identical. Also at E = 8, where d<sub>1</sub> = 96 would otherwise be the first band edge, and at 50 % render scale.
- **The error bound:** the same pairs; ≤ 1 % of pixels past the bound at E = 2.
- **The seam:** a flat plain and a 45° slope crossing d<sub>1</sub> and d<sub>2</sub>: no pixel beyond ε<sub>k+1</sub> projected, and with dither and TAA, after 32 frames, no visible ring. Ahmed judges the picture at the gate.
- **Edits:** mine a block at 400 blocks (level 1). The level-1 voxel changes within 100 ms, and the level-2 one too.
- **Shadows:** a 1-block pillar casting on the floor from 20 blocks gives the same shadow pixels as today. A 1-block pillar 300 blocks from its shadow, under the cone rule: the shadow may vanish or blur, and the test documents what it does.
- **Dim-light cut:** at the threshold, no lit pixel moves by more than 2 / 255.
- **2³ coalescing and the distance byte:** `tests/cuda/traverse/` already traces against a reference that knows no skipping; every hit must be the same voxel and `t` to the bit, with step counts lower.
- **Uniform water bricks:** the water walk's result is identical with and without, on every water golden picture.
- **Temporal upscaling:** convergence (section 4.7); a moving camera has no ghost past TAA's own clamp.
- **Settings:** old files with `full_detail:` load with one log line; the new defaults file has no such key; golden Settings pictures regenerated after looking at them.

## 9. Risks

| Risk | What's done about it |
|---|---|
| E = 2 buys little at render distance 64 (section 2.3) | Expected. The gain is from pass-by removal, secondary rays, boxes and phase 07. E is one YAML number for Ahmed to raise |
| A visible ring at band edges | Dither plus TAA; nesting; the seam test; Ahmed's eye at the phase 04 gate. Fallback: a wider `band_dither` |
| Coarse levels change silhouettes of peaks against the sky | Peaks are where error shows most. Phase 04 measures the error share on `jagged_peaks` and his spot. Q6 offers a smaller E for silhouettes (pixels whose ray grazes) |
| The reduction costs more than 1/7 of upload, or misses 100 ms | Measured in phase 03. Fallback: a card kernel writing into slabs the host set aside per column |
| Removing Full detail changes what the sim loads (map radius 32 → about 27 chunks at E = 2) | The sim radius was always full detail; it now follows r<sub>0</sub>. A floor `block_radius_min_chunks` (YAML, default 12) keeps gameplay's reach. Mobs and ticking are untouched inside it |
| A narrow FOV wants more level 0 than the table holds | Capped at `block_radius_max_chunks`, with an F3 note (Q4) |
| Shadow cone LOD loses thin far shadows (a far tower's shadow) | That is the physics of a 0.53° light. `shadow_cone_scale` is tunable; the test documents it |
| The dim-light cut makes shadows vanish at dusk | Only below 2 / 255 of change; Q5 lets Ahmed turn it off |
| Uniform-brick mask changes the section format | Sections are re-encoded from the world on every load; nothing on disk changes. All `world_buffers` static_asserts move together |
| Temporal upscaling softens the still picture | Convergence test; the default stays 100 % until Ahmed has seen 75 (Q9) |
| Plan 008's instruments are not there yet | 009 phase 01 adds only its counters and the reference mode, and records what it lacked |
| Merge conflicts with plans 006 and 008 in `cuda/src/shade/`, `run/src/game/` and F3 | Rebase per phase. Phase 00 records the order |
| The tuning tool at build time makes a CUDA rebuild per edit | Only files including the changed header rebuild. A runtime override is Q11, default no |

## 10. Out of scope

- Rasterizing anything (greedy meshes, a hybrid), OpenGL, Vulkan, OptiX, RT cores, DLSS, hardware VRS (section 2.6).
- A CUDA splatter for far voxels (section 2.6): a later plan, only if phase 08 says the far field still costs most.
- SVDAG compression: VRAM is not the limit.
- Changing terrain, biomes, the climate, or what the far field samples.
- Plan 008's work: pacing, readback, AO cache, resolution controller, turn cache.
- A shadow cache (Q8).
- Moving `config/machine.yaml`'s game keys (Q3).

---

## 11. Phases

Each phase ends somewhere worth stopping and has its own file in plan 004's structure: goal, read first, where it fits, what it must NOT build, Ahmed's words, background, design, owned paths, steps, todo, tests, verification through `tools/test-or-build.sh`, gate, risks, Commits, Issues found.

### Phase 00: The vision and the plan files — done

- [x] This master and phase files 01 to 08, every box of 01 to 08 unticked
- [x] Phase 00 records where 009 sits beside 006 and 008, the order, and the rules every phase follows

### Phase 01: Measure where the steps go

- [ ] His view as a settled row (place, yaw, pitch, sun, seed), at render distance 64 and 32
- [ ] Step histograms by band for primary, shadow, AO and alpha rays; a heatmap PNG
- [ ] The reference mode: level 0 to render distance
- [ ] Experiments: shadows, far field, alpha, AO off; render scale 75; full detail 16 / 24 / 32 / 48 at 64

**Gate:** Ahmed reads the cost table at his spot, the heatmap and the experiments, and answers Q2, Q5 and Q10.

### Phase 02: Compile-time tuning YAML

- [ ] `compile/config/` with READMEs and five files; `compile/src/tuning/`; generated headers; `MC_TUNE_HASH`
- [ ] Existing tuning constants moved, with every picture and hash the same bytes
- [ ] REPO_STRUCTURE and CODING_STANDARD rows

**Gate:** Ahmed changes one number (for example `ao_turns`), rebuilds, and sees it in `--report` and on screen.

### Phase 03: One pyramid of levels

- [ ] Level 1 and up for the near region; 2×2×2 reduction from the map's sections on workers; terrain-built levels outside, as today
- [ ] Pictures unchanged (nothing draws from the new levels yet)

**Gate:** an edit at 400 blocks reaches every level within 100 ms; upload within 1/7 more.

### Phase 04: Continuous LOD replaces Full detail

- [ ] Bands from E, FOV and resolution; one loop over the pyramid; no pass-by; dithered edges
- [ ] The near guarantee and error tests; r<sub>0</sub> drives the map; Full detail removed everywhere

**Gate:** Ahmed plays at render distance 64 with E = 2, 4 and 8, sees no ring and no near change, and picks E.

### Phase 05: Merged boxes for empty space and water

- [ ] 2³ coalescing; per-brick distance; uniform water bricks; coarse pass on the pyramid; mirroring. Each kept or dropped by numbers

**Gate:** step counts and card time at his spot and on `deep_ocean/orbit` against phase 04's.

### Phase 06: Secondary rays by level

- [ ] Shadow rays by the light's cone and by the receiver's band; the dim-light cut; alpha layers by distance

**Gate:** Ahmed looks at shadows by day and by night at his spot and says whether they read right.

### Phase 07: Temporal upscaling

- [ ] 75 % internal with output-resolution accumulation, on plan 008 phase 05's ladder; checkerboard as an experiment

**Gate:** Ahmed compares 100 % and 75 % at his spot, still and turning.

### Phase 08: The final check

- [ ] Every target in section 7 measured like with like; `history.tsv`; full suites; release build; Ahmed's own play

**Gate:** Ahmed's verdict at his spot.

## 12. Still open for Ahmed

Each has a default, and none blocks approval.

| # | Question | Default if not answered |
|---|---|---|
| Q1 | Tuning files at `compile/config/<path>/index.yaml` (your words) or `config/tuning/<path>/index.yaml` (beside `machine.yaml`)? | `compile/config/`, with a REPO_STRUCTURE row |
| Q2 | The world of 5.png: seed and the yaw and pitch you looked at | Your seed from plan 005 phase 8 (`11107447384583428913`); yaw and pitch fitted to the picture |
| Q3 | Move `game.render_distance_default` and `_max` from `machine.yaml` to tuning YAML? | Leave them |
| Q4 | A narrow FOV past `block_radius_max_chunks` of level 0: cap (coarser than E there) or size the table for it? | Cap at 48 chunks, F3 says so |
| Q5 | Skip shadow rays when the light would change a pixel by under 2 / 255 (only near the horizon crossing and under overcast; not ordinary moonlight)? | Yes, at 2 / 255 |
| Q6 | A separate, smaller pixel error for silhouettes against the sky, and per material (leaves, water)? | One E for all, in phase 04; per-kind only if the error share on peaks is over 1 % |
| Q7 | Software variable-rate shading (one shade per 2 × 2 on flat tiles) after phase 07? | Not in this plan |
| Q8 | A world-space shadow cache like plan 008's AO cache? | Not in this plan |
| Q9 | Default internal scale 75 % with temporal upscaling, once you have seen it? | Stay 100 % until you say |
| Q10 | Targets in section 7 | As written, until the phase 01 gate |
| Q11 | A debug-only way to change tuning numbers without a rebuild? | No: compile time, as you asked |

## 13. Sources

- [Karis, Stubbe, Wihlidal, *A Deep Dive into Nanite Virtualized Geometry*, SIGGRAPH 2021 Advances](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf)
- [Nanite's streaming, LOD and occlusion, explained (UIUC CS 418)](https://cs418.cs.illinois.edu/website/text/nanite.html)
- [Lumen brings real-time GI to Fortnite (Epic tech blog)](https://www.unrealengine.com/en-US/tech-blog/lumen-brings-real-time-global-illumination-to-fortnite-battle-royale-chapter-4); Wright, Narkowicz, Kelly, *Lumen: Real-time Global Illumination in Unreal Engine 5*, SIGGRAPH 2022 course notes
- [Virtual Shadow Maps in Unreal Engine](https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine); [VSM in Fortnite Chapter 4](https://www.unrealengine.com/tech-blog/virtual-shadow-maps-in-fortnite-battle-royale-chapter-4)
- [Temporal Super Resolution in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine); [Anti-aliasing and upscaling](https://dev.epicgames.com/documentation/en-us/unreal-engine/anti-aliasing-and-upscaling-in-unreal-engine)
- [Godot: Mesh level of detail](https://docs.godotengine.org/en/stable/tutorials/3d/mesh_lod.html); [Visibility ranges (HLOD)](https://docs.godotengine.org/en/stable/tutorials/3d/visibility_ranges.html); [Occlusion culling](https://docs.godotengine.org/en/stable/tutorials/3d/occlusion_culling.html); [SDFGI](https://docs.godotengine.org/en/stable/tutorials/3d/global_illumination/using_sdfgi.html)
- [Amanatides & Woo, *A Fast Voxel Traversal Algorithm for Ray Tracing*](https://www.eecs.yorku.ca/~amana/research/grid.pdf)
- [Laine & Karras, *Efficient Sparse Voxel Octrees*](https://www.nvidia.com/docs/IO/88889/laine2010i3d_paper.pdf); [Laine & Karras, *High-Performance Software Rasterization on GPUs*](https://research.nvidia.com/publication/2011-08_high-performance-software-rasterization-gpus)
- [Kämpe, Sintorn, Assarsson, *High Resolution Sparse Voxel DAGs*](https://www.cse.chalmers.se/~uffe/HighResolutionSparseVoxelDAGs.pdf)
- [Fang, Wang, Wang, *Aokana: A GPU-Driven Voxel Rendering Framework for Open World Games* (2025)](https://arxiv.org/html/2505.02017v1)
- [dubiousconst282, *A guide to fast voxel ray tracing using sparse 64-trees* (2024)](https://dubiousconst282.github.io/2024/10/03/voxel-ray-tracing); [Liu & Rokne, micro 64-tree, GI 2013](https://graphicsinterface.org/proceedings/gi2013/gi2013-22)
- [van Wingerden, *Real-time Ray tracing and Editing of Large Voxel Scenes* (Utrecht, 2015)](https://studenttheses.uu.nl/handle/20.500.12932/20460)
- [Teardown frame breakdown (mip-mapped occupancy, ambient and sun tracing)](https://juandiegomontoya.github.io/teardown_breakdown.html)
- [Deakin & Knackstedt, *Efficient ray casting of volumetric images using distance maps for empty space skipping* (2020)](https://link.springer.com/content/pdf/10.1007/s41095-019-0155-y.pdf)
- [cgerikj/binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing)
- [Distant Horizons FAQ (CurseForge)](https://blog.curseforge.com/distant-horizons-frequently-asked-questions/); [Distant Horizons releases](https://modrinth.com/mod/distanthorizons/changelog?page=2)
- [Voxy (Modrinth)](https://modrinth.com/mod/fNtGd1cx); [VoxyServer](https://modrinth.com/mod/voxyserver)
- [Nvidium](https://modrinth.com/project/uvBS5xOp)
- [Transvoxel / Lengyel, voxel terrain](http://www.terathon.com/lengyel/Lengyel-VoxelTerrain.pdf)
- [AMD FidelityFX Super Resolution 2 (GPUOpen, MIT)](https://gpuopen.com/fsr2/); [FSR 2 technique manual](https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-temporal/)
- [El Mansouri, *Rendering Rainbow Six Siege*, GDC 2016](https://archive.org/download/GDC2016Mansouri/GDC2016-Mansouri.pdf)
- [NVIDIA Adaptive Shading deep dive](https://www.nvidia.com/en-us/geforce/news/nvidia-adaptive-shading-a-deep-dive/); [NVIDIA VRS](https://developer.nvidia.com/vrworks/graphics/variablerateshading)
- `research/001.md` (lines 98 to 125), `research/002.md`, `research/003-speed-research.md`; plans 001 (renderer design, section 2), 005 phases 8 and 14 (far-field cost), 008 (the capped card and its instruments)

## Verification of this plan

    ls plans/009-*
    grep -L "## Issues found" plans/009-*phase-*.md   # prints nothing
    git status --short                               # only other agents' work
