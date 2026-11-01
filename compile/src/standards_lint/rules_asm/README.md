# compile/src/standards_lint/rules_asm

**Purpose:** the required form of NASM files, and `.inc` includes.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_rules_asm_check`: runs them on one file.
**May be called by:** `standards_lint/rules_tree`.
**Depends on:** `standards_lint/support`.
