# tests/run/types

**Purpose:** ctest `run_types`. Tests all of Stage 03.1: `types`, `assert`, `log`, `main/cpu_check`, `asm/src/cpuid`, and the
built `minecraft_c` itself.
**Tier:** test.
**Owns:** nothing outside the test arena.
**Lifetime:** One run.
**Threads:** main, and child processes it forks and waits for.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `/proc/cpuinfo` as an independent reference for CPUID, `config/machine.yaml`, GCC and
nvcc for `fixtures/`.

`fixtures/` is compiled by the test, with the build's own flags, and judged by the compiler's answer: the printf rule in
`plans/CODING_STANDARD.md` section 2 exists because of what GCC rejects, so GCC is what gets asked. Each fixture says
at its top whether it must compile.

It runs `minecraft_c` with `--report`, `--pretend-missing-cpu`, `--pretend-leak`, a bad argument, and with no argument. No
mode opens a display yet. **Stage 05.1 must move the no-argument run to the headless mode**, or ctest would take the card.
