# compile/src/game_tables

**Purpose:** turns what `config/` says about the game into C tables, in `build/<preset>/generated/tables/`, refusing any file with a field or a reference it does not know.
**Tier:** Owner (depth 1 of `compile/src`).
**Owns:** one mapped process arena. Its part is given it.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is a command line, and `compile/include/game_tables/index.h` holds its exit statuses:

- `game_tables --config config --textures textures --out <generated>/tables`: writes one directory per table. Exit 0, or 1 with every problem on stderr as `path:line:column: message` and nothing written, or 2 on a bad command line.

**May be called by:** `cmake/generated.cmake`, and ctest `compile_game_tables`.
**Depends on:** `blocks/`, `items/`, `biomes/`, `compile/src/texture_pack/support/`.

## Parts

| Part | Reads | Writes |
|---|---|---|
| `blocks` | `config/blocks/` | `tables/blocks/index.h`, `index.c`. Its README has the fields and the table |
| `items` | `config/items/`, and which blocks exist in `config/blocks/` and textures in `textures/` | `tables/items/index.h`, `index.c`. Its README has the table, and `config/items/README.md` the fields |
| `biomes` | `config/biomes/`, and which blocks exist in `config/blocks/` | `tables/biomes/index.h`, `index.c`. Its README has the table, and `config/biomes/README.md` the fields |

`config/{recipes,mobs}` get their parts when their stages arrive. The game includes `"tables/blocks/index.h"`, `"tables/items/index.h"` and `"tables/biomes/index.h"` from the generated root; nothing under `compile/` is linked into it.
