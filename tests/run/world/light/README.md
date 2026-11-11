# tests/run/world/light

**Purpose:** ctest `run_world_light`. Tests `run/src/world/light/`: incremental light against a separate full recompute on random edits, one at a time and in batches, under open sky and a roof; columns arriving in any order and leaving; a random walk of the map; blocks written past the log; a pass out of room, a log that lost records and a full pool; which sections get flagged; and exact values worked out by hand. And `run/src/world/light/column/`, plan 005 phase 14: a column's light worked out alone must be what `world_light_column_added` gives a column with nothing beside it, and columns taken in from it with `world_light_column_added_from` must match columns added the old way byte for byte, in any order, after edits, and on a random walk against the full recompute.
**Tier:** test.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/world/` and its part `light/column`, `run/src/time/`, and the generated `tables/blocks/`.

`light_reference_compute` is the full recompute: a flat array, starting from nothing and going over every voxel until nothing changes, sharing no code with what it checks. `the_reference_agrees_with_hand_worked_values` is there because a reference wrong the same way as the code would prove nothing.

A box of `z` from -1 to 63 holds all the light the random tests can make, since their blocks stop at 47 and block light dies in 14 more. `light_reference_differences` still looks at all 1536 levels of every column: above the box it wants open sky, and below it darkness.

`cost_on_this_machine` prints what the operations cost, a column taken in both ways among them, for the stage file, and checks nothing about it, since a timing on a shared machine would make a flaky test.
