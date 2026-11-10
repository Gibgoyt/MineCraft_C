# tests/run/mob/animal

**Purpose:** ctest `run_mob_animal`, named by its path. Tests the animals of plan 005 phase 4: a body's sex in `run/src/entity/store/`, a sex's boxes and size in `run/src/entity/animation/`, the herds, their cap and the store's limit in `run/src/mob/spawn/`, and the passive mind and what an animal gives alive in `run/src/mob/passive/`, on the six animals of `config/mobs/`, the pig most, in a real `world_t` as `tests/run/physics/fixtures/test_world.h` makes one, opened 9 chunks wide so that the animals' cap is 8 and not 1.
**Tier:** test.
**Owns:** the test arena, and what its world, `entity_t` and `mob_t` reserve, with a spawn arena of its own over test memory, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main. Path searches run on the sim, with no job system.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/mob/`, `run/src/mob/spawn/`, `run/src/entity/`, `run/src/physics/`, `run/src/world/`, and the generated tables. No card, no display and no input device.

The tests that want herds of pigs alone pass weights in `mob_spawn_view_t.weights`, as `tests/run/mob/spawn/` does for the turned-off hostile mobs. `proof/` is the scripted headless proof in a whole game, with pictures.

What it checks:

- **The tables:** the pig is passive, comes by itself, never despawns, comes by day on grass in its seven biomes, and has a boar (1.25 times, its own dark body, tusks and ridge after the pig's tail, a pork of its own) and a sow; every sex of every mob fits what physics takes. Each of the six has a bull, ram, boar, billy or rooster bigger than its female (15 to 30%, a rooster 50%) with more boxes, a 128 by 64 skin, and a weight.
- **A body:** 4000 pigs are about half male, each male's box 1.25 times a female's, and 2000 of each of the six about half male too; a zombie is neither and draws no number for it; a sex stays with its body when another is removed; a boar's boxes are the pig's tail and its own ten, all 1.25 times, where a sow's are the tail and seven at 1.
- **The rules:** grass at noon passes and at midnight is too dark; not nearer than 24, but on the first day as near as 12; no far; stone is not grass; the biomes allow and refuse. Mountain goats come only in the four peak and slope biomes, and not on stone at the sea, where a goat does, but on stone 320 blocks up, where cattle do not.
- **The herds:** on a field of grass at noon on the first day they fill their own cap and no more, the first of each tick's newcomers a male, with both sexes about, and none goes when the player is 5000 blocks away; with zombies weighed too, the animals' cap and the zombies' are apart, each full; on the first day the cap fills in under 50 ticks, and after it about 1 tick in 400 brings a herd, none nearer than 24; with no animal weighed nothing is drawn. **The store's limit:** with 256 animals left 2000 blocks behind, each herd that comes here forgets the furthest, and the store stays at 256 with the cap here full; with 256 left 200 to 455 behind none is forgotten and no herd comes; with 250 left behind only 6 more come.
- **The mind:** a hit pig, boar or sow, runs from the player faster than it walks for 5 seconds, then stops, and nothing ever strikes; a dead boar leaves 2 to 6 items and a sow 1 to 5, over 400 deaths, and each of the six leaves its own meat and its hide, wool or feathers and nothing else; a stray 12 blocks from its herd walks back to within 8; a tick of spawning and minds takes no heap.
- **Alive (stage 3):** a cow, a nanny goat and a mountain goat nanny give a milk bucket to a bucket, not to shears or a hand, then not again until 6000 ticks are out; their males give nothing. A hen lays an egg by herself a whole turn after she comes, then again; a rooster never. A ram or a ewe gives 1 to 3 white wool to shears (2 on average over 200), shows its skin till the wool grows back after 6000 ticks, and the wool is the sheep's one fleece box. Goats and mountain goats give no wool, alive or dead. The bed is three white wool over planks.
