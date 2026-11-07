# run/src/game/atlas

**Purpose:** the game's side of the Atlas (plan 004 phase 09), kept out of `run/src/game/index.c`, which is at its split
line: opening the Atlas's store as a world opens (with `<world>/atlas` beside `<world>/blocks` when the world has files,
else nothing on disk, as `--test-world` runs), closing it as the world closes, what the world tells the Atlas each frame
(the eye's place and yaw, the systems' time `t` from the clock, the world time, the census week and the day of the year),
its keys, and drawing it in place of the HUD with the F3 overlay over it.
**Tier:** Arena by depth; it allocates nothing. The Atlas's memory is ui's (`run/src/ui/index.c`).
**Owns:** nothing.
**Lifetime:** a world's: `game_atlas_open` from `game_open`, `game_atlas_close` from `game_close`.
**Threads:** main's, but for nothing: the column job's top and the sim's write and save step are called from
`run/src/game/index.c` straight into `ui/atlas`.

**Public functions:** `game_atlas`, `game_atlas_open`, `game_atlas_close`, `game_atlas_world`, `game_atlas_input`,
`game_atlas_up`, `game_atlas_compose`.

**May be called by:** `run/src/game/` (`game_open`, `game_close`, `game_screen_input`, `game_screen_up`,
`game_hud_compose`), `run/src/game/session/` (the census screen closes the Atlas), tests.
**Depends on:** `game`, `ui` and `ui/atlas`, `calendar`, `climate/weather` (`climate_weather_time_of_clock`), `save`,
`file`, `log`.
