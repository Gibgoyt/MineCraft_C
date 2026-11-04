# run/src/game/tick

**Purpose:** `game_sim_tick`: one tick of the world, in its order: the hands, what they spill, minds and bodies, drops and arrows, the player and what hurts it, the world's own tick, and what is published for the picture and the HUD to read.
**Tier:** Arena by depth, and it allocates nothing but from `game`'s tick arena, which it resets.
**Owns:** nothing.
**Lifetime:** Nothing is kept here.
**Threads:** the sim thread, or the one thread of a run with no threads.
**Public functions** (declared with the rest of `game`, in `run/include/game/index.h`): `game_sim_tick`, `game_spill`.
**May be called by:** `run/src/game/`.
**Depends on:** `game`'s header, `player`, `item`, `entity`, `mob`, `ui`, `render/entities`, `world`, `profiler`, and the generated block table.
