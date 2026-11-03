# run/src/entity

**Purpose:** everything in a world that moves and is not a block or the player. The Owner of `item_drop`, `store`, `grid`, `animation` and `explosion`.
**Tier:** Owner.
**Owns:** `owned_region`, 2 MiB, with `arena_world` over it: what its parts keep for as long as a world is open. A `static_assert` checks that `item_drop`, `store`, `grid` and `explosion` fit.
**Lifetime:** Process for `entity_t` and the region. World for what is in the arena: `entity_open` starts it and `entity_close` ends it, and nothing is kept until Stage 16.1 saves.
**Threads:** `main` for the pair and `entity_open`, before the threads start, and `entity_close`, after they stop. `entity_tick` on `sim`, on the `physics_t` it was given, which `player` also moves on. They take turns on one thread, so one will do.
**Public functions** (`run/include/entity/index.h`):

- `entity_init`, `entity_deinit`: reserve the region; release it. Deinit is safe on a zeroed `entity_t`.
- `entity_open`, `entity_close`: a world's start, from a seed, and its end.
- `entity_tick`: one tick of everything in the world.

**May be called by:** `run/src/main/` for the pair, and `run/src/game/` for the rest.
**Depends on:** `heap`, `arena`, `physics`, `world/map`, and its part below.

**Parts:** `item_drop`: dropped items. `store`: every body with a mind of its own, one array per field, and how a body moves. `grid`: which bodies are near a place, and how two that overlap shove each other. `animation`: what a body's parts are and where they are. `explosion`: what a blast breaks and hits; `entity_tick` does not call it, and whatever blows up does.

A tick moves the items, then the bodies, and then builds the grid, so that what a mob asks next tick is of where everyone ended this one, and shoves and strides follow from that.
