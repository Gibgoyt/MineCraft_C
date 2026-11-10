# run/src/game/settle

**Purpose:** `game_settle`: on one thread, runs the sim's steps and the uploads until everything the eye's place wants is on the card, so that a run under `--headless` gives the same picture whatever the clock says. Not in the plan's list: `game/index.c` passed the standard's 1500 lines for splitting again as the mobs met it, and this goes only through public calls.
**Tier:** Arena by depth, and it allocates nothing.
**Owns:** nothing.
**Lifetime:** Nothing is kept here.
**Threads:** the one thread of a run with no threads. Never with `game_loop_start` going.
**Public functions** (declared with the rest of `game`, in `run/include/game/index.h`): `game_settle`.
**May be called by:** `run/src/game/` and its callers.
**Depends on:** `game`'s header, `game/fill` (stepped after each round), `render`, `world`, `log`, `time`.

**A full pool ends the wait.** It sends what it refused round again and so publishes a view each time, which would otherwise never go quiet: on a peak, a short render distance waited forever. The pool is sized not to fill (`run/include/render/index.h`), and if one does, part of the world is not drawn, and the game says so.
