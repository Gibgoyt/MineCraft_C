# run/src/game/settle

**Purpose:** `game_settle`: on one thread, runs the sim's steps and the uploads until everything the eye's place wants is on the card, so that a run under `--headless` gives the same picture whatever the clock says. Not in the plan's list: `game/index.c` passed the standard's 1500 lines for splitting again as the mobs met it, and this goes only through public calls.
**Tier:** Arena by depth, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept here.
**Threads:** the one thread of a run with no threads. Never with `game_loop_start` going.
**Public functions** (declared with the rest of `game`, in `run/include/game/index.h`): `game_settle`.
**May be called by:** `run/src/game/` and its callers.
**Depends on:** `game`'s header, `render`, `world`, `log`, `time`.
