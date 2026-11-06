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

### 2026-11-06, plan 004 phase 07: 226.5 ms, met

Profile preset, through the build lock, 16 workers, seed `4D494E4543524146`, 196 kept tiles in 49 blocks. Exit status 0.

| | |
|---|---|
| First census (climate tiles built, every cell from its spin-up) | 1101.7 ms: prepare 634, blocks 460 |
| Censuses of weeks 13 to 17 | 398.2, 226.5, 220.9, 244.4, 174.5 ms; median **226.5 ms** (target 250) |
| What the jobs spent a week, summed over the workers | day lists 0.8 to 1.1 s, normals and wind 0.47 s, noise 0.21 s, days 0.3 to 3.0 s (more in a thunder-heavy week) |
| Flow | 7 ms; the outlook's input is filled beside the last week's blocks, so its stage is 0.3 ms |

Before the changes in the phase 07 file's Issues found 15, the same run gave a median of 387.3 ms (MISSED) and a first census of 4.6 s.

### 2026-11-06, the gate's reruns: 251 to 262 ms, at the target, not under it

The full profile build, run four more times while the machine's load average was 51 (other agents' work): medians 261.6, 251.2, 253.6 and 256.1 ms (all MISSED), first censuses 1.10 to 1.15 s. So the census is at the 250 ms budget, not safely under it: the 226.5 above was a quieter moment. The blocks' jobs are 49 on 16 workers, so the last round runs one job alone; splitting the work by day as well as by block is the next step if a quiet machine also misses (Risks R1).
