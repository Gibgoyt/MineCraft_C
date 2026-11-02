# tests/run/entity/store

**Purpose:** ctest `run_entity_store`, named by its path. Tests `run/src/entity/store/`, `run/src/entity/grid/` and `run/src/entity/animation/`, through `run/src/entity/`, on a real `world_t` from `tests/run/physics/fixtures/test_world.h`.
**Tier:** test.
**Owns:** the test arena, and what its world and `entity_t` reserve, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/entity/`, `run/src/physics/`, `run/src/world/`, `terrain/rng`. No card, no display and no input device.

What it checks:

- **The store:** a body is as asked; a removal moves every array of the last body, each checked by a number of its own; a full store refuses; a freed slot comes back last with a new handle, and an old handle finds nothing; a generation wraps and never to the handle of nothing; 200000 random spawns and removals, up to full and down to empty, against a model of what each handle should find.
- **Moving:** a fall by hand, tick by tick, and what it adds up to on landing; a wish against the speed it comes to; stopping on stone and sliding on ice by the sum of what each tick keeps; a wall, to the contact; a slab walked up, a block not, a jump over it; a jump used up in the air; water by hand, lava, a bank hopped out of; a ladder holding a fall and climbed; a column not loaded and the edge of what is; 64 bodies for 2000 ticks at random among walls, slabs and pools, each box clear of everything after every tick.
- **The grid:** overlaps against touches in every direction and in negative cells; a list too short; a wide body found from a cell its middle is not in; answers of when it was built; the dead left out; 2000 queries of every size against looking at every body, with both ways of answering shown to have run; boxes huge, infinite and NaN.
- **The shove:** by hand; two in the same place, the same way each time; a crowd within its limit, with the sum of what it did at zero; one waiting left alone; a pile coming apart in a running world.
- **Animation:** every part of every rig on its texture and the lowest on the ground; a zombie standing, by hand, every box; a turn; a stride by hand with its opposite legs and a creeper's pairs; head, attack, aim and swell by hand; what a tilted part covers against its reach; the stride and the attack over ticks, by hand; a waiting body's stride kept.
- **The Owner:** the same calls give the same bytes, and another seed others; no heap in a tick; the arena no fuller, and within what the Owner's `static_assert` counted on; a world opened again is empty; a tick of nobody; a full store of 1024 ticking; the region given back.
