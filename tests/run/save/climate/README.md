# tests/run/save/climate

**Purpose:** ctest `run_save_climate`. Tests `run/src/save/climate/` (plan 004 phase 07) with the census of `run/src/game/census/` on a synthetic sloped coast: 50 tiles of random valid state written (out of order) and read back with the same digest, every padding byte zero, and written again to the same bytes; a bit flipped at every byte of a two-tile file, another seed, another version with good hashes, soil of 151 and a NaN with good hashes, each refused with the store empty and the file unchanged on disk, and the census then starting from spin-up with its screen told; a world closed mid-week and reopened in a fresh climate giving the same file when saved again and the same bytes at the next census as an uninterrupted run; a crash after `week.13` (state at 12) rerunning week 13 to the same bytes, and a crash before it (state at 13, only `week.12`) owing week 13 by itself, the same bytes again; a world with no `climate/` directory opening at its state file's week, every tile fresh, and one cell of the first census equal byte for byte to its spin-up plus seven `climate_weather_day`s by hand.
**Tier:** test.
**Owns:** nothing outside the test arena, and a directory of its own under `/tmp` for each test, removed at its end.
**Lifetime:** One run.
**Threads:** main, and the job workers of the census and the climate.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/save/climate/`, `run/src/game/census/`, `run/src/climate/` and its parts, `file`, `terrain` (the hash streams), `tests/run/game/census/fixtures/weather.h`. No graphics card.
