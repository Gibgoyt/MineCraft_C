# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 3: Turn off zombies, skeletons and creepers

**Status:** done, all todos built and tested (updated 2026-11-10)

## Goal

Zombies, skeletons and creepers never appear. The mob system stays, ready for Ahmed's own mobs and for sheep (phase 4).

**Ahmed's words:** "completely disable skeletons, creepers, and zombies, I have ideas for my own mobs"

## How it is today

Each mob has a `spawn: weight:` in `config/mobs/<mob>/index.yaml`, and `config/mobs/README.md` says "0 never comes by itself". So turning them off is mostly data.

## Todo

- [x] Set `weight: 0` for `zombie`, `skeleton` and `creeper`
- [x] Remove any of them already in a saved world when it opens, with a log line, so an old world doesn't keep them
- [x] Keep the code, models and textures, so a mob of Ahmed's can reuse the parts. Note in `config/mobs/README.md` that they're turned off and why
- [x] Update the tests that expect them to spawn naturally: `run_mob_spawn`, `run_playthrough`, the night script, and `run_mob_sight`. Keep the tests that place a mob on purpose (combat, explosions), so the code stays tested
- [x] Add a test that a long night in the open spawns no zombie, skeleton or creeper
- [x] Check whether `MOB_SPAWN_CAP` (70) is still right for passive mobs only, and record it

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_mob_.*|run_playthrough|run_entity_.*|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: stay out through a whole night, and see no hostile mob.

## Commits

Appended as they land:

## Issues found

1. **How it is done.** `weight: 0` in the three files (each was 100, noted beside it). `mob_spawn_tick` now adds the weights up first and, at 0, tries no place and draws nothing from its random stream: it only despawns. The hostile spawn path is untouched and general: phase 4 adds animals by giving them weights (and day rules).
2. **The rules stay tested.** `mob_spawn_view_t` gained `weights` (offset 32, size 32 to 40): `nullptr`, as the game passes, means each mob's `spawn.weight`; a test can pass its own. `run_mob_spawn` passes the old 100s for every test of the tick, so packs, caps, caves, torches and the same-bytes tests run as before (with 100s the stream is drawn exactly as before). `bench_mobs` and `fuzz_save` place their mobs by hand and are unchanged; the bench's spawn step now does what the game's does, which is less.
3. **Old saves.** `mob_spawn_retire` takes away every mob whose weight is 0, and `game_open` calls it right after `save_state_apply`, logging `game: took N of mob <name> out of the saved world, as it is turned off` per kind. Not counted as despawned. **Consequence, written in `config/mobs/README.md`:** a weight of 0 now also means "not kept from a save", so a future mob of Ahmed's that only comes some other way (summoned, placed) must change that rule first.
4. **Tests changed.** `run_mob_spawn`: tables check weight 0 for all three; tick tests on the old weights; new `a_long_night_in_the_open_brings_no_zombie_skeleton_or_creeper` (24000 midnight ticks plus 4000 noon in an open field on the real weights: nothing spawned, no RNG drawn, and the old weights then fill the same field to the cap) and `retiring_takes_every_turned_off_mob_and_leaves_the_rest`. `run_mob_zombie`: the program test is now 12000 ticks of midnight in the real game bringing 0 mobs, 0 blows, 0 arrows, 0 bursts. `run_playthrough`: the night script is unchanged (its menu clicks still land); the test now checks every report says 0 mobs came, struck, shot, burst, and the player never died. `run_save_state_game`: new `a_world_saved_with_turned_off_mobs_opens_without_them` (four placed by hand, saved, reopened: none; saved again and reopened: none). `run_mob_sight`, `run_mob_combat`, `run_mob_creeper`, `run_mob_skeleton` place mobs on purpose and needed no change.
5. **`MOB_SPAWN_CAP` (70): left as it is, for now.** With no mob coming it stops nothing. 70 is the published game's monster cap; its animals have their own cap of 10, because they never despawn and would pile up. One cap of 70 for passive mobs would let a world fill with 70 animals that stay. **Phase 4 should give animals a cap of their own, near 10 (per 289 loaded columns, as now), or a separate category, rather than share 70.** Written in `run/src/mob/spawn/README.md`.
6. No golden picture or pinned digest changed. The save's table hash covers mob names, not weights, so saves from before still open. Run here: the Verification regex, plus `run_save_state_game`, `run_save_state`, `run_failure` and `run_game_sun`; not the whole suite.

**This probably also fixes the lag after Alt+Tab** (phase 7): the world filled to 70 mobs while Ahmed was away. Phase 7 still proves it, and doesn't rely on this.
