# compile/src/game_tables/biomes

**Purpose:** reads `config/biomes/<biome>/index.yaml`, checks each file and the blocks it names, and generates the biome tables.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/game_tables/biomes/index.h`):

- `game_tables_biomes_build`: the whole job, from a config directory to `<out>/biomes/index.{h,c}`.

**May be called by:** `compile/src/game_tables/`.
**Depends on:** `compile/src/yaml/`, `compile/src/texture_pack/support/`.

## What it refuses

Every problem gets `path:line:column: message`, and reading goes on, so one build shows them all. Nothing is written unless there are none. The fields are in `config/biomes/README.md`.

- A field it does not know, at any level, and a `name` that is not its directory's.
- A block with no `config/blocks/<block>/index.yaml`.
- A range whose low end is not below its high end, or a number outside what its field allows.
- `surface` on an `underground` biome, `cave` on any other, `z` on any but an `underground` biome and `height` on one.
- Layers that add up to more than `BIOME_LAYERS_MAX`, the deepest `run/src/terrain/surface/` looks.
- A `where` that no biome fills, since a column of that kind would have no biome, and over 8 `underground` biomes, since each column keeps them as bits.

## The tables

`tables/biomes/index.h`, from the generated root:

| Name | What |
|---|---|
| `biome_id_t`, `BIOME_ID_PLAINS` | Biome ids, sorted by name. Adding a biome moves those after it, so a save stores names |
| `biome_defs[BIOME_COUNT]` | `biome_def_t` by id: its box in the climate, then its surface or cave blocks |
| `biome_patches[BIOME_PATCH_COUNT]` | `biome_patch_t`: a biome's rows are `first_patch` and `patch_count` of them |
| `biome_features[BIOME_COUNT][BIOME_FEATURE_COUNT]` | How many of each feature a 16 by 16 chunk tries, for `run/src/terrain/feature/` |
| `biome_underground[BIOME_UNDERGROUND_COUNT]` | The `underground` biomes: highest `priority` first, then by name. The first that holds a voxel has it |
| `biome_where_t`, `biome_feature_t` | The words the yaml uses, as numbers |

A decimal is printed as it was written, as in the block tables, so the tool never rounds.
