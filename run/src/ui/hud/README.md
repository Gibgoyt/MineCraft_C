# run/src/ui/hud

**Purpose:** what is on the screen while playing: the crosshair, the hotbar with what is on it, the hearts and the drumsticks, the armour while any is worn, the bubbles while short of air, and the debug overlay with the frames per second and each stage's time.
**Tier:** Arena: its textures come from the arena its Owner gives. Nothing is allocated once it has started.
**Owns:** nothing.
**Lifetime:** Process, for the textures.
**Threads:** `main`. It takes plain values and reads no other thread's data: getting them across is `run/src/game/`'s job.
**Public functions** (`run/include/ui/hud/index.h`):

- `ui_hud_init`, `ui_hud_deinit`: read the textures, and an icon for each item; empty the handle.
- `ui_hud_armor_icon`: one of the armour row's three pictures, full, half or empty.
- `ui_hud_half_units`: a body's number as the halves of a row.
- `ui_hud_bubbles`: air as the bubbles of a row.
- `ui_hud_layout`: where things go on a canvas of some size.
- `ui_hud_item_draw`: one item with its wear and count, for any slot.
- `ui_hud_draw`: all of the HUD, from a `ui_hud_state_t`.
- `ui_hud_debug_draw`, `ui_hud_debug_panel`: the overlay, from a `ui_hud_debug_t`, and where it lies.
- `ui_hud_stage_name`: a stage's name as the overlay writes it, which `game/watch` also logs.
- `ui_hud_coordinates_draw`, `ui_hud_coordinates_panel`: x, y and z on a small panel at the top left, under a notice if there is one; the panel's rect without drawing it, which the draw itself uses, so the two never disagree.
- `ui_hud_date_draw`: plan 004 phase 01's date line, such as `Year 1, Day 80, Early spring`, on a small panel at the top right: since plan 004 phase 10 it is the corner with only its date line, so the two are the same pixels.
- `ui_hud_corner_clear`, `ui_hud_corner_draw`, `ui_hud_corner_arrow_ends`: plan 004 phase 10's top right corner (below).
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
| Armour | the same as the hearts, 10 above their top, and only while `armor` is 1 or more |
| Bubbles | the same as the drumsticks, 10 above their top, and only while `bubbles_shown` says; 10 higher again while the armour is up too |

A half shows the half nearest the full ones. Each number is rounded up to a half, so that any health left shows. Bubbles have no halves: one for each 30 ticks of air, rounded up, so that the last goes just as the air runs out. `game` puts them up while the body is under water or still refilling, and not when it is dead.

## Armour

Plan 005 phase 05. `armor` is the points worn, 0 to 20, as `run/src/game/` tells the body's survival each tick from what is worn, and `run/src/player/` sends across with its health. An icon is two points: full for two, `ui/icon_armor_half` for an odd one at the end, `ui/icon_armor_empty` for the rest, so 1 point is a half and nine empties, 10 is five full and five empty, and 20 the whole row. Each is a picture of its own, drawn whole, as the armour icon is 9 wide with no middle to cut it at, unlike the hearts. With no armour there is no row at all. All three are our own recipes in `textures/ui/`. Their images live after the items' icons in `borrowed_icons`, as `ui_t` cannot grow: `app_t` is on main's stack, near its frame limit. A canvas smaller than all this draws what fits: `ui_compose_*` clip, and nothing outside the canvas is touched.

## Coordinates

Whole blocks, rounded down, so that the block you stand in has the numbers of its low corner, on either side of 0. z is up, and 0 is sea level, so the numbers are the ones the world's limits are given in. They come from the feet.

The panel is as wide as its text, in the overlay's text size. `game` draws it only if the setting is on, and not while the overlay is up. Under a notice's band it goes one panel lower, as a body that has died is the one that most wants to know where.

## The top right corner

Plan 004 phase 10. A short stack of lines from a `ui_hud_corner_t`, plain text `game` has made: slot 0 for a settlement's name, which a later plan fills and which is **never drawn in plan 004**, whatever it holds; slot 1 the date line; slot 2 the climate line; slot 3 a storm warning. Each line is its own panel, as wide as its text and two margins, right-aligned a margin from the canvas's right edge, the panels touching top to bottom with no gap. An empty line takes no room, so with the date off the climate line is the top one. All at the overlay's text size (half the HUD's scale, rounding up), with a margin of two of its pixels, as the coordinates' panel.

| What | Where |
|---|---|
| The first panel's top | a margin down, or a margin and a notice band's height (a line and two margins) under a notice |
| A panel | its text and two margins wide, a line and two margins tall, its text a margin in |
| The climate line's arrow | after the text and a space, in a square a line high (`UI_FONT_LINE`), in the middle of the panel's height; the panel is that much wider |
| The warning | on the notice band's red (`0xC0A00000`), white text |

**Clear of the top left:** `avoid` is the top-left panel that was drawn (F3's, or the coordinates'). If the whole corner would overlap it, the whole corner starts a margin below it. At 640 by 480 the longest date line and F3 do not meet; at 320 by 240 they do, and the corner goes under.

**The arrow** is three lines, the shaft and two head strokes 35 degrees either side of it, 0.4 of the shaft long, each a text pixel thick, white over the text's shadow, by Bresenham's line here (the Atlas's draws on its own pictures, not on a canvas). It is a unit vector on the screen, x right and y down, which `game` works out relative to where the eye looks; its tail and tip are the square's middle less and plus the vector times half the square less a text pixel (`ui_hud_corner_arrow_ends`).

## The debug overlay

F3 turns it on and off, on top of the HUD and over any kind of run, flying or walking. It lists what `ui_hud_stage_t` does: the five passes of the card and its total, from the card's own clock; the sim's last tick; and on `main`, what the HUD and copying the picture out took. Then come the coordinates, whatever the setting says, and its panel is wide enough for eight digits each way. Its last line is the eye's latitude and the sun's elevation there (plan 004 phase 02): `lat 45.00 N  sun 30.0`, `S` south of the equator and `N` at exactly 0, two decimals for the latitude and one for the sun. The longest, `lat 90.00 N  sun -66.5`, fits the panel. With no body, they are the flying eye's. Two more lines follow (plan 005 phase 7, for a world that grows slow when left alone): `mobs 70  drops 12`, the mobs in the world and the stacks on the ground as of the sim's last tick, and `uploads 300  far jobs 16`, the sections of the newest snapshot not yet on the card and the far-field columns being built on workers. Frames per second count **new pictures**, as `game_report` does, not how often `main` went round. The window is half a second, in `run/src/game/`.

Its text is half the HUD's scale, rounding up.

An item is drawn flat from its own texture, a block as one face of it.

Tested by ctest `run_ui_hud`, against a model, against golden pictures and in the real game, and the corner by ctest `run_ui_hud_corner` (`tests/run/ui/hud/corner/`).
