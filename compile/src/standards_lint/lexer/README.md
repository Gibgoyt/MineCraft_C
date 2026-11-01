# compile/src/standards_lint/lexer

**Purpose:** splits C and CUDA source into tokens, so comments and strings never trigger a rule.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_lex`: tokens and `#include`s of one file.
- `lint_token_is`: compares a token to a word.
**May be called by:** `standards_lint/rules_c`, `standards_lint/rules_include`, `standards_lint/rules_tree`.
**Depends on:** `standards_lint/support`.
