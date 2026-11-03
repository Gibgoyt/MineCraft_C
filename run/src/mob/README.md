# run/src/mob

**Purpose:** the minds of what walks a world. The Owner of `path`, and of the parts Stages 15.3 to 15.7 add.
**Tier:** Owner.
**Owns:** `owned_region`, 4 MiB, with `arena_world` over it: what its parts keep for as long as a world is open. A `static_assert` checks that `path` and `creeper` fit. Untouched pages cost nothing.
**Lifetime:** Process for `mob_t` and the region. World for what is in the arena: `mob_open` starts it and `mob_close` ends it.
**Threads:** `main` for the pair, `mob_open` before the threads start and `mob_close` after they stop. `mob_tick` on `sim`, which may hand searches to workers for the length of the call.
**Public functions** (`run/include/mob/index.h`):

- `mob_init`, `mob_deinit`: reserve the region; release it. Deinit is safe on a zeroed `mob_t`.
- `mob_open`, `mob_close`: a world's start and end. Close waits for any job still out.
- `mob_tick`: one tick of what answers the minds. `run/src/game/` ticks the minds themselves after it, since they need the player.

**May be called by:** `run/src/main/` for the pair, and `run/src/game/` for the rest.
**Depends on:** `heap`, `arena`, `job`, `world`, and its part below.

**Parts:** `path`: bounded A\*, batched on workers, with a cache. `creeper`: its approach, its fuse, and what it leaves.
