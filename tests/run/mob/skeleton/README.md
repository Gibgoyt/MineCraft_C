# tests/run/mob/skeleton

**Purpose:** ctest `run_mob_skeleton`, named by its path. Tests `run/src/mob/skeleton/` and `run/src/entity/projectile/`, on a real `world_t` from `tests/run/physics/fixtures/test_world.h`, with a real `entity_t` and `mob_t`, ticked in the order `run/src/game/` ticks them.
**Tier:** test.
**Owns:** the test arena, and what its world, `entity_t` and `mob_t` reserve, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/mob/`, `run/src/entity/`, `run/src/physics/`, `run/src/world/`, and for the last test `run/src/game/` and what it runs on. All but the last test need no card. None needs a display or an input device.

What it checks:

- **Flying:** position and velocity to the published sum, tick by tick; how far astray a shot goes.
- **Blocks:** where it sticks; no way through a wall at any speed or slant; a slab's half; a plant; water's drag; lava; lasting its time, and falling when its block goes; waiting where a column is not loaded.
- **Hits:** a body's health and knockback; a slower arrow's less; the margin at its edge, either side; its shooter and a dead body not being hit; the first of a wall, a body and the target along its way; no target; the published knockback sum; a full store; the books adding up.
- **Sight:** a line through a plant and water, and not through glass or into a column not loaded.
- **Keeping its distance:** stepping back, directly and at full speed; staying between `NEAR` and `FAR`; circling at half speed; coming on from outside `FAR`; a path from past its range. A cliff, lava, water and a wall behind it, none of which it backs into.
- **The bow:** the exact ticks of the first shots; hits on a target in the open from where it really stands, by its real arrows; not shooting through a wall or past its sight; forgetting, and its arms coming down; strolling with no target; a body waiting for its column; swimming.
- **Dying:** drops by the table, in range and on average; one with no health gone, with its drops; an arrow killing one; other kinds left alone; a slot used again.
- **The cost:** the same calls giving the same bytes and another seed others; no heap and no more arena in a tick; 70 skeletons in a fight inside the phase's budget; a world opened again being empty.
- **In a real game:** on the card, through `run/src/game/`'s own tick with a real player: a skeleton's shot goes into the snapshot of what is drawn, its arrows hurt the player, and with no health it is gone with its drops.
