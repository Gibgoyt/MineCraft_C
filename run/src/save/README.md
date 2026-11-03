# run/src/save

**Purpose:** a world kept on disk: the memory its parts borrow, and the one world open at a time.
**Tier:** Owner.
**Owns:** `owned_region`, with `arena_world` over it, reset each time a world closes.
**Lifetime:** Process for the region; World for what is in it.
**Threads:** `main` for init and deinit. `game/session` opens and closes a world, with no game open. Past that, see `world/README.md`.
**Public functions** (`run/include/save/index.h`):

- `save_init`, `save_deinit`: reserve and release the region. Deinit closes any world still open, and is safe on a zeroed `save_t`.
- `save_open`, `save_close`: start and end one world, in one directory.

**May be called by:** `run/src/main/`, which holds it, and `game`, which opens and runs it.
**Depends on:** `heap`, `arena`, and its parts below.

## Parts

| Part | Job |
|---|---|
| `world` | Seed and changed-section deltas, with versioned headers, replaced through a crash-safe rename |


