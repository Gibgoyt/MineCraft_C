# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 4: Turning without fresh work

**Status:** in progress: corner estimate, AO cache and TAA's fast read built and tested; measuring   (updated 2026-11-13)

Read first: `plans/008-ahmed-bohemoth-smooth-frames-when-turning-phase-00-vision-and-plan-files.md`, then the master `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md`: section 1 (C3, C4), 3.4, 4 and 8. Then `cuda/src/shade/README.md` ("AO") and `cuda/src/post/README.md` ("The temporal pass"), and phase 01's C3 and C4 rows, which give the size of this phase's prize.

## Goal

Turning no longer makes the card trace four AO rays for every pixel that comes into view. TAA's history read no longer costs 16 reads a pixel on a fast turn. A still camera's picture does not change. When this phase is done:

- **A fresh pixel's first AO is a corner estimate** from the voxels around its face, with no rays, weighted as one sample. The normal one-in-four rays refine it, as today.
- **A world-space AO cache** on the card holds what AO rays found, keyed by voxel, face and place on the face. A fresh pixel looks there first, so a strip that comes back into view during a shake finds its AO already done.
- **TAA uses a 5-tap bicubic history read** when the camera turned more than 2 pixels at the picture's centre since the last frame. Below that it keeps Catmull-Rom's 16. A still camera keeps the one exact read.
- **On phase 01's settled shake at 720°/s, AO's card time (history pass plus fresh) is at most 1.3 times its still-camera time,** and post's at most 1.1 times.
- **Golden pictures and every settled still row's hash are unchanged.**

## Where this fits

**The vision.** Master C3: at 720°/s a fifth to a third of every picture is a new strip, and each pixel of it sends 4 AO rays. That is 3.8 to 5.7 times the steady AO rays, and it feeds itself, since slower pictures turn further. Master C4: a turn of any size makes every pixel read 16 history texels.

Both scale with turn speed and with nothing else, which is exactly the "when shaking" part of Ahmed's words. Phase 01 says how many milliseconds they are on his card.

**What reads it:**

| Reader | What |
|---|---|
| Phase 05 | A cheaper picture under turning means fewer steps down the ladder |
| Phase 07 | AO and post ms by turn speed before and after |
| Ahmed | How the strip at the edge looks while shaking |

**What this phase must NOT build:**

- No change to AO's result at rest. Its rays, reach, sample count, turns and spatial filter stay. A still camera converges to the same integral it does today.
- No cache of colour or sun visibility. The sun moves and colour depends on everything; the AO cache is enough for this phase. A sun-visibility cache is a separate idea, for a later plan if phase 01 shows sun rays matter.
- No change to TAA's clamp, blend or jitter.

## Ahmed's words

- "when shaking my head vigorously frame rate drops drastically we have to be smarter about what we are doing to fix this please!"

---

## Background, explained

### What a fresh pixel is today

`shade_ao_at` (`cuda/src/shade/index.cu:202-271`) finds where a pixel's surface was in the last frame with `post_reproject`, then looks at that place's 3×3 pixels for the same plane (`shade_ao_same_surface`):

