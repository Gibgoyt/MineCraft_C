# run/src/game/fill

**Purpose:** the fill-in report of plan 005 phase 14: how long the world takes to be drawn around the eye after a world opens, and after each change of render distance or full detail. One log line per ring of chunks as it is first drawn, one for the whole fill, and in the end-of-session report one per fill.
**Tier:** Arena by depth (2). It allocates nothing.
**Owns:** nothing. `game_fill_t` is a part of the game's World arena beside the column jobs (`game_fill_of`, in `game/index.c`), as `game_t` is at its size and `app_t` at main's frame limit.
**Lifetime:** World. `game_fill_reset` empties it as a world opens.
**Threads:** `game_fill_step` is the gpu thread's, after each round of uploads and far-field work, or main's in `game_settle` with no threads. `game_fill_start` runs with neither thread going: in `game_open`, and in `game_reconfigure` between the stop and the start. The five times at the top are atomics, written with release once each per fill, so a test or a bench on main can watch them while the threads run.
**Public functions** (`run/include/game/fill/index.h`):

- `game_fill_reset`, `game_fill_start`: nothing going on; a fill from now, of a kind (`GAME_FILL_OPEN`, `GAME_FILL_CHANGE`). A fill cut short by the next is kept as it got, with `done` never.
- `game_fill_step`: the work, and the log lines.
- `game_fill_summary`: the fill going now, as far as it has got.
- `game_fill_report`: the end-of-session lines, from `game_report`, with how many sim steps a column waited for its climate tiles (`climate_waits`, which the sim adds to in `game_columns_request`).
- `game_fill_of`: the game's.

**May be called by:** `game` (`game/index.c`, `game/settle`, `game/report`), tests, `bench/world`.
**Depends on:** `render` (the host's copy of the column table, `render_upload_idle`, the far field's `stats.columns_built` and `render_lod_idle`), `log`, `time`, glibc's libm.

## What it measures

Ring `r` is every column with `(r - 1)^2 < dx^2 + dy^2 <= r^2` from the eye's column, the centre of the table's window, out to full detail: the map loads a circle, so these are the circle's rings. A ring is **drawn** when every one of its columns is `ready` in the host's copy of the table and the upload queue is between views (`render_upload_idle`), which is when every section of the view that put them there has gone to the card. The table is looked at no more than once every 10 ms, and only while a fill goes on. The far field is **done** when `render_lod_idle` says nothing is wanted that is not there.

| Time | Means |
|---|---|
| first change | the first column come or gone in the table, or the first far column built, since the start: the change starting to show |
| near rings | rings 0 to 4, the targets' "ground near you" |
| all rings | every ring out to full detail |
| far field | the far field done, or 0 with none |
| all | both |

All are from the start: `game_open` for an opening, which leaves out what the menu did before, and the end of `game_reconfigure`'s change, so after the threads stopped. Every one is wall time, also under `--headless`, where it is only in the log and changes no picture.

The lines look like:

    fill: ring 4 of 32, 28 columns, drawn at 0.41 s
    fill: after opening at render distance 32, 32 block by block: the first change drawn at 0.12 s, the 5 nearest rings at 0.41 s, all 33 at 3.20 s, the far field (0 columns) at 0.00 s, all of it at 3.20 s
    fill: 3 fills finished, the last 3 follow; -1.00 s is never
    fill: finished a change to render distance 64, 32 block by block: first change 0.30 s, near rings 0.00 s, ...

`tests/run/render/distance_live/` holds the game to these, and `bench/world` keeps them in its history.
