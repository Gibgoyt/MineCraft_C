# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 5: Armour bar

**Status:** not started   (updated 2026-11-05)

## Goal

Wearing armour shows a row of up to 10 armour icons above the hearts: one per 2 points, half for an odd one.

**Ahmed's words:** "when I put on armour i have no armour bar at all please make a plan to fix that!!"

## Why it happens

**It was never drawn.** `run/src/ui/hud/README.md` says "Armor points and a bar for them are not in this stage". Armour works: it's worn, stops damage and wears out. You just can't see it. `textures/ui/icon_armor` exists, unused.

## Todo

- [ ] Send the worn armour points from the sim to the HUD, as health is sent, in `run/src/game/`
- [ ] Draw 10 icons above the hearts in `run/src/ui/hud/index.c`, full, half and empty, shown only while at least one point is worn. Bubbles go above it when both are showing
- [ ] Add an empty and a half icon if `textures/ui/` lacks them, as our recipes
- [ ] Add tests in `tests/run/ui/hud/`: 0, 1, 10 and 20 points, alongside bubbles, with golden pictures
- [ ] Update the HUD README

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_ui_hud|run_item_equipment.*|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: put on armour one piece at a time and watch the bar fill, then go under water to see both rows.

## Commits

Appended as they land:

## Issues found

None.