- **its own place on the same plane:** it goes on from its mean;
- **only neighbours on the same plane:** their mean, weighted as one sample;
- **neither** (the pixel's surface was off the last picture, or hidden): the pixel goes on the fresh list, and `shade_ao_fresh_kernel` sends `SHADE_AO_FRESH_RAYS = 4` rays at once.

The list runs as a list, so its warps are full, which plan 001 stage 17.3 measured as the right way to send them. The cost is the number of rays, not their coherence.

### Corner AO, with no rays

Minecraft darkens a block face's corners by the solid blocks beside them. For a face with normal $n$, take each of its four corners in turn. Let $s_1$ and $s_2$ be the two cells beside that corner in the face's plane, one step along $n$ out, and $c$ the diagonal cell between them. Each is 1 if solid, 0 if not. The corner's value is:

$$v = \begin{cases} 0 & s_1 = s_2 = 1 \\ 1 - (s_1 + s_2 + c)/3 & \text{otherwise} \end{cases}$$

Then it is mapped to AO's range by `visible = 0.4 + 0.6 · v`. The 0.4 floor is this file's guess, not a measurement. Step 2 reads what ray AO converges to in a two-wall corner, from a still picture's AO history, and sets the floor to that. The four corners are blended bilinearly by where on the face the pixel's hit is (`shade_surface_t.local`).

**Examples**, on paper (the test checks them):

| Face | Cells | Each corner | Visible |
|---|---|---|---|
| A top face in an open field | All 0 | $v = 1$ | 1.0 everywhere |
| A floor with one wall along its north edge | The north corners have $s_1 = 1$ | North corners $1 - 1/3$, so 0.8 | 0.8 at the north edge, 1.0 at the south, 0.9 in the middle |
| A floor in a two-wall corner | One corner has $s_1 = s_2 = 1$ | That corner $v = 0$ | 0.4 there |

It costs at most 8 cell reads a face, through the tree, cached per warp as `shade_cell_material_kept` does. Four AO rays of up to 12 steps each are many more. **What counts as solid:** a cell whose material is opaque and whole (the far field's stop table, `frame_lod_stops`, `run/src/render/frame/index.c:215-227`, already says which blocks those are).

### The world-space cache

**Why world space.** A head shake goes left and right. The strip that comes into view on the left was on screen 100 to 300 ms ago, then went off the left edge, and screen-space history dropped it then. The surfaces have not moved. So their AO is still right, if it was kept somewhere that does not scroll with the screen.

**The table.** One entry is 8 bytes:

| Field | Bits | What |
|---|---|---|
| tag | 16 | The key's high bits, to tell collisions |
| visible | 16 | AO's mean, 0 to 1 in 1/65535 |
| samples | 8 | Up to `SHADE_AO_SAMPLES_MAX` (32) |
| face | 3 | 0 to 5 |
| age | 21 | The frame it was written, modulo 2²¹ (about 9.7 hours at 60) |

2 M entries make 16 MiB.

**The key.** The voxel's world cell (x, y, z), the face, and the hit's place on the face in a 4×4 grid, with a section revision so an edit breaks it (below). Hashed with a fixed 64-bit mix (the one `shade_ao_hash` uses, widened). The slot is the low 21 bits; the tag is 16 bits from the top.

**Writes.** Every AO ray result (`shade_ao_kernel`'s turn, and fresh rays) is folded into its entry as a running mean, by `atomicCAS` on the 8-byte word, up to 4 tries. If it loses all 4, it drops the write: it is only a cache.

**Reads.** A fresh pixel reads its entry. If the tag matches, the face matches, and the age is under 4 s (240 frames), it takes the mean with its sample count capped at 8, so new rays can still move it. Otherwise it uses the corner estimate. A 4×4 grid is coarser than pixels near the eye, so a near face's AO from the cache is the mean of a quarter-block square. That is a little softer for one frame, and then the pixel's own rays take over.

**Edits.** The column table's `generation` (`run/include/gpu_bridge/world_buffers/index.h:100`) changes per load, not per edit. So step 1 checks whether a re-sent section always gets a new slab offset (`world_buffers_column_t.sections[z]`):

- if it does, that offset is the key's revision;
- if not, a 16-bit edit counter goes in the column table beside `sections`, bumped by `run/src/render/upload/` when it sends a section again.

Either way, an edited section's old entries never match again.

### TAA's 5-tap read

Catmull-Rom over 4×4 texels can be had from 5 bilinear reads: the centre 2×2 block as one weighted read, plus the four arms (Jimenez, "Filmic SMAA", SIGGRAPH 2016 course notes; also in Unreal's and others' TAA). It drops the four corner texels, whose weights are small. Without hardware filtering (the history is a plain array here, not a texture) each bilinear read is 4 loads. So it is 20 loads, not 16. The saving is in the multiply-adds and in reading as two rows of contiguous pairs.

So this is **measured, not assumed.** If 5-tap is not faster on the card, it is dropped and recorded, and the cheaper alternative is plain bilinear (4 loads) above 2 pixels of turn, where motion hides its softness.

### Why 2 pixels

At a picture's centre, one pixel is 0.065° (70° over 1080 rows). A 2-pixel turn between pictures is 0.13° a picture, 7.8°/s at 60 pictures. Below that the camera is nearly still and Catmull-Rom keeps sharpness. Above it the eye cannot follow fine detail anyway.

---

## Design

### `cuda/src/shade/`

- **`shade_ao_corner`:** inline, host and device, in `run/include/gpu_bridge/shade/index.h` beside its siblings, so a host test runs the same lines. It takes the 8 cell flags and the place on the face, and returns `visible`.
- **`shade_ao_cache_*`:** key, slot, tag, pack and unpack, inline, host and device, tested on the host.
- **`shade_ao_at`:** where today it sets `*out_fresh`:
  1. look in the cache;
  2. on a miss, take the corner estimate;
  3. in either case go on as a pixel with one sample, or up to 8 from the cache.

  Only if the cache and the corner both cannot answer (a face of a shape that is not a cube: slabs, stairs, plates) does it go on the fresh list as today.
- **`shade_ao_kernel` and `shade_ao_fresh_kernel`** write their results into the cache.
- **The table** is a new device block, `borrowed_ao_cache`, in `post_temporal_t` beside AO's history, from the frames budget (master section 6).

**Q2 switch:** `SHADE_FEATURE_AO_FIRST_GUESS` (on by default) selects the corner-plus-cache path. Off, it is today's 4 fresh rays. `--fresh-ao-rays 1` is the fallback B for comparison.

### `cuda/src/post/`

`post_history_at` gains a `fast` argument. `post_taa_kernel` works out the turn at the centre once per frame on the host, as the angle between this camera's and the previous camera's forward vectors over the pixel size. It passes `fast` in `post_temporal_t`.

The still read and the Catmull-Rom read are unchanged.

### Settings

None new for Ahmed. The feature bit is an effect bit like the others, on by default. `bench_world --features-off` can turn it off for comparison.

---

## Owned paths

This phase may edit these, plus this file:

- `cuda/src/shade/`, `run/include/gpu_bridge/shade/`
- `cuda/src/post/`, `run/include/gpu_bridge/post/`
- `run/src/render/frame/` (the cache block in `post_temporal_t`, the turn at the centre), `run/include/render/frame/`
- `run/src/render/upload/`, `cuda/src/world_buffers/`, `run/include/gpu_bridge/world_buffers/` (only if the edit counter is needed)
- `run/src/game/` where the frame blocks are made (the cache block's allocation)
- `tests/cuda/` (the `render_feature_*` suites), `tests/run/render/frame/`
- `bench/world/` (`--fresh-ao-rays`)

## Steps

1. **Check how a re-sent section's slab offset behaves;** decide the edit key. *Why: the cache must never show AO from before an edit.*
2. **`shade_ao_corner` with its paper tests.** *Why: the cheapest part, and it stands alone.*
3. **The cache's pure parts (key, slot, tag, pack) with host tests.** *Why: bugs there are silent.*
4. **The cache on the card:** writes from AO rays, reads for fresh pixels, the age rule. *Why: the part that pays during a shake.*
5. **TAA's fast read:** measure 5-tap against bilinear against Catmull-Rom on the settled shake; keep the fastest that passes the still-picture check. *Why: master C4, measured not assumed.*
6. **Phase 01's settled shake at every speed, AO on and off, the switch on and off; the live-capped rows.** *Why: the targets.*
7. **Look at the strip:** shots from `jagged_peaks/shake-720` at frames 10, 11, 12, today's path against this one, side by side. Describe them below. *Why: the picture changes for a moving camera; it must look right.*
8. **READMEs** (`cuda/src/shade/README.md` "AO", `cuda/src/post/README.md` "The temporal pass"); tick todos as they land.

## Todo

- [x] The edit key decided (slab offset, or a per-section counter) and recorded below: neither, a hash of each section's bytes
- [x] `shade_ao_corner`, host and device, with its paper tests
- [x] `shade_ao_cache_*` pure parts with host tests
- [ ] The cache block (2 M entries, 16 MiB) in `post_temporal_t`; frames budget measured
- [x] AO rays write the cache; fresh pixels read it, else take the corner estimate; shapes that are not cubes keep the fresh list
- [x] `SHADE_FEATURE_AO_FIRST_GUESS`, on by default; `--fresh-ao-rays 1` for comparison
- [ ] TAA's fast read chosen by measurement (5-tap, bilinear, or none) and recorded below
- [ ] Settled shake at 0 to 1440°/s: AO ms at 720 ≤ 1.3 × still, post ≤ 1.1 × still; results below
- [ ] Live-capped shake rows; results below
- [ ] Strip shots looked at and described below
- [ ] Golden pictures and every settled still row's hash unchanged; moving rows' new hashes recorded after looking
- [ ] READMEs

## Results (filled in by this phase)

| Row | AO + fresh ms, before / after | Fresh rays a picture, before / after | Cache hit share on fresh pixels | Post ms, before / after | Interval p99 (live-capped), before / after |
|---|---|---|---|---|---|
| `spot/shake-0` | | | | | |
| `spot/shake-360` | | | | | |
| `spot/shake-720` | | | | | |
| `spot/shake-1440` | | | | | |
| `spot/shake-720-live-capped-r64` | | | | | |

## Tests

### Host tests (in the shade suite, as `shade_ao_*` are tested today)

- **`corner_ao_in_an_open_field_is_one`.** All 8 cells empty: 1.0 at every place on the face.
- **`corner_ao_beside_one_wall_is_point_eight_at_that_edge`.** The paper example above: 0.8 at the wall's edge, 1.0 at the far edge, 0.9 in the middle, ± 0.001.
- **`corner_ao_in_a_two_wall_corner_is_the_floor`.** 0.4 at that corner.
- **`a_cache_entry_round_trips`.** Pack, unpack: every field the same.
- **`a_tag_mismatch_is_a_miss`.** Two keys with the same slot and different tags: the second reads a miss.
- **`an_old_entry_is_a_miss`.** Age 241 frames: miss. 239: hit.
- **`an_edit_breaks_the_key`.** The same cell and face with the next revision: a different key.

### `render_feature_post` and `run_render_frame` (extended)

- **`a_strip_that_comes_back_hits_the_cache`.** A settled world. Frames 1 to 20 still; frames 21 to 30 turned 30° left, then back. On the frame back, at least 90 % of fresh pixels hit the cache, and no AO ray is sent for them.
- **`a_block_placed_is_not_shown_with_old_ao`.** Look at a floor, turn away, place a block on that floor, turn back. The floor beside the block has no cache hit from before the edit (its AO comes from the corner estimate, which sees the block, then rays).
- **`a_still_camera_converges_to_todays_ao`.** 200 still frames, with the switch on and off: every pixel within 1/255.
- **Golden pictures** unchanged (they are still cameras).

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                         # must print nothing before the bench runs
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(render_feature_.*|cuda_traverse)$' --output-on-failure
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_render_frame$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu_p04.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --record bench/world/history.tsv --label "008 phase 04: fresh AO and TAA" --shots /tmp/mc_c_shake/p04; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --only shake --fresh-ao-rays 1
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --only orbit
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"; pgrep -af gpu_watch                       # both must print nothing

## Gate

Ahmed plays, shakes his head hard at his spot and at a place with trees, and watches the edges of the screen as they come in. He says:

1. Does the edge look right while turning, or is there a band that looks different, too bright or too dark, for a moment?
2. Is it smoother than after phase 03?
3. Q2: keep A (corner plus cache), or try B (one ray)?

His answers are recorded below.

## Risks and open questions

| Risk or question | Default |
|---|---|
| The corner estimate's look differs from ray AO, so the strip flashes lighter or darker | Weighted as one sample, refined within about 0.5 s at one ray in four; the floor (0.4) is checked against ray AO in phase 01's still pictures; Ahmed judges |
| The cache shows AO from before an edit | The edit key (step 1); the edit test |
| Hash collisions show another face's AO | A 16-bit tag and the face; at most a 1 in 65536 wrong match, and only for one frame's weight |
| `atomicCAS` contention on hot entries | Up to 4 tries, then the write is dropped |
| 16 MiB tips frames over their 256 MiB budget | Measured; 1 M entries (8 MiB) if needed |
| 5-tap is not faster without hardware filtering | Measured; bilinear above 2 pixels, or no change, recorded |
| Shapes that are not cubes still send fresh rays | They are a small share of most pictures; measured in the fresh count; a later step if phase 01 says otherwise |
| Moving rows' hashes change | Expected; recorded after looking at the shots; still rows must not change |

## Commits

Appended as they land:

## Issues found

**Step 1, the edit key: neither of the plan's two.** A section sent again at its size is written in place (`world_buffers_section_write`'s `in_place`), so its slab offset does not tell an edit. The first try was the plan's second choice, a per-section count of sends in the column table's 96 spare bytes (`_pad1`), counted in the 27 sections around each, and it made the picture hang on the threads: streaming sends the same bytes again at times the workers decide, every send changed the keys, and `the_marks_change_no_picture` (two games turning from the same opening) failed one run in two. So the 96 bytes are `world_buffers_column_t.revisions`: 8 bits of a hash of each section's bytes, never 0 for a section with bytes. The same bytes sent again keep it; an edit changes it all but one time in 256 (then the AO shown is at most 4 s old and weighs 8 samples). A key holds the revisions of every section within 4 blocks of the face's open cell (`shade_ao_revision`: its own, and across any border nearer than `SHADE_AO_REACH` + 1), and its column's load generation, so a block placed across a chunk border breaks the keys on both sides (`a_block_placed_is_not_shown_with_old_ao` puts it on one).

**Step 2, the floor: 0.25, and the plan's paper had the wall case wrong.** Ray AO at the vertex of a two-wall corner is a quarter of the cosine-weighted sky, exactly, and 0.277 at a sixteenth of a block in (the same integral render_feature_post's AO reference adds up, worked numerically over 9216 cosine-weighted directions: walls 3 blocks or taller, reach 3). So `ao_corner_floor` is 0.25, in `compile/config/render/shade/index.yaml`. A wall along one edge makes that edge's side cell and both its diagonals solid, so its corners are 1 - 2/3, not 1 - 1/3: with the floor, 0.5 at the wall, 0.75 between, 1 at the far edge, against ray AO's 0.53 and 0.69 (the plan said 0.8 / 0.9 / 1 with a floor of 0.4). The test is named `corner_ao_beside_one_wall_is_half_at_that_edge`.

**What changed from the plan, and why:**

- **The cache is written by `atomicMax`, not folded in by `atomicCAS` as a running mean.** A running mean by CAS depends on the order the card runs the threads in, and `the_same_frames_give_the_same_picture_to_the_byte` holds the renderer to one picture from one script. So an entry is a pixel's own mean after its ray this frame (up to 32 samples, a better value than one ray's), packed from the high bits as age (21), samples (8), tag (16), face (3), mean (16): the newest frame wins, then the most samples, the same word whatever the order. `shade_ao_kernel` writes; `shade_ao_fresh_kernel`, after it, reads; and when it reads it writes nothing (a fresh pixel that sends rays then, on a slab or a plant, does not write). The table is emptied once in 2^21 frames, as the ages wrap.
- **The first guess runs only while the camera moves** (`POST_TEMPORAL_MOVED`: this frame's camera without jitter is not the last's). A still camera with TAA has a few hundred fresh pixels a frame from the jitter (phase 01), and taking the corner for those would have changed still pictures; with this they send their four rays as before, and still pictures are the same to the bit (`a_still_camera_converges_to_todays_ao` holds 200 frames to the byte with it on and off).
- **The fresh pixels are filled in `shade_ao_fresh_kernel`, from the list, not in `shade_ao_at`,** so the cache is read after every write of the frame (above), and the list still makes the warps full.
- **Where the cache's block lives.** `render_frame_t` (960 bytes, cache-line aligned, 3 spare) and `render_t` have no room for a `device_block_t`, and `app_t` is at main's frame limit. So its handle is in the frame arena (`frame_ao_cache_take`, from `DEVICE_POOL_FRAMES`, 16 MiB, zeroed), and `post_temporal_t.borrowed_ao_cache` points at it; `render_frame_deinit` gives it back. To make room for that pointer, `post_temporal_t.jitter[2]` became a bit, `POST_TEMPORAL_JITTER`: every reader works it out from the frame's number with `post_temporal_jitter`, which the card computes to the same bits as the host (each step is a division or a sum of an exact product). The struct's last byte became `flags`: `JITTER`, `MOVED`, `FAST`, `FIRST_GUESS`, `FRESH_ONE`. Without room on the card the frame runs with no cache, and fresh pixels take the corner estimate alone.
- **`SHADE_FEATURE_AO_FIRST_GUESS` is not an effect of the Settings screen.** `run_settings` holds that every bit of `SHADE_FEATURES_EVERY` has a setting, and the plan wanted none new for Ahmed. So it is as `SHADE_FEATURE_SUN_APART`: past `SHADE_FEATURES_EVERY`, to `render_frame_features_set` only, which turns it into a flag; the game adds it unless its `features_off` has it (`game_frame_features`), so `bench_world_shake --features-off 80000` turns it off and a settings file of before reads the same. `--fresh-ao-rays 1` (`render_frame_ao_fresh_one_set`) is Q2's B; `--fresh-ao-rays 4` is the same as `--features-off 80000`.
- **The tests are a suite of their own, `render_feature_ao` (`tests/cuda/ao/`),** as `tests/cuda/post/index.c` is at 1435 lines. The world buffers' revision test is in `run_render_upload`.
