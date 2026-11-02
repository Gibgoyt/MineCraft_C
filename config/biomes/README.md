# config/biomes

One directory per biome, and to see what a biome is, open its one file:

```yaml
# config/biomes/taiga/index.yaml
name: taiga
where: land
climate: { temperature: [-0.45, -0.15], humidity: [0.15, 0.4], height: [-1000, 110] }
surface:
  top: grass_block
  filler: dirt
  patches:
    - { block: podzol, from: 0.45, to: 1 }
features: { tree_spruce: 8, grass: 2, fern: 3, mushroom: 0.2 }
```

To add a biome: make `config/biomes/<name>/index.yaml`, and build. `compile/src/game_tables/biomes/` turns these into the tables, and refuses a field it does not know or a block that does not exist, with the line. `run/src/terrain/biome/README.md` says how a biome is chosen, and `run/src/terrain/surface/README.md` what each rule does.

## Fields

| Field | Needed | Values | Default |
|---|---|---|---|
| `name` | yes | The directory's name: lower case, digits, underscores | |
| `where` | yes | `land ocean deep_ocean river beach stony_shore underground`: the kind of place it can be | |
| `priority` | | 0 to 255: wins among boxes the column is equally near | 0 |
| `climate` | | A map of ranges, below | All of each |
| `surface` | unless `where: underground` | A map, below | |
| `cave` | if `where: underground` | A map, below | |
| `features` | | A map of feature to tries per 16 by 16 chunk, from 0 to 256 | None |

### Climate

Each is `[low, high]`, low below high, both ends in the range.

| Field | Values | Means |
|---|---|---|
| `temperature`, `humidity`, `continentalness`, `erosion`, `weirdness` | -1 to 1 | `run/src/terrain/climate/README.md` says what each means. About `(v + 1) / 2` of the world is below a value `v` |
| `height` | -1000 to 1000 | The ground, in blocks. Not for `underground` |
| `z` | -1000 to 1000 | The voxels it can hold, and only for `underground` |

### Surface

| Field | Needed | Values | Default |
|---|---|---|---|
| `top` | yes | A block: the top of dry ground | |
| `filler` | yes | A block: under the top | |
| `filler_depth` | | 0 to 16 layers. The column's noise adds up to 3 when this is not 0 | 3 |
| `under`, `under_depth` | together | A block, and the layers of it under the filler | None |
| `wet_top`, `wet_depth` | together | A block, and how many layers it covers under water, over whatever hydrology laid there | None |
| `steep` | | `{ block, from }`: bare block, from this steepness up, in blocks of ground per block across | None |
| `high` | | `{ block, from }`: the top, at or over this `z` | None |
| `bands` | | `{ from, depth }`: terracotta in stripes at or over this `z`, this many layers deep | None |
| `patches` | | Up to 4 of `{ block, from, to }`: in place of `top` or `high`, where the column's patch noise is in the range, from -1 to 1. The first that holds wins | None |

`1 + filler_depth + 3 + under_depth` and `bands.depth` can be at most 16.

### Cave

| Field | Needed | Values |
|---|---|---|
| `floor`, `ceiling` | yes | Blocks: the stone under a cave, and over one |
| `patches` | | As for `surface`, in place of `floor` |

### Features

Each says how many a 16 by 16 chunk tries, except `snow` and `ice`, which above 0 are on for every column of the biome, and `vine`, which is shared out among the biome's trees. `run/src/terrain/feature/README.md` says what each does: `tree_oak tree_birch tree_spruce tree_jungle tree_acacia tree_dark_oak tree_mangrove tree_cherry grass fern flower cactus sugar_cane dead_bush mushroom lily_pad vine melon pumpkin seagrass kelp snow ice`.
