# config/ores

One directory per ore or rock that lies in veins, and to see where it spawns, open its one file:

```yaml
# config/ores/copper/index.yaml
name: copper
block: copper_ore
deep_block: deepslate_copper_ore
drops: raw_copper
order: 140
z_min: -185
z_max: 96
vein: 10
per_256_chunks: 8192
spread: triangle
```

To add an ore: make its two blocks in `config/blocks/` and what it drops in `config/items/`, make `config/ores/<name>/index.yaml`, and build. `compile/src/game_tables/ores/` turns these into the table `run/src/terrain/ore/` lays veins from, and refuses a field it does not know or a block that does not exist, with the line. **Changing any number changes every world**, and the terrain tests' pinned digests say so: `run/src/terrain/ore/README.md` says what each number does and where the first ones came from.

## Fields

| Field | Needed | Values | Default |
|---|---|---|---|
| `name` | yes | The directory's name: lower case, digits, underscores | |
| `block` | yes | A block: what a vein makes in stone, granite, diorite and andesite | |
| `deep_block` | | A block: what it makes in deepslate and tuff | `block`, which makes it a rock: it then replaces only stone and deepslate |
| `drops` | | An item, which both blocks must list in their own `drops`. Mining is decided there; this says it here for the reader | None |
| `veins` | | A list of rows, below, for an ore with more than one range of depths | One row, from the fields at the top, named for the ore |

### A row

In a file of one row these are at the top. In a file with `veins`, each entry has them, and `row`, and none are at the top.

| Field | Needed | Values | Default |
|---|---|---|---|
| `row` | in `veins` | Lower case, digits, underscores: the row's stream is `ore/<row>`, so renaming a row moves every vein of it | |
| `order` | yes | 0 to 1000000, different for every row of every ore: rows are laid down lowest first, each replacing what the last left | |
| `z_min`, `z_max` | yes | -1250 to 1250, `z_min` below `z_max`, both included. A `z_min` below the world, -250, is allowed: a vein that lands outside the world is lost | |
| `vein` | yes | 1 to 255: the voxels a vein aims for | |
| `per_256_chunks` | yes | 0 to 1048576: veins tried in 256 chunks of 16 by 16, so 256 is one a chunk | |
| `spread` | | `uniform`: the same at every z. `triangle`: most at the middle, none past the ends | `uniform` |
| `min_column_height` | | -1250 to 1250: only in a chunk whose middle column is at least this high | Any column |

A new ore goes last, with the highest `order`: an ore replaces only rock, never another ore, so a row laid last leaves every vein before it where it was.
