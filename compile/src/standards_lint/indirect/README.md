# compile/src/standards_lint/indirect

**Purpose:** calls through function pointers, which GCC's call graph records as `__indirect_call` with no target. Job
functions become real edges, and every other such caller must be in the reviewed list,
`compile/src/standards_lint/indirect_calls.yaml`.
**Tier:** Arena. Allocates only from the `lint_arena_t`s it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_indirect_check`: runs after every `.ci` file is read and before the cycle check.
**May be called by:** the tool.
**Depends on:** `standards_lint/graph`, `standards_lint/lexer`, `standards_lint/support`, and `yaml` (the
`compile/src/yaml` parser) for the list.

## Job functions

Each source a `.ci` file came from is lexed again. Every call to `job_spawn`, `job_submit` or `job_try_submit`, or to a
name that starts with one of those (the `_at` forms), hands the job system its second-to-last argument, the
`job_function_t`. That argument must be a function's name, or a choice between names with `?:`. The name is looked up
as GCC writes it: `unit:name` for a static, plain `name` otherwise. Then an edge goes from each function under
`job_dispatchers:` in the list (today only `job_run`) to each of those functions, with the submission as its place. A
job that can reach its own submission, as a self-spawning job that ran inline once did, is then a cycle like any other.

A static function that hands one of its own `job_function_t` parameters on, such as `census_single`, is a wrapper: what
its callers pass in that place is a job function too, and the walk repeats until it finds no new wrapper. Anything else
is `job_function_unresolved`: a pointer read from a variable or a struct, or a forwarding function that is not static,
since its callers could be in any file. A token scan also sees code that an `#if` leaves out, so a submission counts only
when the function it is written in was compiled in this preset (`TEST(name)` is `test_body_name`).

## The list

    root: ../../..                    # the repository, from the list's own directory
    job_dispatchers:
      - file: run/src/job/index.c     # where the function is written, from root
        caller: job_run               # its own name
        why: "..."
    indirect_calls:
      - file: ...
        caller: ...
        pointer: "..."                # the type of what it calls
        why: "..."                    # why none of its targets can reach it again

`file` is where the definition is written, as the `.ci` node's label gives it, so a static inline function in a header
is listed once under that header, however many sources include it. Rules:

- `indirect_call_unlisted`: a function with an `__indirect_call` edge that the list does not name, reported at the
  call.
- `indirect_call_stale`: an entry whose file is gone, whose file no longer has the word, or whose function this preset
  compiled without any call through a pointer. An entry whose function this preset does not compile at all, such as a
  fuzzer outside `sanitize` or code under `#ifndef NDEBUG` in release, is left alone.
- A list that does not parse, or an entry without `file`, `caller`, `why` (and `pointer` under `indirect_calls`), or a
  key it does not know, is an error: exit status 2.

Without `--indirect-calls` the list is empty: every caller through a pointer is a breach and no job edges are added.
