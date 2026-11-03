# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 4: Zombie

**Status:** done   (updated 2026-11-03)

## Goal

Zombie chases, attacks in melee, and drops items.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/mob/zombie/`
- `tests/run/mob/zombie/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement chase, melee and drops in `run/src/mob/zombie/index.c`
- [x] Register test `run_mob_zombie`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Results

All run on 2026-11-03.

- Debug build: clean, `standards_lint` clean, no recursion, `clang_format` passed.
- `run_mob_zombie`: 26 tests, 0 failed, about 259000 checks, in debug and release, and passed under the sanitize preset with one local flag (issue 4).
- `run_mob_path` and `run_mob_spawn`: passed in debug.
- All 57 `^run_` ctests in debug: 56 passed, 384 s. `run_types` failed, from before this stage (issue 3). `compile_*`, `asm`, `cuda` and the rest of the full suite were not run.
- A hundred zombies, 96 of them chasing a target going back and forth behind a wall, one tick being paths, minds and bodies: 0.098 ms in release, 0.47 ms in debug, with no heap and no arena. This is not `bench_mobs` (issue 2), and it is flat ground.
- `build/debug/bin/minecraft_c --headless --test-world --walk --time-of-day 0 --no-day-cycle --render-distance 4 --width 160 --height 90 --ticks 12000`: `zombies struck 8 blows`, `hurt 8 times, died 1`, `6 items dropped`, `0 bytes live`. The same run is in the test, which holds that the player was struck: a change to terrain or spawning could make that seed need longer.
- 6000 deaths: 0, 1 or 2 rotten flesh about a third each; iron, carrot and potato each within 20 to 80 of the 50 expected.
- Not run: anything on a real display. Nobody has seen a zombie, as nothing draws one (issue 1).

## Wiring outside the owned paths

- `run/include/mob/index.h`, `run/src/mob/index.c`, `run/src/mob/README.md`: `mob_t` holds `mob_zombie_t` in its 40 spare bytes, so its size stands. `mob_open` takes a seed. New `mob_minds_tick`.
- `run/include/game/index.h` and `run/src/game/index.c`, `run/src/game/README.md`: `game_mobs_tick` builds the view, calls `mob_minds_tick` after `mob_tick` and before `game_items_tick`, and gives its blow to `player_survival_hurt`. `mob_open` gets the terrain seed. `game_report` logs the zombies' counts. `index.h` only for formatting.
- `tests/run/mob/path/index.c`: `mob_open`'s seed in 3 places, and its arena limit now counts the zombies'.
- `run/include/main/index.h`, `tests/run/ui/hud/index.c`: see issue 3.
- `plans/REPO_STRUCTURE.md`: the row for `tests/run/mob/zombie/`.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_mob_zombie$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `2547ce3` Give zombies a mind: they stroll, notice and chase the player, strike it in melee, and leave their drops when they die.

## Issues found

1. **Nothing draws a zombie.** Stage 15.1's issue 4, which named this stage as the natural place.
   - Expected: a zombie can be seen coming.
   - Actual: `game_entities_publish` submits item drops and the target only, so an invisible zombie now hits the player: in the run above, the player dies of something it never saw.
   - Done: left alone and escalated to Ahmed. It is outside this stage's paths and its Todo, and it is not wiring this module needs to be called. `entity_animation_pose` has what `render_entities_item_t` takes. This needs a stage before the phase gate's night means anything.
2. **`bench_mobs` still has its stand-in mind.** Stage 15.1's issue 5 asks Stages 15.2 to 15.7 to add their work to it, but `bench/mobs/` is not in this stage's paths or Todo. Left alone. Its number for the phase gate does not include zombies yet; Results has what the test measured.
3. **The integration branch did not build, and has one red test.** Each comes from merges adding fields, with nothing of this stage in them.
   - Expected: `cmake --build --preset debug` is clean on `691c90d`.
   - Actual: `run/include/main/index.h:52: static assertion failed`, with `sizeof(app_t)` 14464 against 14080. Then `tests/run/ui/hud/index.c:118: static assertion failed`, with `fixture_t` 1216 against 896 since `ui_t` grew to 1024.
   - Done: both numbers corrected, as nothing could be built or verified otherwise. `clang_format` also failed on one merged line in each of `run/include/game/index.h` and `run/include/main/index.h`, which are reformatted.
   - Also: `run_types`, `game_runs_the_core_runtime_and_ends_with_nothing_live` and `game_exits_with_its_own_status_on_a_leak` want `12 reserved, 12 released` and `12 regions`, and the program has 13: the `mob` region from Stage 15.2, as this stage reserves nothing. Left alone, being outside these paths and not needed by this stage. Both numbers in `tests/run/types/index.c` want updating.
4. **The sanitize preset does not build, from before this stage.** `run/src/main/index.c:805: the frame size of 17344 bytes is larger than 16384 bytes`: `app_t` on `main`'s stack, with its red zones. This stage did not grow `app_t`. Left alone. To run this test under the sanitizers, a local `build/sanitize` was configured with `-DCMAKE_C_FLAGS=-Wno-error=frame-larger-than=`, which is in no file of the repo.
5. **A zombie that only stands never meets a player on real ground.** Expected: chase and melee alone would do, as the plan asks no more. Actual: 12000 ticks at midnight, `zombies struck 0 blows`, with zombies 10 or more blocks above or below the player, past `MOB_ZOMBIE_NOTICE_UP`. Done: added the published random stroll, in this stage's own paths, with its own tests. With it, the same run was struck 8 times.
6. **Damage is a constant here and not in `config/mobs/`.** A mob's file has no field for it, and the part reading those is outside these paths. Stages 15.5 and 15.6 may want it there, and then `MOB_ZOMBIE_DAMAGE` should move.
7. **A stroll can end early.** If its answer is put off to a later tick by the round's budget, `mob_path_follow` reads the last way, finds it over, and the stroll ends with the wish at 0. Its next one comes in about 6 seconds. Left as it is. While chasing the same reading only stops it until the answer comes.
8. **Five wrong expectations in the first draft of the new test, and two more the first time it ran with strolling.** The code was right in all of these, and the tests were corrected: the place it first noticed was behind the wall; a low wall did hide a target, so it gave up as it should; a crowd behind a wall noticed nothing, which also made its same-bytes check empty, and that check now also requires a blow; a hundred were mostly out of range; a loop did not run for slot 0; a target that walks 1 block in 14 ticks is not asked for 15 times; and the box's lower walls still hid the target.
9. **`pgrep -a minecraft_c` in this file's Verification is not scoped to a worktree**, as Stage 15.2's issue 6 found. `pgrep -af "$PWD/build"` is what was used.
