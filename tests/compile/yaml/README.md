# tests/compile/yaml

**Purpose:** proves `compile/src/yaml`: valid documents read back correctly, and bad ones fail on the right line.
**Tier:** Test. No server, no display, no files.
**Owns:** nothing. Storage is fixed static arrays holding no heap memory.
**Lifetime:** Process.
**Threads:** main only.
**Public functions:** none. It is the ctest `compile_yaml`.
**May be called by:** ctest.
**Depends on:** `tests/harness/` and `compile/src/yaml`.

The text of `config/machine.yaml` from plan section 5 is embedded and checked value by value, along with the dirt block
example from `plans/REPO_STRUCTURE.md`. The last tests truncate and corrupt those texts at every position, and check
that each result is either a valid tree or an error with a real line number. Run them under the `sanitize` preset to
catch memory errors as well.
