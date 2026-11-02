# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8: CUDA renderer core

**Status:** in progress: everything an agent can run passes; awaiting Ahmed's flight on KMS and the FPS from it   (updated 2026-11-02)

## Goal

The CUDA ray caster draws a textured test world and presents it, and Ahmed can fly through it. This is the first picture.

**Wave:** W6.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-1-device-memory-bridge.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-2-section-encode-upload.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-3-traverse.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-stage-4-shade-post-frame.md`

## Todo

- [x] Stage 08.1: Device memory and C bridge
- [x] Stage 08.2: Section encoding and upload
- [x] Stage 08.3: Ray traversal
- [ ] Stage 08.4: Shading, post and frame. All of it passes but `--test-world` on a text console, which is Ahmed's
- [ ] Phase gate passed. Golden PNGs match; the flight on KMS and its FPS are Ahmed's

## Verification

**Gate:** First picture: Ahmed flies a test world on KMS at R=32; golden headless PNGs match; FPS recorded.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_gpu|run_render|cuda_)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --test-world

The end of that run logs `frames: ... rate ... Hz`, the flip rate, and `game: ... new pictures on the screen in ... s: ... FPS`. That second number is the FPS the gate asks for.

Run by the gate checker on 2026-11-02, RTX 2080 Super, X up, compute only, on `build/` removed first, after the fixes in issue 2:

| Check | Result |
|---|---|
| Debug, clean build | Passes |
| `ctest --preset debug`, the whole suite | 60 of 60 |
| The gate's ctest line, plus `run_game` | All pass on debug, release and sanitize |
| Golden PNGs on debug | 3 of 3, 57600 of 57600 pixels exact on each |
| Release, clean build | Passes, after issue 1. `readelf -d`: only `libm`, `libc`, `ld-linux` |
| `pgrep -a minecraft_c` | Prints nothing |
| `./build/release/bin/minecraft_c --test-world` | **Not run: Ahmed's** |
| The refusal of `--test-world` under X | **Not run from the program, as it is KMS mode.** Read in the code: `display_init` calls `display_device_check_console` first, before any memory or the card. `run_display_vt_guard` tests that refusal through `--test-pattern` |

Beyond the gate:

- `--headless --test-world` at the defaults, release, 1920x1080, render distance 32, 83 frames of fast flight: total mean 2.634 ms, p99 2.805 ms, 133.3 MiB of slabs, heap 0 bytes live at exit. These are the card's times, with no KMS and no flip. Two of the pictures were read by eye: real terrain, with grass, dirt, stone, sand, water, sky and fog.
- Edges of the program: 1x1, 7x5, render distance 1, and render distance 64 all run clean. `--width 0` exits with usage. 8000x4000 exits 5 with `hits: over this pool's budget`, and leaves nothing behind. Render distance 512 was not run, since its pool would pass the 4 GiB limit on a card the desktop shares.
- Three deliberate bugs of the checker's own, all built together and then removed, with `git diff` on those files empty afterwards. Each failed the test meant for it. Green read from red in shading: goldens and traces. The camera never published to the gpu thread: `run_game` and the threads test. The first dirty slot not flushed: `run_render_upload`.
- Looked for and not found: stubs and TODOs, tests that cannot fail, a golden that writes itself (only with `MC_GOLDEN_WRITE`), a missing card passing as a skip, public functions nothing calls apart from what tests use on purpose (`traverse_rays`, `traverse_wait`, `world_buffers_probe`, `device_memory_zero`), `cudaMalloc` outside `device_memory`, a `.hu` in C, owning statics.
- Not done: no thread sanitizer was run, as in Stage 08.4. `sanitize` does not look for races.

## Commits

Appended as they land:

- `b9b80ba` Fix the phase 8 gate's findings: a clean release build that failed on the generated tables, sim and main sharing a cache line in `game_t`, an unchecked CUDA call, no way to read the FPS or a full pool after a KMS run, and names and literals outside the standard.
- `94ef054` Record the phase 8 gate's results, what it fixed and what awaits Ahmed's flight on KMS.

## Issues found

Found by the gate checker. Files are named so the fix can be found; the stage files say which stage each belongs to.

**1. The gate's own release build failed from clean.**
- Expected: `cmake --build --preset release` from nothing succeeds.
- Actual: `FAILED: CMakeFiles/mc_test_run_game_callgraph.dir/tests/run/game/index.c.o`, `run/include/terrain/biome/index.h:6:10: fatal error: tables/biomes/index.h: No such file or directory`, then `ninja: build stopped`, with exit 1. Also for `mc_test_run_display_mode_callgraph` and `mc_test_run_display_vt_guard_callgraph`. Stage 08.2 issue 8 saw this and left it, because a second run hides it.
- Cause: on release, `cmake/standards_lint.cmake` compiles each C source again as a `_callgraph` object library for the recursion check. Those copies waited for `mc_prebuild` and `mc_struct_offsets` but not for the tables.
- Done: fixed: they now depend on `mc_game_tables`. Clean release built twice since, exit 0.

**2. Defects no test or lint saw, all fixed in `b9b80ba`.**
- `game_t` had `tick_count` and `columns_made`, which the sim writes, on one cache line with `last_frame_ns`, `center_x` and `borrowed_last_picture`, which main writes every frame. Standard section 3 rule 5. Expected: the two apart. Moved, with `offsetof` asserts for it, and `sizeof` still 896. Read from the header's offsets: no timing was taken before or after.
- `frame_gpu_deinit` discarded the result of `cudaEventDestroy`, with a comment saying why. Standard section 8: every CUDA call's result is checked. It now returns the first failure, and `render_frame_deinit` logs it. A failure there was not provoked, as on the rest of this card: Stage 08.1 issue 5.
- The gate asks for FPS, and a KMS run had only the flip rate, which counts a picture shown twice as two, and the card's time per frame. `game_report` now logs new pictures per second. `run_game` holds the count to at least 1 and at most the frames drawn, and the clock to run forward. **The number itself has never been printed, as only a KMS run reaches it.**
- A pool that filled during a threaded run told no one: only `game_settle` warned, and the threads do not call it. So a KMS run could show holes with nothing in the log. `game_report` now says. Not tested for the log line itself, and the pool has not filled in any R = 32 run measured here.
- Names without their prefix: `traverse_view_t.table` and `.pool`, `render_block_wanted_t.block` and `.name`. The lint does not look at these.
- `F32()` in `run/src/render/frame/index.c` and `run/include/render/frame/index.h`, which are C only and not compiled by nvcc. Standard section 2: C-only files use the suffix.

**3. Stale records.** Stage 08.3's status and its line that nothing calls it were from before Stage 08.4. Corrected there.

**4. One run outside the lock.** The checker read the golden counts by running `build/debug/bin/tests/run_render_frame` once directly under `timeout`, not through `tools/test-or-build.sh`. Other agents' GPU tests could have been running with it. It passed, and the same test had passed under the lock just before.

**5. Command forms the worktree isolation refused.** Compound shell commands holding `tools/test-or-build.sh` or `sed` with variables were refused as too complex to verify. They were run again as plain separate commands, as the refusals asked. Nothing was bypassed.

**6. Open, for Ahmed.** Stage 08.3 issue 3 and Stage 08.4 issue 7: a lattice-shaped cave misses 8.33 ms at 1920x1080, and nothing chooses a lower internal size at run time, since upsampling comes with Stage 09.4. Stage 08.4 issue 5: `test_world` is a stand-in for the real generator until Stage 10.7, by design for this phase.
