# compile/src/game_tables

**Purpose:** turns what `config/` says about the game into C tables, in `build/<preset>/generated/tables/`, refusing any file with a field or a reference it does not know.
**Tier:** Owner (depth 1 of `compile/src`).
**Owns:** one mapped process arena. Its part is given it.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is a command line, and `compile/include/game_tables/index.h` holds its exit statuses:

- `game_tables --config config --textures textures --out <generated>/tables`: writes one directory per table. Exit 0, or 1 with every problem on stderr as `path:line:column: message` and nothing written, or 2 on a bad command line.

**May be called by:** `cmake/generated.cmake`, and ctest `compile_game_tables`.
**Depends on:** `blocks/`, `compile/src/texture_pack/support/`.

## Parts

| Part | Reads | Writes |
|---|---|---|
| `blocks` | `config/blocks/` | `tables/blocks/index.h`, `index.c`. Its README has the fields and the table |

`config/{items,recipes,biomes,mobs}` get their parts when their stages arrive. The game includes `"tables/blocks/index.h"` from the generated root; nothing under `compile/` is linked into it.
