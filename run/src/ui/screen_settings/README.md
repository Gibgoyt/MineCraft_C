# run/src/ui/screen_settings

**Purpose:** the Settings screen: a slider or a box for every setting in `run/src/settings/`, over the world from a pause, or on its own from the title screen.
**Tier:** Arena by depth, and it allocates nothing: labels are stack locals, and the widgets draw on the frame they are given.
**Owns:** nothing. Its `ui_screen_settings_t` is a field of `ui_t`.
**Lifetime:** From `ui_screen_settings_open` until a person leaves it.
**Threads:** `main`.
**Public functions** (`run/include/ui/screen_settings/index.h`):

- `ui_screen_settings_open`: to the main page, with these values.
- `ui_screen_settings_message_set`: says what went wrong with keeping them.
- `ui_screen_settings_frame`: one frame, which says whether a value changed and whether the person has finished.
- `ui_screen_settings_distance_label`: a distance slider's label, in chunks and blocks (plan 005 phase 8).

**May be called by:** `run/src/game/session/`, and `run/src/ui/`, whose header holds its state.
**Depends on:** `settings/persist` for the values, ranges and names of effects, `ui/widget`, `ui/compose`, `assert`.

## How it works

It changes the values it is given and nothing else. It does not know what a render distance does, or that a file exists. What runs it, `run/src/game/session/`, hands the values on to `game_reconfigure` and to `settings_save`: see `run/src/settings/README.md`.

Fits 320 by 240 at any scale, like the menus, with one column of nine rows: six sliders, VSync and HUD... beside Effects..., the sound volume's slider, and Done. HUD... (plan 004 phase 10, in the place the Coordinates box had) opens the HUD page, titled "HUD": a 200-wide box a row for what the HUD's corners show, Coordinates (row 0), Date (row 1, the date line of plan 004 phase 01), Temperature and wind (row 2) and Storm warnings (row 3), the last two each a bit of `climate_hud`, and then Pause when away (row 4, plan 005 phase 7's `pause_on_focus_loss`, here because this page had the room); Back is in Done's place, row 8. At 640 by 480 (scale 2, the screen's top at 0) the click points are HUD... (300, 356), and on the HUD page (320, 68), (320, 116), (320, 164), (320, 212), (320, 260) and Back (320, 452). Ids 112 to 115 are HUD..., Temperature and wind, Storm warnings and Pause when away; Coordinates and Date keep 109 and 111 on the new page. What went wrong with keeping them takes the title's place. The first seventeen effects (`SETTINGS_EFFECT_ON_SCREEN`) are a second page, two columns of eight, each with a box, with the last on the row of Back, to its left. The eighteenth, plan 004 phase 08's weather, has no room there yet: it is kept in the file, and `--no-weather` turns it off for a run. Esc goes back a page (from the effects page or the HUD page to the main page), and from the main page leaves, like Done.

A slider takes a drag, or a step for each detent of the wheel over it, so that every value from 2 to 512 can be reached.

The two distance sliders say how far in blocks as well as chunks, a chunk being `MC_WORLD_CHUNK_SIZE` (16) blocks wide: "Render distance: 32 (512 blocks)", "Full detail: 32 (512 blocks)" (plan 005 phase 8; with the word "chunks" too the label is wider than the track). While the pointer is over one, a tooltip says in one line what it means and that the first number is chunks: render distance is how far you see, full detail how far it is drawn block by block, with coarser land past it. The lines are `UI_SCREEN_SETTINGS_EXPLAIN_*` in the header; every label and both lines fit at scale 1.

A label says what takes more than this screen: `(new world)` on full detail past what the open world's table holds, and `(next start)` on a worker count other than what is running. Those are saved all the same.
