# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 3: Overlapping the card's work

**Status:** in progress: the overlap built and tested; measuring   (updated 2026-11-12)

Read first: `plans/008-ahmed-bohemoth-smooth-frames-when-turning-phase-00-vision-and-plan-files.md`, then the master `plans/008-ahmed-bohemoth-smooth-frames-when-turning.md`: section 1 (C7), 3.3, 5 and 8. Then phase 01's C7 row ($P$ by part, readback ms) and phase 02's start rule.

## Goal

The card never waits on the picture's trip home or on the gpu thread's CPU work, and the pictures are the same, byte for byte. When this phase is done:

- **The readback runs on a second stream,** from one of two device pictures written in turn. Frame N's copy home overlaps frame N+1's coarse and primary passes.
- **The gpu thread keeps one frame in flight.**
  - It queues frame N+1's kernels, then waits on frame N's end event instead of `cudaDeviceSynchronize` (`cuda/src/frame/index.cu:182`).
  - The CPU part of $P$ (upload planning, far-field planning, fill, camera, entities, weather) overlaps the card's work.
- **The per-frame copies are asynchronous:** upload sections, the column table, entity boxes and the weather map go by `cudaMemcpyAsync` from pinned staging. Each staging area is reused only after its frame's end event has passed.
- **Uploads still run on the frame's stream, between frames,** as `cuda/src/frame/README.md` requires for slab reuse.
- **A kernel fault is still reported,** with its own words, at most one frame later.
- **`--serial-frames`** keeps today's path, and the pictures of both paths are the same byte for byte.
- **The card's time between pictures falls by the readback** (1.38 ms at 1080p on the capped card) **and by most of $P$.**

## Where this fits

**The vision.** Master C7. Every picture, the card spends 1.38 ms copying the picture home with no kernel running. Then it idles while the gpu thread does $P$ for the next picture, because `cudaDeviceSynchronize` and synchronous copies make the two take turns. Neither is turning-specific. But on a capped card they are 10 to 20 % of the slot, and they count against every picture in phase 02's rule.

**What reads it:**

| Reader | What |
|---|---|
| Phase 05 | A shorter $P + G$ means fewer steps down the resolution ladder |
| Phase 07 | Card time and intervals before and after |

**What this phase must NOT build:**

- No change to any kernel's work or any picture.
- No second stream for uploads. The slab rule stays as it is.
- No more than one frame in flight. Two would double the frame blocks and the latency.

## Ahmed's words

- "we have to be smarter about what we are doing to fix this please!"

---

## Background, explained

### Streams, events and the legacy stream

A CUDA **stream** is a queue of work the card runs in order. An **event** is a mark in a stream; the host can wait for it, or another stream can wait for it.

Today every kernel and every copy is on the legacy default stream. A synchronous `cudaMemcpy` from host memory waits for everything queued before it, then copies, then returns (`cuda/src/device_memory/index.cu:289`, `cuda/src/world_buffers/index.cu:103,371,402,413`). So even without `cudaDeviceSynchronize`, the gpu thread's first upload copy for frame N+1 would wait for frame N's kernels. That is why this phase moves those copies to `cudaMemcpyAsync` from pinned memory: they then only queue, and the host goes on.

### Why two device pictures

Post writes the XRGB picture (`targets.borrowed_xrgb`). If the copy stream is still reading frame N's picture while frame N+1's post writes it, the picture tears. With two device pictures written in turn, N+1's post writes the other one. The copy stream waits on frame N's "post done" event before reading.

That is 8.3 MB more of the card at 1080p, inside the frames budget (master section 6).

### One frame in flight, step by step

For frame N+1 the gpu thread:

1. Plans upload, far field and fill on the CPU while frame N's kernels run.
2. Queues N+1's async copies and kernels on the frame stream, and records `end[N+1]`.
3. Queues the copy home of N+1's picture on the copy stream, after waiting on `post_done[N+1]`, and records `home[N+1]`.
4. Waits on `home[N]` (the picture main will take next), checks `end[N]`'s status, publishes picture N, and releases N's staging.

So main gets picture N while N+1 is on the card. The gpu thread's wait is for the copy of N, which overlapped N+1's first passes.

**Latency:** a picture is published when its copy is done, as today. The camera of N+1 is read while N is still on the card, which is earlier than in phase 02's just-in-time start. So phase 02's rule aims at `home[N+1]` and keeps the just-in-time start. This phase must not make the camera older: phase 01's camera-to-screen p50 is the check.

