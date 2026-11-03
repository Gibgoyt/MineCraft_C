# run/src/physics

**Purpose:** moves boxes through the world's blocks, for anything with a body. The Owner of `collision` and `raycast`.
**Tier:** Owner.
**Owns:** `owned_region`, 2 MiB, with `arena_boxes` over it: the boxes a move might meet, reset by every move. The region is sized for the largest box and step in `run/include/physics/index.h`, checked by a `static_assert`.
**Lifetime:** Process, with what is in the arena lasting one move.
**Threads:** `main` for init and deinit. One thread at a time for the rest, the one holding the map, which is `sim`. A thread that moved things at the same time would need a `physics_t` of its own.
**Public functions** (`run/include/physics/index.h`):

- `physics_init`, `physics_deinit`: reserve the region; release it. Deinit is safe on a zeroed `physics_t`.
- `physics_move`: moves a box by a delta through the world, in pieces of at most 8 blocks, and says how far it went and what stopped it.
- `physics_box_is_clear`: whether a box could stand there: nothing solid, and nothing not loaded.

**May be called by:** `run/src/main/` for the pair, and `player`, `mob/spawn` and `entity`, each with its parts, for the rest.
**Depends on:** `heap`, `arena`, `assert`, `world/map`, libm, and its part below.

**Parts:** `collision`: the sweep, the boxes of a block, and what a cell reads as. `raycast`: DDA block targeting, which needs no memory of `physics`'s and so takes no `physics_t`.

Neither `physics_move` nor `physics_box_is_clear` touches the heap, so both may run every tick.
