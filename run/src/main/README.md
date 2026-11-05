# run/src/main

**Purpose:** the program's entry point: holds `app_t` on the stack, checks the CPU, starts each system and stops it again.
**Tier:** Root. Allocates nothing.
**Owns:** `app_t app`, which everything else in the program hangs from. That is `job_system_t job`, `simd_t simd`, `texture_t texture`, `ui_t ui`, `world_t world`, `terrain_t terrain`, `physics_t physics`, `player_t player`, `entity_t entity`, `mob_t mob`, `item_t item`, `gpu_bridge_t gpu_bridge`, `render_t render`, `game_t game`, `math_t math`, `display_t display`, `display_offscreen_t offscreen`, `input_t input`, `settings_t settings`, `save_t save` and `profiler_t profiler`, and the plain values `thread_topology_t topology`, `cpu_required` and `cpu_present`.
**Lifetime:** Process.
**Threads:** main, which it pins to its place in `config/machine.yaml`.
**Public functions:** `main`. A command line:

    minecraft_c                                   on a terminal, the same as --menu; otherwise, as under ctest, the check below
    minecraft_c --menu [--saves <dir>] [--render-distance <chunks>] [the effects' flags]
                                                  the game, through KMS, from a text console only: main menu, make or choose a
                                                  world, walk in it, Esc to pause. See run/src/game/session/README.md
    minecraft_c --headless --menu [the same] [--script ...] [--screenshot-dir ...] [--ticks ...] [--width ...] [--height ...]
                                                  the same with no display: a script clicks through it, into PNG screenshots
    minecraft_c <any of these but --report and --test-pattern> --profile <path>
                                                  times the run: a trace at <path> and a table on stderr, at exit. Not in a
                                                  release build. See run/src/profiler/README.md
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

`--seed <n>`, with `--test-world`, opens this seed's land and not the default's, as a test can then stand on ground it chose, and a seed from `bench_world_map` can be walked on.

`--render-distance` is how far the world is drawn in all and `--full-detail` how far block by block, 32 chunks unless told: past it, coarse grids take over, and `--no-lod` stops the world where full detail does.

`--no-shapes` draws every block a whole cube, and `--no-alpha` stops rays at see-through texels. `--no-under-water` leaves a body's eyes in water with what the rays alone make of it. `--no-transparency` stops them at water and part-covered texels too, `--no-water-reflection` leaves the mirror out of water, and `--no-water-tint` leaves its colour with depth out. `--no-ao` leaves out the short rays that darken corners, and `--no-taa` has each frame stand alone. `--god-rays` turns on the one effect that is off until asked for. Each effect of phase 9 is a switch like these, and with `--menu` a box on the screen in `run/src/ui/screen_settings/`.

`--walk` puts a body in that world, on the nearest dry land to its middle, in place of the flying camera. WASD walks, Space jumps, swims up and climbs, Left Shift sneaks, Left Ctrl sprints, the mouse looks, the left button mines and the right button places what was mined. E opens the inventory, the right button on a crafting table, furnace or chest opens that, and E or Esc closes it, with the mouse then a pointer. A script drives the same keys. See `run/src/player/README.md`. It also brings the HUD. F3, in any `--test-world` run, turns the debug overlay on and off: see `run/src/ui/hud/README.md`.

Worlds live under `--saves`, or `saves` where the program was started, which git ignores. What a person chose on the Settings screen lives in `--settings`, or `settings.yaml` where the program was started, which git ignores too, and only `--menu` reads it.

With no flag and stdin not a terminal, running the program starts the job system, runs one job per worker through it, logs the heap's counts and
stops. `ctest` runs it that way, with stdin from `/dev/null`. On a terminal it is the game, as `--menu` is, and inside X that is refused with nothing touched.

Exit status: 0 for success, 2 for a bad command line, 3 for a CPU that lacks what the build uses, 4 for heap regions
still live after `app_deinit` or device blocks still live before it, 5 for a core runtime or a card that would not start or did not run its jobs, 6 for a display that is not ours to take (nothing on
it was changed), 7 for KMS failing partway, 8 for a bad script or a screenshot that could not be written under `--headless`, 9 for a card that went during the run, with the world saved if the log says so (`MAIN_EXIT_*`).

`--pretend-gpu-lost <pictures>` makes the card fault for real after that many pictures, to see all of that happen: see `run/src/game/README.md`. It costs only this program's context on the card, and a line in the kernel's log.

Only `--test-pattern`, `--test-world` and `--menu` without `--headless`, and no flags at all on a terminal, open the display and read input. The default run off a terminal stays away from
the card and from `/dev/input`. Each starts `input` only after `display_init` said OK, so a run that is refused never looks at
an input device, and `--test-world` asks for the display before it opens a world. `--test-pattern` starts no job system. The mouse moves a cursor square, a held key or button turns it red, and Esc ends the run.
`--headless` starts the same runtime as the default run, and then no display card, console or input device is opened.
Both start `gpu_bridge` if `gpu.enabled` says so. That is compute only: CUDA on the card through the driver, and never
`/dev/dri`. With `gpu.enabled` true, no CUDA device means exit status 5. `--report` and `--test-pattern` do not start it.
Later systems' `_init` calls go in `app_init` after the CPU check, and their `_deinit` calls in `app_deinit` in reverse.

**May be called by:** the OS. Tests link everything but this file, and run the built program.
**Saves:** with `--menu`, `save_init` and `game_save_set`; nothing else has a world on disk. See `run/src/save/README.md`.
**Settings:** with `--menu`, `settings_load` comes before `job_init`, as the worker count is in it, and `game_session_settings_set` hands them to the session. See `run/src/settings/README.md`.
**Depends on:** `display`, `game`, `physics`, `player`, `entity`, `item`, `mob`, `simd`, `settings`, `ui`, `profiler`, `gpu_bridge`, `input`, `job`, `math`, `render`, `save`, `terrain`, `texture`, `world`, `thread`, `time`, `heap` (only its counters and leak check), `log`, `types`, `main/cpu_check`, the
generated `machine/index.h`.
