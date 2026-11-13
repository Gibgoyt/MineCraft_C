# bench/world/shake

**Purpose:** plan 008 phase 01: what a head shake costs, and what each direction costs. Ahmed, 2026-11-12: "only when shaking my head does frame rate tank; when walking around without my mouse/view frame rate sits nicely". The yaw (or the pitch) swings back and forth at a constant speed, at a chosen place and seed, settled and on the game's own threads with vsync on and off, and each row says where a picture's time went. `--rose` draws the cost rose: a still picture's cost in 72 yaws by 3 pitches.
**Tier:** bench: a program on its own, the Root of its own memory, as `bench/world` is.
**Owns:** one heap region per row, for an `app_t`, the row's samples, a column's working memory, the picture, its PNG and the rose's plot, released before that row's process exits.
**Lifetime:** Process. Each row has a process of its own.
**Threads:** the parent, with no threads and no card. In a row's process: main and the job workers, and the sim and gpu threads on a live row.
**Public functions:** none; a program, `bench_world_shake`.
**May be called by:** a person, through the build lock, with Ahmed's game closed (`pgrep -af 'build/release/bin/minecraft_c'` prints nothing).
**Depends on:** `main/index.h` for `app_t`, `game` and `game/watch` (the rings and marks it reads), `render`, `gpu_bridge`, `world`, `terrain`, `job`, `thread`, `ui`, `texture`, `math`, `simd`, `display/headless/png`, `file`, `heap`, `time`, `log`, and the generated `tables/`.

It is its own program and not a path of `bench_world`, as that file is at the standard's 1500 lines. Its history is `history.tsv` beside this file, with columns of its own.

## Running it

On the profile preset, with the `nvidia-smi` sidecar beside it in the same wrapper call (`bench/world/gpu_watch.sh`, killed at the end):

    pgrep -af 'build/release/bin/minecraft_c'      # must print nothing: Ahmed plays on this card
    tools/test-or-build.sh cmake --build --preset profile
    tools/test-or-build.sh sh -c 'bench/world/gpu_watch.sh /tmp/mc_c_shake/gpu.csv & w=$!; timeout 900 build/profile/bin/bench_world_shake --record bench/world/shake/history.tsv --label "008 phase 01: before"; s=$?; kill $w; wait $w; exit $s'
    tools/test-or-build.sh timeout 900 build/profile/bin/bench_world_shake --rose --at -80 134 418 --seed <his seed> --render-distance 64 --shots /tmp/mc_c_shake

## The rows

`--list` prints them; `--only =a,b,c` runs just those. Each is `<place>/<name>`: `jagged_peaks` (the nearest jagged peaks to the world's middle, 12 blocks over the highest ground near it, as `bench_world` finds it) or, with `--at X Y Z`, `spot`, whose eye is 1.62 over the block X Y Z, as F3 shows the feet's block.

| Row | Speed | How |
|---|---|---|
| `shake-0`, `-90`, `-360`, `-720`, `-1440` | 0 to 1440°/s | settled: frame k at k/60 s, 30 warm and 240 measured, yaw a triangle wave ±60° about `--yaw`, pitch -10° |
| `shake-720-pitch` | 720°/s | settled: pitch ±20° about -10°, yaw fixed |
| `shake-0-live-capped`, `shake-720-live-capped` | 0, 720°/s | the game's threads for `--seconds` (10), main at 120 Hz, vsync on: a picture starts only once main took the last |
| `shake-720-live` | 720°/s | the same with vsync off: the gpu thread draws back to back |
| each with `-r64` | | at render distance 64 |

Settled, the intervals are the card's time on each picture, as there is no clock to show it by, and P is the host's time on a draw past the card's. **Settled, the card's clock speed is read before every measured picture** (outside its passes' times), and each row says its card time in millions of the SM's cycles (`Mcycles`, ms x MHz / 1000): on Ahmed's laptop the speed moves between about 60 and 1750 MHz from second to second, so ms alone compare clocks, not work. Live, they are main's takes off the game's own rings (`run/src/game/watch/`): the interval between pictures main took, late ones, the card's time, P (the gpu thread's own work on a picture), the gpu thread's wait for main to take the last, shade's five kernels, the fresh AO count and the card's MHz from the `clock64` kernel once a second. A live row also prints its trend by sixths: card ms, P ms and MHz.

**Plan 008 phase 02.** A live row says main's next take before each frame (`game_loop_next_take`, the tick it sleeps to), as a window does, so the gpu thread starts pictures by the start rule (`run/src/game/pace/`); `--pace-off` says none, which keeps the rule from before, for a before and after in one run. "Late" is then a picture ready after the take the rule aimed it at, as in the frame log, and each live row prints `pace:` the card's idle time before a picture the rule started at once (after a late one, or back to back): the phase's target is under 0.5 ms.

**Plan 008 phase 03.** `--serial-frames` finishes each picture before the next is begun, as before that phase, for a before and after. Each live row prints `overlap:` the row's time over every picture drawn, taken or not (back to back, the card's time between pictures), the readback's time a picture, the share of pictures begun with the one before still on its way, camera to screen p50 (a picture's start, where its camera is read, to main's take) and the gpu thread's own part (start to all queued). With a picture on its way the row's P is no longer the gpu thread's own work: a picture's start to ready then holds the end of the one before.

**Plan 008 phase 04.** While the camera moves, a newly seen pixel on a whole cube takes its AO from the world-space AO cache or the corner estimate, with no rays (`cuda/src/shade/README.md`, "AO"). `--features-off 80000` turns that off (four rays, as before that phase), and `--fresh-ao-rays 1` sends one ray with no first guess: Q2's choice B, for comparison. The fresh count is still the pixels on the fresh list, whatever fills them.

## The rose

`--rose`: at the place, settled, 72 yaws (every 5°, 0 looking east, 90 north) by 3 pitches (-20°, 0°, +15°), each drawn after 8 warm frames there, the last with the card's clock read first. The plot and the worst direction go by **ms at 600 MHz** (`card_ms_at_600_mhz`, from each direction's cycles), the master's capped card: a first rose drawn in plain ms drew the clock's steps between 148 and 400 MHz, not the directions. Into `--shots` (or `.`): `rose-<place>-r<distance>.csv` with every kernel, `rose-<place>-r<distance>.png` (a polar plot, north up: blue -20°, green 0°, orange +15°, grey circles at one, two and three slots of 8.33 ms), and `rose-<place>-r<distance>-worst.png`, the dearest direction's picture.

## `gpu_watch.sh`

`bench/world/gpu_watch.sh <file.csv>` writes `nvidia-smi`'s clocks, power draw, enforced power limit, temperature, use and throttle reasons once a second to `<file.csv>`, and `sensors` to `<file.csv>.sensors`, until killed. It changes nothing.

## Results

See the plan 008 phase 01 file's results table, which these rows filled.
