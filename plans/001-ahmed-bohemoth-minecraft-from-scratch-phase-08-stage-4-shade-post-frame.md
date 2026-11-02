# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8, stage 4: Shading, post and frame

**Status:** done, except what only Ahmed can run on KMS: `--test-world` on a text console   (updated 2026-11-02)

## Goal

Textures, face shade, tone map, readback, present and a free-fly camera exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-cuda-renderer-core.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/game/index.*` and `README.md` (the Owner tier)
- `cuda/{src,include}/shade/`
- `cuda/{src,include}/post/`
- `cuda/{src,include}/frame/`
- `run/{src,include}/render/frame/`
- `tests/run/render/frame/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `game` Owner tier: `run/src/game/index.c` owns the World, Tick and Frame arenas and runs the loop connecting `sim`, `gpu` and `main`; later stages add their tick and frame calls
- [x] Implement texture lookup and face shade in `cuda/src/shade/index.cu`
- [x] Implement the tone map to XRGB8888 in `cuda/src/post/index.cu`
- [x] Implement frame kernels and readback in `cuda/src/frame/index.cu`
- [x] Implement present and the free-fly camera through `run/src/render/frame/index.c`
- [x] Add golden headless PNG tests, registered as `run_render_frame`, and inspect the images
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_render_frame$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/debug/bin/minecraft_c --test-world

## Commits

Appended as they land:

- `1c34375` Add shading, post and the frame, the game Owner with its threads and a test world, and make traversal walk the format the encoder writes.
- `70e7f8e` Merge of that work onto the integration branch, with `app_t` resized.
- `b65ba89` Start the sim and gpu threads on the first frame, after the guardian's fork, and test the world through the display loop on a fake kernel with that fork real.
- `1b65e8c` Test what the deliberate bugs got past, the slots of the exchange, a wrapped column and the test world's surface, and move the display-loop tests into run_game so the frame test stays under 1500 lines.
- `3c67659` Take the traverse fixture pool from the heap, so that its measurement at render distance 32 fits again on the new format.
- `47a62ba` Merge of the integration branch at `45351f2`, bringing in Stage 10.6.

## Results

**Not run: `./build/debug/bin/minecraft_c --test-world` on a text console.** Agents may not touch KMS. No real card, real `/dev/input` or real VT has been through this path, and the refusal under X has not been run from the program either. `display_init` runs the same check as `--test-pattern`, whose refusal `run_display_vt_guard` tests with stdin from `/dev/null`.

Run on this branch with the integration branch merged at `45351f2`:

- `cmake --build --preset debug`: clean.
- `ctest --preset debug`: 60 of 60 passed, `clang_format` and `standards_lint` among them.
- `run_render_frame` and `run_game` also passed on `sanitize` and `release`, one merge earlier, before Stage 10.6 came in, which changed nothing they use. `sanitize` does not look for data races between threads, and no thread sanitizer was run.
- `pgrep -a minecraft_c`: printed nothing.

What `run_render_frame` holds to:

- A hand-built world of 4 blocks against an exact `float64_t` trace: 4130737 pixels, 197023 of them hits, 0 wrong.
- Real terrain through the real encoder, upload and rays, against the map: 288000 pixels and 32100 rays, 0 wrong and 0 differing, and the same while scrolling.
- Golden PNGs: 3 of 3, every pixel exact. Made by this code on this card, so they guard against change and do not prove correctness. Looked at.
- The same script gives byte-identical PNGs across two runs of the built program.
- The real threads on a plain buffer draw what one thread draws, with heap counters flat.
- `run_game`, a new test: the real loop through `display_frames_run`: issue 6.

PNGs read with the Read tool, at 1920x1080 and render distance 32: grass, dirt, stone, sand, snow on the peaks, water, sky and fog. It is a real world and it looks right, with the limits under "Left for later stages".

Headless frames, release, 1920x1080, render distance 32, 601 frames flying fast over land and then sea, two runs, by the card's clock. These include readback, and no KMS, flip or HUD. One thread settles the world before each frame, and that time is not in these numbers.

| Pass | Mean (ms) | Max (ms) |
|---|---|---|
| coarse | 0.347 | 0.644-0.646 |
| primary | 0.483-0.484 | 0.788 |
| shade | 0.197 | 0.246-0.249 |
| post | 0.117 | 0.135-0.138 |
| readback | 1.274-1.276 | 1.444-1.470 |
| total | 2.419-2.421 | 2.833-2.847 |

p50 2.438 ms, p99 2.774 ms. The world held 97.3 MiB of slabs. Stage 08.3's table is for the old format and for other paths, which were not timed again: issue 1.

The real threads at render distance 32 from nothing, release, `MC_FRAME_MEASURE_THREADS=1`, one run: 3209 columns, first picture at 9 ms, all in at 2513 ms, 125.2 MiB of slabs, pool never full, longest `game_loop_frame` 4.98 ms, frame total p99 2.77 ms, max 3.73 ms. Terrain here is the density and surface of `test_world`, so real generation in Stages 10.4 to 10.7 will take longer.

