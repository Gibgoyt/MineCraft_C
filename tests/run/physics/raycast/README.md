# tests/run/physics/raycast

**Purpose:** ctest `run_physics_raycast`. Tests `run/src/physics/raycast/` and `run/src/player/camera/`, with what `run/src/player/` and `run/src/game/` do with them.
**Tier:** test.
**Owns:** the test arena, and what its world, game and card blocks take, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main; and for the last tests, the real workers and the real `sim` and `gpu` threads.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/physics/`, `run/src/player/`, `run/src/game/` and everything under it, and a card for the tests of the game.

Compute only. No real display, console or input device is opened: pictures go into plain memory.

What it checks:

- **Casts by hand:** each of the six faces at its distance, diagonals, negative coordinates and chunk borders, reach to the exact face, starting inside, touching, slabs, stairs, what is looked through and what is not solid but aimed at, what is not loaded, the world's ends, broken rays, and a place 2^25 blocks out.
- **Every block in the table:** what it can be aimed at by, against its `config/blocks/` entry.
- **Random casts against brute force:** a random world and random rays, each against a march in tiny steps through the same cells.
- **Mouse look:** the same turn as flying, the limits, and the yaw staying small over thousands of turns.
- **The aim and the picture:** the aim is the middle pixel's ray, and a point on any pixel's ray comes back to that pixel, by `traverse_camera_t`'s own formula.
- **What can be seen of a target:** the near face, the top from above, a block in front hiding pieces of it, and a flush neighbour not hiding a shared edge.
- **The outline's pixels:** lines on the seen edges and nowhere else, nothing with no target, width by size, and nothing outside the buffer from any place and any look.
- **Through the player:** the look main sent, a target reaching main with its tick, and no heap.
- **The game:** a script looking down on the real card, the outline in the shown frame and only the outline's colour changed; real threads with a turning mouse, the lines on the camera that drew that picture; and flying, showing the picture as it is.
