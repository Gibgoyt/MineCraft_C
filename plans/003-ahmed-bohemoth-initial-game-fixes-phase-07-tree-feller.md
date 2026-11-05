# 003-ahmed-bohemoth-initial-game-fixes — phase 7: Tree feller

**Status:** in progress: every check an agent can run passes after the gate check, on the merged code, in debug and with the release program built. The whole suite is 104 of 104, in the way issue 11 says. Only Ahmed playing it waits   (updated 2026-11-05)

## Goal

Chopping a tree with an axe brings the whole tree down: every log above the cut breaks and drops, quickly from the cut upward, and the leaves follow soon after. No wood is left floating.

**Ahmed's words:** "let's please make tree feller I hate the idea that wood just floats there and hypixel has treefeller"

## How it works today

- `interaction_mine` (`run/src/player/interaction/index.c:223`) breaks one block. Nothing looks at what that block was holding up.
- Leaves already decay if no log is within 6 steps (`tick_leaf_held`, `run/src/world/tick/index.c:654`), so a felled tree's leaves do go, but only when random ticks reach them. They can hang in the air for a while.
- A voxel holds only a block id, so **a log placed by a player looks the same as one grown on a tree**.

## Design

### What counts as the tree

Starting from the log just broken, walk to touching logs, including diagonals, because acacia and cherry branch sideways and jungle and dark oak trunks are 2 wide.

- **Same wood only:** oak breaks oak.
- **At or above the cut only.** Cutting mid-trunk leaves the stump below standing on the ground, and nothing floats.
- **It must look like a tree:** the logs found must touch leaves. A log wall or log cabin has none, so it's left alone. This is what protects builds, and it isn't perfect: a log wall with a tree growing against it could still be taken.
- **Sneaking chops one log**, for when you want just one.
- **A limit on how many.** Set above the biggest tree the generator makes, measured by a test, not guessed. Past the limit, the rest stay.
- **A column that hasn't loaded stops the walk**, so a very wide tree at the edge of the world could leave some behind.

### Why it isn't all in one tick

The drop ring holds 32 (`PLAYER_INTERACTION_DROPS`, `run/include/player/interaction/index.h:15`), and a full ring loses the oldest drop. A giant jungle tree has more logs than that, so breaking it all at once would **lose logs**.

So felling breaks a few logs each tick, nearest the cut first, about 8 a tick. At 20 ticks a second that's about 160 logs a second: it reads as the tree coming down fast, and later gets a chain of crunches from phase 5.

### Leaves

When felling ends, the leaves near the removed logs get checked promptly, through the existing decay rule and drops, instead of waiting for random ticks. They clear over a second or two.

### Axe wear

**Proposed:** each log costs one use, like chopping it by hand. If the axe breaks mid-tree, felling stops there, and the rest stay up. A wooden axe has 59 uses.

## Hypixel

Ahmed names Hypixel as the reference. The closest I could find is SkyBlock's axes: one source says the Jungle Axe breaks up to 10 connected wood blocks in a chop, and the Treecapitator is its stronger version. **I couldn't confirm the Treecapitator's limit or cooldown**, and those sources are low quality. There, it's a special axe. This plan makes it every axe, because that's what stops floating wood. Ahmed can compare once he has played it, and a special axe later is a small change.

## Todo

- [x] Add `run/src/player/felling/`: the walk, the queue of logs to break, and the per-tick breaking, with a README and header per the standard. Called from `run/src/player/interaction/`
- [x] Share the wood check: move `tick_is_log` out of `run/src/world/tick/index.c`, or add a flag in `config/blocks/*_log/index.yaml`, so that this and leaf decay agree. It is `log: true`, `BLOCK_FLAG_LOG`, held to the names by `the_log_flag_is_on_every_block_named_a_log_and_no_other`
- [x] Start felling in `interaction_mine` when the broken block is a log, the held tool is an axe and the player isn't crouched
- [x] Spawn drops through `player_interaction_drops_spawn`, and wear the axe per log through `equipment_tool_block_broken`. Stop if the axe breaks. In a real game on the card: `in_a_real_game_an_axe_fells_a_tree_wears_a_use_a_log_and_stops_when_it_breaks`
- [x] Add prompt leaf checks in `run/src/world/tick/`, reusing `tick_leaf_held`. It is `world_tick_leaves_check`, and it takes vines too: issue 2
- [x] Add a test that walks every tree type from `run/src/terrain/feature/`, sets the limit from the biggest, and fells each: no log left above the cut, and the drop count equals the log count. Measured over 4000 seeds of each kind: the biggest is a jungle giant of 99 logs, and the limit is 256
- [x] Add tests for: a mid-trunk cut leaves the stump. A log wall with no leaves is left alone. Other wood touching isn't taken. Sneaking breaks one. Bare hands break one. A breaking axe stops felling
- [x] Add a test that no drop is lost on the biggest tree, counting `drops_lost_count`
- [x] Add a scripted headless proof: before and after screenshots of a real tree, checked by eye. `tests/run/player/felling/fixtures/fell.script`, read on seed 36: issue 4
- [x] Update `plans/REPO_STRUCTURE.md`
- [x] Gate check: a test that cuts every kind of tree at nine heights and finds no wood or vine floating, and one in a real game with the real shift key and bare hands. Issue 10
- [ ] Ahmed plays it: the steps under Verification. Only he can say whether it feels right

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_felling|run_player_interaction|run_world_sim|run_item_equipment|run_playthrough|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

