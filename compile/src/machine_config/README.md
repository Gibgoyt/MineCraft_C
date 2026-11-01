# compile/src/machine_config

**Purpose:** turns `config/machine.yaml` into `index.h`, `index.hu`, `index.inc` and `index.cmake`, so C, CUDA, NASM and CMake share one source.
**Tier:** Owner. Maps the tool's one process region from `index.c` and releases it before exit.
**Owns:** that region, in `machine_config_t` on `main`'s stack.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** `main`. A command line, not an API:

    machine_config <machine.yaml> <output directory>

Exit status 0 is success, 1 is a refused `machine.yaml` (`path:line:column: message` on stderr), 2 means the tool could
not do its job, such as an unreadable input or an unwritable directory.

**May be called by:** `cmake/machine.cmake` at configure time, and `tests/compile/machine_config/`. Nothing links it.
**Depends on:** `compile/src/yaml/` and glibc.

## Where the output goes

`build/<preset>/generated/machine/`, with `build/<preset>/generated` on every include path:

| File | For | Use it as |
|---|---|---|
| `index.h` | C | `#include "machine/index.h"` |
| `index.hu` | CUDA | `#include "machine/index.hu"` |
| `index.inc` | NASM | `%include "machine/index.inc"` |
| `index.cmake` | CMake | Loaded by `cmake/machine.cmake` before anything else needs it |

All four hold the same names and the same values, in the same order. Nothing in them depends on the time, the paths or
the key order in the yaml. A file whose content would not change is not touched, so a configure with an unchanged yaml
rebuilds nothing.

## What gets defined

The table `machine_keys` in `index.c` is the whole list, with each define written out so a grep finds its row:
`threads.workers` is `MC_THREADS_WORKERS`, `cpu.simd.avx2` is `MC_CPU_HAS_AVX2`.

- True and false are `1` and `0` in every language, so `#if`, `%if` and `if()` all work. Test the value, not whether it
  is defined.
- Text arrives in double quotes, as C, NASM and CMake all read it.
- A key with a fixed list of words, such as `display.backend`, also gets one define per word, because `#if` cannot
  compare text: `MC_DISPLAY_BACKEND_KMS` is `1`.

To add a key: add it to `config/machine.yaml`, add a row to `machine_key_id_t` and `machine_keys`, and add it to the
fixture in `tests/compile/machine_config/index.c`.

## What gets refused

Nothing is written unless everything passes, so a refused yaml leaves the last good output in place. Each error names a
line. The tool refuses:

- YAML the parser refuses, a key the table does not have (so a typo cannot pass), and a key the table has that is missing.
- A wrong type, including `"true"` in quotes, and a number outside its row's range.
- Text that would need escaping in any output: control characters, non-ASCII, and `` " \ ` $ ; ``. Keys that may reach a
  compiler flag, such as `cpu.arch_flag`, are limited to letters, digits, `.`, `_` and `-`.
- Rules across keys: thread counts past `cpu.logical_threads`, GPU budgets past `gpu.vram_mib`, a SIMD level without the
  one it builds on, a cache line, huge page, chunk or section size that is not a power of two, `render_distance_default`
  past `render_distance_max`, and `z_sea` outside `z_min` to `z_max`.

Because threads are pinned one to a logical CPU, `threads.workers` above 16 on this machine's yaml is an error until
`threads.reserved_os` or `cpu.logical_threads` changes to allow it.

## `report/`

`report/index.c` builds `bin/tools/machine_config_report`, which prints defines from the compiled header. It is how you
see a yaml change reach a compiled binary, until the real `run/src/main/` can report them itself.
