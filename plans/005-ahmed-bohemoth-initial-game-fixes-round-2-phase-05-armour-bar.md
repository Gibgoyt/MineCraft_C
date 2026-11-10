# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 5: Armour bar

**Status:** done   (updated 2026-11-10)

## Goal

Wearing armour shows a row of up to 10 armour icons above the hearts: one per 2 points, half for an odd one.

**Ahmed's words:** "when I put on armour i have no armour bar at all please make a plan to fix that!!"

## Why it happens

**It was never drawn.** `run/src/ui/hud/README.md` says "Armor points and a bar for them are not in this stage". Armour works: it's worn, stops damage and wears out. You just can't see it. `textures/ui/icon_armor` exists, unused.

## Todo

- [x] Send the worn armour points from the sim to the HUD, as health is sent, in `run/src/game/`
- [x] Draw 10 icons above the hearts in `run/src/ui/hud/index.c`, full, half and empty, shown only while at least one point is worn. Bubbles go above it when both are showing
- [x] Add an empty and a half icon if `textures/ui/` lacks them, as our recipes
- [x] Add tests in `tests/run/ui/hud/`: 0, 1, 10 and 20 points, alongside bubbles, with golden pictures
- [x] Update the HUD README

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_ui_hud|run_item_equipment.*|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: put on armour one piece at a time and watch the bar fill, then go under water to see both rows.

## Commits

Appended as they land:

- 17a8f7a5 Show an armour bar: ten icons over the hearts while armour is worn, with the bubbles a row higher beside it.

## Issues found

- **How the points get across.** `player_vitals_t` is one 8-byte word and full, so the armour points go in their own relaxed `_Atomic uint8_t player_t.armor_points`, in what was padding at offset 2097 (`player_t` did not grow). `player_vitals_publish` (now public) writes it from `survival.armor_points`, which `run/src/game/tick/` already sets each tick from `equipment_armor_points`; `game_hud_state_read` (`run/src/game/report/`) reads it with `player_armor_points_read` into `ui_hud_state_t.armor`, the byte that was `_pad0`. `run/src/save/state/` now calls `player_vitals_publish` in place of its own copy of it, so a loaded body shows its armour before its first tick.
- **The three icons are not in `ui_hud_t`.** Growing `ui_hud_t` grows `ui_t` by a whole 64-byte line and `app_t`, which is on main's stack at 16128 of the 16384 bytes `-Wframe-larger-than` allows. So the images sit after the items' icons in the `borrowed_icons` arena array, and `ui_hud_armor_icon(hud, UI_HUD_ARMOR_FULL|HALF|EMPTY)` returns them.
- **Textures.** `icon_armor` and `icon_armor_empty` were already our recipes. Only the half was missing: `textures/ui/icon_armor_half` is a new recipe, the left half of the full icon over the right half of the empty one, added to `textures/vector.yaml`. The half is its own picture, not a cut of the full one as the hearts do, because the 9-wide icon has no middle column to cut at. `texture_vector check`: 200 recipes, 479 needed, all there. `textures/README.md`'s recipe count went from 192 (already stale) to 200.
- **Where the bubbles go.** As the todo says, the bubbles move one row up (`ui_hud_layout_t.air_high_y`) while armour is showing too. The armour row is on the left over the hearts and the bubbles are on the right over the drumsticks, so they would not overlap at the same height (vanilla Minecraft keeps them level). This was done as the plan says. Making them level again means one line in `ui_hud_draw` and its model.
- **Golden pictures.** New: `armor_1_640x480.png` (one half and nine empties), `armor_10_air_640x480.png` (five full and five empty, with the bubbles a row above them on the right) and `armor_20_air_320x240.png` (a full row at scale 1, with the bubbles above). Each was checked by eye. The existing goldens are unchanged, and with 0 points the picture is still `air_640x480.png`.
- `tests/run/ui/hud/index.c` is now 1829 lines, under the 2000-line limit but close to it. The next HUD test should go in a program of its own, as `corner/` did.
