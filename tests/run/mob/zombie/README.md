# tests/run/mob/zombie

**Purpose:** ctest `run_mob_zombie`, named by its path. Tests `run/src/mob/zombie/` and `mob_minds_tick`, with real bodies through real physics and pathfinding, in a real `world_t` from `tests/run/physics/fixtures/test_world.h`, and the built program once.
**Tier:** test.
**Owns:** the test arena, and what its world, `entity_t`, `mob_t` and `job_system_t` reserve, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main, and the job system's workers in one test.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/mob/`, `run/src/entity/`, `run/src/player/survival/`, `run/src/world/`, and `minecraft_c` as its one argument, always `--headless`.

What it checks:

- **Sight:** what hides and what does not, in both directions and on the negative side; a slab; a column not loaded; and random lines through random blocks, against a slab test of every cube that knows nothing of the walk.
- **Reach:** each edge by hand, sideways and up and down.
- **Noticing:** about one in ten per tick; not behind a wall, past its range, too far up or down, or when dead; yes at the edge of each.
- **Strolling:** with nothing to chase it moves and stops by turns, staying about, seldom asking for a path; a stroll that can go nowhere ends, and it still notices.
- **The chase:** straight, at its speed; stopping short and facing it; round a wall out of sight; up blocks; after something walking away; round a wall built on the way; towards what it cannot get to, seldom asking.
- **Giving up:** on death, on range, and out of sight, with the count starting over when seen again.
- **The blow:** at once in reach, then exactly 20 ticks apart; never through a wall built in the way; two in a tick as one; what `player_survival_hurt` makes of it, with seven killing and armor helping.
- **What it leaves:** rows lying as the right item; each row's chance and counts over 6000 deaths; only zombies die here, several in a tick, with handles still good; nothing left by a body taken away otherwise.
- **The rest:** a slot used again knows nothing; a body waiting for its column does nothing; the same bytes twice, and with workers; another seed others; a hundred chasing take no heap and no arena, with what a tick took printed.
- **The program:** at midnight in a real world, on the real rules, mobs come, the report says what they did, and it exits with nothing left on the heap. Whether a zombie gets to a player standing on this seed's mountain is the seed's business: a blow through the whole game is in `run_mob_combat`.
