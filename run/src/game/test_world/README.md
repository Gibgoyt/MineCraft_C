# run/src/game/test_world

**Purpose:** the blocks of a column to fly through, until Stages 10.4 to 10.7 make the real world: the terrain's own shape, with grass, dirt, sand and snow laid over its stone.
**Tier:** Arena, and it allocates nothing: the caller says where to write.
**Owns:** nothing.
**Lifetime:** Job.
**Threads:** `worker`. It touches only what it is given, so any thread may call it.
**Public functions** (`run/include/game/test_world/index.h`):

- `game_test_world_fill`: a whole column of states.
- `game_test_world_ground_z`: the top of what is at one place, to start the eye above it.

**May be called by:** `game`.
**Depends on:** `terrain/density`, `world/column`, `world/section`, the generated block tables.

The surface is the least that lets a person tell what they are looking at, and is not a biome: under water or within two blocks of the sea it is sand, from z = 170 up it is snow straight on rock, and otherwise grass over four of dirt. When `terrain/surface` exists this goes, and the golden pictures in `tests/run/render/frame/fixtures/` are made again.
