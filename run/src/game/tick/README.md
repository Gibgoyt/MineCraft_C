# run/src/game/tick

**Purpose:** `game_sim_tick`: one tick of the world, in its order: first the weekly census of plan 004 phase 01 (`game_census_check`, which may stop the world before the tick runs), then the hands, what they spill, minds and bodies, drops and arrows, the player and what hurts it, the world's own tick, and what is published for the picture and the HUD to read.
**Tier:** Arena by depth, and it allocates nothing but from `game`'s tick arena, which it resets.
**Owns:** nothing.
**Lifetime:** Nothing is kept here.
**Threads:** the sim thread, or the one thread of a run with no threads.
**Public functions** (declared with the rest of `game`, in `run/include/game/index.h`): `game_sim_tick`, `game_spill`, `game_census_take` (main's: the census latch, by exchange).
**May be called by:** `run/src/game/`.
**Depends on:** `game`'s header, `game/weather` (plan 004 phase 08: the weather's tick before the body moves, and the wind it moves in), `player`, `item`, `entity`, `mob`, `ui`, `render/entities`, `world`, `profiler`, and the generated block table.
