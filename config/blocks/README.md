# config/blocks

One directory per block, and to see what a block does, open its one file:

```yaml
# config/blocks/dirt/index.yaml
name: dirt
hardness: 0.5
tool: shovel
drops: [{ item: dirt, count: 1 }]
texture: { all: blocks/dirt }
sound: dirt
```

To add a block: make `config/blocks/<name>/index.yaml`, and build. `compile/src/game_tables/blocks/` turns these into the tables, and refuses a field it does not know or a reference to something missing, with the line.

**Adding a block keeps every world**, though the ids go by the alphabet and a new one moves those after it. A world keeps the names its numbers mean, and they are found by name when it opens: see `run/src/save/world/README.md` and `run/src/save/state/README.md`. Taking a block out keeps the world too: that block reads as air, and the log says so. The same goes for items and mobs. What does change with the ids is the numbers in pinned digests of terrain and in golden pictures that show them: those are a test's to say, and are not about worlds.

## Fields

| Field | Needed | Values | Default |
|---|---|---|---|
| `name` | yes | The directory's name: lower case, digits, underscores | |
| `hardness` | yes | 0 to 1000, or `unbreakable`. 0 breaks at a touch | |
| `drops` | yes | A list of rows, or `[]` for nothing. A row is below | |
| `sound` | yes | `stone wood dirt gravel sand grass leaves snow glass metal`: what mining and placing it sounds like, from `sounds/`. `none` only for air and fluids, which are never dug | |
| `texture` | unless `shape: none` | A map of faces, below | |
| `tool` | | `none pickaxe axe shovel hoe shears sword`: what breaks it fastest | `none` |
| `tier` | | `none wood stone iron diamond`: the lowest of `tool` that gets the drops at all. Needs a `tool` other than `none` or `shears` | `none` |
| `shape` | | `none cube slab stairs cross crop torch thin fluid torch_wall` | `cube` |
| `light` | | Block light given off, 0 to 15 | 0 |
| `opacity` | | Light lost passing through, 0 to 15 | 15 for `cube`, otherwise 0 |
| `solid` | | `true` if a body collides with it | `true` for `cube slab stairs`, otherwise `false` |
| `replaceable` | | `true` if placing a block here overwrites it | `true` for `none fluid`, otherwise `false` |
| `falls` | | `true` if gravity moves it | `false` |
| `decays` | | `true` for leaves, which go away far from a log | `false` |
| `facing` | | `none horizontal`: `horizontal` turns it to the four ways, and it is placed facing the way the player looks, turned by R | `none` |
| `half` | | `none upper_lower`: the block is two tall, and its state says which half: a door | `none` |
| `open` | | `true` if its state says open or shut: a door, a gate | `false` |

`facing`, `half` and `open` are what the voxel holds beside the block's id: `run/include/types/block_state/README.md` says how, and what each facing means for a shape. A shape is drawn, collides and is aimed at as it faces north, turned by the voxel's facing. A block with none of the three has no state, and is its id alone.

`torch_wall` is a torch on the side of a block, `wall_torch`, which faces its wall: facing north, it hangs from the block to its north and leans out to the south. `run/src/world/wall_torch/` says which block that is. Placing a torch picks its facing by the face aimed at.

### A drop row

`{ item: gravel, count: 1, chance: 0.9 }`. Each row is rolled on its own.

| Field | Needed | Values |
|---|---|---|
| `item` | yes | An item with `config/items/<item>/index.yaml`, which a block's own item is too |
| `count` | yes | 1 to 64 |
| `max` | | `count` to 64: gives a count from `count` to `max` |
| `chance` | | Above 0, up to 1. Default 1 |

### Faces

`z` is up, and `west east south north` are `-x +x -y +y`: x is east and y is north, as everywhere in the game. Either `all` alone, or every face covered by `top`, `bottom` and `side`, where a named face beats `side`. A value is `blocks/<name>`, in `textures/blocks/`. The sides of water and lava show their flowing texture.

```yaml
texture: { top: blocks/furnace_top, bottom: blocks/furnace_bottom, side: blocks/furnace_side, north: blocks/furnace_front }
```
