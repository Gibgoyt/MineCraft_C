# 003-ahmed-bohemoth-initial-game-fixes — phase 7: Tree feller

**Status:** in progress: everything an agent can run passes, in debug and with the release program built; only Ahmed playing it is left   (updated 2026-11-05)

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
