# 008-ahmed-bohemoth-smooth-frames-when-turning — phase 3: Overlapping the card's work

**Status:** done, for Ahmed's gate: the readback is off the card's critical path, the pictures are the same byte for byte, camera to screen as serial   (updated 2026-11-13)

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
- [x] Phase 01's rows again; results below
- [x] READMEs

## Results (filled in by this phase)

All on Ahmed's card (RTX 2080 Super, his game closed, checked before each run), `build/profile/bin/bench_world_shake` at his spot (`--at -80 134 418 --seed 14148492376765118422`), 1920x1080, every effect, the sidecar beside the whole run (`bench/world/gpu_watch.sh`, `/tmp/mc_c_shake/gpu_p03d.csv`, not kept). **Serial and overlapped alternated in one run**, twice each, then r64 once each, recorded in `bench/world/shake/history.tsv` as "008 phase 03: serial, final, ..." and "008 phase 03: overlap, final, ...". **The card was at its thermal floor all along:** `clock64` 148 to 187 MHz mean per row (readings 147 to 532), 42.5 W mean of a 50 W limit. So a still picture costs 31 ms here, not phase 01's 9.8 ms at 547 MHz, and the gain below is a small share of it; at 600 MHz the same 1.3 ms would be 13 % of a still picture.

"Card ms a picture" is the row's time over every picture the gpu thread drew (taken or not): back to back, the card's time between pictures. "Readback on the critical path" is that less the card's own time a picture with the readback taken out (card mean - readback): what of the copy home, and of the gap between frames, the card still waits for. "P seen by the card" is the gpu thread's own part (start to all queued); overlapped, the next picture is queued before the card is free, so the card sees none of it.

