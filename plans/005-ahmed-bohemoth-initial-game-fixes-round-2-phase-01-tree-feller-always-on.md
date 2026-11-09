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
- [ ] **Wear only for what you chop yourself, not for what falls** (Ahmed, `../prompt.md`: "I can chop 4 wood at the base of the tree but my axe gets worn as if I have cut every block that fell down!!"). The log you break costs one use, as any block does. Logs that fall with the tree cost nothing. Today `run/src/game/tick/index.c:120` charges one use per felled log, which was plan 003's default "each log wears the axe", and Ahmed has now rejected it
- [ ] Add a test: a whole tree felled with a fresh axe costs exactly as many uses as logs broken by hand, and a wooden axe can fell a tree of more than 59 logs
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

**Ahmed again, 2026-11-09, after playing today's `main`:** "also tree feller must be on regardless if I mine a tree with an axe!!", written three times. His log shows why: `the player mined 1 blocks ... the last mined was acacia_log` and `an axe felled 0 logs past what it cut`, with nothing in hand. Expected: the tree comes down. Actual: one log broke, because today's code fells only with an axe. That's exactly this phase's first todo, which isn't built yet. **This is now Ahmed's decision, not a default.**

**Axe wear, from `../prompt.md` (2026-11-09).** Expected, by plan 003 phase 7's default: one use per log that falls. Actual complaint: chopping 4 logs at the base wore the axe for the whole tree. Done here: the todo now charges only the logs Ahmed breaks himself. This replaces the old wear todo, which still charged per log when holding an axe.

**Open for Ahmed: keep sneak-to-chop-one?** "Always on" reads as no exceptions, so the default is to remove it. Keeping it costs nothing, and gives a way to take one log when building.
