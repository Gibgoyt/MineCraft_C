# bench/mobs

**Purpose:** times one sim tick of every mob against the plan's target of at most 5 ms at 100 mobs, of the 50 ms a tick has.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its `world_t`, `physics_t`, `entity_t` and samples, released before it exits, with what those reserve.
**Lifetime:** Process.
**Threads:** main, as the sim.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 300 build/release/bin/bench_mobs`, so no build shares the CPU. Exit status 0, 1 if a gated row missed the target or touched the heap, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `entity`, `physics`, `world`, `heap`, `time`, `terrain/rng`, and the generated `tables/blocks`. No card.

Only a release build's numbers mean anything: debug is at `-O0`.

A tick here is what stands for a mind and then all of `entity_tick`. The mind so far: every mob asks the grid who is within 8 blocks, turns now and then, and jumps at a wall or in water. Then every stack of items and mob moves through the world, the grid is built, overlaps shove, and strides go on. The world is a stone floor of 104 by 104, with 1500 walls, steps, slabs and pools on it, and a walled pit in the middle.

| Row | Gated | What it asks |
|---|---|---|
| `scattered, the target` | yes | 100 mobs around the whole floor: the plan's number |
| `packed in a pit` | yes | The same 100 in a pit 7 blocks a side, each overlapping others: the worst 100 can do to the shove and the grid |
| `scattered` | no | 400, to show how it grows |
| `scattered, a full store` | no | 1024 |

The gate is on the 99th percentile of 2000 ticks, after 200 to settle: the worst single tick on a shared machine says more about the machine. It prints the worst too.

Stages 15.2 to 15.7 add what a mob really thinks, and the phase gate's number is only final once this bench runs that: each should add its work to `bench_think`.

## Results

### 2026-11-03, Stage 15.1: bodies only, no mob AI yet

`build/release/bin/bench_mobs`, exit status 0, on the shared machine under the build lock, in milliseconds a tick. This is not yet the phase gate's number: pathfinding and each mob's behaviour are not in it.

| Row | Mobs | Mean | Median | p99 | Worst |
|---|---|---|---|---|---|
| `scattered, the target` | 100 | 0.055 | 0.053 | 0.102 | 0.116 |
| `packed in a pit` | 100 | 0.064 | 0.061 | 0.116 | 0.157 |
| `scattered` | 400 | 0.365 | 0.360 | 0.443 | 0.625 |
| `scattered, a full store` | 1024 | 1.499 | 1.482 | 1.720 | 2.364 |

No heap calls in any row.
