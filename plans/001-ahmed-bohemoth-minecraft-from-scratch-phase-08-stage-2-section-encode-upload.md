# 001-ahmed-bohemoth-minecraft-from-scratch — phase 8, stage 2: Section encoding and upload

**Status:** done   (updated 2026-11-02)

## Goal

Worker section encoding, a budgeted upload queue and a slab pool exist.

**Wave:** W6. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-08-cuda-renderer-core.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/render/index.*` and `README.md` (the Owner tier)
- `cuda/{src,include}/world_buffers/`
- `run/{src,include}/render/encode/`
- `run/{src,include}/render/upload/`
- `tests/run/render/encode/`
- `tests/run/render/upload/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `render` Owner tier: `run/src/render/index.c` with paired `render_init` and `render_deinit`, owning the regions and arenas its parts borrow
- [x] Encode sections into the three-level `uint64_t` bitmask tree on workers, in `run/src/render/encode/index.c`
- [x] Implement the budgeted upload queue in `run/src/render/upload/index.c`
- [x] Implement the slab pool and toroidal column table with per-column max height in `cuda/src/world_buffers/index.cu`
- [x] Register tests `run_render_encode` and `run_render_upload`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each. One exception, as in Stage 08.1 issue 3: `run/src/gpu_bridge/world_buffers/` holds only a README

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_render_encode$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_render_upload$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Run on 2026-11-02 on the RTX 2080 Super, with X up, after the last source change: all pass, and `pgrep` prints nothing. `run_render_encode` is 9 tests, `run_render_upload` 27 tests and about 4.86 million checks. Beyond those: the whole debug suite, 52 of 52, `clang_format` among them, and `run_render_encode`, `run_render_upload` and `run_types` on `release`, and the first two on `sanitize`. All GPU work is compute only, and no test holds more than 129 MiB on the card. `minecraft_c` ran only under `run_types` and the other existing tests, never in KMS mode.

## Wiring outside the owned paths

- `run/src/main/index.c`, `run/include/main/index.h`, `run/src/main/README.md`: `render_t` in `app_t`, its init after `terrain` and its deinit before it. `app_t` grows from 3840 to 4736 bytes. It takes nothing on the card until `game` calls `render_open` (Stage 08.4).
- `tests/run/types/index.c`: the program now reserves 8 heap regions: see issue 2.
- `run/include/gpu_bridge/world_buffers/index.h`, `run/src/gpu_bridge/world_buffers/README.md`, a line in `run/src/gpu_bridge/README.md`: the C API of the new area, where Stage 08.1 issue 3 says it goes.
- `plans/REPO_STRUCTURE.md`: the parts and the two tests.

## Commits

Appended as they land:

- `41ce581` Add the render Owner, section encoding on workers, a budgeted upload queue, and the slab pool and toroidal column table on the card, checked voxel for voxel by a real kernel against the world.
- `60dec72` Add tests for the three deliberate bugs that survived, for both budget limits, for a refused section arriving later and for a column gone from behind the queue, and correct two comments to the measured numbers.

## Issues found

**1. The previous attempt's work could not be picked up.** Expected: `git merge worktree-wf_916eee71-48c-53` brings its work. Actual: "Already up to date": its report says nothing of it was committed, and that branch is an ancestor of the integration branch. Its uncommitted files are in another worktree, which this stage's instructions forbid touching, and its report warns that a mutation script may have left a deliberate bug in any of them. So nothing was read or copied from there. This is a fresh implementation, which used only that report's numbers and design notes. That worktree is still as it was, for Ahmed to discard.

**2. `run_types` states a heap region count that the integration branch had already passed.** Expected: `tests/run/types/index.c` says 6 regions, and the running program has 6. Actual, with this stage's one region added: `heap at exit: 8 reserved, 8 released, 0 bytes live`, against the test's 6, so 7 before this stage. That 7 is by subtraction: the integration branch alone was not run. Both sentences now say 8, and `run_types` passes on debug and release.

**3. Decision owed before Stage 09.2: light is stored only for occupied voxels, as the plan says.** Expected: that a hit voxel's own light is what shading wants. The catch: a flood fill (Stage 11.1) usually leaves opaque voxels at 0 and puts the light in the air next to them, and the air's light is not on the card. If that is how 11.1 lands, shading gets dark faces. Nothing is wrong yet: today nothing writes light but tests. The choices are for 11.1 to write each opaque voxel's light from its brightest neighbour before the section goes out, or to store light for air next to blocks too, which changes this format. Left for 09.2 and 11.1, and not decided here.

