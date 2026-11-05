# run/src/save

**Purpose:** a world kept on disk: the memory its parts borrow, and the one world open at a time.
**Tier:** Owner.
**Owns:** `owned_region`, with `arena_world` and `arena_state` over it, each reset when a world closes.
**Lifetime:** Process for the region; World for what is in it.
**Threads:** `main` for init and deinit. `game/session` opens and closes a world, with no game open. Past that, see `world/README.md`.
**Public functions** (`run/include/save/index.h`):

- `save_init`, `save_deinit`: reserve and release the region. Deinit closes any world still open, and is safe on a zeroed `save_t`.
- `save_open`, `save_close`: start and end one world, in one directory, blocks and state. Close waits for an autosave's write under way, so it is never for a worker.

**May be called by:** `run/src/main/`, which holds it, and `game`, which opens and runs it.
**Depends on:** `heap`, `arena`, and its parts below.

## Parts

| Part | Job |
|---|---|
| `names` | A table's names as a list on disk, and what a saved list's numbers mean in this build |
| `world` | Seed and changed-section deltas, with versioned headers, replaced through a crash-safe rename |
| `state` | Player, inventory, entities and time, in a file that goes with one save of the blocks |
| `autosave` | Every 30 seconds of world time: the sim copies it all out, and a worker writes it |

