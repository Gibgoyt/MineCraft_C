# compile/src/standards_lint/support

**Purpose:** arena, text, path and report helpers every part uses.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_arena_alloc`, `lint_arena_text`, `lint_arena_join`: bump allocation over memory the Owner mapped.
- `lint_breach`, `lint_error`: report a broken rule, or a failure of the lint itself.
- `lint_text_equal`, `lint_starts_with`, `lint_ends_with`: text checks.
- `lint_path_matches`, `lint_path_exists`, `lint_read_file`: paths and files.
- `lint_tier_of_path`: the memory tier a path's depth gives it.
**May be called by:** Every part of this tool, and the tool itself.
**Depends on:** `standards_lint/catalog`, to check that a reported rule is listed.
