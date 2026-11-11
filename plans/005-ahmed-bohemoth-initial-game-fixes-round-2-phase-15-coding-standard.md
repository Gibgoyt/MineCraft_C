# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 15: Follow the coding standard

**Status:** in progress   (updated 2026-11-11)

## Goal

Every file in the repository follows `plans/CODING_STANDARD.md`, including the rules the lint does not check yet. Where a rule can be checked by a machine, it now is.

**Ahmed's words:** "coding standard was not followed please launch appropriate agents to finish".

## What was found, 2026-11-11

The build's `standards_lint` is clean, because it checks only what section 10 lists. A review against the rest of the standard found:

1. **Files past the 1500-line split** (section 5: "Split at 1500 lines, by making sub-directories"). 20 files, not counting the lint's own fixtures:

   | File | Lines | Over 1500 before plan 005? |
   |---|---|---|
   | `run/src/game/index.c` | 1638 | No (1500) |
   | `run/src/save/world/index.c` | 1563 | No (1499) |
   | `tests/run/save/world/index.c` | 1994 | Yes |
   | `tests/run/world/tick/index.c` | 1947 | Yes |
   | `tests/run/ui/hud/index.c` | 1860 | Yes |
   | `tests/run/player/survival/index.c` | 1838 | Yes |
   | `tests/cuda/shapes/index.c` | 1798 | No (1394) |
   | `tests/run/settings/index.c` | 1769 | Yes |
   | `tests/run/player/interaction/index.c` | 1755 | Yes |
   | `tests/run/item/equipment/index.c` | 1747 | Yes |
   | `tests/compile/texture_vector/index.c` | 1742 | Yes |
   | `tests/run/save/state/index.c` | 1667 | No (1497) |
   | `tests/run/terrain/biome/index.c` | 1628 | Yes |
   | `tests/compile/game_tables/index.c` | 1613 | No (1235) |
   | `tests/run/item/crafting/index.c` | 1560 | No (1500) |
   | `tests/cuda/entity_rays/index.c` | 1554 | Yes |
   | `tests/run/mob/path/index.c` | 1531 | No (1493) |
   | `tests/run/terrain/feature/index.c` | 1528 | Yes |
   | `tests/run/display/vt_guard/index.c` | 1521 | Yes |
   | `tests/run/mob/combat/index.c` | 1511 | Yes |

2. **New directories missing from `plans/REPO_STRUCTURE.md`** (section 5: "Full layout: plans/REPO_STRUCTURE.md"): `bench/lod`, `compile/src/game_tables/ores`, `compile/src/texture_pack/default_pack`, `run/src/game/fill`, `run/src/game/watch`, `run/src/world/block_state`, `run/src/world/door`, `run/src/world/light/column`, `tests/cuda/bridge`, `tests/cuda/preview`, `tests/run/game/focus`, `tests/run/player/interaction/forward`, `tests/run/player/interaction/preview`, `tests/run/render/distance_live`, `tests/run/render/lod`.

3. **The split rule is checked only by review,** so it slipped as phases added code.

## Todo

- [ ] Split `run/src/game/index.c` and `run/src/save/world/index.c` under 1500 lines into sub-directories, keeping the memory tiers (a depth-2 part allocates only from an arena it is given)
- [ ] Split the save, world-tick and settings tests: `tests/run/save/world`, `tests/run/save/state`, `tests/run/world/tick`, `tests/run/settings`
- [ ] Split the HUD, player and item tests: `tests/run/ui/hud`, `tests/run/player/survival`, `tests/run/player/interaction`, `tests/run/item/equipment`, `tests/run/item/crafting`
- [ ] Split the CUDA, compile and terrain tests: `tests/cuda/shapes`, `tests/cuda/entity_rays`, `tests/compile/texture_vector`, `tests/compile/game_tables`, `tests/run/terrain/biome`, `tests/run/terrain/feature`
- [ ] Split the mob and display tests: `tests/run/mob/path`, `tests/run/mob/combat`, `tests/run/display/vt_guard`
- [ ] Add every directory in finding 2, and every directory the splits make, to `plans/REPO_STRUCTURE.md`
- [ ] Make `standards_lint` fail a file over 1500 lines (outside its own fixtures), with a fixture test, and update section 10 of the standard so the split rule is machine-checked
- [ ] Full debug and release suites pass, and no test was lost: the same test cases run before and after each split

## Verification

    tools/test-or-build.sh cmake --build --preset release
    tools/test-or-build.sh timeout 3000 ctest --preset release -I 1,85 --output-on-failure
    tools/test-or-build.sh timeout 3000 ctest --preset release -I 86,400 --output-on-failure
    git ls-files '*.c' '*.h' '*.cu' '*.hu' '*.asm' '*.inc' | grep -v fixtures | xargs wc -l | awk '$1>1500 && $2!="total"'   # prints nothing
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

## Issues found

None yet.
