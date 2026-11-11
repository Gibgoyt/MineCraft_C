# tests/run/world/section

**Purpose:** ctest `run_world_section`. Tests `run/src/world/section/` against a brute-force array: random edits with 2, 5, 17, 300 and any number of states, each change of form in both directions, running out of room, memory used again, the bytes each form holds, and an import encoded off the pool's thread being the same bytes as a plain one (plan 005 phase 14).
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/world/section/`.

`memory_per_section_is_as_the_readme_says` prints the table in `run/src/world/section/README.md`, and fails if a number moves. Every structure check also recounts a palette's counts from its indices, so a count that drifts fails before it could give a wrong read.
