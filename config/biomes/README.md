# config/biomes

One directory per biome, and to see what a biome is, open its one file:

```yaml
# config/biomes/taiga/index.yaml
name: taiga
where: land
landform: { height: [-1000, 300] }
climate: { families: [boreal], wetness: [0.15, 0.4], rain_mm: [300, 1200] }
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
| `landform` | | A map of ranges of the land's shape, below | All of each |
| `climate` | yes, unless `where: underground` | The climate families it belongs to, and the ranges that choose within one, below | |
| `surface` | unless `where: underground` | A map, below | |
| `cave` | if `where: underground` | A map, below | |
| `features` | | A map of feature to tries per 16 by 16 chunk, from 0 to 256 | None |

### Landform

Each is `[low, high]`, low below high, both ends in the range.

| Field | Values | Means |
|---|---|---|
| `continentalness`, `erosion`, `weirdness` | -1 to 1 | `run/src/terrain/fields/README.md` says what each means. About `(v + 1) / 2` of the world is below a value `v` |
| `height` | -1250 to 1250 | The ground, in blocks (`terrain_biome_height`: less plan 007's plateau lift once it has one). Not for `underground` |
| `z` | -1250 to 1250 | The voxels it can hold, and only for `underground` |

### Climate

Plan 006 phase 03: a column's climate family comes first, from the climate at its own height, and only that family's biomes
are looked at (`run/src/terrain/biome/README.md`). Before, this block held noise `temperature` and `humidity` boxes and the
landform axes; the tool refuses those keys here and says where they went.

| Field | Needed | Values | Means |
|---|---|---|---|
| `families` | yes, unless `underground`; never for `underground` | A list of `ice tundra boreal continental temperate mediterranean steppe desert savanna monsoon rainforest`, or `[all]` | The families it belongs to. Every family must have a biome for every surface `where` (land, ocean, deep_ocean, river, beach, stony_shore), or the tool refuses the table, naming the family and the `where` |
| `variety` | | `[low, high]`, -1 to 1 | A noise field that picks among one family's members, such as windswept forest against windswept hills. Default all of it |
| `wetness` | | `[low, high]`, -1 to 1 | A noise field plus half of the yearly rain's bias (1000 mm is 0, 2000 and up +1, none -1), so plains against forest against dark forest. Default all of it |
| `t_warm`, `t_cold` | | `[low, high]` in °C, -100 to 100 | The warmest and coldest month at the column. Counted as distance, 10 °C an axis: past them the biome is less likely, not barred |
| `rain_mm` | | `[low, high]`, whole mm a year, 0 to 20000 | The yearly rain, counted as distance, 1000 mm an axis. Its middle is the rain plan 006 phase 04 scales the biome's trees and plants by |

Which biome is in which family, and why, is plan 006 phase 03's table, in its phase file (Issues found 1). Underground
biomes have no family (caves do not follow the surface's climate) and may give `variety` and `wetness` only.

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
