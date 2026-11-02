# run/src/game

**Purpose:** connects the sim, gpu and main threads: streams the world around the eye, uploads it, draws it and shows it. Later stages add their tick and frame calls here.
**Tier:** Owner.
**Owns:** `owned_region`, with `arena_world`, `arena_tick` and `arena_frame` over it. The first holds the column jobs and their scratch, reset at `game_close`. The other two are the Tick and Frame arenas of `plans/CODING_STANDARD.md` section 4, reset at those boundaries, and nothing takes from them until Stages 11 to 15.
**Lifetime:** `game_open` to `game_close`: World. `game_t` itself is Process, under `app_t`.
**Threads:** `main` calls everything but what runs on the two threads it starts, `sim` and `gpu`, or does all of it itself with no threads.
**Public functions** (`run/include/game/index.h`):

- `game_init`, `game_deinit`: remember what it runs on, taking nothing; stop everything. Deinit is safe on a zeroed `game_t`.
- `game_open`, `game_close`: a world, its blocks on the card, and its pictures.
- `game_sim_step`, `game_fly`, `game_settle`: a step of streaming, a step of flying, and running the sim and upload here until the card holds all the eye's place wants.
- `game_headless_scene`: what `display_headless_run` drives. One thread, no clock, and one script gives one set of pictures.
- `game_loop_start`, `game_loop_frame`, `game_loop_stop`: the real threads, and a frame on the main thread.
- `game_loop_source_begin`, `game_loop_source`, `game_loop_discard`: what `display_frames_run` calls: the threads start on its first frame, then real keys and a real mouse to `game_loop_frame`, and Esc to stop.
- `game_fly_from_script`, `game_fly_from_input`: either kind of input to a step of flying.
- `game_report`: what the frames took.

**May be called by:** `run/src/main/`.
**Depends on:** `render`, `world`, `terrain`, `display`, `input`, `texture`, `job`, `thread`, `time`, `heap`, `arena`, `log`, `assert`.

A column's blocks are `terrain_generate_box`, on a worker, over all of the column's storage. Each job lends it the states and a byte per voxel.

## The threads

| Thread | Does | Tells the others through |
|---|---|---|
| `main` | Input, moves the eye, shows the newest picture | A triple buffer of cameras to `gpu`; two relaxed atomics, the eye's chunk, to `sim` |
| `sim` | Columns to and from workers, into the map, their light and every edit's, then a snapshot. A tick at 20 Hz, with nothing in it yet | The snapshots of `world/edit` to `gpu` |
| `gpu` | The upload queue, then the frame's kernels | A triple buffer of pictures to `main` |

None of them ever waits for another, as the plan wants: a slow tick cannot stall a frame, and a slow frame cannot stall input.

A sim step spends at most `GAME_COMMIT_BUDGET_NS` on finished columns, since each gets its light there. The rest wait for the next step, so snapshots keep going out.

With no threads, as under `--headless`, `game_settle` does a sim step and an upload round in turn until all of it is on the card, before each frame. So a picture does not depend on how fast a worker was, and the same script always gives the same bytes. It is the same code as the threads run, in another order.

`tests/run/render/frame/` runs the threads for real, on a plain buffer for a display, and `tests/run/game/` through `display_frames_run` on a fake kernel with a real guardian forked. Only Ahmed can run them on KMS.

## The guardian's fork

Under KMS, `display/vt_guard` forks its guardian as the console is taken, with CUDA live and the pictures already pinned. So:

- The sim and gpu threads start on the first frame, after that fork, so nothing of theirs is mid-write as it happens. The job workers are already running then, with nothing to do.
- The guardian touches nothing of CUDA's and leaves by `_exit`, so no exit handler of the runtime's runs in it.
- A pinned page must stay the parent's across a fork, or the card would go on writing to a page the program no longer sees. The kernel keeps it so for pins taken with `FOLL_PIN`. Whether this driver's are was measured, not assumed: `a_world_runs_through_the_display_loop_with_a_real_guardian_forked` draws through pictures pinned before the fork, with the guardian alive, and compares with a picture made before it.

## Why `--headless` alone still shows the demo

Plain `--headless` stays what `tests/run/display/headless/` tests, with no world and no card work: `--headless --test-world` is this. The line in `run/src/display/headless/README.md` that said `game` would replace the demo was written before there was a reason to keep a run that needs no card.
