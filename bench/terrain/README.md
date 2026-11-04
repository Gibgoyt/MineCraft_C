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

A **column** here is what it is in the plan and in `run/src/world/column/`: a chunk's, 16 by 16 and 1536 layers, the very box the game asks for. A round is 36 of them, each in its own box. Each place keeps its best of 3 rounds, and the gate is on its 16-worker number, as the plan's target is. The `tour` directory beside this is the phase gate's biome tour, over every layer of the world, -250 to 1250.

## Results

### 2026-11-05, plan 003 phase 8 stage 7, final: every place beats it

The same land and seed, after features stopped making a whole chunk's own ground a second time. Three runs in a row, load average about 1 to 3 before each, nothing else of ours on the lock. Target 1000 on 16 workers: **met by all 9, exit status 0 on all three**. The margin of the slowest is 6.5% to 11%.

| Place | 1 worker | 16 workers, three runs |
|---|---|---|
| `plains` | 230-236 | 1859, 1817, 1942 |
| `forest` | 164-166 | 1382, 1341, 1423 |
| `jungle` | 144-146 | 1191, 1221, 1204 |
| `dark_forest` | 165-169 | 1441, 1409, 1418 |
| `mangrove_swamp` | 194-200 | 1572, 1514, 1645 |
| `desert` | 230-234 | 1890, 1822, 1904 |
| `snowy_taiga` | 126-128 | 1065, 1108, 1091 |
| `jagged_peaks` | 140-144 | 1204, 1169, 1149 |
| `deep_ocean` | 322-326 | 2422, 2374, 2429 |

### 2026-11-05, plan 003 phase 8 stage 7: on the final land, before that

Same seed and same bench, three runs in a row with nothing else of ours on the lock. Target 1000 on 16 workers: **missed by one place, by 0.4% to 3%, exit status 1 on all three.** Before stage 7's change to lining, on the same land, three runs read `jagged_peaks` 854, 840 and 847.

| Place | 1 worker | 16 workers, three runs |
|---|---|---|
| `plains` | 200 | 1460, 1649, 1607 |
| `forest` | 148 | 1105, 1207, 1192 |
| `jungle` | 138 | 1146, 1153, 1172 |
| `dark_forest` | 160 | 1339, 1311, 1385 |
| `mangrove_swamp` | 187 | 1551, 1409, 1464 |
| `desert` | 207 | 1593, 1626, 1680 |
| `snowy_taiga` | 114 | 980, 969, 996 |
| `jagged_peaks` | 126 | 1077, 1033, 1099 |
| `deep_ocean` | 283 | 2220, 2238, 2243 |

Each place is the first column of that biome the search meets, and they moved with the land, so this is not like for like with the table below. In `snowy_taiga` about 57% of the time is `terrain_feature_fill_box`.

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

A second run on a clean build of the same code, for the same gate, gave `jungle` 125 and 1051, `snowy_taiga` 181 and 1283, and every place over the target, exit status 0. So the margin is 5% to 10% from one run to the next. Later stages that add work to a column should expect to have to pay for it.

### 2026-11-02, Stage 10.7: withdrawn

That table counted a block's column of 1 by 1, so each number was 256 times what the plan means, and it held the 1-worker number against a target set for 16. Its 22,725 and 193,174 for `jungle` were 89 and 755 in the plan's unit, under the target: see the phase file's issue 1.
