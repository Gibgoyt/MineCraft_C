# 003: How to make this game extremely fast

Research gathered on 2026-11-01 for `plans/001-ahmed-bohemoth-minecraft-from-scratch.md`.

Each claim is marked:

- **sourced:** stated by a source that I read, or by `research/001.md` or `research/002.md`.
- **engineering judgment:** my reasoning, not yet measured on this machine. Phase 01 and Phase 17 test these.

**What this research did not establish:** nothing here is a measurement on this machine. Published GPU numbers are for
much older hardware. Whether 120 FPS is reachable gets decided by Stage 01.3, not by this document.

---

## 1. The biggest choice: no triangles

**sourced:** `research/002.md` credits its speed to having the GPU do all visibility work, in contrast to Minecraft's
single core doing it. Nvidium uses NVIDIA mesh shaders to cull terrain on the GPU before it is drawn.

**sourced:** Software triangle pipelines on a GPU run 2-8x slower than the hardware pipeline (Laine & Karras 2011).

**sourced:** GPU voxel ray casting is reported to be competitive with triangle-based ray casting. The 2010 paper reports
60.9 million primary rays per second on one scene and 122.0 million on its non-displaced version.

**engineering judgment:** We cannot use the hardware pipeline, because OpenGL and Vulkan are banned, and CUDA exposes no
rasterizer. A ray caster gets visibility for free, one pixel at a time, and shadows, AO and reflections reuse the same
traversal. Cost depends on pixels more than on world size. Ahmed chose this approach on 2026-11-01.

**The arithmetic:** 1920 × 1080 × 120 = 248.8 million primary rays per second, before shadow and AO rays. That is about
twice the 2010 figure, on a GPU several generations newer, with far simpler geometry (axis-aligned blocks). Plausible,
**not proven**. The gate is Stage 01.3.

---

## 2. Render speed

| Technique | Status | Use |
|---|---|---|
| Grid traversal: each step needs only two floating-point comparisons and one addition | **sourced** (Amanatides & Woo 1987) | Core ray loop; player block targeting |
| Beam optimization: a coarse pass gives primary rays a safe starting distance. City scene at 2048×1536: 97.4 → 123.8 Mrays/s | **sourced** (Laine & Karras 2010, section 4.2, 8×8 blocks, results table) | Coarse depth pass |
| Empty-space skipping with 64-bit masks and bit scans | **engineering judgment**, adapted from the bit tricks in binary-greedy-meshing | Section → brick → voxel tree |
| Per-column max height, so sky rays leave early | **engineering judgment** | Column table |
| LOD: distant chunks at one block per 8 m³, down to one block per chunk | **sourced** (research/001); the approach is also popular through Distant Horizons | Clipmap far field |
| Lower rate and temporal reuse for AO and shadows | **engineering judgment** | Stage 09.4 |
| Render scale with TAA as a fallback | **engineering judgment** | Fallback if Stage 01.3 misses |
| Shadow cascades | **sourced** (research/001), but **not needed**: sun shadows are one more ray | Skipped |

### What we do not use, and why

| Technique | Status | Why not |
|---|---|---|
| Greedy meshing (research/001). Binary version: 50-200 µs per chunk, 8 bytes per quad, about 4x faster with masks | **sourced** (cgerikj/binary-greedy-meshing; docs.rs binary-greedy-meshing) | No meshes exist. Its bitmask thinking carries over |
| Frustum and backface culling by face direction (research/001) | **sourced** | Ray casting has no faces to cull |
| Compact vertices, multi-draw, regions, cave culling (Sodium) | **Not confirmed by my searches.** Sodium's issues do discuss chunk-graph occlusion problems, and a derivative (Halite) documents graph culling and 16-byte vertices, but I did not confirm Sodium's own format | No triangles, so not applicable |
| Hardware RT cores | **engineering judgment**: reachable only through OptiX or Vulkan, both banned | All traversal is plain CUDA compute |

---

## 3. CPU and simulation speed

