# bench/climate/census

**Purpose:** plan 004 phase 07's census cost: 100 km² of generated land (10 by 10 tiles round the world's middle, 196 kept with the 2-tile drainage margin) on the default seed (`TERRAIN_SEED_DEFAULT`), the census of `run/src/game/census/` on all workers, no file. The first census builds the climate tiles round the land and starts every kept cell from its spin-up; then 5 censuses of one week each are timed, from the trigger to idle, and the median is the result. Each line also says what the blocks' jobs spent building day lists of weather systems and on everything else (summed over the workers).
**Tier:** bench: a program on its own, the Root of its own memory (one region for its parts and the census's arena).
**Owns:** that region.
**Lifetime:** One run.
**Threads:** main and all the workers.
**Public functions:** none; a program.
**May be called by:** a person, through the build lock.
**Depends on:** `climate` and `climate/weather`, `game/census`, `terrain`, `job`, `thread`, `heap`, `time`.

Run it on the profile preset, as the phase file's Verification does:

    tools/test-or-build.sh cmake --preset profile
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_climate_census

Exit status 0 if the median is under 250 ms (the phase 07 file's target), 1 if not (printed as `MISSED`, and recorded, not hidden), 2 if it could not start. Results are kept in `history.tsv` beside this file.

## Results
