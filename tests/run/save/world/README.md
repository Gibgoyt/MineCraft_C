# tests/run/save/world

**Purpose:** ctest `run_save_world`. Tests `run/src/save/` with `world`, and what `run/src/game/` and `run/src/game/session/` do with them: each record's bytes by hand; a manifest's bytes by hand; worlds reloaded against a brute-force model, over many saves and on real terrain with real light; only what changed being on disk; the same changes making the same files; what is on its way being read before a file; columns leaving and coming back; running out of room; a disk that refuses; every kind of file refused, and one flipped bit at every byte of a manifest; bad bytes in a delta; children killed at every step of a save and at random moments, each world checked afterwards against the model of exactly one save; readers on threads through saves; no heap; the real game on the card; and the built program digging, reopened, and refused another seed.
**Tier:** test.
**Owns:** nothing outside what each test starts and stops, and a directory under `/tmp` for each, removed at its end. Other memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main, forked children that really die with `SIGKILL`, and in one test real reader and writer threads.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/save/`, `run/src/world/`, `run/src/terrain/`, `run/src/game/`, `run/src/ui/menu/catalog/`, the card for the game's test (compute only), and the built program, which `test.cmake` names and which runs only with `--headless`.
