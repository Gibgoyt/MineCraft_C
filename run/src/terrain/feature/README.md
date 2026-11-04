# run/src/terrain/feature

**Purpose:** what grows on the ground: every kind of tree, grass, ferns, flowers, cactus, sugar cane, dead bushes, mushrooms, lily pads, vines, melons, pumpkins, sea plants, ice spikes, and lakes, ice and snow. Placed across chunks as a pure function of the seed.
**Tier:** Arena: its one struct comes from an `arena_t *` its parent gives it. Filling allocates nothing, and works on its own stack frames.
**Owns:** nothing. Its struct is in the arena `terrain_init` lends.
**Lifetime:** World: its numbers are made for one seed.
**Threads:** `terrain_feature_init` and `terrain_feature_deinit` on the thread running `terrain_init`. Filling from any thread at once, since nothing changes once it is made and each call works in its own stack frame.
**Public functions:**

- `terrain_feature_init`, `terrain_feature_deinit`: a pair.
- `terrain_feature_fill_box`: everything here, for one box.
- `terrain_feature_chunk_read_box`, `terrain_feature_fill_chunk_box`: the same for a box that is one whole chunk, reading the chunk's own columns from the blocks at hand, as `terrain_generate_box` does. Those blocks have to be as the stages up to the flood left them, since lining and ore can change a top, so the first is called before those, and the second after. It gives the same bytes, and costs no second run of the stages over that chunk: `tests/run/terrain/feature/own/` holds it to that.
- `terrain_feature_chunk_columns`, `terrain_feature_chunk_lake`: what a chunk has on it before anything grows, and its lake.
- `terrain_feature_tree_reach`, `terrain_feature_tree_is_wide`, `terrain_feature_canvas_of`, `terrain_feature_paint_tree`, `terrain_feature_lake_depth`: the shapes, as plain arithmetic, for `tests/run/terrain/feature/` to hold to account.

**May be called by:** `terrain` and its parts.
**Depends on:** `terrain/biome`, `terrain/cave`, `terrain/density`, `terrain/hydrology`, `terrain/surface` and `terrain/rng` (its siblings), `arena`, `assert`, `machine`, `types`, and the generated `tables/biomes/` and `tables/blocks/`.

## The order

It is the sixth step of `run/src/terrain/surface/README.md`'s order, and `terrain_generate_box` in the Owner runs all six. It needs to be last, as it reads what is in the box.

## How a voxel stays a pure function

**Everything starts from a chunk,** 16 by 16 and lined up on the world, and works out a voxel's block from that chunk and the chunks round it, never from a stream carried from one place to another or from what else is in the box.

A box is worked in tiles, one per chunk it touches. In each tile, in this order, which is always the same for a voxel since a voxel is in one tile:

1. The tile's own chunk: its lake, then each row in the biome table's order, then the ice spikes.
2. The 8 chunks round it, `y` then `x`, lowest first: only their trees, as nothing else leaves its chunk.
3. Ice and snow, on the tile's own columns.

What a step writes at a voxel depends only on what is at that voxel (below), and on the chunks. No step reads what any other voxel has become: **a site's ground comes from the stages before this one, and never from another feature**, so no answer depends on which voxels the box holds.

What may go over what:

| Writing | Goes over |
|---|---|
| A log | Air, water, plants, vines, leaves. Never solid ground, or another log |
| Leaves | Air, plants, vines |
| A vine, a land plant, snow | Air |
| A sea plant, ice | Water |

## A chunk's columns

A column's **top** is its highest block that is not air or water, by what hydrology, the surface, the caves and the flood left there. Its **water** is the layers directly over that.

To find them, the chunk is walked from the top down in slices of 16 layers lined up on the world, running those stages on each, until every column has a top. Where to start and stop comes from the density's own corners, as the surface's bounds do: no ground over `height + softness + 10`, and none missing under `height - softness - 10`, for the highest and lowest corner of the chunk, with a river's `carve_top` lowering the second.

**A column with no top in that range has had its ground cut away** by a cave, and grows nothing: `TERRAIN_FEATURE_NO_GROUND`. So a tree never stands on a cave floor, and a box always knows how far below its top a feature can reach.

Rock lining and ore are not run on the walk. They only change one rock to another, and nothing here tells those apart. Nor is a site's column read from the box being filled, so no voxel depends on the box.

The walk costs about as much as making 2 to 5 sections of the chunk, so it is put off until a site that can touch the box has passed its biome's chance.

## Sites

Each row of the biome table has a stream per chunk, from the world's seed, the row's number and the chunk: never a stream carried over. For a chunk, a row's **tries** are the most any biome in the chunk asks for, rounded up, and each try draws an `x`, a `y`, a chance and a seed, whatever happens next, and whatever the box. A try stands if its column's biome passes `chance * tries < biome's number`. So a fraction of a try comes out as a fraction of the chunks, and the biome that a site lands in is the one whose number counts.

A site then needs what its row asks for. Those in the table come in addition to not standing on a lake or a cut-away column:

