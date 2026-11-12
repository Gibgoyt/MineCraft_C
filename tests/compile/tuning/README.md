# tests/compile/tuning

**Purpose:** ctest `compile_tuning`: proves `compile/src/tuning/` writes the defines a file says, as written, refuses each mistake at its line and column with nothing written, leaves unchanged headers alone, and that the real `compile/config/` is accepted and every define in it is used.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Its buffers are static, and it writes only under `build/<preset>/test_work/tuning/`.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_tuning`, on `tests/harness/`, with the paths from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, the `tuning` tool, the real `compile/config/` and what the build generated from it, and `rm`, `ln` and `grep`. It starts no server, touches no display and needs no card.

## What it checks

- `a_good_file_gives_the_defines_it_says`: a fixture with one key of each type; the `.h` byte for byte, the `.hu` the same
  but for its guard, and the summary's count and lines.
- `a_decimal_comes_out_as_written`: `0.1`, `1.25`, `0.000004`, `16.0`, `0.0625`, `-0.5` and `100.0` come out as `F32(0.1)`
  and so on, and a negative one in brackets.
- `each_mistake_is_refused_at_its_line`, each with its line and column: an unknown field, a missing `why`, a value over
  `max`, an `f32` written `2` and `3.0e0`, a `u32` written `-1`, `"true"` in quotes for a bool, an unknown unit, the unit
  `bool` on a `u32`, a key in camel case, a tab in the indentation, an empty `why`, an unknown type, a bool with a `min`,
  and a `min` past its `max`. Each exits 1 and makes no output directory.
- `every_problem_is_printed`: problems in two files, an `index.yaml` at the top of `--config` and a directory in capitals,
  all reported in one run.
- `two_files_making_one_define_are_refused`: `render/a`'s `b_c` and `render/a_b`'s `c`.
- `a_refused_run_writes_nothing`: after a good run, a bad one leaves every output's bytes and time as they were.
- `an_unchanged_file_is_not_rewritten`: a second run keeps every header's time, and rewrites only the stamp; a changed
  value rewrites its own file's headers and the summary, and not another file's.
- `the_hash_moves_with_one_value_and_not_with_order`: reordered keys give the same hash and the same header bytes; one
  value changed gives another hash.
- `files_come_out_in_sorted_order`: files written out of order, three levels deep, come out sorted by path, keys by name.
- `a_bad_command_line_cannot_run`: a missing `--out` and a missing `--config` directory exit 2.
- `the_real_files_are_accepted`: the committed `compile/config/` passes, and the tool's summary is byte-identical to what
  the build generated.
- `every_define_is_used`: each define the real files make is found by `grep` in `run/`, `cuda/` or `bench/`.

The generated `tuning/index.h` is read as a file, not included: `compile/include/` comes first on a compile test's include
path, and holds the tool's own `tuning/index.h`.
