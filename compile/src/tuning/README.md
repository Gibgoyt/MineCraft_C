# compile/src/tuning

**Purpose:** turns every `compile/config/<path>/index.yaml` into a header of `MC_TUNE_*` defines, so a number that tunes the renderer's speed or look is changed by editing one line of YAML and running one build. Plan 009 phase 02.
**Tier:** Owner. Maps the tool's one process region in `index.c` and releases it before exit.
**Owns:** that region, in `tuning_t` on `main`'s stack: the files' text, their parsed nodes, the table of keys, and one output at a time.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** `main`. A command line, not an API, and `compile/include/tuning/index.h` holds its exit statuses:

    tuning --config compile/config --out <generated>/tuning [--stamp <file>]

Exit status 0 is success, 1 is a refused file (every problem on stderr as `path:line:column: message`, and nothing
written), 2 means the tool could not do its job: a bad command line, an unreadable input or an unwritable output.

**May be called by:** `cmake/generated.cmake` at build time, and ctest `compile_tuning`. Nothing links it.
**Depends on:** `compile/src/yaml/` and glibc.

## What it writes

For each `compile/config/<path>/index.yaml`, in `build/<preset>/generated/tuning/<path>/`:

| File | For |
|---|---|
| `index.h` | C, and the `run/include/gpu_bridge/` headers nvcc reads too |
| `index.hu` | CUDA-only code under `cuda/` |

The two hold the same defines and differ only in their guard, so a CUDA file that includes both through different headers
sees one definition. A float is written `F32(3.0)`, with `#include "types/index.h"` when the file has one: that is
`3.0f32` to GCC and `3.0F` to nvcc, as `plans/CODING_STANDARD.md` section 2 asks of a header both sides read.

And `generated/tuning/index.h`, for `--report` and `bench_world`'s history:

- `MC_TUNE_HASH`: FNV-1a 64 over one line per key, `path key value` and a newline, files in path order and keys in name
  order. Reordering keys changes nothing; changing any value changes it.
- `MC_TUNE_KEY_COUNT`, and `MC_TUNE_TEXT`: those lines as one string.

A define is `MC_TUNE_<PATH>_<KEY>` upper-cased, `/` as `_`: `render/shade`'s `ao_turns` is
`MC_TUNE_RENDER_SHADE_AO_TURNS`. A `u32` is written `4u`, an `i32` `-2` as `(-2)`, a `bool` `1` or `0`, and an `f32` as
the exact decimal the YAML says, so `0.1` is `F32(0.1)`, the very literal the code had, and the code gets the float it
always had. The unit and the
range go in a comment beside each.

Nothing in an output depends on the time, the paths given or the order keys are written in. An output whose bytes would
not change is not touched, so a header keeps its time. The headers are the custom command's byproducts and `--stamp` is
its output, so Ninja's restat rebuilds only what includes a header that changed. The stamp is rewritten on every run.

## What it refuses

Everything is checked before anything is written, and every problem in the run is printed, not only the first:

- YAML the parser refuses, including a tab in the indentation.
- A file whose top is not a mapping, an `index.yaml` directly in `--config`, and a directory name or key that is not lower
  snake case (`[a-z][a-z0-9_]*`).
- A field other than `value type unit min max why`, and a missing one. A `bool` has no `min` or `max`.
- A type other than `u32 i32 f32 bool`, and a unit not on the list in `compile/config/README.md`. The unit `bool` goes
  with the type `bool` and with nothing else.
- A value, `min` or `max` of the wrong type: `"true"` in quotes for a bool, `-1` for a `u32`, `2` for an `f32` (an `f32`
  is written digits, a point and digits, exactly as it is to come out).
- A value outside `min`..`max`, and a `min` past its `max`, compared exactly.
- An empty `why`.
- Two keys, in different files, that make the same define: `render/a_b`'s `c` and `render/a`'s `b_c`.

Limits, each refused when reached: 256 files, 2048 keys, 1 MiB of YAML in all, 8 directories deep.

A file removed from `compile/config/` leaves its old header in `generated/tuning/` until a clean build. Code that still
includes it compiles against the last values; a grep for the define finds such code.
