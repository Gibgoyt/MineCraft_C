# tests/compile/standards_lint

Proves that each banned construct is rejected. ctest name `standards_lint_fixtures`, set in `test.cmake`. The other ctest
in this stage, `standards_lint`, lints the repository and is registered in `cmake/standards_lint.cmake`.

| Fixtures | What happens | What `EXPECT` holds |
|---|---|---|
| `fixtures/lint/<name>/` | A small repository, linted with `--root` | The exact rule ids a run must report. Empty means clean |
| `fixtures/callgraph/<name>/` | Its `.c` files are compiled with `-fcallgraph-info=su` and checked with `--callgraph`, and with `--indirect-calls` when it has an `indirect_calls.yaml` | The same |
| `fixtures/compiler/<name>/` | `index.c` is compiled with the real flags from `cmake/flags.cmake` | `pass`, or `fail <text from the error>` |

The match on rules is exact, so a fixture cannot pass by tripping some other rule. Any rule in `standards_lint --list-rules`
with no fixture fails the test, which keeps "one failing fixture per banned construct" true as rules are added.

The `ok_*` fixtures must stay clean, and guard against false alarms. `harness/` tests `tests/harness/` and is its own ctest,
`compile_standards_lint_harness`. Its partner `compile_standards_lint_harness_samples_fail` runs the failing samples and must
exit non-zero, which holds even if the harness has stopped counting failures and so cannot report on itself.
