# compile/src/game_tables/blocks

**Purpose:** reads `config/blocks/<block>/index.yaml`, checks each file and what it refers to, and generates the block tables.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/game_tables/blocks/index.h`):

- `game_tables_blocks_build`: the whole job, from a config directory to `<out>/blocks/index.{h,c}`.

**May be called by:** `compile/src/game_tables/`.
**Depends on:** `compile/src/yaml/`, `compile/src/texture_pack/support/`.

## What it refuses

Every problem gets `path:line:column: message`, and reading goes on, so one build shows them all. Nothing is written unless there are none.

- A field it does not know, in a block, a drop or a texture. A typo must not quietly mean the default.
- A missing `name`, `hardness`, `drops`, or `texture`, and a `name` that is not its directory's.
- A texture with no `<textures>/blocks/<name>/index.mctex`, or that is not `blocks/<name>`.
- A drop item with no `config/items/<item>/index.yaml`, even if a block has its name. Air cannot be a drop.
- A value out of range or of the wrong type: see `config/blocks/README.md`.

A file that is missing altogether has no line to point at, and is reported as `path: missing`.

## The tables

`tables/blocks/index.h`, from the generated root:

| Name | What |
|---|---|
| `block_id_t`, `BLOCK_ID_DIRT` | Block ids. **0 is air**, so a zeroed section is empty. The rest sort by name |
| `block_defs[BLOCK_COUNT]` | `block_def_t` by id: name, hardness, six faces, drops, tool, tier, shape, light, opacity, flags |
| `block_drops[BLOCK_DROP_COUNT]` | `block_drop_t`: a block's rows are `first_drop` and `drop_count` of them |
| `block_drop_items[BLOCK_DROP_ITEM_COUNT]` | Every item in `config/items/`, as anything can lie on the ground, sorted so that row i is the `item_id_t` i + 1, with the block of that name or `BLOCK_NO_ID`, and its `item_id_t` |
| `block_texture_names[BLOCK_TEXTURE_COUNT]` | `"blocks/dirt"`. Faces index this |
| `block_shape_t`, `block_tool_t`, `block_tier_t`, `block_face_t`, `BLOCK_FLAG_*` | The words the yaml uses, as numbers |

- **Faces are names, not texture ids.** The ids belong to the blob, and `run/src/texture/registry/` finds them by name. A caller that needs ids makes them once at startup.
- **Ids are not stable across a change to the set of blocks.** Adding a block moves those after it. A save must store names, or a table of them, rather than trust these numbers.
- **A decimal is printed as it was written:** `0.5` is `5e-1f32`. The tool never rounds, so what the compiler makes of it is the same on any machine.
- **One block per state.** Wheat is `wheat_stage_1` to `_8`, because the world stores one `uint16_t` per block and nothing says what else it holds.
