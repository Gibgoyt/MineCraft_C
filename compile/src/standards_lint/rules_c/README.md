# compile/src/standards_lint/rules_c

**Purpose:** the token rules for C and CUDA: banned identifiers, attributes, literals, structs, statics, `goto`, tiers.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_rules_c_check`: runs them on one lexed file.
**May be called by:** `standards_lint/rules_tree`.
**Depends on:** `standards_lint/lexer`, `standards_lint/catalog`, `standards_lint/graph`, `standards_lint/support`.
