# tests/run/save/state/game

**Purpose:** ctest `run_save_state_game`. The part of `run_save_state` that needs the whole game: frames of the real threaded game with and without saves coming, on the card, with a player digging; that game reloaded with nothing ticking and saved again being the same bytes; a column that will not fit waiting for an autosave; what was waiting for its first tick going on after a reload; the far field showing what is saved, made again when captured, and given back when gone; the built program killed in play, then run on from what it left; the built program at the top of the world, placing on the last layer, refused the one past it, and standing above it; and the built program refused a world, which says so on the list and lets another be played.
**Tier:** test.
**Owns:** nothing outside what each test starts and stops, and a directory under `/tmp` for each, removed at its end. Other memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main and the game's threads, and the built program as a child that really dies with `SIGKILL`.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/save/`, `run/src/game/`, `run/src/ui/menu/catalog/`, the card (compute only), and the built program, which `test.cmake` names and which runs only with `--headless`, under `/usr/bin/timeout`.

The built program's pictures at the top of the world, `arrived`, `placed`, `refused` and `stood`, stay on disk for a person to read when `MC_TOP_KEEP` is set. The test prints where.
