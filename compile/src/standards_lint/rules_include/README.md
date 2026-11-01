# compile/src/standards_lint/rules_include

**Purpose:** which headers a file may include: banned libraries, and the direction in `plans/REPO_STRUCTURE.md` section 11.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_rules_include_check`: runs them on one lexed file.
- `lint_rules_include_system_rank`: a `run/` system's row in the section 11 diagram.
**May be called by:** `standards_lint/rules_tree`.
**Depends on:** `standards_lint/lexer`, `standards_lint/catalog`, `standards_lint/support`.
