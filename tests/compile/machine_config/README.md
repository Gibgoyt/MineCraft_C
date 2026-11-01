# tests/compile/machine_config

**Purpose:** ctest `compile_machine_config`: proves `machine_config` writes the right thing for all four languages, and refuses bad input on the right line.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Its buffers are static, and it writes only under `build/<preset>/test_work/machine_config/`.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** `main`, with the paths from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `compile/src/yaml/`, the `machine_config` and `machine_config_report` tools, the generated `machine/index.h`, and `gcc`, `nasm`, `nvcc`, `cmake` and `timeout`. It starts no server and touches no display, and nvcc only compiles.

## What it checks

- The fixture's values appear in all four files, including a negative number, a false, text and a choice.
- Changing `threads.workers` changes all four files, and nothing else.
- A file with the same content keeps its modification time, and reordered keys give identical bytes.
- `gcc`, `nasm`, `nvcc` and `cmake -P` each accept their own file and see the right values at compile time.
- Each bad input is refused with exit status 1 on the expected line, and leaves the previous output alone.
- The file cut short at every byte, and with bytes overwritten at every position, ends in exit status 0 or 1, never a crash.
- A missing input, a directory as input, an unwritable output and wrong arguments give exit status 2.
- The real `config/machine.yaml`: the tool's output is byte-identical to what the build generated, the header compiled into this test matches values parsed directly from the yaml, and `machine_config_report` prints them.

It writes its own `main`, as the YAML test does, because it needs its own helpers and does not depend on any harness.
Every child runs under `timeout`, so nothing can outlive the test.
