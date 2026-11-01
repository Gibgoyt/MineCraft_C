# tests/harness

**Purpose:** the minimal test framework every test links.
**Tier:** none; tests are not tiered. It maps the one test arena under the standard's "tests may use a test arena".
**Owns:** that arena, for the length of `test_main`.
**Lifetime:** Process.
**Threads:** the test's main thread. Checks are not thread-safe, so report results from it.
**May be called by:** any test.
**Depends on:** glibc.

## A test

Create `tests/<path>/index.c` and a `README.md` next to it. CMake finds it with no list to edit, links this harness, and
registers ctest `<path with / as _>`: `tests/run/world/section/` is `run_world_section`.

```c
#include "harness/index.h"

TEST(air_is_the_default) {
    uint8_t *blocks = test_alloc(t, 4096u);     /* zeroed; lasts until this test ends */
    CHECK_EQ_U64(blocks[0], 0u);
}

TEST_MAIN()
```

Tests in `tests/compile/` link `compile/src/`, and all others link the game without its `main`.

| Call | Use |
|---|---|
| `CHECK(cond)`, `CHECK_EQ_U64`, `CHECK_EQ_I64`, `CHECK_STR_EQ`, `CHECK_CONTAINS`, `CHECK_MEM_EQ`, `CHECK_NEAR` | Records a failure with file, line and values, and carries on. All but `CHECK_NEAR` return whether they held |
| `REQUIRE(cond)` | Records a failure and ends this test, for when the rest would crash |
| `test_alloc(t, bytes)` | Zeroed memory until the test ends. Tests use this and not `malloc` |
| `test_process_run(t, argv, timeout_ms, &result)` | Runs a program, captures its output, and kills its whole process group at the deadline |
| `t->arg_count`, `t->borrowed_args` | The program's arguments, set with `MC_TEST_ARGS` in a `test.cmake` |

The exit status is 0 only if at least one test ran and none failed. `TEST_FILTER=name` in the environment runs only tests
whose name contains `name`. A test program can also write its own `main` and call `test_main(argc, argv, filter)`.

## test.cmake

A `test.cmake` next to the test is optional. See `mc_add_test` in `cmake/targets.cmake`: it can set `MC_TEST_NAME`,
`MC_TEST_ARGS`, `MC_TEST_LIBS` and `MC_TEST_TIMEOUT`.

## Tested by

`tests/compile/standards_lint/harness/`, which runs itself to prove that a failing check really fails the program.
