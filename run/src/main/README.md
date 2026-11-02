# run/src/main

**Purpose:** the program's entry point: holds `app_t` on the stack and checks the CPU before anything else runs.
**Tier:** Root. Allocates nothing.
**Owns:** `app_t app`, which everything else in the program hangs from.
**Lifetime:** Process.
**Threads:** main.
**Public functions:** `main`. A command line:

    minecraft_c                                   runs the program
    minecraft_c --report                          prints the compiled defines and CPU features, then exits
    minecraft_c --pretend-missing-cpu <key>       treats a feature as absent, to see the CPU check fail

Exit status: 0 for success, 2 for a bad command line, 3 for a CPU that lacks what the build uses (`MAIN_EXIT_*`).
Later systems' `_init` calls go in `app_init` after the CPU check, and their `_deinit` calls in `app_deinit` in reverse.

**May be called by:** the OS. Tests link everything but this file, and run the built program with `--report`.
**Depends on:** `log`, `main/cpu_check`, the generated `machine/index.h`.
