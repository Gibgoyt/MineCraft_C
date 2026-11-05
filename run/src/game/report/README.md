# run/src/game/report

**Purpose:** what `game` says about itself: what the HUD and the debug overlay show now, and the log's lines for the end of a run. Not in the plan's list: `game` reached its size for splitting with Stage 14.5, and these only read.
**Tier:** Arena by depth, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept.
**Threads:** `main`.
**Public functions** (declared with the rest of `game`, in `run/include/game/index.h`):

- `game_hud_state_read`, `game_hud_debug_read`: what the HUD and the overlay show now, from what the other threads last said.
- `game_hud_block_read`: the whole block the body's feet are in, from what the sim last said, or the flying eye's.
- `game_report`: what the frames took, and the books of what was dropped.
- `game_calendar_date`, `game_calendar_south`, `game_calendar_line`: main's world clock as a date, which half of the world the eye is in (y below 0 is south, until plan 004 phase 02), and the HUD's date line.

**May be called by:** `run/src/game/` and its callers.
**Depends on:** `game`'s header, `log`, `render/entities`, `tables/blocks`, `time`.
