# compile/src/standards_lint/rules_cmake

**Purpose:** the build files cannot undo the checks. Flags are set only in `cmake/flags.cmake`, and a flag that weakens a check is refused anywhere.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_rules_cmake_check`: runs them on one `CMakeLists.txt` or `*.cmake` file.
**May be called by:** `standards_lint/rules_tree`.
**Depends on:** `standards_lint/support`.

This reads words, with comments removed, and is not a CMake interpreter. Words inside strings count, because a flag in a
string is still a flag. Limits are under "What it does not check" in `compile/src/standards_lint/README.md`.
