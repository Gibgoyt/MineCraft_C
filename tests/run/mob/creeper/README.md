# tests/run/mob/creeper

**Purpose:** ctest `run_mob_creeper`, named by its path. Tests `run/src/mob/creeper/` and `run/src/entity/explosion/`, on a real `entity_t`, `mob_t` and player body, in a real `world_t` from `tests/run/physics/fixtures/test_world.h`.
**Tier:** test.
**Owns:** the test arena, and what its world, `entity_t` and `mob_t` reserve, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main. Paths are searched on it, with no job system.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/mob/`, `run/src/entity/`, `run/src/player/movement/`, `run/src/player/survival/`, `run/src/physics/`, `run/src/world/`. No card, no display and no input device.

What it checks:

- **Resistance:** each kind of published number, and that every block has one.
- **Sight:** a line through open air, a wall, a plant, water, a slab over and through, a face touched, a start inside a block, a column not loaded, and above the world.
- **Exposure:** all and none, half a body behind half a wall, and its grid for a player.
- **A blow, by hand:** damage and push worked out on paper at given distances; out of reach; behind a wall it is 1 with no push; eyes at the middle.
- **What breaks:** a crater in dirt, each cell listed once and none out of reach; stone less than dirt; obsidian, bedrock, water and lava never; a wall shielding what is behind it; a column not loaded; the top of the world; the strongest blast staying in the list's bounds; the same seed the same list, and another seed another.
- **What goes with it:** a torch and plants on what broke; one block in `power` giving drops, counted, and a torch on a wall giving its torch at any power; a bench, listed by either half or both, going whole, giving one, and taking what stood on or hung from it, never half of one from real rays, and standing if its other half is not loaded; a chest's block going through the edit log.
- **The store and the items:** hurt and pushed by hand-worked numbers, health stopping at 0, a waiting body and one far off left alone; a near item gone and counted, so the store's sums add up; a further one flying; a blast's own drops surviving it.
- **The creeper, on real ticks:** noticing only in sight and in reach; walking to the player, and around a wall; the fuse lit under 3 at exactly 30 ticks, holding still with the swell going up; put out past 7 and out of sight, running back; a dead player ignored; the burst's blocks, the player's damage and push, and that a burst leaves no gunpowder; a killed creeper leaving 0 to 2 gunpowder, about one on average, with no blast; killed by another's blast and leaving its drops; 16 bursts in a tick and the rest the next; strolling and stopping; a waiting body holding; a slot used again starting over.
- **Same input, same bytes,** another seed others; **no heap and no more arena** in a tick or a blast; **the cost** of a blast and of a crowd's tick against the phase's budget.
