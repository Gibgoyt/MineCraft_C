# 003-ahmed-bohemoth-initial-game-fixes — phase 7: Tree feller

**Status:** not started   (updated 2026-11-04)

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

- [ ] Add `run/src/player/felling/`: the walk, the queue of logs to break, and the per-tick breaking, with a README and header per the standard. Called from `run/src/player/interaction/`
- [ ] Share the wood check: move `tick_is_log` out of `run/src/world/tick/index.c`, or add a flag in `config/blocks/*_log/index.yaml`, so that this and leaf decay agree
- [ ] Start felling in `interaction_mine` when the broken block is a log, the held tool is an axe and the player isn't crouched
- [ ] Spawn drops through `player_interaction_drops_spawn`, and wear the axe per log through `equipment_tool_block_broken`. Stop if the axe breaks
- [ ] Add prompt leaf checks in `run/src/world/tick/`, reusing `tick_leaf_held`
- [ ] Add a test that walks every tree type from `run/src/terrain/feature/`, sets the limit from the biggest, and fells each: no log left above the cut, and the drop count equals the log count
- [ ] Add tests for: a mid-trunk cut leaves the stump. A log wall with no leaves is left alone. Other wood touching isn't taken. Sneaking breaks one. Bare hands break one. A breaking axe stops felling
- [ ] Add a test that no drop is lost on the biggest tree, counting `drops_lost_count`
- [ ] Add a scripted headless proof: before and after screenshots of a real tree, checked by eye
- [ ] Update `plans/REPO_STRUCTURE.md`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_felling|run_player_interaction|run_world_tick|run_item_equipment|run_playthrough|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: chop an oak, a jungle giant and an acacia at the base, then one tree mid-trunk. Then try a small log wall, and chop while sneaking.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

**No dependency on phase 1:** this adds no blocks or items, so it can be built at any time and doesn't touch saved worlds.

**A felling interrupted by quitting isn't saved.** The queue lives for about a second at most, so quitting at that moment leaves the rest of that tree standing.

**Open for Ahmed:**

1. **Should bare hands fell too?** The plan says axes only, like Hypixel, so wood punched by hand can still float until you have an axe. Letting hands fell ends floating wood completely, but the first tree you punch gives you all its logs, which makes the early game much easier.
2. **Should each log wear the axe?** The plan says yes, so a wooden axe stops after 59 logs. Charging one use per tree is more generous.
3. **Is sneak-to-chop-one what you want?** It's my addition.
