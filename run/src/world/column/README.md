# run/src/world/column

**Purpose:** a 16x16 column of 96 sections covering `z` in `[-256, 1280)`, the coordinate helpers, a mask with one bit per section (`column_mask_t`, two words, since 96 sections do not fit one), and the limits that keep gameplay to `z = -250 ... +1250`.
**Tier:** Arena (depth 2): `column_array_alloc` takes from an `arena_t *` its Owner gives.
**Owns:** nothing. Its sections hold their arrays from the `section_pool_t` passed to each call.
**Lifetime:** World.
**Threads:** as `section`: one writer, holding the pool, and no readers during a write. The helpers touch no state, so any thread may call them.
**Public functions** (`run/include/world/column/index.h`):

- `column_chunk_of`, `column_local_of`: a world `x` or `y` to its chunk and its place in it, flooring for negatives.
- `column_z_in_gameplay`, `column_z_clamp`, `column_z_in_storage`: the limits.
- `column_section_of`, `column_section_local_of`: a `z` in storage to its section and its place in it.
- `column_array_alloc`: room for many, from the arena, untouched.
- `column_init`, `column_deinit`: air under open sky, and giving every section back.
- `column_block_get`, `column_block_set`, `column_light_get`, `column_light_set`: one voxel, refusing any `z` outside gameplay.
- `column_memory_bytes`: what it holds, struct included.

**May be called by:** `run/src/world/` and its parts, `entity/item_drop`, `entity/store`, `mob/path`, `physics`, `physics/raycast`, `player/movement`, `player/survival`, `render/lod`, `save/world`, and `bench/`.
**Depends on:** `arena`, `assert`, `machine`, `run/src/world/section/`, whose types it stores, which `plans/REPO_STRUCTURE.md` section 11 requires this line to say: `world/section`. And the generated `tables/blocks/`, for air.

## Two ranges

Storage covers `[-256, 1280)`, because 96 sections of 16 must start and end on a multiple of 16. Gameplay is `[-250, 1250]`, from `world:` in `config/machine.yaml`. So the lowest sections hold layers nothing may write: 6 under `-250`, and 29 above `1250`.

`column_block_set` and the others refuse those with `COLUMN_STATUS_Z_OUT_OF_RANGE`, and reads return false and leave `*out` alone, so a `z` of `-251` or `1251` cannot reach a block by any function here. `column_z_clamp` is for something that should stop at the limit and not be refused: a player's feet, a tree's top. A renderer or a light pass that wants a section whole reads `column.sections[]` directly, by `column_section_of`.

A section with nothing in it costs 24 bytes in the column, so a column is 2312 bytes before any array. All-air sections above the ground and solid ones below it need nothing more.