The proof by eye, by hand, as it is aimed from screenshots: `tests/run/player/felling/README.md`.

Ahmed: chop an oak, a jungle giant and an acacia at the base, then one tree mid-trunk. Then try a small log wall, and chop while sneaking.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

- `37588e0` Make an axe bring down the rest of a tree, a few logs a tick with a drop and a use for each, and have the leaves and vines it held go straight after, leaving a log wall, a stump and other wood alone.
- `06e2620` List the tree feller test in the repository structure.
- `b7daaed` Say in the repository structure where an axe bringing down a tree lives, and what the proof by eye is.
- `19ca349` Keep what is coming down of a tree out of the player and out of app_t, whose stack frame has no byte to spare in the release build.
- `d5b76cf` Record in its plan file that the tree feller is built and what the work found, with only Ahmed playing it left.
- `c7cb697` Record in the tree feller's plan file the whole suite and the proof by eye on the merged code, with run_profiler's failure, which is not the feller's.
- `7f70b3c` Test that no wood or vine is left floating wherever a tree is cut, and that sneaking and bare hands cut one log in a real game with the real shift key.
- `9a8c337` Have the profiler's mining script look up at the sky first, as the new world starts a walking body with a hill in reach of a level eye.

## Issues found

**No dependency on phase 1:** this adds no blocks or items, so it can be built at any time and doesn't touch saved worlds.

**A felling interrupted by quitting isn't saved.** The queue lives for about a second at most, so quitting at that moment leaves the rest of that tree standing.

**Open for Ahmed, built on the defaults of the plan's table:** bare hands do not fell, each log wears the axe, and sneaking chops one log.

1. **Should bare hands fell too?** The plan says axes only, like Hypixel, so wood punched by hand can still float until you have an axe. Letting hands fell ends floating wood completely, but the first tree you punch gives you all its logs, which makes the early game much easier.
2. **Should each log wear the axe?** The plan says yes, so a wooden axe stops after 59 logs. Charging one use per tree is more generous.
3. **Is sneak-to-chop-one what you want?** It's my addition.

### From the build, 2026-11-05

**1. The Verification line named a test that does not exist.** Expected `run_world_tick`, as written above. `tests/run/world/tick/test.cmake` names it `run_world_sim`, so `ctest -R` would have matched nothing and said nothing. Fixed in the line above.

**2. A felled tree left its vines floating, which the plan did not mention.** Expected from the plan: logs and leaves go. Actual, from `terrain_feature_paint_tree`: `feature_vines` hangs up to 3 strands on a trunk, and nothing in `world/tick` ever removes a vine (`config/blocks/vine/index.yaml` does not decay). Added to the same sweep, `tick_vine_held`: a vine goes if nothing solid other than leaves is beside or above it, and no vine is above it, going from the top down. It only runs in a box felling asks for, so a vine anywhere else, and one on a stump or a wall in that box, stays. Tests: `every_kind_of_tree_comes_down_whole_with_a_drop_for_every_log` counts vines left, and `a_box_of_leaves_goes_in_its_ticks_by_the_rule_of_a_random_tick` keeps one on a wall.

