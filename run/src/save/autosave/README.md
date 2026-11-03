# run/src/save/autosave

**Purpose:** a save of everything, every 30 seconds of world time, with no hitch: the sim copies out in a fraction of a millisecond, and a worker does the disk.
**Tier:** Arena, by depth; it allocates nothing.
**Owns:** nothing.
**Lifetime:** Started by `game_open`; stopped by `save_autosave_finish`, or by `save_close`.
**Threads:** the sim, or whichever one thread holds the map, for everything but the write. One job on a worker, handed over with `writing`, for the write.
**Public functions** (`run/include/save/autosave/index.h`):

- `save_autosave_start`, `save_autosave_stop`: a pair. Stop waits for a write under way.
- `save_autosave_step`: from each sim step. Never waits.
- `save_autosave_request`: a column cannot leave until there is room for it.
- `save_autosave_finish`: all of it now, as a world closes.
- `save_autosave_interval_set`: a test's way in.
- `save_autosave_resume`: goes on after a `save_autosave_finish` that failed, with the world staying open.
- `save_autosave_trouble`: what the disk said to the last save, for a person to read, or nullptr.

**May be called by:** `save`, and `game`.
**Depends on:** `save/world`, `save/state`, `job`, `time`, `log`, `assert`.

## One save

On the sim, at the end of a step, so between ticks and never in one:

1. `save_world_capture_dirty` copies the changed sections, and takes their flags.
2. `save_state_capture` copies everything else to the image.
3. `save_world_commit_begin` turns what was captured into the set to write. A new one is ready at once, and a column leaving goes into that one.
4. A job is handed over.

On a worker: `save_state_write`, then `save_world_commit_write`. See `run/src/save/state/README.md` for why that order makes one save of the two.

Until the job has finished the sim starts no other and leaves the image and the set being written alone. If the disk is slower than the interval the next save starts when it can, so a kill can lose an interval and the write under way, and no more: that is the gate, and `tests/run/save/state/` kills children to show it.

## When the disk says no

The game goes on, it says so once, and it keeps what it has: the set that failed is held, and its readers still see it. The same set and the same image go again at the next interval, so that they stay one moment. Nothing is written more often than that while the disk is refusing, even if something has asked.

A log is nothing a person at a console can read, so `save_autosave_trouble` says it too: the HUD has it in a band across the top while it lasts, and the pause screen says it. As a world closes, `save_autosave_finish` fails the same way, and `game/session` keeps the world open: see `run/src/game/session/README.md`. Whatever had got as far as a temporary file is removed, so a failed save gives back the room it took, and once a person has made room the next interval takes all of it. `tests/run/failure/` does this on a disk that is really full.

## Only one writer

`save_world_commit_write` runs in one place at a time, and while a game runs only here: `game_column_leaving_save` asks with `save_autosave_request` when a column will not fit, and the column stays until there is room. With more dirty sections than one set holds, which is 64 MiB or 32768 sections, a single save holds part of what changed by then, and the rest goes in the save that follows straight away. Only that case can mix two moments, by less than the time between those two.

## What it costs

Measured by `tests/run/save/state/` and `tests/run/save/state/game/`, on a debug build, and in Stage 16.2's Results.
