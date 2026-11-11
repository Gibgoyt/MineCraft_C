# compile/src/standards_lint

**Purpose:** fails the build on a breach of `plans/CODING_STANDARD.md`, and proves recursion absent from GCC's call graph.
**Tier:** Owner. Maps the tool's one region and splits it into a walk arena and a per-file arena.
**Owns:** that region, released before exit.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** `main`. A command line, not an API:

    standards_lint --root <repository>       every rule on every directory
    standards_lint --callgraph <list file>   recursion, from the .ci files next to the listed objects
        [--indirect-calls <yaml>]            with the reviewed calls through pointers, indirect_calls.yaml
    standards_lint --list-rules              every rule id, one per line

Exit status 0 is clean, 1 is a breach, 2 means the lint could not do its job. A report line reads
`path:line: [rule] what is wrong`.

**May be called by:** the build (`cmake/standards_lint.cmake`, targets `mc_standards_lint`, which everything compiled
waits for, and `mc_recursion_check`) and ctest (`standards_lint`).
**Depends on:** its parts below, and `yaml` (`compile/src/yaml`), which `indirect/` reads its list with. `compile/`
includes nothing from `run/` but `types/`, and this tool does not need that.

## What it looks at

Every directory under the root, found by walking, so a new directory needs no list. Three are skipped at the root only:
`build/`, `.claude/` and `.git/`. So is any `fixtures/` directory under `tests/`, because those break the rules on
purpose. Symbolic links are not followed. Code is any `.c .h .cu .hu .asm .inc` file. `CMakeLists.txt` and `*.cmake`
are read as well, for `rules_cmake/`.

## Where a rule lives

| Part | Rules |
|---|---|
| `catalog/` | The banned identifiers and where each is allowed: `banned:<word>`, including other names for the same types and allocators (`__int128_t`, `__libc_malloc`, `SYS_mmap`), libc calls that hand back a malloc'd block, `dlopen`, `_Pragma`, and `SA_NODEFER` with its other name `SA_NOMASK`, which let a signal handler interrupt itself. Also the `borrowed_` static allowlist, and the one data file a source directory may hold, `indirect_calls.yaml` |
| `rules_c/` | `literal_*`, `packed`, `pragma_pack`, `pragma_other`, `digraph`, `line_splice_in_token`, `int_fast_least`, `cuda_struct_method`, `cuda_inheritance`, `bitfield`, `bool_in_struct`, `struct_*` (the `alignof` and `offsetof` asserts on structs under `run/include/gpu_bridge/`, `simd/` and `save/`), `type_name`, `owning_static`, `borrowed_*`, `tier_*`, `init_deinit_pair`, `goto_cleanup_only`, `inline_asm`, `intrinsic`, `vector_extension`, `simd_target`, and the calls that feed `recursion_cuda` |
| `rules_include/` | `include_*`, `banned_header:*`, `cuda_cpp_header`, and the system ranks from `plans/REPO_STRUCTURE.md` section 11. `#include "x"` and `#include <x>` obey the same rules, because GCC finds either file through either spelling |
| `rules_asm/` | `asm_*` |
| `rules_tree/` | `file_name`, `file_location`, `header_location`, `one_module`, `cuda_header_ext`, `line_limit`, `readme_*`, `stray_file`, `foreign_source`, `unknown_system` |
| `rules_cmake/` | `cmake_flags_outside`: compile flags set anywhere but `cmake/flags.cmake`, per source or per target. `cmake_weakening_flag`: `-Wno-*`, `-w`, `-ffast-math` and the like, anywhere |
| `graph/` | `recursion` and `recursion_cuda` |
| `indirect/` | `indirect_call_unlisted`, `indirect_call_stale`, `job_function_unresolved`, and the job edges that let `recursion` see through `job_run` |

To add a rule: add its id to `catalog/`, report it, and add a fixture. `standards_lint_fixtures` fails for any rule with
no fixture, and `lint_breach` treats an id missing from the catalog as a lint bug.

## Recursion

GCC writes a call graph per object with `-fcallgraph-info`. `--callgraph` reads all of them as one graph and reports
each cycle. The graph comes from `-O0` code on every preset: `debug` compiles that way already, and the others compile
each C source again only for it (`cmake/standards_lint.cmake`), because inlining would move a call through a pointer
into another function than the one the list names.

GCC cannot say where a call through a function pointer goes, and writes it as an edge to `__indirect_call`. `indirect/`
closes that gap two ways. A job is followed: every function the sources pass to the job system gets an edge from
`job_run`, so a job that can reach its own submission is a cycle. Every other function that calls through a pointer
must be in `indirect_calls.yaml`, next to this file, with the pointer's type and why none of its targets can reach it
again; the lint fails on one that is missing and on an entry that is no longer true. `indirect/README.md` has the
details. A signal handler that its own signal could interrupt would be recursion the graph cannot see at all, so
`SA_NODEFER` is banned outright.

To review a new caller: find what the pointer can hold (the functions whose address is stored in it), and check that
none of them can reach the caller, directly or through a job. If one can, remove the recursion; do not list it.

## Stricter than the standard's words

These follow from what the standard says but are not spelled out in it: `auto` and `__auto_type`, because a declaration
must show its size; `ptrdiff_t`, `intmax_t`, `uintmax_t`, `intptr_t`, `wchar_t` and the `int_fast`/`int_least` families,
because section 2's table of allowed types does not have them; every `#pragma` but `#pragma once`, because
`#pragma GCC diagnostic ignored "-Wpadded"` would switch off the padding check; digraphs; a line splice inside a word.

## What it does not check

- A build file that removes a flag without naming a flag or a flags variable, for example `string(REPLACE)` on a compile
  rule variable. `rules_cmake/` looks for the words that do this, and it is not a CMake interpreter.
- A shared struct outside the three paths under "Where a rule lives". The list is `shared_struct_paths` in `catalog/`,
  and a new kind of shared struct has to be added to it. The `alignof` and `offsetof` asserts must be written with those
  words directly after `static_assert(`, and they must be in the same file as the struct.
- A jump through a register, in `asm_vzeroupper`: its target is not a label the lint can follow. Direct jumps both ways are.
- `char` used for bytes, `bool` passed through memory, and any other rule that needs types and not tokens.
- A pointer hidden inside a static struct, and ownership prefixes on struct fields. Those stay with review, as section 10
  of the standard says.
- That a reason in `indirect_calls.yaml` is right. The lint holds the list complete and current, and review holds it
  true.
- Recursion that only optimization adds or removes. The graph is from `-O0` code with the preset's own defines: see
  "Recursion" above.
- CUDA recursion comes from a token scan of `.cu` files, not from a compiler. It counts `name(` and kernel launches
  `name<<<...>>>(` as calls, finds bodies with qualifiers or attributes before their `{`, and keys a `static` function
  by its file. A call through a function pointer in CUDA is not followed; there are none.
- A rule inside code that an `#if` hides is still checked, since the lint does not preprocess. That errs on the strict side.
