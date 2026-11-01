# compile/src/standards_lint

**Purpose:** fails the build on a breach of `plans/CODING_STANDARD.md`, and proves recursion absent from GCC's call graph.
**Tier:** Owner. Maps the tool's one region and splits it into a walk arena and a per-file arena.
**Owns:** that region, released before exit.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** `main`. A command line, not an API:

    standards_lint --root <repository>       every rule on every directory
    standards_lint --callgraph <list file>   recursion, from the .ci files next to the listed objects
    standards_lint --list-rules              every rule id, one per line

Exit status 0 is clean, 1 is a breach, 2 means the lint could not do its job. A report line reads
`path:line: [rule] what is wrong`.

**May be called by:** the build (`cmake/standards_lint.cmake`, target `mc_standards_lint`, which everything compiled
waits for) and ctest (`standards_lint`).
**Depends on:** its parts below. `compile/` includes nothing from `run/` but `types/`, and this tool does not need that.

## What it looks at

Every directory under the root, found by walking, so a new directory needs no list. Three are skipped at the root only:
`build/`, `.claude/` and `.git/`. So is any `fixtures/` directory under `tests/`, because those break the rules on
purpose. Symbolic links are not followed. Code is any `.c .h .cu .hu .asm .inc` file.

## Where a rule lives

| Part | Rules |
|---|---|
| `catalog/` | The banned identifiers and where each is allowed: `banned:<word>`. Also the `borrowed_` static allowlist |
| `rules_c/` | `literal_*`, `packed`, `pragma_pack`, `bitfield`, `bool_in_struct`, `struct_*`, `type_name`, `owning_static`, `borrowed_*`, `tier_*`, `init_deinit_pair`, `goto_cleanup_only`, `inline_asm`, `intrinsic`, `vector_extension`, `simd_target`, and the calls that feed `recursion_cuda` |
| `rules_include/` | `include_*`, `banned_header:*`, `cuda_cpp_header`, and the system ranks from `plans/REPO_STRUCTURE.md` section 11 |
| `rules_asm/` | `asm_*` |
| `rules_tree/` | `file_name`, `file_location`, `header_location`, `one_module`, `cuda_header_ext`, `line_limit`, `readme_*`, `stray_file`, `unknown_system` |
| `graph/` | `recursion` and `recursion_cuda` |

To add a rule: add its id to `catalog/`, report it, and add a fixture. `standards_lint_fixtures` fails for any rule with
no fixture, and `lint_breach` treats an id missing from the catalog as a lint bug.

## What it does not check

- `char` used for bytes, `bool` passed through memory, and any other rule that needs types and not tokens.
- A pointer hidden inside a static struct, and ownership prefixes on struct fields. Those stay with review, as section 10
  of the standard says.
- Recursion through a function pointer: GCC records `__indirect_call` and not its target.
- Recursion under `release` and `profile`, where `-flto` means GCC writes no `.ci` file at compile time. Those presets
  build the same source that `debug` and `sanitize` check.
- CUDA recursion comes from a token scan of `.cu` files, not from a compiler: calls by function pointer, and names
  reused by `static` functions in two files, can fool it.
- A rule inside code that an `#if` hides is still checked, since the lint does not preprocess. That errs on the strict side.
