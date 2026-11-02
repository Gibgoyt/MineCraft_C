# run/src/physics/raycast

**Purpose:** what a ray from the eye comes to first among the world's blocks: DDA through the cells, then the block's own boxes.
**Tier:** Arena by depth. It allocates nothing: a cast keeps all it needs on its stack.
**Owns:** nothing.
**Lifetime:** Each call.
**Threads:** Whichever holds the map, normally `sim`.
**Public functions** (`run/include/physics/raycast/index.h`):

- `raycast_block_boxes`: what a block can be aimed at by.
- `raycast_cast`: the walk: the first box along a ray, within a distance.
- `raycast_hit_in_front`: the cell in front of a hit's face, where a block placed against it goes.

**May be called by:** `run/src/player/` and its parts, and later anything that aims, through `physics`'s header.
**Depends on:** `physics/collision` for what a cell reads as and a solid block's boxes, `world/map`, `world/column`, the generated `tables/blocks`, libm.

## How a cast goes

Amanatides & Woo 1987, in `float64_t`. Each next wall's distance is worked out from the cell's own number, so no error adds up along a ray. In each cell it reads the block and runs a slab test on that block's boxes, so a ray over a slab or past a plant's box goes on into the next cell. It costs a few cells per block of distance, and `RAYCAST_DISTANCE_MAX` bounds them.

- **A touch is not a hit.** A ray must go into a box for some length. Running along a face, or starting on a face and going away from it, is not going in. A plane of faces counts as part of the cell above it, like `floor`.
- **Starting inside is a hit**, at distance 0, with no face.
- **A column that is not loaded stops it**, with `RAYCAST_RESULT_NOT_LOADED`: nothing can be said about what is there.
- **Above and below gameplay's heights is empty.** `collision_cell_read` says bedrock below the world, so that nothing falls out. That is no block to aim at, so a ray going down just runs out.

## What can be aimed at

| In `config/blocks/` | Boxes |
|---|---|
| `shape: none`, `shape: fluid` | None: air and fluids are looked through |
| `solid` | What it collides with: see `run/src/physics/collision/README.md` |
| `shape: cross` | Three quarters of the cell's width and all its height, around two plates no one could aim at |
| `shape: crop` | The whole cell |
| `shape: torch` | Twice the stick's width, and its height |
| `shape: thin` | The plate as drawn: the +y side, or the floor for a lily pad |
