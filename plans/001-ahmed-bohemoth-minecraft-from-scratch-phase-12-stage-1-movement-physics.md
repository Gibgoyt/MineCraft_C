# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12, stage 1: Movement and physics

**Status:** done   (updated 2026-11-02)

## Goal

Swept AABB collision, walk, sprint, sneak, jump, swim, climb and fall exist, on a fixed tick with render interpolation.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-player-mining.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/player/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/physics/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/physics/*`
- `run/{src,include}/player/movement/`
- `tests/run/physics/`
- `tests/run/player/movement/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `player` Owner tier: `run/src/player/index.c` with paired `player_init` and `player_deinit`, owning the regions and arenas its parts borrow
- [x] Create the `physics` Owner tier: `run/src/physics/index.c` with paired `physics_init` and `physics_deinit`, owning the regions and arenas its parts borrow
- [x] Implement swept AABB collision under `run/src/physics/`
- [x] Implement all movement modes in `run/src/player/movement/index.c`
- [x] Run on the 20 Hz tick with render interpolation
- [x] Register tests `run_physics` and `run_player_movement`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_physics$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_player_movement$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `3b5541a` Add swept AABB collision and a player that walks, sprints, sneaks, jumps, swims, climbs and falls on the 20 Hz tick with render interpolation, behind --test-world --walk.

## Results

`physics_init` holds the 2 MiB region for a move's boxes. `player_init` holds none: its part needs no memory, and its README says so, as `math`'s does.

Verified on 2026-11-02, debug preset unless said:

- The Verification section: built clean; `run_physics`, `run_player_movement` and `standards_lint` pass; `pgrep -a minecraft_c` prints nothing.
- All 63 ctests pass, in 218 s.
- `run_physics` and `run_player_movement` also pass on the sanitize and release presets. A walk from debug and from release ends at `16.637, 9.013, -3.000` with a byte-identical `w60.png`.
- Measured by `run_player_movement` on hand-built worlds: walk 4.317, sprint 5.612, sneak 1.295 blocks/s, jump 1.2522 blocks in 12 ticks, swim 1.96, lava 0.784, climb up 2.35 and down 3.0 blocks/s, and terminal fall under 78.4 blocks/s.
- Deliberate bugs: 26 were put in one at a time, in collision, physics, movement, player and game. All 26 fail the tests now. Each of the first run's survivors got a new test, except one, which showed that a tie rule between two boxes changed nothing, so the rule was deleted. See issue 6. Every result counted here comes from after the flaky test of issue 4 was fixed.

**Wiring, outside the owned paths, each the least that lets it be called:**

- `run/{src,include}/game/index.*`: `game_player_set`, `game_walk_send`, and a player's tick, view, spawn and report. `game_sim_tick` now takes when its tick was due. `borrowed_player` came out of `game_t`'s padding, so its size is unchanged.
- `run/{src,include}/main/index.*`: `physics_t` and `player_t` in `app_t`, their init and deinit, and a `--walk` flag that needs `--test-world`.
- `tests/run/types/index.c`: the two pinned heap counts go from 8 to 9, for `physics_init`'s region. See issue 5.
- `run/src/game/README.md`, `run/src/main/README.md`, `plans/REPO_STRUCTURE.md`.

**Not changed:** a run with no flags still runs the runtime's jobs and stops, and `--test-world` alone still flies, so the golden pictures stand. The phase gate's plain `./build/release/bin/minecraft_c` does not walk yet: `--test-world --walk` does. That belongs to whichever stage opens a world by default.

## Ahmed's check

Only a person at a text console can feel this. Agents cannot run KMS. The same code has run under `--headless`, and on the real threads with fake kernels, but not on real KMS and real keys. It is not a box above, since the stage's own Verification does not list it:

    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --test-world --walk

## Issues found

1. **The spawn at the world's middle was in water.** Expected: the first headless walk to walk. Actual: `game: the player is at 16.700, 13.700, -3.000`, and an all-blue `w200.png`: default-seed `(8, 8)` is water, so the body swam, and my first test of the real threads was measuring swimming. Done: `game_spawn_find` looks for dry land in widening squares, 16 blocks apart, out to 1 km, for a walking run only. Flying still starts at the middle, so the goldens are untouched.
2. **That dry land can be a high spot.** With the default seed the body starts at `-7.5, -7.5, 85.0`, and walking along the start yaw goes over a cliff and ends in water at `z = -3`, 202 ticks later, as the log above shows. Expected of terrain this steep, and physically right. Left alone: choosing a kinder place is a matter of taste for Ahmed's first walk, and easy to add to `game_spawn_find` if he wants it.
3. **The picture and the collision disagree on every shape but the cube.** `grep -rn 'BLOCK_SHAPE\|\.shape' run/src cuda/src` finds only `run/src/physics/collision/`: nothing that draws reads a block's shape, so a slab is drawn as a whole cube, and so is a flower or ladder cell with its cutout texture. So a body sinks half a block into what looks like a whole block, and walks through a cell that looks filled. Collision follows `config/blocks/`, which is what the plan wants. Left for the stage that draws shapes: out of scope here, and no stage's Goal in this phase mentions it, so Ahmed should expect to see it.
4. **A test measured the machine, not the code, and my own mutation run hid it for a while.** `the_real_threads_walk...` required every frame's step to be under a tick's worth, and failed 3 runs in 6: a frame late by most of a tick on a busy machine really does take most of a tick's step. Done: it now requires the camera to move on over twice as many frames as there were ticks, which only interpolation gives, and passed 8 runs in 8. Exact steps are held by `a_picture_at_any_rate_never_goes_back_and_never_jumps`, on a clock of its own. Separately, a full `ctest` run straight after the mutation script tested the script's last mutant, since restoring the sources does not rebuild them. It failed the hop test on the `no hop` mutant, which looked like a real bug. The next build cleared it. The results above are from after that rebuild.
5. **`run_types` pins the number of heap regions.** Expected: pass. Actual: `does not contain "heap at exit: 8 reserved, 8 released, 0 bytes live"`, holding 9, at `tests/run/types/index.c:914`, and the same at `:922`. `physics_init` adds a region to every run but `--report` and `--test-pattern`. Done: both numbers changed to 9, which is the count going up for the reason it exists to catch.
6. **What the first mutation run found.** 5 deliberate bugs survived the tests as first written. `floor tie off`: the tie rule between two boxes did nothing, so it went. `no split`, `exit without epsilon`, `pieces forget a stop` and `step on the way up` each got a new test case. `step over air` got `a_step_must_land_on_something`, and `no hop` a case with no Space held.