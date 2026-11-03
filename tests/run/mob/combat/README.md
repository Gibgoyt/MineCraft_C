# tests/run/mob/combat

**Purpose:** ctest `run_mob_combat`, named by its path. Tests `run/src/mob/combat/`, `run/src/player/combat/`, and what `run/src/player/` and `run/src/game/` do with them, on a real `entity_t`, `mob_t` and `player_t` in a real `world_t` from `tests/run/physics/fixtures/test_world.h`, and once in a whole real game.
**Tier:** test.
**Owns:** the test arena, and what its world, `entity_t`, `mob_t` and, in the last test, the game reserve, given back by the end of each test.
**Lifetime:** One run.
**Threads:** main. Paths are searched on it, with no job system, but for the real game's, on its real workers.
**Public functions:** none; a test program.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `tests/run/physics/fixtures/`, `run/src/mob/`, `run/src/entity/`, `run/src/player/`, `run/src/item/`, `run/src/world/`, and for the last test `run/src/game/`, `run/src/render/`, `run/src/gpu_bridge/` and the real card, compute only. No display and no input device.

What it checks:

- **A swing:** every tool's speed against the published table, and strength coming back by hand-worked numbers.
- **The aim:** the nearest body by hand-worked distances, the edge of reach on each side, to one side, over a head, down at an angle, an eye inside a box; a wall, tall grass and a column not loaded shielding; a dead or waiting body not there.
- **The button:** a press swings once and a hold does not; a click between two ticks counts; nobody at the controls, a dead body, and a screen up do not hit, and a press that brought a body back is not a new one; a weak swing's damage by hand; a miss and a change of hand costing the swing, and a press on a block not; a critical hit and every reason it is not one; a sprint's push, and what it costs the sprinter.
- **The hands:** the button on a body does not mine what is behind it, and mines it once the body has gone, and a run that does not ask about bodies only mines.
- **A hit:** its size, and the ten ticks after by every rule, at the ninth and tenth tick; health stopping at 0; a body already dead.
- **A push:** by hand on the ground and in the air, its limit going up, with no strength and no direction; a blow's from each side, and only with a whole hit; a sprint's on top of it; from on top of the body; a real body flying and landing, and stopped by a wall; a zombie really chasing, knocked back and coming on.
- **Falls and lava:** by the players' sum on real falls, on the tick it lands only; a long fall killing; lava every ten ticks, water not at all; a waiting body not hurt, with no time passing for it; a slot used again starting over.
- **Death:** each kind killed by a blow, gone on its mind's next tick, with its drops on the books; a creeper killed with its fuse lit not bursting.
- **Same fight, same bytes,** another seed another; **no heap and no more arena;** **the cost** against the phase's budget, on the plan's 100 mobs; a world opened again.
- **A real game,** through `game_headless_scene`'s tick and a script's button: the sword in hand, a blow's damage and wear, tiring, the push, nothing mined behind, three blows to kill a zombie, its drops in the world, and then the same button mining the wall.
