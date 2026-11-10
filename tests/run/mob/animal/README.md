# tests/run/mob/animal

**Purpose:** ctest `run_mob_animal`, named by its path. Tests what animals need (plan 005 phase 4 stage 1): a body's sex in `run/src/entity/store/`, a sex's boxes and size in `run/src/entity/animation/`, the herds and their cap in `run/src/mob/spawn/`, and the passive mind in `run/src/mob/passive/`, on the stand-in pig of `config/mobs/pig/`, in a real `world_t` as `tests/run/physics/fixtures/test_world.h` makes one, opened 9 chunks wide so that the animals' cap is 8 and not 1.
**Tier:** test.
**Owns:** the test arena, and what its world, `entity_t` and `mob_t` reserve, with a spawn arena of its own over test memory, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main. Path searches run on the sim, with no job system.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/mob/`, `run/src/mob/spawn/`, `run/src/entity/`, `run/src/physics/`, `run/src/world/`, and the generated tables. No card, no display and no input device.

The pig's file has weight 0, so the tests that want herds pass it a weight in `mob_spawn_view_t.weights`, as `tests/run/mob/spawn/` does for the turned-off hostile mobs.

What it checks:

- **The tables:** the pig is passive, never despawns, comes by day on grass in its seven biomes, and has a boar (1.2 times, two tusks after its six boxes, a drop of its own) and a sow; every sex of every mob fits what physics takes.
- **A body:** 4000 bodies are about half male, each male's box 1.2 times a female's; a zombie is neither and draws no number for it; a sex stays with its body when another is removed; a boar's boxes are the pig's six and its two tusks, all 1.2 times, where a sow's are six at 1.
- **The rules:** grass at noon passes and at midnight is too dark; not nearer than 24, but on the first day as near as 12; no far; stone is not grass; the biomes allow and refuse.
- **The herds:** on a field of grass at noon on the first day they fill their own cap and no more, the first of each tick's newcomers a male, with both sexes about, and none goes when the player is 5000 blocks away; with zombies weighed too, the animals' cap and the zombies' are apart, each full; on the first day the cap fills in under 50 ticks, and after it about 1 tick in 400 brings a herd, none nearer than 24; with no animal weighed nothing is drawn.
- **The mind:** a hit pig, boar or sow, runs from the player faster than it walks for 5 seconds, then stops, and nothing ever strikes; a dead boar leaves its 1 to 2 leather and a sow 0 to 1, over 400 deaths; a stray 12 blocks from its herd walks back to within 8; a tick of spawning and minds takes no heap.
