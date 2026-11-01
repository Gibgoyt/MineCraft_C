# tests/compile/standards_lint/harness

Tests `tests/harness/`. ctest name `compile_standards_lint_harness`. It lives here because discovery skips everything under
`tests/harness/`, and a `tests/compile/` test does not have to link the game. It runs itself with `--samples`, where tests
fail on purpose, and reads the output and the exit status.
