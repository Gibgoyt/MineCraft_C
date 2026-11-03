# bench/mobs

**Purpose:** times one sim tick of every mob against the plan's target of at most 5 ms at 100 mobs, of the 50 ms a tick has.
**Tier:** none; benchmarks are not tiered.
**Owns:** one heap region for its `world_t`, `physics_t`, `entity_t`, `mob_t`, `job_system_t`, the player's two parts and samples, released before it exits, with what those reserve.
**Lifetime:** Process.
**Threads:** main, as the sim, and the real workers, which run the searches as in the game.
**Public functions:** `main`. Run `tools/test-or-build.sh timeout 300 build/release/bin/bench_mobs`, so no build shares the CPU. Exit status 0, 1 if a gated row missed the target, touched the heap, lost count of its mobs or saw the minds do nothing, or 2 if it could not start.
**May be called by:** people.
**Depends on:** `mob` and its parts, `entity`, `physics`, `world`, `job`, `thread`, `player/movement`, `player/survival`, `heap`, `arena`, `time`, `terrain/rng`, and the generated `tables/blocks` and `tables/mobs`. No card.

Only a release build's numbers mean anything: debug is at `-O0`.

A tick here is everything a mob is, in the order `game_sim_tick` and `game_items_tick` run it, with the real calls: `mob_tick` (the searches, on the real workers), `mob_minds_tick` (zombies), `mob_spawn_tick`, `mob_skeleton_tick`, `mob_creeper_tick` (which bursts, breaking blocks through `world_edit`), `entity_tick`, `mob_combat_tick` and `projectile_tick`. It is a copy of that order, as those functions of `run/src/game/` are private to a game with a card: a change there should change `bench_tick`. Thirds zombies, skeletons and creepers, on a stone floor of 104 by 104 with 1500 walls, steps, slabs and pools on it and a walled pit in the middle, at midnight. The player walks round in a circle so that no answer stays good; it is healed every tick, so that it is always there to chase. What is not timed: the player's own tick, the world's light, and putting back what burst or fell through a hole, so that the count holds. The floor is made again for each row.

| Row | Gated | What it asks |
|---|---|---|
| `scattered, the target` | yes | 100 mobs around the whole floor: the plan's number |
| `packed in a pit` | yes | The same 100 in a pit 7 blocks a side with the player, each overlapping others: the worst 100 can do to the shove and the grid, with every mind awake |
| `all around the player` | yes | The same 100 within 12 blocks of the player in the open, and of where it goes: the most searches |
| `scattered` | no | 400, to show how it grows |
| `scattered, a full store` | no | 1024 |

The gate is on the 99th percentile of 2000 ticks, after 200 to settle: the worst single tick on a shared machine says more about the machine. It prints the worst too, and under each row what the minds did: a row where they did nothing fails, as a fast number would then say nothing.

## Results

### 2026-11-03, phase gate: every mind, in the game's order

`build/release/bin/bench_mobs`, exit status 0, from a clean build, on the shared machine under the build lock, in milliseconds a tick. This is the phase gate's number.

| Row | Mobs | Mean | Median | p99 | Worst | What the minds did |
|---|---|---|---|---|---|---|
| `scattered, the target` | 100 | 0.181 | 0.038 | 1.291 | 2.402 | 2053 searches, 3.6 zombies chasing, 5 blows, 27 arrows, 3 bursts |
| `packed in a pit` | 100 | 0.721 | 0.133 | 2.461 | 2.514 | 7075 searches, 14.3 chasing, 322 blows, 2732 arrows, 28 bursts |
| `all around the player` | 100 | 0.371 | 0.112 | 2.351 | 2.544 | 5692 searches, 10.1 chasing, 47 blows, 336 arrows, 16 bursts |
| `scattered` | 400 | 0.595 | 0.263 | 2.467 | 2.576 | 7256 searches, 12.7 chasing, 32 blows, 159 arrows, 14 bursts |
| `scattered, a full store` | 1024 | 1.144 | 1.548 | 2.720 | 2.887 | 10721 searches, 19.2 chasing, 70 blows, 438 arrows, 20 bursts |

No heap calls in any row. The p99 of about 2.4 ms is mostly a whole round's search budget, `MOB_PATH_ROUND_NODES`, spent on workers. A debug build at `-O0` misses the target at about 12 ms, which is no gate.

### 2026-11-03, Stage 15.1: bodies only, no mob AI yet

`build/release/bin/bench_mobs`, exit status 0, on the shared machine under the build lock, in milliseconds a tick. This is not yet the phase gate's number: pathfinding and each mob's behaviour are not in it.

| Row | Mobs | Mean | Median | p99 | Worst |
|---|---|---|---|---|---|
| `scattered, the target` | 100 | 0.055 | 0.053 | 0.102 | 0.116 |
| `packed in a pit` | 100 | 0.064 | 0.061 | 0.116 | 0.157 |
| `scattered` | 400 | 0.365 | 0.360 | 0.443 | 0.625 |
| `scattered, a full store` | 1024 | 1.499 | 1.482 | 1.720 | 2.364 |

No heap calls in any row.