**4. Left alone: applying a view's table looks at every slot.** `upload_table_apply` runs once per view, over `slot_count` entries, on the `gpu` thread. At the default radius that is a few thousand slots. At the largest table it is about a million, per view. Not measured at that size, since a world of that radius is not something these tests open. If it shows up, the publisher can name the slots that changed: Stage 17.3.

**5. Measured.** On debug except where it says, all on one machine that other agents' builds were sharing:

| What | Result |
|---|---|
| Section copy, 640 bytes | about 3.3 µs (3.0 on release) |
| Section copy, 3656 bytes | about 3.5 µs (3.3 on release) |
| Section copy, 12944 bytes | about 5.3 µs (4.9 on release) |
| So copy speed, from those | about 6 GB/s, and about 3 µs to start any copy |
| Table flush, 10 slots of 4761, one copy each | about 27 µs |
| Table flush, 75 slots of 4761, as one 1.2 MiB copy | 39 to 85 µs across runs |
| Encoding a terrain-shaped section on one thread | about 38 µs on debug, 8.3 µs on release |
| Size of that section | 7885 bytes on average |

The plan's VRAM line assumes 4 KiB a section, and this test's sections take nearly twice that, in slabs a little larger still. It is a synthetic section: surface shape, 1 in 64 holes. Real terrain gets its number from Stage 17.3, and `RENDER_POOL_BYTES_PER_COLUMN` is a first guess until then.

The upload budget of 256 sections and 4 MiB a frame comes from those copy timings, about 0.8 ms of a frame. **It has never been measured in a real frame**, since there is no frame until Stage 08.4. Flushes come on top of it, and have no budget, for the reason in `cuda/src/world_buffers/README.md`. A whole-table copy turned out cheaper than the 1-in-64 rule assumes, so the rule sends more single-slot copies than it has to. It is safe, and left for Stage 17.3 to tune.

**6. Limits of the slab pool, left as they are.** Each size class holds at least a whole page while it has a section, so a pool under 12 MiB can refuse a size with most of its bytes free: `random_writes_match_a_brute_force_model_on_the_card` first ran with 8 pages and hit this. `RENDER_POOL_BYTES_MIN` is 12 pages for that reason. A page with one small section left stays with that size, and there is no compaction. When the pool is full a section is refused and stays stale, then goes back through a ring to be marked dirty, and is re-encoded per view for as long as the pool stays full. That costs CPU in a state the game should not be in, and it loses nothing.

**7. How far the tests can be trusted.** After the code was committed, 28 deliberate one-line bugs went through a script in the scratchpad, one at a time, each restored afterwards, with `git status` confirming the source clean. 23 were caught the first time, three of those by a re-run, since my first look at the output cut off their lines. Five were not:

- Three survived: the `chunk_x` check in the voxel lookup, the section count half of the budget, and `render_close` in `render_deinit`. Each now has a test, and each is caught.
- Two failed to build, so they tested nothing: dropping the restore in `render_upload_resend_drain`, and dropping the stale-section check. Rewritten so that they build. The stale check was caught. The restore survived, because the test happened to keep a column that was never refused. That test now keeps the column of a section it reads from the ring as refused, and the bug is caught.

The stale-section path cannot happen by itself, since a view's sections always match its own table: its test takes the column away behind the queue's back. Real card faults during upload were not provoked, as in Stage 08.1 issue 5, on a card the desktop shares.

**8. Product bug, out of scope: a clean build can fail on a missing generated header.** Expected: `cmake --build --preset release` from nothing succeeds. Actual, in a clean release build: `FAILED: CMakeFiles/mc_test_run_texture_callgraph.dir/tests/run/texture/index.c.o`, `fatal error: tables/blocks/index.h: No such file or directory`. A second run of the same command then succeeds, since by then the tables exist. An earlier clean release build also stopped partway and passed when run again, though I did not keep which step failed that time. It looks like that callgraph target not waiting for the generated tables, though the cause was not looked into further. It is not in this stage's paths, so it was left alone, and it is recorded here for whoever owns `cmake/`.

**9. My own first test run.** Four tests were wrong and the code right: a size past the largest section expecting "pool full", radius 2 taken for a square when it is a circle of 13 columns, a flush threshold off by one slot, and a buffer reused without being dirtied again. Each was fixed in the test. The encode and upload tests found no bug in the code on their first run, which is why issue 7 was worth doing.