### Staging lifetime

An async copy reads its host memory when the card gets to it, not when it is queued. So a staging area written for frame N cannot be rewritten until `end[N]` has passed. Staging is a ring of two, one per frame in flight, sized as today's per-frame upload budget (4 MiB, plan 005 phase 14) plus the column table, entities and the weather map.

---

## Design

### `cuda/src/frame/`

- `frame_gpu_t` gains:
  - a copy stream, made with `cudaStreamNonBlocking`;
  - events `post_done[2]`, `end[2]` and `home[2]`;
  - `xrgb[2]` targets.
- **`frame_gpu_queue`:** queues one frame's kernels and its copy home, and returns at once.
- **`frame_gpu_finish`:** waits on `home` of a queued frame, reads its timings (the existing events, now per slot), and reports any fault from `cudaEventSynchronize`'s status.
- **`frame_gpu_run`** stays, as `queue` then `finish` of the same frame, for `--serial-frames` and every existing caller (tests, the headless path, `bench_world`'s settled rows).

### `run/src/render/frame/`

`render_frame_draw_weather` (`run/src/render/frame/index.c:650-764`) splits into `render_frame_queue` and `render_frame_finish`. The temporal state's swaps (`index.c:737-750`) happen at queue time, since they describe the order of frames, not their completion. The exchange's publish happens at finish.

### Async copies

- **`device_memory_write_async`** beside `device_memory_write`: takes a pinned staging pointer and the frame's stream.
- **`world_buffers_flush`** and `upload_sections_send` (`run/src/render/upload/index.c`) write into the staging ring, then queue async copies. The far field's `world_buffers_flush` per level does the same.
- **Entities and the weather map** go the same way.
- **Staging:** pinned with `frame_host_pin` (`cudaHostRegister`), from the World arena, so `run/src/heap/` stays the only allocator, as `cuda/src/frame/README.md` says of the pictures.

### The gpu thread

`game_gpu_thread` (`run/src/game/index.c:1456`) runs, after phase 02's start rule:

1. The CPU part: upload planning, far-field planning, fill, day, weather, entities.
2. `render_frame_queue(N+1)`.
3. `render_frame_finish(N)`: publish picture N.

On a fault it stops as today, with the error from `finish`.

### `--serial-frames`

A flag for the game and `bench_world` that uses `frame_gpu_run` for every frame, as today. It exists for tests and to compare.

---

## Owned paths

This phase may edit these, plus this file:

- `cuda/src/frame/`, `run/include/gpu_bridge/frame/`
- `cuda/src/device_memory/`, `run/include/gpu_bridge/device_memory/` (`device_memory_write_async`)
- `cuda/src/world_buffers/`, `run/include/gpu_bridge/world_buffers/` (async flush)
- `run/src/render/frame/`, `run/src/render/upload/`, `run/src/render/lod/` (flush only), `run/src/render/entities/` (send only), `run/src/game/weather/` (the map's send only)
- `run/src/game/index.c` (`game_gpu_thread`), `run/src/main/` (`--serial-frames`)
- `tests/run/render/frame/`, `tests/run/render/upload/`, `tests/cuda/bridge/`
- `bench/world/` (`--serial-frames`)

## Steps

1. **Two device pictures and the copy stream, still waited for at the end of each frame.** Pictures byte for byte. *Why: the smallest step that moves the readback off the frame stream.*
2. **`frame_gpu_queue` and `frame_gpu_finish`,** and `frame_gpu_run` on top of them. *Why: the shape every later step needs, with the old path intact.*
3. **`device_memory_write_async`, the staging ring, async flushes.** The staging test. *Why: without these, a synchronous copy brings back the wait.*
4. **The gpu thread with one frame in flight.** *Why: the overlap itself.*
5. **Faults:** a test kernel that faults on purpose (the existing fault path's test, if one exists, or a new one in `tests/cuda/bridge/`), reported within 2 frames. *Why: master section 8.*
6. **Measure:** phase 01's rows, with and without `--serial-frames`. *Why: the target.*
7. **READMEs:** `cuda/src/frame/README.md` ("Why it is the way it is" changes: two streams, one frame in flight), `run/src/render/upload/README.md`. Tick todos as they land.

## Todo

- [x] Two device pictures, written in turn; the copy stream; `post_done`, `end` and `home` events per slot
- [x] `frame_gpu_queue`, `frame_gpu_finish`; `frame_gpu_run` as both, unchanged for every caller
- [x] `render_frame_queue` and `render_frame_finish`; temporal swaps at queue time, publish at finish
- [x] `device_memory_write_async`; a pinned staging ring of two in the World arena
- [x] Upload sections, the column table, far-field flushes, entities and the weather map sent by async copies from staging
- [x] `game_gpu_thread` keeps one frame in flight; phase 02's rule aims at `home`
- [x] `--serial-frames` for the game and `bench_world`
- [x] A fault is reported within 2 frames with its own words (test)
- [x] Pictures byte for byte with and without `--serial-frames`; golden pictures and settled hashes unchanged
- [ ] Phase 01's rows again; results below
- [x] READMEs

## Results (filled in by this phase)

| Row | Card ms a picture, serial / overlapped | Readback on the critical path, ms | $P$ seen by the card, ms | Interval p50 / p99 | Camera-to-screen p50 |
|---|---|---|---|---|---|
| `spot/shake-0` | | | | | |
| `spot/shake-720-live-capped` | | | | | |
| `spot/shake-720-live-capped-r64` | | | | | |
| `plains/flight-live` (uploads busy) | | | | | |

## Tests

### `run_render_frame` (extended)

- **`overlapped_frames_draw_the_same_pictures`.** 60 frames of a scripted flight, with uploads and far-field changes, drawn with `--serial-frames` and without. Every picture is the same byte for byte.
- **`the_real_threads_draw_the_same_picture`** (existing) with the overlap on.
- **Golden pictures** unchanged.

### `run_render_upload` (extended)

- **`staging_is_not_reused_while_its_copy_is_queued`.** Writes a pattern in frame N's staging, queues frame N+1 with a different pattern, checks the card got N's pattern for N's sections and N+1's for N+1's, over 3 frames, with a slow kernel added to keep the card busy.

### `render_feature_bridge` (`tests/cuda/bridge/`, extended)

- **`a_fault_is_reported_within_two_frames`.** A kernel that reads out of bounds on purpose, under the debug build's checks. `render_frame_finish` of that frame or the next returns the error, with "frame:" in its words.

## Verification

    pgrep -af 'build/release/bin/minecraft_c'                         # must print nothing before the bench runs
    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^run_render_frame$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_render_upload|run_render_lod|run_render_distance_live|render_feature_bridge|run_game_pace)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 1200 ctest --preset sanitize -R '^(run_render_frame|run_render_upload)$' --output-on-failure
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu_p03.csv & w=$!; timeout 900 build/profile/bin/bench_world --only shake --record bench/world/history.tsv --label "008 phase 03: overlap"; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world --only shake --serial-frames
    tools/test-or-build.sh cmake --build --preset release
    pgrep -af "$PWD/build"; pgrep -af gpu_watch                       # both must print nothing

The threads change too, so the `tsan` preset runs the real-threads test:

    tools/test-or-build.sh cmake --build --preset tsan
    tools/test-or-build.sh timeout 1200 ctest --preset tsan -R '^run_render_frame$' --output-on-failure

## Gate

This phase changes nothing Ahmed can see but speed. He reads the results table: card ms a picture before and after, and the same pictures. He may play and say whether shaking feels any different from phase 02. The decision is whether to keep the overlapped path as the default; `--serial-frames` stays for tests either way.

## Risks and open questions

| Risk or question | Default |
|---|---|
| Two streams break the slab rule | Uploads stay on the frame stream; the copy stream only reads a finished picture after an event; the same-pictures test over a flight with uploads |
| Async copies read staging after it was rewritten | A ring of two released by `end` events; the staging test |
| A fault surfaces a frame late and names the wrong frame | `finish` reports which frame; the test; `--serial-frames` to bisect |
| The camera gets older with one frame in flight | Phase 02's rule aims at `home`; camera-to-screen p50 is a target (master section 4) |
| `cudaHostRegister` of staging fails or is slow | Done once at start, as for the pictures; fails the start with its words, as today |
| The far field's per-level flushes are many small copies | Batched into one staging area per frame, queued in order; measured |
| Little is gained if phase 01 shows $P$ is small | Then the readback (1.38 ms) is the gain, recorded as such |

## Commits

Appended as they land:

## Issues found

None.
