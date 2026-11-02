# tests/run/world/edit

**Purpose:** ctest `run_world_edit`. Tests `run/src/world/edit/`: random edits against a brute-force array of every voxel, flags against brute-force flags per consumer, the log against what was written, a log that wraps, flags across a column coming and going, and snapshots, with a real reader thread that must end with an exact copy of the world.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main, and one `gpu`-role thread in the tests with a reader.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/world/`, `run/src/thread/`, `run/src/time/`, and the generated `tables/blocks/`.

`snapshot_cost_on_this_machine` prints what a publish costs, for the stage file, and checks nothing about it, since a timing on a shared machine would make a flaky test.
