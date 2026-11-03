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

**May be called by:** `run/src/game/session/`.
**Depends on:** `settings/persist` for the values, ranges and names of effects, `ui/widget`, `ui/compose`, `assert`.

## How it works

It changes the values it is given and nothing else. It does not know what a render distance does, or that a file exists. What runs it, `run/src/game/session/`, hands the values on to `game_reconfigure` and to `settings_save`: see `run/src/settings/README.md`.

Fits 320 by 240 at any scale, like the menus, with one column of nine rows: six sliders, VSync beside Effects..., a row for what went wrong, and Done. The sixteen effects are a second page, two columns of eight, each with a box. Esc goes back a page, and from the main page leaves, like Done.

A slider takes a drag, or a step for each detent of the wheel over it, so that every value from 2 to 512 can be reached.

A label says what takes more than this screen: `(new world)` on full detail past what the open world's table holds, and `(next start)` on a worker count other than what is running. Those are saved all the same.
