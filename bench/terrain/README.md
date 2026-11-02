# bench/terrain

**Purpose:** times every stage of terrain together, `terrain_generate_box`, against the plan's target of 1000 columns a second, in the places where what is on the ground differs most.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers, released before it exits.
**Lifetime:** Process.
**Threads:** main, and up to 16 job workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain`, so no build shares the CPU. Exit status 0, 1 if a place missed the target or could not be found, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `terrain`, `job`, `thread`, `heap`, `time`, and the generated `tables/biomes/`.

Only a release build's numbers mean anything: debug is at `-O0`.

A round is 36 chunks of whole columns, 751 layers from the bedrock to the top of the world, each in its own box as a chunk would be asked for. Each place keeps its best of 3 rounds, and the gate is on its 1-worker number, as the plan gives one number and a worker should meet it alone. The `tour` directory beside this is the phase gate's biome tour.

## Results
