# tests/run/mob/path

**Purpose:** ctest `run_mob_path`, named by its path. Tests `run/src/mob/` and `run/src/mob/path/`, on a real `world_t` from `tests/run/physics/fixtures/test_world.h`.
**Tier:** test.
**Owns:** the test arena, and what its world, `entity_t`, `mob_t` and `job_system_t` reserve, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main, and real workers in the tests that say so.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/mob/`, `run/src/entity/`, `run/src/job/`, `run/src/physics/`, `run/src/world/`. No card, no display and no input device.

What it checks:

- **Each move, by hand:** a straight line point by point; diagonals; a wall walked round without cutting its corners, to the cost; a block jumped and two not; room overhead for a jump, here and there; a fall up to what was asked, and the most any ask may have; a slab up and down, and stairs; lava and cactus; a pool swum and got out of; a ladder both ways; a column not loaded.
- **The bounds:** a budget of nodes, down to one; the reach; a way too long for its points; a shut room searched to its walls and no more; a start inside a block, or too far over the ground; a radius; places in the air.
- **Against a separate search:** over random ground, the cost of every way against a plain shortest-path search written here from the table in the module's README and sharing nothing with it, and every step of the way against that table.
- **The cache:** the same ask runs no second search and gives the same bytes; a body on a way gets the rest of it; a partial way is never given that way; an edit far away, or one no body could tell, spoils nothing, and one in the way does. **Through 4000 random steps of edits and asks, every exact answer is byte for byte what a search run then gives, and every answer by the rest of a way costs what a search run then finds.** The longest unused goes first; too many edits at once empty it; a column going keeps whole ways that do not need it, and drops the rest.
- **The round:** bodies asking the same share one search; a new ask replaces the old and keeps its turn; an old handle finds nothing; the budget in searches and in nodes, with the rest keeping their turn; the same bytes from workers as from none, with workers shown to have run; every worker held busy, and the tick still finishes, on its own thread, with a late job finding nothing.
- **Real bodies through real physics:** what it finds is walked by `mob_path_follow` through `entity_tick`, round a wall, over blocks, off a ledge, up a slab and stairs, through a pool, up a ladder; a block put on the way, stuck, and asked again; a crowd shoving; what has nothing to walk.
- **The Owner:** the region given back, a world opened again with nothing known, the arena within what the `static_assert` counted on, and no heap in a tick. A whole round's budget is timed and printed, not judged here.
