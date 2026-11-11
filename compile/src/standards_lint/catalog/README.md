# compile/src/standards_lint/catalog

**Purpose:** every rule id the lint can report, and the table of banned identifiers with where each is still allowed.
**Tier:** Arena. Allocates only from the `lint_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions:**
- `lint_catalog_find_word`: the row for an identifier.
- `lint_catalog_has_rule`, `lint_catalog_rule_count`, `lint_catalog_rule_at`: the rule list behind `--list-rules`.
- `lint_catalog_borrowed_static_allowed`: the `borrowed_` static allowlist.
- `lint_catalog_struct_is_shared`: the paths whose structs need `alignof` and `offsetof` asserts.
- `lint_catalog_data_file_allowed`: the one data file a source directory may hold, `indirect_calls.yaml`.
**May be called by:** `standards_lint/support`, `standards_lint/rules_c`, `standards_lint/rules_tree` and the tool.
**Depends on:** `standards_lint/support`.
