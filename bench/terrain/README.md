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

### 2026-11-02, Stage 10.7

`build/release/bin/bench_terrain`, seed `0x4D494E4543524146`, columns a second, exit status 0. Target 1000: every place beat it, the slowest by about 22 times on 1 worker.

| Place | 1 worker | 16 workers | Scaling |
|---|---|---|---|
| `plains` | 61104 | 518915 | 8.5x |
| `forest` | 34718 | 280656 | 8.1x |
| `jungle` | 22725 | 193174 | 8.5x |
| `dark_forest` | 54055 | 453648 | 8.4x |
| `mangrove_swamp` | 66744 | 554847 | 8.3x |
| `desert` | 61809 | 502267 | 8.1x |
| `snowy_taiga` | 33839 | 246174 | 7.3x |
| `jagged_peaks` | 37780 | 280440 | 7.4x |
| `deep_ocean` | 96081 | 734900 | 7.6x |

Run under `tools/test-or-build.sh`, so no other agent's build or test shared the CPU. Each place is the first column of that biome the search meets, so its 36 chunks are not all that biome: `dark_forest` beating `forest` says as much about that as about the trees.
