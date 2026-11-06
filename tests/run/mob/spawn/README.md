# tests/run/mob/spawn

**Purpose:** ctest `run_mob_spawn`, named by its path. Tests `run/src/mob/spawn/` and the generated `tables/mobs/`, on a real `entity_store_t`, in a real `world_t` from `tests/run/physics/fixtures/test_world.h`.
**Tier:** test.
**Owns:** the test arena, and what its world and `entity_t` reserve, with a spawn arena of its own over test memory, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/mob/spawn/`, `run/src/entity/`, `run/src/physics/`, `run/src/world/`. No card, no display and no input device.

What it checks:

- **The tables:** what the files say, each mob's rig being its own rows, its drops and its `never_on`.
- **The sky:** the darkening by the sun's height (plan 004 phase 02): overhead, at the horizon, straight down and on the way, phase 02's worked examples, and on the equator at the equinox the same as the old curve by the time of day, at 100,001 times of day.
- **Each rule at its edge, by hand:** distance at 24 and 128 in three dimensions; a column not loaded; every kind of bad floor; block light; sky light at each roll and time of day; a ceiling at the height of each mob; water and lava at the feet and at the head; a plant in the way being no matter.
- **The tick:** in a real world with real light: an open field by day stays empty; the same field at midnight fills to the cap, and every body it made passed the rules and is on the floor; a roofed cave fills by day, and never within a torch's light; the cap by columns, and a store that is full; weights and packs; the same calls give the same bytes, and another seed others.
- **Despawning:** past `far` at once; within `near` never, with the idle count starting over; between, not before `idle_ticks` and then at about one in `one_in`; a waiting body neither counts nor rolls; other kinds left alone; the handles of those left still good; a slot used again starting over.
- **The cost:** no heap and no more arena in a tick; starting again from a seed is empty, and does what it did; a tick well under its share of the budget.
