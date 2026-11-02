# run/src/main

**Purpose:** the program's entry point: holds `app_t` on the stack, checks the CPU, starts each system and stops it again.
**Tier:** Root. Allocates nothing.
**Owns:** `app_t app`, which everything else in the program hangs from. So far that is `job_system_t job`, `simd_t simd`, `texture_t texture`, `world_t world`, `terrain_t terrain`, `physics_t physics`, `player_t player`, `gpu_bridge_t gpu_bridge`, `render_t render`, `game_t game`, `math_t math`, `display_t display`, `display_offscreen_t offscreen` and `input_t input`.
**Lifetime:** Process.
**Threads:** main, which it pins to its place in `config/machine.yaml`.
**Public functions:** `main`. A command line:

    minecraft_c                                   runs the program
    minecraft_c --report                          prints the compiled defines and CPU features, then exits, starting nothing
    minecraft_c --pretend-missing-cpu <key>       treats a feature as absent, to see the CPU check fail
    minecraft_c --pretend-leak                    skips the shutdown, to see the leak check at exit fail
    minecraft_c --test-pattern [--card <path>] [--connector <name>] [--crash-test[=segv|abort|kill|hang]]
                                                  shows a test pattern through KMS, from a text console only; Ctrl+Alt+Backspace
                                                  is the emergency exit; --crash-test dies on purpose after 240 frames
    minecraft_c --headless [--script <path>] [--screenshot-dir <dir>] [--ticks <n>] [--width <w>] [--height <h>]
                                                  no display: replays a script of input and writes PNG screenshots; see
                                                  run/src/display/headless/README.md
    minecraft_c --test-world [--walk] [--render-distance <chunks>] [--no-shapes] [--no-alpha]
                                                  flies through a generated world through KMS, from a text console only. WASD,
                                                  Space and Left Shift move, the mouse looks, the wheel sets the speed, Left Ctrl
                                                  goes faster, Esc ends the run, Ctrl+Alt+Backspace is the emergency exit
    minecraft_c --headless --test-world [--walk] [--render-distance <chunks>] [--script ...] [--screenshot-dir ...] [--ticks ...]
                [--width ...] [--height ...]      the same with no display: a script flies it, into PNG screenshots

`--no-shapes` draws every block a whole cube, and `--no-alpha` stops rays at see-through texels. `--no-transparency` stops them at water and part-covered texels too, `--no-water-reflection` leaves the mirror out of water, and `--no-water-tint` leaves its colour with depth out. `--no-ao` leaves out the short rays that darken corners, and `--no-taa` has each frame stand alone. `--god-rays` turns on the one effect that is off until asked for. Each effect of phase 9 is a
switch like these until `run/src/ui/screen_settings/` (Stage 14.5) has a screen for them.

`--walk` puts a body in that world, on the nearest dry land to its middle, in place of the flying camera. WASD walks, Space jumps, swims up and climbs, Left Shift sneaks, Left Ctrl sprints, the mouse looks, the left button mines and the right button places what was mined. A script drives the same keys. See `run/src/player/README.md`.

With no flag, running the program starts the job system, runs one job per worker through it, logs the heap's counts and
stops. Until the game has a menu, `--test-world` is how to get to a world.

Exit status: 0 for success, 2 for a bad command line, 3 for a CPU that lacks what the build uses, 4 for heap regions
still live after `app_deinit` or device blocks still live before it, 5 for a core runtime or a card that would not start or did not run its jobs, 6 for a display that is not ours to take (nothing on
it was changed), 7 for KMS failing partway, 8 for a bad script or a screenshot that could not be written under `--headless` (`MAIN_EXIT_*`).

Only `--test-pattern` and `--test-world` without `--headless` open the display and read input. The default run stays away from
the card and from `/dev/input`. Each starts `input` only after `display_init` said OK, so a run that is refused never looks at
an input device, and `--test-world` asks for the display before it opens a world. `--test-pattern` starts no job system. The mouse moves a cursor square, a held key or button turns it red, and Esc ends the run.
`--headless` starts the same runtime as the default run, and then no display card, console or input device is opened.
Both start `gpu_bridge` if `gpu.enabled` says so. That is compute only: CUDA on the card through the driver, and never
`/dev/dri`. With `gpu.enabled` true, no CUDA device means exit status 5. `--report` and `--test-pattern` do not start it.
Later systems' `_init` calls go in `app_init` after the CPU check, and their `_deinit` calls in `app_deinit` in reverse.

**May be called by:** the OS. Tests link everything but this file, and run the built program.
**Depends on:** `display`, `game`, `physics`, `player`, `gpu_bridge`, `input`, `job`, `math`, `render`, `terrain`, `texture`, `world`, `thread`, `time`, `heap` (only its counters and leak check), `log`, `types`, `main/cpu_check`, the
generated `machine/index.h`.
