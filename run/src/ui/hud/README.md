# run/src/ui/hud

**Purpose:** what is on the screen while playing: the crosshair, the hotbar with what is on it, the hearts and the drumsticks, the bubbles while short of air, and the debug overlay with the frames per second and each stage's time.
**Tier:** Arena: its textures come from the arena its Owner gives. Nothing is allocated once it has started.
**Owns:** nothing.
**Lifetime:** Process, for the textures.
**Threads:** `main`. It takes plain values and reads no other thread's data: getting them across is `run/src/game/`'s job.
**Public functions** (`run/include/ui/hud/index.h`):

- `ui_hud_init`, `ui_hud_deinit`: read the textures, and an icon for each item; empty the handle.
- `ui_hud_half_units`: a body's number as the halves of a row.
- `ui_hud_bubbles`: air as the bubbles of a row.
- `ui_hud_layout`: where things go on a canvas of some size.
- `ui_hud_item_draw`: one item with its wear and count, for any slot.
- `ui_hud_draw`: all of the HUD, from a `ui_hud_state_t`.
- `ui_hud_debug_draw`, `ui_hud_debug_panel`: the overlay, from a `ui_hud_debug_t`, and where it lies.
- `ui_hud_coordinates_draw`: x, y and z on a small panel at the top left, under a notice if there is one.
- `ui_hud_date_draw`: plan 004 phase 01's date line, such as `Year 1, Day 80, Early spring`, on a small panel at the top right, built as the coordinates' panel is and under a notice the same way.
- `ui_hud_notice_draw`: one line in a red band across the top, for what a person playing has no other way to learn: `game` uses it when saves are failing.

**May be called by:** `ui`, `run/src/game/` and the screens under `ui`.
**Depends on:** `arena`, `assert`, `log`, `texture`, `item/stack`, `item/inventory`, the generated item table, and its siblings `ui/compose` and `ui/font`, and `ui_widget_scale_for` from `ui/widget`, so that the HUD and the screens are one size.

## The hotbar has nine slots and its texture eight

`textures/ui/hotbar` is 162 by 22: slots 20 apart, from a pack made for an eight-slot game. So it is drawn in pieces: its eight slots, the last of them again, and the two columns that close it, 182 wide. Those are also the sizes `ui/hotbar_selected`, 24 square with a hole of 16, was drawn to go round.

## Where things go

In texture pixels, all times `ui_hud_layout`'s scale, which is the widgets':

| What | Where |
|---|---|
| Crosshair | 6 square, in the canvas's middle |
| Hotbar | 182 by 22, centered, on the bottom edge |
| The slot in hand | 24 square, one up and left of its slot |
| An item | 16 square, 3 in from its slot's left and top |
| A worn item's bar | 13 by 2, 2 in and 13 down from the item's top left, filled from the left, green to red |
| A count | white with a shadow, ending 17 in from the item's left, 9 down |
| Hearts | ten of 9 square, no gap, from the hotbar's left, 10 above its top |
| Drumsticks | the same, from the hotbar's right, going left |
| Bubbles | the same as the drumsticks, 10 above their top, and only while `bubbles_shown` says |

A half shows the half nearest the full ones. Each number is rounded up to a half, so that any health left shows. Bubbles have no halves: one for each 30 ticks of air, rounded up, so that the last goes just as the air runs out. `game` puts them up while the body is under water or still refilling, and not when it is dead. A canvas smaller than all this draws what fits: `ui_compose_*` clip, and nothing outside the canvas is touched.

## Coordinates

Whole blocks, rounded down, so that the block you stand in has the numbers of its low corner, on either side of 0. z is up, and 0 is sea level, so the numbers are the ones the world's limits are given in. They come from the feet.

The panel is as wide as its text, in the overlay's text size. `game` draws it only if the setting is on, and not while the overlay is up. Under a notice's band it goes one panel lower, as a body that has died is the one that most wants to know where.

## The debug overlay

F3 turns it on and off, on top of the HUD and over any kind of run, flying or walking. It lists what `ui_hud_stage_t` does: the five passes of the card and its total, from the card's own clock; the sim's last tick; and on `main`, what the HUD and copying the picture out took. Then come the coordinates, whatever the setting says, and its panel is wide enough for eight digits each way. Its last line is the eye's latitude and the sun's elevation there (plan 004 phase 02): `lat 45.00 N  sun 30.0`, `S` south of the equator and `N` at exactly 0, two decimals for the latitude and one for the sun. The longest, `lat 90.00 N  sun -66.5`, fits the panel. With no body, they are the flying eye's. Frames per second count **new pictures**, as `game_report` does, not how often `main` went round. The window is half a second, in `run/src/game/`.

Its text is half the HUD's scale, rounding up.

An item is drawn flat from its own texture, a block as one face of it. Armor points and a bar for them are not in this stage. The texture `ui/icon_armor` is there for them.

Tested by ctest `run_ui_hud`, against a model, against golden pictures and in the real game.