**3. The queue does not fit on main's stack, and not even a pointer to it fits in `player_t`.** Expected a `player_felling_t` inside `player_t`, like its other parts. Actual: `static_assert(sizeof(app_t) == 16000)` in `run/include/main/index.h` failed, with `app_t app` on main's stack against `-Wframe-larger-than=16384` in `cmake/flags.cmake`, and the queue is 5184 bytes. First answer: a pointer in `player_t`, which grew `player_t` and `app_t` by 64 bytes. Every debug test passed with that, and the release build then failed: `run/src/main/index.c:1057:1: error: the frame size of 16448 bytes is larger than 16384 bytes`. `main`'s frame was exactly at the limit, so `app_t` has no byte to spare. Fixed: `player_t` and `app_t` are the size they were. `run/src/game/index.c` takes the queue from its world arena at `game_open`, the pointer sits in padding `game_t` already had, and the game passes it in through `player_interact_fell_tick`. `player_interact_tick` is that with nullptr, where an axe cuts one log. The leaf sweep's four boxes come from the tick's arena for the same reason, so `world_t` stays 2048 bytes. **Anything that adds to `app_t` will hit this**, which later phases should know. Small wiring outside the phase's own paths: `run/include/game/index.h`, `run/src/game/index.c`, `run/src/game/tick/index.c`, `run/src/game/report/index.c` (one line of the report), `run/include/world/index.h`, and `compile/src/game_tables/blocks/` for the flag.

**4. The proof by eye, on seed 36: mangroves whose roots touch come down together.** `tests/run/player/felling/fixtures/fell.script` crafts a wooden axe through the real screens and cuts once at the foot of a real mangrove. The report: `the player mined 53 blocks`, 5 of them before the axe, and `an axe felled 47 logs past what it cut`. I read `before.png`, `during.png` and `later.png`, looked at from about the same place. Before, trunks and a crown fill the right of the picture. Just after, they are gone and there is open sky, with logs lying on the ground and leaf drops in the air. Later, the drops have landed and nothing hangs in the air in view. The tree on the left is untouched. **But 47 is more than one mangrove**, which `run_player_felling` measures at up to 65 with the deepest roots and usually fewer: the walk went on through roots that touch a neighbour's, as they are the same wood. That follows the plan's rule, so it is left alone. In a dense swamp or jungle one cut can take several trees, up to the limit of 256 or what the axe has left, 59 for a wooden one. **Left for Ahmed to judge when he plays it.** The sign to look for is a neighbour going with the tree he cut, and the way to narrow it would be a smaller limit, or no walking at the level of the roots.

**5. Changing tool in that second stops it.** Not in the plan. With no axe in hand there is nothing to charge a use to, so `game_fell_tick` gives `uses_left` 0, and what is left stays up, the same as an axe breaking. Changing to another axe goes on, and charges that one. A death does the same as changing tool, as the slot empties.

**6. The bench will move what `fell.script` crafts.** Phase 4 moves wooden tools to the bench by default, and the script makes its axe on a crafting table, so it will click on nothing after that. It is deliberately not a ctest, and `tests/run/player/felling/README.md` says so. The ctests give their axes directly and do not depend on it.

**7. Known and left alone.**

- A stack of plants on a log, past what the hands' ring holds in one tick, could still push a drop out, counted in `drops_lost_count`. The room check allows a log's rows and one over. It takes a wall of sugar cane on a log, and there is no such thing in generated trees.
- Floating wood is still possible by the plan's own rules: from bare hands, a sneak chop, or an axe that broke. An axe on what floats takes it only if some of it still touches leaves, so wood whose leaves have since decayed has to be taken a log at a time.
- Phase 5's chain of crunches can hang off `player_felling_tick`'s return value, or `felled_count`.

**8. `run_profiler` fails in the whole suite, and the feller is not the cause.** Expected from the whole suite, which the first agent on this phase had not run: all pass. Actual, `tools/test-or-build.sh timeout 1700 ctest --preset debug --output-on-failure` on this branch merged with main at `3c6d85f`: 103 of 104 passed, with `run_player_felling` among them, and `run_profiler` failed, the same again when run alone. `tests/run/profiler/index.c:1095`, in `what_is_aimed_at_and_what_lies_there_have_kernels_when_they_are_there`: `CHECK failed: overlay > 0u && overlay < frames`, printing `161 frames: entity_rays in 116, entity_overlay in 161`. So an outline is on screen in every frame, including those before the script looks down, where the test expects at least one frame with none. Why I think it is not this phase: a full run in the worktree of another stage, `wf_b0782147-7f9-34`, whose log has no `run_player_felling` in it, fails the same check with the same three numbers. Felling also runs only when an axe cuts a log, and this script has no axe. Not proven: I did not run it on main alone, or find which merge changed it. In phase 10's notes the test passed, and the gate check of phase 8 reports the whole suite passing, so it changed after those. Probably where the player starts in `--test-world`, or what is in reach of it, but that is a guess. **Left alone, as outside this phase, for the final check to fix or send on.** `run_profiler` is not on this phase's Verification line.

