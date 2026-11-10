# run/src/settings/persist

**Purpose:** what the settings are, their ranges and defaults, and the file they are kept in: our own runtime parser, since `compile/` and its YAML are not linked at run time.
**Tier:** Arena by depth, and it allocates nothing: the caller gives it text and a place for text.
**Owns:** nothing.
**Lifetime:** Nothing is kept.
**Threads:** any, one call at a time on a set of values.
**Public functions** (`run/include/settings/persist/index.h`):

- `settings_persist_defaults`: what a new install runs with.
- `settings_persist_clamp`: each value into its range.
- `settings_persist_effect`: the eighteen graphics effects, for the file, and the first `SETTINGS_EFFECT_ON_SCREEN` (17) for the screen.
- `settings_persist_parse`: text to values. Never fails: each problem is logged with its line.
- `settings_persist_write`: values to text.

**May be called by:** `run/src/settings/`, and `run/src/ui/screen_settings/` for the effects' names and the ranges.
**Depends on:** `log`, `assert`, `machine` for the worker and distance limits, `gpu_bridge/shade` for the effects' bits, `render/lod` for the default full detail.

## The file

    version: 1
    # A comment. So is the rest of a line after a hash.
    render_distance: 32
    full_detail: 32
    fov_degrees: 70
    sensitivity_percent: 100
    render_scale_percent: 100
    worker_count: 16
    sound_volume_percent: 70
    vsync: true
    show_coordinates: true
    show_date: true
    pause_on_focus_loss: true
    show_climate: true
    storm_warnings: true
    effect_shapes: true
    ...

It is the flat part of YAML and nothing more, so a person can read and change it, and any YAML tool agrees.

A line a file does not have leaves that setting at its default, with no warning. So a file from before `show_coordinates` was added needs no new version: it loads, and shows them. The same for `show_date`, the HUD's date line from plan 004 phase 01: a file without it shows the date. And for plan 004 phase 10's `show_climate` (the temperature and wind line) and `storm_warnings`, each `true` or `false`: two lines kept as two bits of one byte, `climate_hud` (`SETTINGS_CLIMATE_HUD_LINE`, `SETTINGS_CLIMATE_HUD_WARNINGS`), so that neither `settings_values_t` nor what holds it grew past what `app_t` has room for; a file without them has both on, and `settings_persist_clamp` clears any other bit. And for plan 005 phase 7's `pause_on_focus_loss`: a window that loses the focus opens the pause screen, and a file without it has it on, as Minecraft does. `SETTINGS_VERSION` stays 1.

- The first line that says anything says which version this is. Any other version sets nothing, since its lines may mean something else.
- Lines come in any order, and the last of two for one key wins. What is missing stays what it was.
- A number is only digits. One past its range is held to the nearest end, and said so.
- A true or false is exactly that.
- A line that makes no sense, or names nothing, or is past 120 bytes, is logged with its number and ignored, and the rest are read. No file is worth not starting for: it is a preference, and the Settings screen is how to mend it.
