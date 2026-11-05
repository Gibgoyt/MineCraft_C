# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 1: Tree feller always on

**Status:** not started   (updated 2026-11-05)

## Goal

Breaking any log of a tree brings the whole tree down, with any tool or none. It's part of the game, always on.

**Ahmed's words:** "tree feller is not a mod in my minecraft it is a built in setting please make it always on!!", and "tree felling does work but only when using an axe! ... needs to be fixed".

## Why it happens

It worked as built, to a rule Ahmed didn't want. Plan 003 phase 7 asked him "should bare hands fell too?", and with him away the default "axes only" was used.

- The check is in `run/src/player/interaction/index.c:375`: `tool == BLOCK_TOOL_AXE && crouched == 0`.
- **Why it seemed to stop:** in the middle of the workflow, phase 4 moved wooden axes to the woodworking bench. A wooden axe got harder to make, so it's likely Ahmed wasn't holding one. Whatever the cause, the axe rule is what this phase removes.

## Todo

- [ ] Remove the axe condition in `interaction_mine`, so any tool or bare hands starts felling
- [ ] Remove the sneak exception too, unless Ahmed says to keep it (see Issues)
- [ ] Wear one use per log only when the tool is an axe, in `run/src/game/tick/index.c:120`. Other tools and hands take no wear from felling, and hands keep chopping at hand speed
- [ ] Update `tests/run/player/felling/`: bare hands, a pickaxe and an axe each fell a whole tree. A log wall with no leaves is still left alone
- [ ] Update `tests/run/player/felling/fixtures/fell.script` and `run_playthrough` to fell with bare hands
- [ ] Update the phase 7 notes in plan 003 that say "axes only", and `run/src/player/felling/README.md`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_felling|run_player_interaction|run_playthrough|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: punch the bottom log of a tree with bare hands, and the whole tree falls.

## Commits

Appended as they land:

## Issues found

**Open for Ahmed: keep sneak-to-chop-one?** "Always on" reads as no exceptions, so the default is to remove it. Keeping it costs nothing, and gives a way to take one log when building.