**9. The proof by eye again, on the final merged code.** The first agent had not re-read the pictures after `19ca349`. Run again as in `tests/run/player/felling/README.md`: `the player mined 53 blocks and placed 1`, `an axe felled 47 logs past what it cut`, the same numbers as issue 4, and `0 destroyed, 0 lost` for items. I read `before.png`, `during.png` and `later.png`. Before, big trunks and a crown fill the right of the picture. Just after, they are gone, with logs on the ground and leaf drops in the air. Later, the drops have landed. No log or leaf hangs in the air in view, and the trees on the left and further off are still there. So `fell.script` still works on what is merged, and issue 6 waits for phase 4.

### From the gate check, 2026-11-05

**10. The tests only cut at the foot, or in a made-up trunk, so "no wood floating" had not been shown for where a person cuts.** Expected: the phase's goal holds at any height on a real tree. Actual, which was only unproven and not wrong: `pf_fell_one` cuts at the lowest log, the mid-trunk test uses a plain 14-high trunk, and sneaking and bare hands were set in the rig, with `crouched` written by hand, and never from a key. Added `wherever_a_tree_is_cut_no_wood_is_left_floating`: 8 kinds, 2 seeds, 9 heights, and a check after each that every log left joins one at the foot, and that every vine left has something to hang on. Added `in_a_real_game_a_sneaking_body_and_bare_hands_cut_one_log` in `run_item_equipment`. An exploring run over 12 seeds at heights 0 to 2, and 3 seeds at heights 0 to 8, found no floating log. What those runs did find, none of it a fault in the code:

- The plan's "no log left above the cut" is only true for a cut below any fork. Cherry, cut above where a branch leaves the trunk, keeps the branch, 3 of the first run's cuts. It stands on the stump, so nothing floats, and the rule stays as it is. The new test checks floating, not that.
- A cut up in a crown keeps leaves, 53 to 109 of them, as they touch the stump and the rule of a leaf holds them. An oak cut at its third log did it once in 36, and heights 0 and 1 never did, so the new test holds those two at no leaves left. Height 1 is where a body on flat ground looks. **Left for Ahmed to judge when he plays it:** the way to change it is for a sweep not to count the stump. `run/src/player/felling/README.md` says this now.

To see that the tests can fail, I broke the code and ran them, one at a time, then put it back. Taking away `next_z >= z` fails the stump and giant tests. Taking away the leaves check fails the wall test and `a_second_tree_cut_while_the_first_comes_down_goes_too`. Taking away the room check fails the no-drop-lost test. Taking away the crouch check fails the new real-game test. No TODO or stub in the feller, the lint passes, and nothing in the code is uncalled except `player_felling_pending`, `world_tick_sweeps_pending` and `turned_away_count`, which only tests read. They are small and were left.

**11. `run_profiler` is fixed, and issue 8's guess is confirmed.** Cause: the test's script starts with a level eye and takes it that nothing is in reach. A screenshot of `--headless --test-world --walk` at tick 1 shows the body at -119.5, 440.5, 15.0 with a sand hill outlined straight ahead, so the outline is on from the first frame. That is the body's start from phase 8 (`a54a446`, `837d96b`), and neither a fault in the outline nor the feller's. The program is right and the test's belief is out of date. Fixed in the test only, as small wiring outside the phase: the script looks up at the sky first, then down, then mines (`9a8c337`). It now passes.

The whole suite, `tools/test-or-build.sh timeout 1900 ctest --preset debug --output-on-failure`: 102 of 104 in one run, the failures being `run_item_equipment` and `run_player_felling`. **Those two were my own doing:** I had put the broken-on-purpose source back without rebuilding, so those two programs were the broken builds, failing on just the broken checks (`in_a_real_game_a_sneaking_body_...`, `no_drop_is_lost_...`). After a rebuild with no change of source, both pass, with `run_player_interaction`, `run_world_sim`, `run_playthrough`, `run_profiler`, `standards_lint` and `clang_format`. So every test has passed on this source, though not all in the one run. The release program builds: `tools/test-or-build.sh timeout 280 cmake --build --preset release --target minecraft_c`, so the stack limit of issue 3 holds.

**12. The proof by eye once more, on this merge.** `fell.script` as in `tests/run/player/felling/README.md`: `the player mined 53 blocks and placed 1`, `an axe felled 47 logs past what it cut`, `0 destroyed, 0 lost`. I read `before.png`, `aimed.png`, `during.png` and `later.png`. Aimed at a mangrove log with its outline. Before, trunks and crowns fill the view. After, they have gone with open sky there, logs on the ground and leaf drops in the air, and later the drops have landed. Nothing hangs in the air in view. It is one kind of tree on one seed, and other kinds are shown by the tests above. Issue 4's question, a neighbour going too, stays for Ahmed: 47 is still more than one mangrove.
