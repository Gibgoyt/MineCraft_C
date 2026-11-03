# run/src/mob

**Purpose:** the minds of what walks a world. The Owner of `path` and `skeleton`, and of the parts Stages 15.3 to 15.7 add.
**Tier:** Owner.
**Owns:** `owned_region`, 4 MiB, with `arena_world` over it: what its parts keep for as long as a world is open. A `static_assert` checks that `path`, `zombie`, `skeleton`, `creeper` and `combat` fit. Untouched pages cost nothing.
**Lifetime:** Process for `mob_t` and the region. World for what is in the arena: `mob_open` starts it and `mob_close` ends it.
**Threads:** `main` for the pair, `mob_open` before the threads start and `mob_close` after they stop. `mob_tick` and `mob_minds_tick` on `sim`; `mob_tick` may hand searches to workers for the length of the call.
**Public functions** (`run/include/mob/index.h`):

- `mob_init`, `mob_deinit`: reserve the region; release it. Deinit is safe on a zeroed `mob_t`.
- `mob_open`, `mob_close`: a world's start and end. Close waits for any job still out.
- `mob_tick`: one tick of its searches, which answer the minds.
- `mob_minds_tick`: one tick of what every zombie does, returning the blow what is in view takes. After `mob_tick`, before the bodies move.

**May be called by:** `run/src/main/` for the pair, and `run/src/game/` for the rest.
**Depends on:** `heap`, `arena`, `job`, `world`, `entity`, `terrain/rng`, and its parts below.

**Parts:** `path`: bounded A\*, batched on workers, with a cache. `zombie`: chase, melee and drops. `skeleton`: keeps its distance, shoots and drops. `creeper`: its approach, its fuse, and what it leaves. `run/src/game/` ticks the last two after `mob_tick`, as they need the entity store and the player. `combat`: damage, knockback, and the health reaching 0, which each mind above then acts on; `run/src/game/` gives it the player's blows, and ticks it after the bodies move.
