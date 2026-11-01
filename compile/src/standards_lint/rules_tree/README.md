# compile/src/standards_lint/rules_tree

**Purpose:** walks every directory, applies the file and directory rules, and runs the source rules on each file.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_rules_tree_check`: lints a whole repository.
**May be called by:** the tool.
**Depends on:** `standards_lint/rules_c`, `standards_lint/rules_include`, `standards_lint/rules_asm`, `standards_lint/lexer`, `standards_lint/graph`, `standards_lint/catalog`, `standards_lint/support`.
