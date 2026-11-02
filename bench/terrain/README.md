# bench/terrain

**Purpose:** times every stage of terrain together, `terrain_generate_box`, against the plan's target of 1000 columns a second on 16 workers, in the places where what is on the ground differs most.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its buffers, released before it exits.
**Lifetime:** Process.
**Threads:** main, and up to 16 job workers.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 300 build/release/bin/bench_terrain`, so no build shares the CPU. Exit status 0, 1 if a place missed the target or could not be found, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `terrain`, `job`, `thread`, `heap`, `time`, `world/column`, and the generated `tables/biomes/`.

Only a release build's numbers mean anything: debug is at `-O0`.

A **column** here is what it is in the plan and in `run/src/world/column/`: a chunk's, 16 by 16 and 768 layers, the very box the game asks for. A round is 36 of them, each in its own box. Each place keeps its best of 3 rounds, and the gate is on its 16-worker number, as the plan's target is. The `tour` directory beside this is the phase gate's biome tour.

## Results

### 2026-11-02, the phase 10 gate

`build/release/bin/bench_terrain`, seed `0x4D494E4543524146`, chunk columns a second, exit status 0. Target 1000 on 16 workers: every place beat it, the slowest, `jungle`, by about 10%, which is not much room.

| Place | 1 worker | 16 workers | Scaling |
|---|---|---|---|
| `plains` | 288 | 2275 | 7.9x |
| `forest` | 186 | 1418 | 7.6x |
| `jungle` | 127 | 1096 | 8.6x |
| `dark_forest` | 259 | 2004 | 7.7x |
| `mangrove_swamp` | 317 | 2571 | 8.1x |
| `desert` | 290 | 2169 | 7.5x |
| `snowy_taiga` | 183 | 1400 | 7.6x |
| `jagged_peaks` | 181 | 1496 | 8.3x |
| `deep_ocean` | 418 | 3155 | 7.6x |

Run under `tools/test-or-build.sh`, so no other agent's build or test shared the CPU. Each place is the first column of that biome the search meets, so its 36 chunks are not all that biome.

### 2026-11-02, Stage 10.7: withdrawn

That table counted a block's column of 1 by 1, so each number was 256 times what the plan means, and it held the 1-worker number against a target set for 16. Its 22,725 and 193,174 for `jungle` were 89 and 755 in the plan's unit, under the target: see the phase file's issue 1.
