# 001-ahmed-bohemoth-minecraft-from-scratch — phase 17, stage 2: Benchmark worlds

**Status:** done   (updated 2026-11-03)

## Goal

Standard benchmark worlds and paths exist, with a results history in `bench/`.

**Wave:** W10. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-17-performance-hardening.md`.

**Owned paths** (edit only these, plus this file):

- `bench/world/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Define standard worlds and camera paths in `bench/world/index.c`
- [x] Record results history under `bench/world/`
- [x] Add `bench/world/README.md`

## Verification

    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh timeout 600 build/profile/bin/bench_world
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `9d86acb` Add bench_world, with standard worlds and camera paths run on the whole game with no display, and a results history in bench/world.

## What was done

- `bench/world/index.c`: 6 standard worlds, 3 paths, 14 rows, settled or live, at 1920 by 1080 on the default seed. It runs the whole game with no display, each row in its own process, and keeps under 3584 MiB of the card. Flags: `--only`, `--record`, `--label`, `--shots`, `--trace`, `--list`.
- `bench/world/history.tsv`: the results history, a line per row per recorded run, with the first run in it. `bench/world/README.md` has the worlds, the paths, how to read the numbers, and those results as tables.
- Nothing outside `bench/world/` is wired in: `cmake/targets.cmake` finds benchmarks by path. `plans/REPO_STRUCTURE.md` names the history.

**The result:** 5 of the 12 rows at render distance 32 meet 120 fps, and 7 miss. The slowest is `jungle/orbit`, at 34 to 37 fps, of which shade is 24 to 26 ms. The tick at 100 mobs in the real game is under 5 ms in some runs and over it in others: see issue 1. Stage 17.3 has the numbers to work from, and these are not the final numbers for the target table.

Checked:

- `bench_world` on the profile preset, exactly as below: exit status 0, 512.2 s, under its limit of 600 s. Again with `--record`: exit status 0, 496.6 s.
- The 9 settled rows' picture hashes were the same in all 4 full runs, one of them with every row in one process and 3 with a process per row. Heap calls while timed were 0 in every row of those runs.
- What each row looks at, from `--shots`: looked at for `jungle/orbit`, `jagged_peaks/orbit`, `deep_ocean/orbit`, `deep_ocean/flight-live`, `cave/orbit`, `night/walk-mobs` and `plains/orbit-r512`. The others were not looked at, past the row's own check that a picture has things in it.
- `--trace` with `--only night/walk` on the final build: exit status 0, a trace of 78628 events that parses as JSON, and the table on stdout. A release build refuses it. The benchmark builds on debug, release and profile, but was run only on profile.
- A row's process killed with `kill -9`: the row says FAILED, the parent exits 1, and nothing is left on the card by `nvidia-smi --query-compute-apps`.
- The card: 2498 MiB at most, from `nvidia-smi --query-compute-apps` at 0.5 Hz over a whole run and the reruns after it.

## Issues found

**1. What runs on the main thread comes out in two groups from one run to the next, with no cause found.** Expected: with the same pictures, the same `tick` and `past the card` within some noise. Actual, on `night/walk-mobs`, 9 runs with picture `FE63E7196A02774C`: 3 at a tick mean of 0.74 to 0.81, p99 3.5 to 4.9, 2.0 to 2.2 ms past the card, and 6 at 2.0 to 2.6, p99 8.8 to 13.4, 7.0 to 7.8 ms. Those 6 are all 4 full runs and 2 run alone with `--only`, and the card's clock hardly moved between the groups. So the target of 5 ms is met or missed depending on the run. `plains/flight-live` may be the same thing: 3112 to 3752 columns in the 3 runs where its tick mean was about 0.8 ms, and 1182 and 1187 in the 2 where it was 1.6.

I first took it for something one world leaves behind for the next in a process, as a row alone was quiet and in a full run was slow. So I gave each row a process. A full run stayed slow, and then a row alone gave 2.21, 2.11 and 0.79 within two minutes. So that was wrong. Each row keeps its process, as a row that dies then costs only that row. `top -H` during one quiet run showed nothing but this program's threads and a browser's `ServiceWorker` at 69%. It was not looked at during a slow one, so that says little. Not fixed: the cause is not known, and could be the machine as much as the game. Stage 17.3 should take the tick from several runs and find out. The README says so.

**2. The first orbit in `jagged_peaks` looked at a wall.** Expected: long views of steep rock. Actual: `--shots` showed stone filling the picture, from 12 blocks over its own column with higher ground beside it, which is a fast number that says little (172 fps). The check for a picture with things in it passed, as a stone texture has plenty of colours. Fixed: an orbit stands clear of the highest ground on 3 rings out to 36 blocks, as a flight already did. It then came to 160 to 166 fps with rock near and far, so the wall had not moved the number much. All biome orbits moved up with it, as `jungle/orbit` did from 39 to 37 fps.

**3. The first walk stopped being a test of mobs.** Expected: a player chased for the whole row. Actual, from the game's report: `died 1, came back 0`, and the player ended 88 blocks below where it started, under the spawn's mobs. Healing between ticks did not stop what killed it, and a dead player is no target, so the row was timing idle minds. Fixed: nothing can hurt it now, a death makes the row not sound, and it walks at the `plains` place. It then took 37 blows and 5 blasts with 12 zombies dead, against 3 blows in the row where it died.

**4. The first full run left too little room under its limit of 600 s.** Actual: 554.8 s, of which `plains/orbit-r128` and the two other far rows spent 106 to 122 s each on opening. Fixed: that row is gone, as `plains/orbit-r512` draws further and `plains/flight-live-r128` still covers render distance 128. A run now takes about 500 s, with a process per row included. Not much margin: a slower machine will hit `timeout 600`, and rows already printed stay printed, but `--record` writes only at the end.

**Found, and left for Stage 17.3, as none is this stage's to change:**

- Shade is most of every slow row: 7 to 8 ms on `plains/orbit`, 24 to 26 ms on `jungle/orbit`, 10 to 11 ms on `deep_ocean/orbit`.
- Filling a far field takes 106 to 128 s to open at render distance 128 or 512, against 5 to 6 s with none. `cave/orbit` opens in 12 to 21 s, for a reason not looked at.
- At render distance 128 and 512 the primary pass goes from about 0.6 ms to 2 to 3 ms.
- Readback is a flat 1.28 ms of every picture.
- 482 to 825 MiB of voxel pool in use at render distance 32, against the plan's rough 100 MiB. Inside the budget, but the estimate was out by 5 to 8 times.

**Limits to know about, by design:**

- It has only ever run on the shared machine, with the desktop on the same card, and on the profile preset. Numbers for the final table want an idle machine.
- A live flight clears the highest ground on 1000 blocks of its way, so over steep ground it flies well above most of it, and `jagged_peaks/flight-live` and `deep_ocean/flight-live` see much sky. They are about streaming: the orbits are what tests the picture.
- The `tick` on a walk includes `game_settle`, which the game's headless tick calls before the sim's. It finds nothing to do, as the draw before it settled, but it is not only the sim.
