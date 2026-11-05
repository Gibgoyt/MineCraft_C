# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 3: Turn off zombies, skeletons and creepers

**Status:** not started   (updated 2026-11-05)

## Goal

Zombies, skeletons and creepers never appear. The mob system stays, ready for Ahmed's own mobs and for sheep (phase 4).

**Ahmed's words:** "completely disable skeletons, creepers, and zombies, I have ideas for my own mobs"

## How it is today

Each mob has a `spawn: weight:` in `config/mobs/<mob>/index.yaml`, and `config/mobs/README.md` says "0 never comes by itself". So turning them off is mostly data.

## Todo

- [ ] Set `weight: 0` for `zombie`, `skeleton` and `creeper`
- [ ] Remove any of them already in a saved world when it opens, with a log line, so an old world doesn't keep them
- [ ] Keep the code, models and textures, so a mob of Ahmed's can reuse the parts. Note in `config/mobs/README.md` that they're turned off and why
- [ ] Update the tests that expect them to spawn naturally: `run_mob_spawn`, `run_playthrough`, the night script, and `run_mob_sight`. Keep the tests that place a mob on purpose (combat, explosions), so the code stays tested
- [ ] Add a test that a long night in the open spawns no zombie, skeleton or creeper
- [ ] Check whether `MOB_SPAWN_CAP` (70) is still right for passive mobs only, and record it

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_mob_.*|run_playthrough|run_entity_.*|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: stay out through a whole night, and see no hostile mob.

## Commits

Appended as they land:

## Issues found

**This probably also fixes the lag after Alt+Tab** (phase 7): the world filled to 70 mobs while Ahmed was away. Phase 7 still proves it, and doesn't rely on this.
