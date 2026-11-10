# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 1: Tree feller always on

**Status:** done: every todo built and tested, the Verification line passes. Only Ahmed punching a tree waits   (updated 2026-11-10)

## Goal

Breaking any log of a tree brings the whole tree down, with any tool or none. It's part of the game, always on.

**Ahmed's words:** "tree feller is not a mod in my minecraft it is a built in setting please make it always on!!", and "tree felling does work but only when using an axe! ... needs to be fixed".

## Why it happens

It worked as built, to a rule Ahmed didn't want. Plan 003 phase 7 asked him "should bare hands fell too?", and with him away the default "axes only" was used.

- The check is in `run/src/player/interaction/index.c:375`: `tool == BLOCK_TOOL_AXE && crouched == 0`.
- **Why it seemed to stop:** in the middle of the workflow, phase 4 moved wooden axes to the woodworking bench. A wooden axe got harder to make, so it's likely Ahmed wasn't holding one. Whatever the cause, the axe rule is what this phase removes.

## Todo

- [x] Remove the axe condition in `interaction_mine`, so any tool or bare hands starts felling
- [x] Remove the sneak exception too, unless Ahmed says to keep it (see Issues)
- [x] **Wear only for what you chop yourself, not for what falls** (Ahmed, `../prompt.md`: "I can chop 4 wood at the base of the tree but my axe gets worn as if I have cut every block that fell down!!"). The log you break costs one use, as any block does. Logs that fall with the tree cost nothing. Today `run/src/game/tick/index.c:120` charges one use per felled log, which was plan 003's default "each log wears the axe", and Ahmed has now rejected it
- [x] Add a test: a whole tree felled with a fresh axe costs exactly as many uses as logs broken by hand, and a wooden axe can fell a tree of more than 59 logs
- [x] Update `tests/run/player/felling/`: bare hands, a pickaxe and an axe each fell a whole tree. A log wall with no leaves is still left alone
- [x] Update `tests/run/player/felling/fixtures/fell.script` and `run_playthrough` to fell with bare hands
- [x] Update the phase 7 notes in plan 003 that say "axes only", and `run/src/player/felling/README.md`

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

**Done, 2026-11-10 (plan 005 agent).**

**1. Sneak exception removed, by the default.** No answer from Ahmed on keeping it, and "always on" reads as no exceptions. `interaction_mine` now starts felling for every log it cuts, with any tool or none, sneaking or not. Tests: `sneaking_fells_the_whole_tree_too` (`run_player_felling`) and `in_a_real_game_a_sneaking_body_and_bare_hands_fell_the_whole_tree` (`run_item_equipment`).

**2. `player_felling_tick` lost its `uses_left` argument.** Expected: just stop charging felled logs in `game_fell_tick`. Done instead: since nothing falling costs a use any more, an axe running out can no longer stop a tree, so the argument and the code that dropped the queue on it were dead. Removed, with the two tests of it (`an_axe_that_runs_out_...`, `with_the_axe_gone_on_the_cut_itself_...`), replaced by `putting_the_axe_away_while_it_comes_down_leaves_nothing_up`. `game_fell_tick` now only runs the tick and posts the sound. The cut log is still worn by `game_hands_tick` through `equipment_tool_block_broken`, as any block.

**3. Wear tests, in a real game on the card (`run_item_equipment`, not on the Verification line, run and passing).** `in_a_real_game_an_axe_fells_a_tree_taller_than_its_uses_and_wears_only_the_cut`: a fresh wooden axe (59 uses) cuts a 70-log trunk at the second log, 68 fall, the axe is still in hand at wear 1. `in_a_real_game_an_axe_that_breaks_on_the_cut_still_fells_the_tree`: an axe with one use left breaks on the cut, the tree still comes down. First try checked that slot 0 was empty after the break; it failed because the logs the tree dropped were picked up into that slot. Now it checks no wooden axe is left.

**4. The report line changed** from `game: an axe felled N logs past what it cut` to `game: the tree feller felled N logs past what was cut`, since no axe is needed. `run_playthrough` and the READMEs read the new text.

**5. `fell.script` is now bare hands, and much shorter.** Mud underfoot, out of the hole to the mangrove, look up (`before.png`), back down (`aimed.png`), hold the left button 75 ticks on the root log with only mud in hand, look up again (`during.png`, `after.png`, `later.png`), then save and reload as before. Played: `the player mined 11 blocks and placed 0; the last mined was mangrove_log, and the hand holds 1 of mud`, `the tree feller felled 9 logs past what was cut`, `0 destroyed, 0 lost`. I read `before.png`, `aimed.png` and `later.png`: the big mangrove trunk in front is gone in `later.png`, with the trees behind still standing and nothing floating in view. **This mangrove is 10 logs, not the 47 of plan 003's axe run** (a different tree, cut at its root), so `run_playthrough` now asks for at least 8 felled, not 30.

**6. The other three playthrough scripts changed numbers, not behaviour.** `day.script`, `stairs.script` and `bench.script` all start by taking "four logs" by hand from the same mangrove. Now the first of those clicks brings the whole tree down (the cut and 9 more), and the next three clicks find nothing. Crafting, benches, stairs and saves all still work. Only the report counts move: day `mined 11 ... placed 2`, `8 items in 3 stacks` (was 5 and 3/3); stairs `mined 13 ... placed 4`, `13 items in 6 stacks` (was 7 and 8/5); bench `mined 11`/`15 items in 4 stacks` then `mined 12`/`16 items in 5 stacks` (was 5, 10/3, 6, 11/4). Measured by running each script, then confirmed by a passing `run_playthrough`. The scripts' three now-useless log clicks are left in, since removing them would shift every later tick; a later phase can trim them.

**7. Not changed: `plans/REPO_STRUCTURE.md:708`** still says "How an axe brings down a tree". The rules let this phase edit only its own plan file and plan 003 phase 7, so the orchestrator should reword it.

**Verification, 2026-11-10:** `tools/test-or-build.sh cmake --build --preset debug` clean. `tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_player_felling|run_player_interaction|run_playthrough|run_item_equipment|standards_lint|clang_format)$' --output-on-failure`: all passed after `clang-format -i` on the changed files (the first run failed only `clang_format`). `pgrep -a minecraft_c` prints nothing.