Deliberate bugs, one line each, built and run against `run_render_frame`, `cuda_traverse`, `run_render_encode` and later `run_game`: 22 in all, and all are caught now. Mirrored, flipped or darkened faces, the mip, the eye's fraction, red and blue, a wrapped column in x and in y, a column top one short, any full section called uniform, a uniform lookup's light, a mirrored camera, a backward mouse, the exchange's slots, no beaches, no sea bed, leaving columns kept, a picture never published, threads before the fork, Esc ignored, and what waited and the time spent on another VT. Four got past the first round: issue 12.

The golden pictures and everything else in these tests compare with what this code made or with a trace written alongside it, by the same hand. They catch change and disagreement. Whether the world looks as Ahmed wants is his to say.

## Wiring outside the owned paths

- `cuda/{include,src}/traverse/`, `run/include/gpu_bridge/traverse/`, `tests/cuda/traverse/`: issue 1.
- `run/{include,src}/render/` and `render/encode`, `run/include/gpu_bridge/world_buffers/`, `tests/run/render/encode/`, `tests/run/render/upload/`: issues 2 and 3, and `render_open_frame` and `render_t` growing.
- `tests/run/game/`, a new test next to this stage's: issue 13. A test and a fixture change in `tests/cuda/traverse/`: issues 1 and 12.
- `run/{include,src}/gpu_bridge/{shade,post,frame}/`: the C APIs the standard requires for each CUDA area.
- `run/{include,src}/display/` with `display_frames_run`, and a test of it in `tests/run/display/vt_guard/`.
- `run/{include,src}/main/`: `app_t` holds `game_t` and the topology, and the flags `--test-world` and `--render-distance`.
- `run/src/game/test_world/`, a new Arena part: issue 5.
- `plans/REPO_STRUCTURE.md`.

## Left for later stages

Stored light is on the card and unread (Stage 09.2). Water and cutouts are opaque (Stage 09.3). Only frame 0 of an animated texture is drawn. The fog is a simple stand-in. `test_world` uses the density's blocks alone, without the caves, ores, hydrology and biomes of Stages 10.4 to 10.6: issue 5.

## Issues found

**1. Stages 08.2 and 08.3 left two different voxel formats.**
- Expected: traversal walks what the encoder writes, as Stage 08.3's issue 1 hoped.
- Actual: traversal had its own tree format and test encoder, so nothing could be drawn from `world_buffers`.
- Done: traversal and its test encoder now walk `world_buffers`, outside this stage's owned paths. `cuda_traverse` passes on the new format.
- Its measurement had stopped working: the pool at R = 32 is 589.8 MiB and no longer fitted the 1 GiB test arena alongside the dense world (`test arena exhausted asking for 618395328 bytes`). The fixture now takes it from the heap and gives it back. So Stage 08.3's table is for the old format, and this one, `MC_TRAVERSE_MEASURE=1 TEST_FILTER=measure`, release, coarse and primary at 1920x1080, is for the new one:

| Path | Terrain mean | Terrain p99 | Lattice mean | Lattice p99 |
|---|---|---|---|---|
| walk_horizon | 0.66 | 1.28 | 6.00 | 6.99 |
| flyover_high | 1.00 | 1.15 | 6.79 | 8.36 |
| ridge_grazing | 0.86 | 1.12 | 5.71 | 8.09 |
| spin_360 | 0.50 | 0.60 | 5.90 | 6.43 |
| forest_floor | 0.50 | 0.83 | 3.79 | 4.95 |
| canopy_skim | 1.11 | 1.39 | 5.47 | 7.42 |
| cave_interior | 0.44 | 0.58 | 8.88 | 9.64 |

One run. At 1440x810, the worst lattice p99 is 6.41 ms and the worst terrain p99 1.16 ms.

**2. A column of solid stone would not have fitted.**
- Expected: 12944 bytes a section is fine.
- Actual: by arithmetic, about 22 solid sections per column at that size. The cost before the change was not measured.
- Done: a uniform section form of 24 bytes in `world_buffers` and `render/encode`, tested in `run_render_encode` and through real terrain. A world at render distance 32 now holds 97.3 to 125.2 MiB of slabs.

**3. The pool ran out at a small render distance.**
- Expected: `render_pool_bytes_for` giving the larger of a share per column and a floor was enough.
- Actual: at render distance 6 a "pool is full" warning, because each size of slab holds a page of its own however little of it is used.
- Done: the 12-page floor is now added on top. `run_render_frame` requires `pool_full_count` to be 0.

**4. Plain `--headless` still draws the demo.**
- Expected: the headless README said `game` would replace it.
- Actual: `tests/run/display/headless/` tests a run with no card and no world, and that is worth keeping.
- Done: `--headless --test-world` runs `game`, and the READMEs and `plans/REPO_STRUCTURE.md` say so.

