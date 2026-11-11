# tests/run/player/interaction/door

**Purpose:** ctest `run_player_interaction_door`. Tests doors (plan 005 phase 2): `run/src/player/interaction/` and `run/src/world/door/`, with a save in `run/src/save/`. It has its own directory as the bench's does, because `tests/run/player/interaction/index.c` is past the 1500 lines where a file splits.
**Tier:** test.
**Owns:** the test arena, and what its world takes, given back by the end of each test; a directory under `/tmp` for the save test, removed at its end.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/player/`, `run/src/world/`, `run/src/save/`.

No card, no display, and the program itself is not run. What it checks:

- **The blocks:** exactly three doors, each one block with facing, half and open, its item placing it, and not solid as a block. Only the iron door stays shut by hand. Each half finds the other above or below it, facing and open alike.
- **The hinge:** by hand, a door beside goes first, then the walls, and otherwise the left. In the world, a wall on the right turns the door round, a second door to the left of it makes a pair, and a door to the left of a new one hinges the new one on the right.
- **Placing** each kind, each of the four ways: both halves at once for one item, turned by R. The plan holds both cells and voxels, is blocked by the upper cell taken, and placing does what it said. The upper or lower cell taken, or the body in either, refuses and keeps the item. A column not loaded beside it does not stop it; the top of the world does.
- **Opening:** a press on either half opens both, holding the button opens nothing more, the next press shuts both, and the button held on after placing a door leaves it shut. An iron door stays shut. An open door is aimed at by its plate turned on its hinge, sharing the hinge's corner with the shut plate. A body walking north is stopped by the shut door and goes through the open one.
- **Breaking:** either half of each kind, shut or open, takes both and drops exactly one door; a door stacked on a door stays; a half alone drops one door.
- **Saving:** an open wooden door and a shut glass door are saved and come back exactly, and the open one still shuts as one door.

What a door collides with by itself is in `tests/run/physics/`, a shut door in a mob's path in `tests/run/mob/path/`, and the doors in the built program, with screenshots, in `fixtures/door.script`.
