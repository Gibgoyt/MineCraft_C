# run/src/ui/menu

**Purpose:** the screens with no game in them: the main menu, the list of worlds, making a world, and the pause screen over a running one.
**Tier:** Arena: its two textures and the room for the list come from an arena `ui` gives it.
**Owns:** nothing. The list is `UI_MENU_WORLDS_MAX` worlds from `ui`'s `arena_tables`.
**Lifetime:** Process.
**Threads:** `main`, like the rest of `ui`.
**Public functions** (`run/include/ui/menu/index.h`):

- `ui_menu_init`, `ui_menu_deinit`: read the textures and take the room; empty the handle. `ui_init` and `ui_deinit` call these.
- `ui_menu_saves_set`: where worlds live, and what a blank seed means.
- `ui_menu_settings_enable`: gives the Settings button something to ask for.
- `ui_menu_open`: go to a screen.
- `ui_menu_frame`: one frame of it, and what it wants done.

**May be called by:** `ui`, and `game/session`, which is what runs it.
**Depends on:** `arena`, `assert`, `texture`, `file` (only its status names), and the sibling parts `ui/compose` and `ui/widget`, and its own part `catalog`.

**Parts:** `catalog`: worlds on disk, and what a name and a seed turn into.

## The screens

| Screen | What is on it | Esc |
|---|---|---|
| Main | The logo, Singleplayer, Settings, Quit Game | Nothing |
| Select | The worlds with their seeds, Play Selected (or Enter), Create New World, Back. The wheel scrolls | To Main |
| Create | World Name, Seed, Create (or Enter), Cancel. Typing goes to the name from the start | To Select |
| Pause | Over the world's last picture, shaded: Resume, Settings, Quit to Title | Resume |
| Loading | Says which world, and does nothing | Nothing |

Moving between Main, Select and Create is this module's own. What it cannot do itself comes back from `ui_menu_frame` as a `ui_menu_action_t`, with the world to open in `chosen` for `UI_MENU_ACTION_PLAY`. Who opens what, and when, is `run/src/game/session/`.

A new world opens as soon as it is made. The name's field takes 31 characters, which is what fits a row of the list at any scale. A world with no name cannot be made, and Enter then leaves the typing where it was.

Nothing here logs, because `ui_menu_frame` runs each frame inside `display_frames_run`, where no one can read a log. What goes wrong is said on the screen, in `message`: a saves directory that cannot be read, worlds that could not be listed, a world that could not be made.

## What is not here yet

- **Settings** is a disabled button until Stage 14.5's screen calls `ui_menu_settings_enable` and answers `UI_MENU_ACTION_SETTINGS`.
- **A world's blocks and player are not saved.** `world.meta` keeps its name and seed, so choosing it again gives the same terrain as it was generated, without anything that was changed. Stage 16 saves those, in the directory `chosen.directory` names. Until then the pause screen says Quit to Title, and not Save.
- **Worlds cannot be deleted from here.** The plan does not ask for it, and it is the one action here that would destroy anything.

## Sizes

All of it fits 320 by 240 at scale 1, and is in the same proportions at any larger scale: `ui_widget_scale_for` picks how many screen pixels to a texture pixel. The numbers are the enum at the top of `index.c`.

Tested by ctest `run_ui_menu`, on made-up input frames, in directories of its own.