| Row | On | Water | Also |
|---|---|---|---|
| `tree_spruce` | Soil, snow | None | |
| `tree_mangrove` | Soil | 0 to 5 layers, which it stands in | Roots down to the ground on up to 4 sides |
| Other trees | Soil | None | A wide trunk: the other 3 columns are dry and within 2 layers |
| `grass`, `flower` | Soil | None | Swamp flowers are blue orchids |
| `fern` | Soil, snow | None | |
| `cactus` | Sand, red sand | None | No higher ground beside it. 1 to 3 tall |
| `sugar_cane` | Soil, sand, red sand | None | Water beside it, level with its block or over it, or a lake with that level. 1 to 3 tall |
| `dead_bush` | Sand, red sand, coarse dirt, dirt, terracotta | None | |
| `mushroom` | Soil, mycelium | None | Brown or red |
| `lily_pad` | Anything | 1 or more | On the water's top |
| `melon`, `pumpkin` | Grass block | None | |
| `seagrass` | Anything but lava | 2 or more | |
| `kelp` | Anything but lava | 3 or more | Up to 12 tall, and never to the top layer of water |
| Ice spikes | Snow | None | `ice_spikes` only, 1 a chunk, 6 to 20 tall: a row of its own here, as the table has no word for it |

**Soil** is grass block, dirt, podzol, coarse dirt, rooted dirt, moss block or mud.

**A site stays inside its chunk.** A row that looks at what is beside it keeps a margin, so all it looks at is in that chunk and costs nothing more: wide trunks 1 on the high side, mangroves, cactus and cane 1 on both sides, spikes 2. The margin is taken out of the range the `x` and `y` are drawn from, so it does not change how many sites there are.

**Vines go with trees:** `vine`'s number is shared out among the trees the biome asks for. A tree carries that share, whole strands plus a chance of one more, up its trunk. There are none in the leaves, since a vine there would need to read a voxel that may not be in the box.

## The trees

| Kind | Trunk | Crown | Reach |
|---|---|---|---|
| Oak | 4 to 6 | 2 wide layers, 2 narrow | 2 |
| Birch | 5 to 7 | As oak | 2 |
| Spruce | 6 to 10 | A cone of boughs, up to 3 across | 3 |
| Jungle, 1 in 4 | 5 to 8 | As oak | 7 |
| Jungle, 3 in 4 | 12 to 21, 2 by 2 | A blob 5 across, and 2 or 3 boughs with blobs on | 7 |
| Acacia | 3 to 5, leaning by 1 or 2 | A flat top 3 across | 5 |
| Dark oak | 6 to 9, 2 by 2 | A blob 3 across | 4 |
| Mangrove | 5 to 8 over the water | A blob 3 across, and roots | 3 |
| Cherry | 4 to 6 | 2 boughs going opposite ways, each with a blob 3 across | 6 |

**Reach** is the furthest a voxel is from its site on `x` or `y`, and it is under a chunk. So a tree can only reach the chunks next to its own, and that is why a tile looks at 8 others. `tests/run/terrain/feature/` holds each to its reach over many seeds.

A tree's trunk is never stopped by the ground: a column's top is the highest thing in it, so all above is open. Its crown can run into a slope beside it, and leaves stop there, as they do not go over ground.

## Lakes

One chunk in 6 tries for a lake. A lake is two overlapping ellipses, 3 to 6 blocks from the middle on each axis, inside its chunk with 1 block to spare, up to 5 layers deep. It being inside one chunk is what keeps it cheap: whether it can be is that chunk's question and no other's. It does mean no lake is over 14 blocks across.

It stands only if:

- every column of it and its shore has a top, and no water, and its biome's `where` is `land`;
- its **level**, the lowest top of its shore, is above the sea, so that no shore can let it out;
- each of its columns has its top at most 4 over the level, and at most its depth under it;
- and in the real voxels, every voxel of water has ground or more of the lake on all four sides, and ground under it. Those are read from the stages, as a cave can pass under a shore without changing any column's top.

Ground over the level goes, in the lake's own columns, and the bed goes down by a column's depth, from 1 at the edge to the lake's at its middle. Sites inside it do not stand. Sugar cane on its shore counts it as water.

The many that come to nothing have a cost only when they pass the first check, and that is 1 layer or so of the chunk. `every_lake_holds_its_water` prints how many stood.

## Ice and snow

Above 0, `ice` and `snow` in a biome's file are on for every column of it, not tries.

- **Ice:** the top layer of water of a column whose biome has `ice`, including a lake's. Anything else is left as water, so warm water never freezes.
- **Snow:** one `snow_block` on the top of a dry column whose biome has `snow`, unless that top is already snow, ice, packed ice, blue ice or lava, or something has grown there. There is no thin snow in `config/blocks/`, so it is a whole block: Stage 10.6's issue 4.

They go last, into air or water only, so nothing ends up under them. Snow lies on the ground and not on leaves.

## What it does not do

- **No pumpkins in the world yet.** The code is there and shares melon's, but no biome's file asks for any.
- **No sunflowers or tall flowers:** no such block in `config/blocks/`.
- **No bamboo:** the same. `bamboo_jungle` differs from `jungle` by its numbers.
- **No thin snow, no leaf snow, no vines in leaves.**
- **Nothing grows in caves.** Lush caves have their linings and no plants.

## What it costs

A tile's own chunk and 8 others each take a biome area and a lattice of 25 hydrology columns, and those whose sites can reach the tile a walk. A walk reads its chunk's biomes and noise once, `terrain_surface_tile_read`, for all its slices, and cuts its caves from the mask it already asked for. Reading them once a slice and asking twice cost a third more on the slowest ground. `bench/terrain/` measures it in chunk columns a second through every stage.