**5. The world drawn is a stand-in for the real generator.**
- Expected from the plan's order: a real world to draw.
- Actual: when this was written, Stage 10.6, the surface, had not landed, and the density alone is stone and water. It landed on the integration branch while this stage was finishing (`45351f2`), and its issue 7 says that whoever feeds the world from terrain must call its five steps in order. Nothing does yet.
- Done: left alone, as Stage 10.7 is still to come and this stage's goldens would change with it. `run/src/game/test_world/` lays grass, dirt, sand and snow over the density's stone, and `game_test_world_fill` is the one place to replace. No caves, ores, rivers or biomes are in the picture until then. The goldens must then be made again and looked at, as `tests/run/render/frame/README.md` says, and the streaming times above measured again.

**6. The guardian forks with threads and pinned pages live.**
- Expected: KMS runs the world with the same fork `--test-pattern` has, which forks with nothing else running.
- Actual: the first attempt pinned the pictures and started `sim` and `gpu` before `display_vt_guard_take_console` forked. A pinned page that goes copy-on-write in the parent would leave the card writing to a page the program no longer reads: a frozen screen under KMS and nowhere else. It had no test.
- Done: `game_loop_source` starts the threads on its first frame, after the fork. The pictures stay pinned before it. `a_world_runs_through_the_display_loop_with_a_real_guardian_forked`, in `run_game`, runs `display_frames_run` on the fake kernel with the real card and a real guardian alive: a picture drawn after the fork matches byte for byte one drawn before it, a flight then streams and draws, and the end matches a one-thread draw. It passes on debug, sanitize and release, on kernel 7.2.7 and this driver. So the pin survives here. A different driver or kernel is not covered until this test runs there. Real KMS is still Ahmed's to run.

**7. Stage 08.3 left choosing the internal size at run time to this stage.**
- Expected from its issue 3: 1920x1080 on `cave_interior` misses 8.33 ms on rays alone, so some size must be chosen at run time. On the new format that row's p99 is 9.64 ms: issue 1.
- Actual: nothing switches size. The picture is always the display's size. Without upsampling, which Stage 09.4 brings, a smaller picture has no way onto the screen. On terrain here a frame takes under 3 ms.
- Done: left alone and escalated with Stage 08.3's issue 3 to Ahmed: only a cave of lattice shape has shown the problem.

**8. `run_render_frame` needed two fake kernels at once.**
- Expected: `fake_card.h` and `fake_evdev.h` could both be included.
- Actual: their helpers share names (`fake_ops`, `fake_read_fd` and others), and each is `Included once`, by its own tests.
- Done: `tests/run/render/frame/index.c` renames the evdev ones with `#define` around the include. Neither fixture was changed.

**9. `clang_format` failed on the integration branch for a while, from outside this stage.**
- Expected: every test passing after merging it.
- Actual: at `907605c`, 1 of 58 failed, on `tests/run/terrain/{climate,hydrology,seed}/index.c`. `clang-format --dry-run` on that commit's own `seed` gave 6 errors, without this stage's work.
- Done: left alone, as those paths belong to the terrain stages. The integration branch fixed it by `45351f2`, and after merging that, it passes.

**10. A previous attempt ended before its record was written.**
- Expected: each todo ticked as it landed.
- Actual: the attempt on `worktree-wf_916eee71-48c-62` was forced to stop with a deliberate-bug script still running and this file untouched, so its working tree could not be trusted. Only commit `1c34375` was taken.
- Done: merged that commit, resized `app_t` for the merge (7000 bytes in use, `_pad0[40]`), ran everything above again here, and wrote this.

**11. Command forms the worktree isolation refused.**
- Expected: compound shell commands would run.
- Actual: several were refused, as too complex to show they stay in the worktree.
- Done: re-run as plain separate commands, as the refusals asked, with scripts written by the Write tool. Nothing was bypassed.

**12. Four deliberate bugs got past the first round.**
- Expected: all caught, and the frame test's README said a wrapped column not being seen was tested.
- Actual, from the run of 21 (16 caught, 1 did not build, as written): a column wrapped out of its slot seen: survived, as a settled world never shows one, so that line of the README was wrong. The reader's slot overwritten in the exchange: survived. No beaches: survived, as the goldens hold no shore. A guard I had just added in `game_loop_stop`: survived, because `thread_join` already does nothing for a thread never started, so it added nothing.
- Done: `a_column_that_wrapped_out_of_its_slot_is_not_seen` in `cuda_traverse`; the exchange checked to keep three separate slots over 64 turns; `the_test_worlds_surface_is_sand_by_the_water_and_grass_above_it`, which also found 211 of 59959 grass tops over air or water, which is right, under a one-block overhang; the guard removed. A second round of 6 caught all of them, the one that had not built among them.

**13. `run_render_frame` passed 1500 lines.**
- Expected: the standard says to split at 1500.
- Actual: 1676 with the display-loop tests.
- Done: those moved to `tests/run/game/`, ctest `run_game`, with their own copy of the rig. The frame test is under 1500 again.
