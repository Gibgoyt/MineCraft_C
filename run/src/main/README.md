# run/src/main

**Purpose:** the program's entry point: holds `app_t` on the stack, checks the CPU, starts each system and stops it again.
**Tier:** Root. Allocates nothing.
**Owns:** `app_t app`, which everything else in the program hangs from. So far that is `job_system_t job`, `simd_t simd`, `math_t math`, `display_t display` and `input_t input`.
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

Running the program starts the job system, runs one job per worker through it, logs the heap's counts and stops. There is
nothing more to run until `game` (Stage 08.4).

Exit status: 0 for success, 2 for a bad command line, 3 for a CPU that lacks what the build uses, 4 for heap regions
still live after `app_deinit`, 5 for a core runtime that would not start or did not run its jobs, 6 for a display that is not ours to take (nothing on
it was changed), 7 for KMS failing partway (`MAIN_EXIT_*`).

Only `--test-pattern` opens the display and reads input. It starts no job system, and the default run stays away from the
card and from `/dev/input` until `game` arrives. It starts `input` only after `display_init` said OK, so a run that is refused
never looks at an input device. The mouse moves a cursor square, a held key or button turns it red, and Esc ends the run.
Later systems' `_init` calls go in `app_init` after the CPU check, and their `_deinit` calls in `app_deinit` in reverse.

**May be called by:** the OS. Tests link everything but this file, and run the built program.
**Depends on:** `display`, `input`, `job`, `math`, `thread`, `time`, `heap` (only its counters and leak check), `log`, `types`, `main/cpu_check`, the
generated `machine/index.h`.
