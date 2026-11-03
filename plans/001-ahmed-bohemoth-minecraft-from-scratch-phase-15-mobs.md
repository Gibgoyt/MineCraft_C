# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15: Mobs

**Status:** in progress: every check an agent can run passes; what awaits Ahmed is the night on a real display, below   (updated 2026-11-03)

## Goal

Zombies, skeletons and creepers spawn, behave, fight and drop items, and the player can fight back.

**Wave:** W8.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-1-entity-store.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-2-pathfinding.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-3-spawning.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-4-zombie.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-5-skeleton.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-6-creeper.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-stage-7-combat.md`

## Todo

- [x] Stage 15.1: Entity store
- [x] Stage 15.2: Pathfinding
- [x] Stage 15.3: Spawning
- [x] Stage 15.4: Zombie
- [x] Stage 15.5: Skeleton
- [x] Stage 15.6: Creeper
- [x] Stage 15.7: Combat
- [x] Gate, bench: 100 mobs with their real minds at most 5 ms a tick
- [ ] Phase gate passed: needs Ahmed to survive, or not, a night on a text console

## Verification

**Gate:** Bench with 100 mobs meets target (sim tick at most 5 ms); Ahmed survives (or doesn't) a night.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_(entity|mob)' --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 300 build/release/bin/bench_mobs
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    ./build/release/bin/minecraft_c

## Commits

Appended as they land:

- `8428656` Draw mobs part by part in their own skins, time the real minds in bench_mobs, and correct the tests the mob stages left wrong.
- `b7bc122` Let the sanitize preset build, and record the phase 15 gate check, its results and what awaits Ahmed.

## Results

All on 2026-11-03, on this branch.

- Clean debug build: passes, `standards_lint` clean, no recursion.
- `^run_(entity|mob)`: 8 of 8 pass in the clean debug build, and in the sanitize preset. `standards_lint`, `clang_format`, `run_types`, `run_game`, `render_feature_entities`: pass.
- Clean release build, `build/release/bin/bench_mobs`, exit 0: at 100 mobs, p99 1.291 ms scattered, 2.461 ms packed in a pit, 2.351 ms all around the player, against 5 ms. The minds did real work in each row: `bench/mobs/README.md`.
- Each stage's Verification: its test and `standards_lint`, as above.
- `pgrep -af "$PWD/build"` printed nothing.
- Full debug suite, `-j 4`: 88 of 91 passed. See Issue 8 for the other three.
- Not run, as agents must not: KMS mode. **What awaits Ahmed**, on a text console (Ctrl+Alt+F3, log in): `./build/release/bin/minecraft_c`, make a world, and stay out for a night. To be judged: that mobs look right and move right in play, that a fight feels fair, and whether he survives.

## Issues found

Found by the skeptical gate check of 2026-11-03, on the integration branch at `ff8d27b`, in a clean build.

1. **Nothing drew a mob, so the gate's night could not mean anything.**
   - Expected: mobs a player can see. `entity_animation_pose` and the rigs exist for it.
   - Actual: only tests called `entity_animation_pose`. `game_entities_publish` gave only item drops, arrows and the target to the renderer. Stages 15.1, 15.3, 15.4, 15.5 and 15.6 each wrote this down and left it. `textures/mobs/*` were in the blob, and nothing read them.
   - Done: fixed. `game_mobs_publish` gives each part to `render_entities_add_mob_part`. A box can now lean by a pitch (`entity_rays_to_box`, `entity_rays_normal`) and carry its share of a skin (`entity_rays_skin_uv`, in `cuda/src/shade/`), and `render_entities_init` puts each mob's skin in the frame's table. `entity_rays_box_t` went from 40 to 52 bytes and `render_entities_item_t` from 48 to 56. Goldens pass unchanged: no pitch and no skin is the same sums.
   - Evidence: new tests in `render_feature_entities`: `a_box_that_leans_is_as_on_paper`, `a_skin_is_unwrapped_as_on_paper`, random leaning rays against a march (1132 rays that met a box), and `a_mob_s_part_shows_its_share_of_the_skin`, 0 pixels off at 3 pitches. A headless screenshot, with a temporary patch not committed that put one of each in front of the player, was looked at: a green creeper, a white skeleton and a zombie in a teal shirt with its arms out, each recognisable.
   - Left: no hurt flash, no death animation, an arrow is its item's box. How it looks in play is Ahmed's.
2. **`bench_mobs` did not time mobs.**
   - Expected: the gate's number at 100 mobs includes what they think.
   - Actual: `bench_think` turned bodies at random and asked the grid who was near. No path, zombie, skeleton, creeper, combat or arrow code ran. Its 0.102 ms said nothing about the target. Stages 15.1 and 15.4 left it to others.
   - Done: fixed. `bench_tick` makes the real calls in the game's order on real workers, with three gated rows, and a gated row fails if the minds did nothing. See `bench/mobs/README.md`.
3. **`run_types` failed.** `tests/run/types/index.c:914` and `:922`: 13 regions wanted, 14 there, since `mob` and later regions. Stages 15.4 and 15.5 reported it and left it. Done: the numbers corrected.
4. **`run_mob_zombie` failed, and the failing test was tied to a seed.** See Stage 15.4's Issue 10. Replaced by a test on known ground through the whole game.
5. **The sanitize preset did not build.** `run/src/main/index.c:849: the frame size of 18112 bytes is larger than 16384 bytes`: red zones round `app_t`. Stage 15.4 got round it with a local flag, so its "passed under sanitize" was on a build nobody else could make. Done: `cmake/flags.cmake` gives that preset alone a limit of 32768. All 8 `run_entity*` and `run_mob*` ctests pass under ASan and UBSan.
6. **`mob_close` did not clear `mob->skeleton`.** It left a pointer into the arena it had just reset, unlike its neighbours. Found by reading. Fixed.
7. **Stale claims in READMEs and missing rows in `plans/REPO_STRUCTURE.md`.** "It is not drawn", "Nothing draws it", "for now", and no rows for two test directories. Corrected.

8. **Three failures in the full suite under `-j 4`, none on this phase's path.**
   - `run_render_frame` fails alone too, every time: `tests/run/render/frame/index.c:1349`, `game->columns_made > 100u`, and `:1355`, `picture == screen: first difference at byte 24945`, in `the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap`. That test's game has no player, entity or mob, so `game_sim_tick` runs none of this phase and `game_entities_publish` returns early. Stage 15.5's Issue 3 reported the same byte, before any of the changes above. Not fixed: it belongs to the render and game-loop stages, and its cause was not found. Left open and reported.
   - `run_render_upload` failed under `-j 4` and passed alone in 1.91 s. Its failing output was not kept. `render_feature_sky` timed out under `-j 4`, and passed alone, as Stage 15.5's Issue 4 saw. Both left alone: the machine and the card were shared.

Looked for and not found: stubs, TODOs and skipped tests in these paths; tests that assert nothing (the fewest checks in any is 3, and those are real); a directory with more than one `index.*` or no README. A test with 3 checks: `a_turn_turns_every_part_about_the_feet`, read, and it is enough.

**The plan's Verification `pgrep -a minecraft_c`** is not scoped to a worktree while other agents test: `pgrep -af "$PWD/build"` was used.
