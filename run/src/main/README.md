# run/src/main

**Purpose:** the program's entry point: holds `app_t` on the stack, checks the CPU, starts each system and stops it again.
**Tier:** Root. Allocates nothing.
**Owns:** `app_t app`, which everything else in the program hangs from. So far that is `job_system_t job`.
**Lifetime:** Process.
**Threads:** main, which it pins to its place in `config/machine.yaml`.
**Public functions:** `main`. A command line:

    minecraft_c                                   runs the program
    minecraft_c --report                          prints the compiled defines and CPU features, then exits, starting nothing
    minecraft_c --pretend-missing-cpu <key>       treats a feature as absent, to see the CPU check fail
    minecraft_c --pretend-leak                    skips the shutdown, to see the leak check at exit fail

Running the program starts the job system, runs one job per worker through it, logs the heap's counts and stops. There is
nothing more to run until `game` (Stage 08.4).

Exit status: 0 for success, 2 for a bad command line, 3 for a CPU that lacks what the build uses, 4 for heap regions
still live after `app_deinit`, 5 for a core runtime that would not start or did not run its jobs (`MAIN_EXIT_*`).
Later systems' `_init` calls go in `app_init` after the CPU check, and their `_deinit` calls in `app_deinit` in reverse.

**May be called by:** the OS. Tests link everything but this file, and run the built program.
**Depends on:** `job`, `thread`, `time`, `heap` (only its counters and leak check), `log`, `types`, `main/cpu_check`, the
generated `machine/index.h`.
