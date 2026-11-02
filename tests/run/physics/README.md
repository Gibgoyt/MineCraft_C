# tests/run/physics

**Purpose:** ctest `run_physics`. Tests `run/src/physics/` with `collision`, on a real `world_t` that `fixtures/test_world.h` fills by hand, which `tests/run/player/movement/` shares.
**Tier:** test.
**Owns:** the test arena, and what its world and physics reserve, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `run/src/physics/`, `run/src/world/`. No card.

What it checks:

- **Sweeps by hand:** each of the six directions stops exactly on the face and says which, sliding along what stopped it, resting on a floor, touching and leaving, a seam, and what a box is already inside.
- **Never through anything:** a thin wall at any speed, on the world's floor, and into a column that is not loaded.
- **Random sweeps against brute force:** a random world and random moves, each against an independent march in tiny steps through the same cells, with the final box clear of everything.
- **Every block in the table:** the boxes it collides with, against what its `config/blocks/` entry says.
- **Memory:** the pair gives back what it took, the largest move fits the region, and no move touches the heap.
