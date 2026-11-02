# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12, stage 2: Camera and targeting

**Status:** done   (updated 2026-11-02)

## Goal

Camera, mouse look, and DDA block targeting with outline exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-player-mining.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/player/camera/`
- `run/{src,include}/physics/raycast/`
- `tests/run/physics/raycast/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement camera and mouse look in `run/src/player/camera/index.c`
- [x] Implement DDA block targeting in `run/src/physics/raycast/index.c`
- [x] Connect the target to the block outline (drawn here on the CPU, as Stage 09.5 has no outline yet: Issue 1)
- [x] Register test `run_physics_raycast`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_physics_raycast$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Verification results

- `tools/test-or-build.sh cmake --build --preset debug`: builds.
- `ctest --preset debug -R '^run_physics_raycast$'`: passes, 32 tests. Run directly 8 more times on debug and 8 on the sanitize preset: 0 failures, and no sanitizer report.
- `ctest --preset debug -R '^standards_lint$'`: passes.
- `ctest --preset debug`, all of it: 65 of 65 pass.
- `pgrep`: nothing of this worktree left running.
- By eye: `--headless --test-world --walk` with a script moving the mouse, at 960x540. The screenshots show a thin black square exactly on the leaf block in the middle of the picture.
- Not checked: the outline on a real display under KMS, and the mouse on a real device. Only Ahmed can run those.

## Commits

Appended as they land:

- `68cb32b`: mouse look, DDA targeting, the outline, and the test.

## Wiring outside the owned paths

The smallest needed to call this stage's modules, as the task allows:

- `run/{src,include}/player/index.*`: `player_look_send`, the aim on each `player_tick`, `player_target_view`, and the target in `player_view_t`. `player_t` goes from 576 to 960 bytes.
- `run/{src,include}/game/index.*`: `game_walk_send` turns with `player_camera_look`; the outline after each present; `picture_cameras` and `shown_camera`. `game_t` goes from 896 to 1088 bytes.
- `run/include/main/index.h`: the sum in `app_t`'s `static_assert`, 7808 to 8384.
- `run/include/physics/index.h`: includes `physics/raycast/`.
- The READMEs of `run/src/player/`, `run/src/physics/`, `run/src/game/`, and `plans/REPO_STRUCTURE.md`.

## Issues found

### Issue 1: the block outline this stage connects to does not exist yet

- **Expected:** Stage 09.5 owns "Add the block outline and breaking overlay", and this stage connects its target to it.
- **Actual:** Stage 09.5 is `not started` on the integration branch at 3f94eee, and no branch has a commit naming it. `grep -rI outline run cuda` finds nothing in the renderer.
- **Done:** the outline is drawn in `run/src/player/camera/`, on the CPU, over the finished picture. A depth test in `cuda/src/post/` would be better, and would be less code, but `cuda/` and `render/frame` are outside this stage's paths and other stages are working in them. So there is no pixel depth. Each edge is 8 pieces, and a ray from the sim's eye decides whether each piece is seen. The lines are solid black and never blended.
- **Known limits:** visibility is correct to an eighth of an edge, not to a pixel. It comes from the eye of the last tick, not of the picture. A plant hides what is behind its aiming box, which is more than it draws. Which block is aimed at is up to one tick, 50 ms, behind the mouse, because the sim works it out.
- **Left for Stage 09.5:** when it lands, `game` should hand its outline the same `player_target_t` and stop calling `player_camera_outline_draw`. It has not been told, and it may do its own: whoever merges it should check that only one outline is drawn. The breaking overlay stays Stage 09.5's.

### Issue 2: `run_render_frame` failed once, and did not again

- **Expected:** passes.
- **Actual:** on one run, with `standards_lint`, `clang_format` and the other run tests in the same `ctest`: `tests/run/render/frame/index.c:1346: the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap: CHECK failed: picture == screen: first difference at byte 24945`. The same test then passed 3 times run directly, 5 of 5 times alone under `ctest`, and in each of 3 later `ctest` runs with other tests.
- **Cause:** not found. The test flies with no player, where this stage's only changes are the larger `game_t`, a camera copied beside each picture, and one copied back. None of them reaches a picture's bytes. Other agents were using the machine and the card at the time, and the test compares against a picture made after the threads stop. That suggests a timing race, but no run on the code before this stage was made to compare, so it cannot be ruled out that this stage made it more likely.
- **Done:** left alone, and reported.

### Issue 3: the first test of which camera draws the outline proved nothing

- **Expected:** drawing through main's newest camera instead of the picture's would fail `on_real_threads_the_outline_sits_on_the_picture_it_is_drawn_over`.
- **Actual:** with that mutation the test still passed, twice. Looking straight down, a turn only spins the picture about its middle, which moves an edge's middle along the edge's own line.
- **Done:** the test now looks along a slope and turns every frame, and checks that most pictures really are behind main's camera. The same mutation then failed 95 checks.

### Issue 4: that stronger test then failed on its own

- **Expected:** every frame of it has some outline.
- **Actual:** the full `ctest` run failed `run_physics_raycast`, and 5 of 6 runs under the sanitize preset failed `:1170: CHECK failed: outlined > 0u`, 1 to 5 times a run.
- **Cause:** the test, not the code. It was turning 0.1 radians every frame, hundreds of times a second, and the aim is a tick old. So the aimed block could be off the picture, with nothing to draw.
- **Done:** it turns 0.04 radians a frame, and requires lines on a quarter of the frames instead of on all of them. Lines that are on the picture are still checked to the pixel. The results of rerunning are under Verification results below.