| Row | Card ms a picture, serial / overlapped | Readback on the critical path, ms, serial / overlapped | $P$ seen by the card, ms, serial / overlapped | Interval p50 / p99, serial / overlapped | Camera-to-screen p50, serial / overlapped |
|---|---|---|---|---|---|
| `spot/shake-0` (settled, no threads) | 31.22 / 31.22 @ 149 MHz (the same path: `frame_gpu_run`) | 1.27 / 1.27 | none, no threads | 31.22 / 31.22 | - |
| `spot/shake-0-live-capped`, passes 1 and 2 | 30.12, 30.94 / **29.89, 29.89** @ 160 to 187 MHz | 1.85, 1.50 / **0.22, 0.27** (readback 1.27 / 1.41) | 0.08 / 0 (0.08 to 0.09 on the CPU) | 33.31 / 33.37, 33.31 / 33.37 // 33.30 / 33.37, 33.30 / 33.38 | 34.93, 35.33 / 35.50, 35.58 |
| `spot/shake-720-live-capped`, passes 1 and 2 | 52.01, 52.14 / **50.40, 50.60** @ 162 to 183 MHz | 1.53, 0.63 / **0.04, -0.42** (below 0 is noise: the card mean is over the pictures taken, the row's time over all drawn) | 0.11 / 0 | 50.00 / 66.70, 50.01 / 66.70 // 50.00 / 66.69, 50.00 / 91.63 | 56.64, 56.23 / 55.52, 56.22 |
| `spot/shake-720-live` (uncapped), passes 1 and 2 | 52.03, 52.95 / **50.64, 51.27** @ 181 to 185 MHz | 1.38, 2.15 / **0.43, 0.85** | 0.11 / 0 | 50.00 / 66.67, 50.00 / 66.75 // 50.00 / 66.69, 50.00 / 66.69 | 55.80, 58.19 / 56.00, 55.74 |
| `spot/shake-720-live-capped-r64` | 54.65 / 55.27 @ 180 / 179 MHz | 1.51 / 0.39 | 0.13 / 0 | 50.03 / 75.00 // 58.31 / 75.00 | 58.05 / 61.34 |
| `plains/flight-live` (uploads busy) | not run: `bench_world_shake` has no flight row, and `bench_world` (at 1494 lines) did not get one. The overlap with uploads, far-field flushes and entity lists is checked for the same pictures by `overlapped_frames_draw_the_same_pictures`, not timed | | | | |

Pictures a second, serial / overlapped: still 33.1, 32.1 / 33.4, 33.4; 720°/s capped 19.1, 18.8 / 19.6, 19.3; uncapped 19.1, 19.1 / 19.7, 19.7; r64 18.2 / 17.9 (that overlapped row had one 121 ms picture, and its card mean is 2 ms over the serial one's at the same clock: noise of the content, not the overlap, whose critical readback there is 0.39 ms).

**What it says.**

- **The readback is off the card's critical path.** Serial, the card waits 1.3 to 2.2 ms a picture past its kernels (the copy home and the gap to the next frame); overlapped, 0 to 0.85 ms, 0.2 to 0.3 on the still rows. 99 to 100 % of overlapped pictures were begun with the one before on its way.
- **$P$ was never the cost:** the gpu thread's part is 0.08 to 0.13 ms (phase 01 said 0.03 to 0.16). The gain is the readback, as the plan's last risk said it would be: about 3 % more pictures a second at this clock, as a 52 ms picture loses 1.3 ms.
- **Camera to screen is as serial** on every 32-chunk row (within 0.6 ms, either way). The one r64 row is 3.3 ms worse with its 121 ms spike; its p50 interval is a slot later too.
- **The copy home is a little slower overlapped:** 1.41 to 1.44 ms against 1.27, as it now runs beside the next frame's kernels.
- **Settled pictures are the same:** `spot/shake-0`'s hash is BA28CEC20AD658FA in every run, as in phase 01 and 02's history.
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

- `44c7a824` Overlap the card's work, plan 008 phase 03: the picture goes home on a second stream from one of two device pictures written in turn, the gpu thread keeps one frame on its way when the start rule would begin the next at once, and every per-frame copy to the card goes through a pinned staging ring of two; --serial-frames keeps the old path, and the pictures of both are the same byte for byte.
- `3d5e1dac` Begin the next picture only 3 ms before the one on its way should be done, by the last picture's time, so the camera is read no earlier than without the overlap; hand the one on its way over first if it is done already; and have bench_world_shake say camera to screen p50 for each live row.
- `2ce56f4c` Begin the next picture once the one on its way is whole on the card, and queue its copy home only behind the next frame's copies and kernels: a copy to the card queued after a copy home still waiting for its picture waited for it, so the card started the next frame only after the whole readback. Now the readback leaves the critical path and camera to screen stays as serial; bench_world_shake also says the gpu thread's own part, start to queued.
- this commit: the measurements, the results, and the verification.

**Verification, as run (2026-11-13, on `e64a93db`, main unmoved since):** his game was not running before any GPU run (`pgrep` printed nothing). `cmake --build --preset debug`: clean. `run_render_frame` (goldens unchanged, the three new cases among them), `run_render_upload`, `run_render_lod`, `run_render_distance_live`, `render_feature_bridge`, `run_game_pace`, `run_game`, `run_profiler`, `run_failure`, `standards_lint`, `clang_format`: all passed. `--preset sanitize`: the whole build stops at `tests/fuzz/support` built for the callgraph without ASan (its own `#error`; not this phase's), so `minecraft_c`, `run_render_frame` and `run_render_upload` were built as targets: both passed under ASan. `--preset tsan`: the whole build stops at `tests/run/ui/hud/corner/index.c:768` (`-Werror=maybe-uninitialized`, not this phase's), so the same targets: every case of `run_render_frame` passed, and ThreadSanitizer reported one race, between `the_real_threads_draw_what_one_thread_draws...`'s atomic read of `game->stream_quiet` (test line 1360) and the sim's plain write of it in `game_columns_request` (`run/src/game/index.c:1121`): neither is changed here, and none involves the gpu thread, the frames on their way or the staging. `cmake --build --preset profile` and `--preset release`: clean, release's 16 KiB frame limit held. The bench lines ran as `bench_world_shake` with the sidecar, serial and overlapped alternated (one script through the lock), recorded in `bench/world/shake/history.tsv`. `pgrep -af "$PWD/build"` and `pgrep -af gpu_watch` printed nothing after.

## Issues found

**Three tries to get the gain, all measured at the spot (history.tsv has each as its own label):**

1. *"008 phase 03: overlap, pass N"* (the plan's design as written: queue N+1 at once while N is on the card, its copy home queued at N-1's publish). Card ms a picture fell 0.6 to 0.9 ms under the card's own time, but N+1 was begun, and its camera read, a whole picture before the card could start it: the master's "the camera of N+1 is read while N is still on the card". The bench did not yet say camera to screen; it does from the next try.
2. *"..., lead, ..."*: N+1 begun 3 ms before N should be done by the last picture's time. Camera to screen p50 was **3 to 5 ms worse** than serial (still 37.8 against 34.6 to 35.3; 720°/s 60.3 against 55.4 to 56.6): turning pictures vary by tens of ms, so the guess was early, and N+1's camera old.
3. *"..., whole, ..."*: N+1 begun only once N's picture is whole on the card (`frame_gpu_whole`, an event query every 0.1 ms). Camera to screen as serial, but **no gain at all**: the card started N+1 1.2 ms after its queue. The cause: N's copy home was queued at N-1's publish, a whole picture early, waiting on N's post; N+1's staging copies (the entity lists, every frame) were queued after it and waited for it, though they are on the other stream. So the copy home of a frame is now queued only behind the next frame's copies and kernels (`frame_home_oldest` in `render/frame`); a frame queued alone goes home at once. With that (*"..., final, ..."*), the readback is off the critical path and camera to screen is as serial: the results above.

**What changed from the plan, and why:**

- **`frame_gpu_queue` queues the kernels only; the copy home is `frame_gpu_home`,** queued when the exchange has a slot for the picture. The exchange keeps three slots (main's, the newest, the writer's): with two frames on their way, the second has no slot until the first is published. A fourth slot would grow `render_frame_t` and so `app_t`, at main's frame limit. And the order measured above wants the copy home queued late anyway.
- **The events per slot are the frame's own kernel events,** not separate `post_done`, `end`, `home`: `owned_events[FRAME_KERNEL_READBACK]` is post done (the picture whole), `owned_events[FRAME_KERNEL_COUNT]` is recorded on the copy stream after the copy (home), so the readback is still timed from the one to the other, and `owned_end` ends the frame's stream work. Shade's span events and the clock kernel's two are per slot too, and the fresh count and the clock's cycles are copied, queued, into the pinned slot: `frame_marks_t` keeps only the outputs and two events for `frame_gpu_clock_mhz`. `frame_marks_t` went from 104 to 72 bytes.
- **Where the new state lives, with `app_t` at 16192 bytes:** `frame_flight_t` (two slots, 392 bytes, a pinned page) and the two staging areas come from the frame arena (`render_frame_arena_bytes` took a `staging_bytes`); `frame_gpu_t` stays 120 bytes, its events moved into the flight and its room given to the copy stream and the `device_staging_t`. `render_frame_t` traded `picture_bytes` (worked out when needed) for `borrowed_xrgb_second`, and its 2 padding bytes for `on_way` and `oldest_slot`. The second device picture is `render_t.owned_xrgb_second`, in its last 24 bytes of padding. `world_buffers_t` grew 8 bytes (`borrowed_staging`), taken from `render_t`'s padding, so `render_t` and `app_t` stay the same size; `render_lod_level_t` grew to 296 and `render_lod_t` kept its size from its padding. `--serial-frames` is `render_t.serial_frames`, one padding byte, as `game_t` is full.
- **Staging: one `device_staging_t` per frame, not "in the World arena":** the frame arena, as the pictures are, pinned with `cudaHostRegister` by `device_staging_init` (no new allocator). Each area is a frame's upload budget (4 MiB), the whole column table, the entity lists, the rain top and 1 MiB for the far field: 16 MiB an area at render distance 64 and 1080p. A copy that does not fit goes waiting, which is exact, only slower: `game_settle`'s loop of many uploads between two frames does that (3 700 times in the flight test), a frame of play does not. The marks are the staging's own events, recorded at each frame's queue (`device_staging_turn`), not borrowed from the frame.
- **Entities and the rain top go through staging by `render_entities_frame_staged` and `render_weather_take_staged`;** the old names stay, with no staging, for the tests that call them.
- **One frame on its way only when the start rule would start the next at once** (late, back to back, uncapped). Otherwise the one on its way is finished first, as before, so pictures that fit two ticks keep phase 02's just-in-time start. The rule is asked as if the one on its way were ready when T-hat says, and waiting for main. Phase 02's slice loop and `planned_ns` guard are kept (the guard only with nothing on its way).
- **`--serial-frames` is on the game and `bench_world_shake`,** not `bench_world`: the shake rows are in `bench_world_shake` since phase 01, and `bench_world` is at the standard's 1500 lines.
- **`a_fault_is_reported_within_two_frames` runs its half in a child process** (`render_feature_bridge` runs itself with `TEST_FILTER` and an argument), as a card that faulted stays faulted for the process. It queued a frame, a kernel writing at 0x1000, and a second frame: the finish said "frame: waiting for the passes" (cudaErrorIllegalAddress) on the second of two finishes.
- **The watch numbers pictures by `pictures_begun`** (in its padding), as one may begin before the last is drawn, and a picture begun with one on its way stamps no wait. In the frame log and the bench, P (ready - start - card) then holds the end of the picture before: the bench's new "start to queued" is the gpu thread's own part.
- **The profiler's gpu lane has `frame_wait`** for the finish beside `frame_draw` for the queue; the card lane is laid out as before.

**Not done here:** Ahmed's own frame log and his feel of it (the gate). The gain at this clock is small (3 % more pictures a second at 52 ms pictures); at his usual 600 MHz the same 1.3 ms is about 13 % of a still picture.