| Technique | Status | Use |
|---|---|---|
| Blocks stored as array data, not objects | **sourced** (research/001) | Sections |
| Java's short-lived objects cause stutter | **sourced** (research/002) | Zero heap calls in hot paths, arenas |
| Use every core | **sourced** (research/002) | Job system on 16 workers |
| Move chunk generation off the main thread | **sourced** (research/001; C2ME is described as offloading generation to other threads) | All terrain on workers |
| Profile first, with timers around every operation | **sourced** (research/001) | Own profiler (Stage 17.1), since `perf` and `valgrind` are not installed |
| Faster physics and AI with no behaviour change | **sourced** (Lithium's description) | Direction only. Specifics are **engineering judgment**: cached lookups, batched AI |
| Lower memory use | **sourced** (FerriteCore's description) | Direction only. Palettes and uniform sections are **engineering judgment** |
| Work stealing, pinned threads, physical cores first | **engineering judgment** | Stage 03.4 |
| Single-producer rings instead of locks on hot paths | **engineering judgment** | Stage 03.3 |
| 2 MiB-aligned arenas for huge pages (THP is `always` here; no reserved hugepages) | **engineering judgment**, with machine state verified | Stage 03.2 |
| 64-byte alignment for shared data, to avoid false sharing | **engineering judgment** | Coding standard |

### NASM SIMD

**sourced (Ahmed's overview, confirmed against `lscpu`):** AVX2 and FMA3, 16 YMM registers, no AVX-512.

**engineering judgment:**

- Calling assembly per vector is a net loss, because the call costs more than the work. So only array-at-a-time kernels.
- Best candidates: noise (8 at a time), scanout copy, HUD blending, bitmask building, batch transforms.
- `vzeroupper` before returning, to avoid SSE/AVX transition costs.
- Every kernel must beat a scalar C reference in `bench/simd/`, or it gets deleted.

---

## 4. Terrain

| Technique | Status | Use |
|---|---|---|
| Layered noise; exponent creates mountain ranges | **sourced** (research/001) | Fractal noise, height splines |
| Overworld shape comes from 3D noise with continentalness, erosion and peaks/valleys | **sourced** (Minecraft Wiki) | Stage 10.2, 10.3 |
| Density above zero means solid; the router covers shape, caves, aquifers, ore veins, biome parameters | **sourced** (community write-up referenced by the search; check before relying on it) | Stage 10.3-10.6 |
| Same seed gives the same world | **sourced** (research/001) | Determinism gate in Phase 10 |
| Save only changed chunks | **sourced** (research/001) | Stage 16.1 |
| Sample noise on a coarse grid and interpolate | **engineering judgment** | Stage 10.3 |

We implement these concepts ourselves. We do not copy Mojang's code or data. Minecraft's exact random-number generator
was not confirmed by my searches and is not needed.

---

## 5. Display path

**Verified on this machine:** 1080p at 120 Hz, `nvidia_drm modeset=Y`, PCIe gen3 x16 capability (the link idles lower), MIT-SHM
present but not used, per Ahmed's KMS decision.

**engineering judgment:** One 1080p XRGB8888 frame is 8.3 MB, so 120 FPS is about 1 GB/s, well under PCIe gen3 x16's
theoretical 12 GB/s or so. Dumb buffer memory may be slow for the CPU to read, so writes must be streaming and one-way.
**Dumb buffers and page flips on the proprietary driver are unproven here.** Stage 01.2 tests them, with Ahmed at the console.

---

## 6. Textures and licences

**sourced:** Pixel Perfection by Hugh "XSSheep" Rutland. The `minetest-texture-packs` GitHub repo states CC-BY-SA-4.0, and
Planet Minecraft and Minecraft Forum listings repeat it. Attribution and ShareAlike apply.

**sourced:** Pixel Perfection Legacy on Modrinth is inconsistent: its notice says CC BY-SA 4.0 while its page header says
CC-BY-4.0. **Do not rely on that fork without checking.**

**sourced:** VoxeLibre (formerly MineClone2) is licensed file by file, CC BY-SA 3.0 and 4.0 in different eras, and some
files credit Faithful-derived art. **So: no bulk import.**

**sourced:** `research/002.md` admits to copying Minecraft's textures and decompiling, and gives that as why that game
cannot be released. We do neither.

**Not established:** whether the Minetest-format Pixel Perfection copy includes mob textures. Stage 06.2 checks this.
Minetest Game's texture licence was not confirmed either.

---

## 7. What to measure first

| Stage | Question | If it fails |
|---|---|---|
| 01.1 | Do C, NASM and CUDA link cleanly? Is libstdc++ avoidable? | Record it as a documented exception |
| 01.2 | Do KMS flips work on this driver? | Ahmed decides; X11 raw protocol is the known alternative |
| 01.3 | Are rays within 8.33 ms at 1080p, R=32? | Render scale with TAA, or a lower FPS target |
| 01.4 | Is copying to scanout within 2 ms? | Reduce copy cost; revisit budgets |

---

## Sources

- [Amanatides & Woo, A Fast Voxel Traversal Algorithm for Ray Tracing](https://www.eecs.yorku.ca/~amana/research/grid.pdf)
- [Laine & Karras, Efficient Sparse Voxel Octrees](https://www.nvidia.com/docs/IO/88889/laine2010i3d_paper.pdf)
- [Laine & Karras, High-Performance Software Rasterization on GPUs](https://research.nvidia.com/publication/2011-08_high-performance-software-rasterization-gpus)
- [cgerikj/binary-greedy-meshing](https://github.com/cgerikj/binary-greedy-meshing)
- [binary-greedy-meshing Rust docs](https://docs.rs/binary-greedy-meshing/latest/x86_64-apple-darwin/struct.Mesher.html)
- [Nvidium](https://modrinth.com/project/uvBS5xOp)
- [Sodium issue 333, chunk occlusion](https://github.com/CaffeineMC/sodium/issues/333)
- [Halite, Sodium-style renderer](https://github.com/VinTeRuS/halite)
- [Performance mod overview: Lithium, C2ME and others](https://maketecheasier.com/best-performance-enhancing-mods-for-minecraft)
- [Minecraft Wiki, Terrain features](https://minecraft.wiki/w/Terrain)
- [Minecraft Wiki, Custom world generation](https://minecraft.wiki/w/Tutorial%3ACustom_world_generation)
- [How Minecraft generates worlds](https://dawnosaur.substack.com/p/how-minecraft-generates-worlds-you)
- [Pixel Perfection, CC-BY-SA-4.0](https://github.com/minetest-texture-packs/Pixel-Perfection)
- [Pixel Perfection Legacy](https://modrinth.com/resourcepack/pixel-perfection-legacy)
- [VoxeLibre](https://zff.dev/teknomunk/VoxeLibre)
- `research/001.md`, `research/002.md`
