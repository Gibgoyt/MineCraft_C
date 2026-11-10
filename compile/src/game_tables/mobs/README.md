# compile/src/game_tables/mobs

**Purpose:** reads `config/mobs/<mob>/index.yaml`, checks each file and what it refers to, and generates the mob tables.
**Tier:** Arena. Allocates only from the `tp_arena_t` it is given.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** the tool's one thread.
**Public functions** (`compile/include/game_tables/mobs/index.h`):

- `game_tables_mobs_build`: the whole job, from a config directory to `<out>/mobs/index.{h,c}`.

**May be called by:** `compile/src/game_tables/`.
**Depends on:** `compile/src/yaml/`, `compile/src/texture_pack/support/`.

## What it refuses

Every problem gets `path:line:column: message`, and reading goes on, so one build shows them all. Nothing is written unless there are none. The fields are in `config/mobs/README.md`.

- A field it does not know, in a mob, a drop, `spawn`, `despawn` or a part, and a `name` that is not its directory's.
- A directory called `none`, which is row 0 and has no file. A `config/mobs` with no mobs in it.
- A `texture` that is not `mobs/<name>`, or has no `<textures>/mobs/<name>/index.mctex`.
- A drop `item` with no `config/items/<item>/index.yaml`, a `never_on` or `on` block with no `config/blocks/<block>/index.yaml`, and a biome with no `config/biomes/<biome>/index.yaml`.
- A number out of range, or of the wrong type: decimals too fine to be meant, too.
- What cannot go together: `pack_max` under `pack_min`, a drop `max` under its `count`, `despawn.far` not above `near` or not above `spawn.min_distance`, since a mob would go the tick it came, `min_light` above `max_light`, `max_height` under `min_height`, a yield's `max` under its `count`.
- More than 16 parts, which is what a rig holds, or none; or a sex whose parts and the mob's own are more than 16.
- A `mind` that is not `hostile` or `passive`, a `despawn` on a passive mob and none on a hostile one.
- A `male` without a `female`, or the other way, an unknown field in either or in a yield, a sex `name` that is not lower case, digits and underscores, a `scale` not above 0 up to 2, more than 4 yields, and a yield `item` or `using`, an `on` block or a `biomes` biome with no file.

## The tables

`tables/mobs/index.h`, from the generated root:

| Name | What |
|---|---|
| `mob_id_t`, `MOB_ID_ZOMBIE` | Mob ids. **0 is `MOB_ID_NONE`**, and the rest sort by name, so adding a mob moves those after it, and a save stores names |
| `mob_defs[MOB_COUNT]` | `mob_def_t` by id: everything the file says, with where its rows are in the arrays below |
| `mob_drops[MOB_DROP_COUNT]` | `mob_drop_t`: an item, a chance, and a count from `count_min` to `count_max`. A mob's are `first_drop` and `drop_count` of them |
| `mob_never_on[MOB_NEVER_ON_COUNT]` | Block ids. A mob's are `first_never_on` and `never_on_count` of them |
| `mob_parts[MOB_PART_COUNT]` | `mob_part_t`: a box of a body. `entity_animation_part_t` is this type |
| `MOB_ZOMBIE_FIRST_PART`, `MOB_ZOMBIE_PART_COUNT`, `MOB_ZOMBIE_TEXTURE_WIDTH`, `MOB_ZOMBIE_TEXTURE_HEIGHT` | The same again as constants, so that `run/src/entity/animation/`'s rigs can be a constant table |
| `MOB_PARTS_MAX`, `MOB_PART_MOVES_HEAD`, `MOB_PART_MOVES_ARM` | The most a body has, 16, its sex's boxes with its own; the bits of `flags` |
| `mob_on[MOB_ON_COUNT]`, `mob_biomes[MOB_BIOME_COUNT]` | Block ids and `biome_id_t`s: a mob's `spawn.on` and `spawn.biomes`, by `first_on`, `on_count`, `first_biome` and `biome_count` |
| `mob_sexes[MOB_SEX_ROW_COUNT]` | `mob_sex_t`: a male or a female, its name, `scale`, and where its parts, drops and yields are. A mob's `first_sex` is its male's row, and its female's is the next; 0 is none |
| `mob_yields[MOB_YIELD_COUNT]`, `MOB_YIELDS_MAX` | `mob_yield_t`: what a living animal gives, to which `tool` held (`ITEM_ID_NONE` for by itself), and how many ticks until again; 4 at most for a sex |
| `MOB_MIND_HOSTILE`, `MOB_MIND_PASSIVE`, `MOB_SEX_NONE`, `MOB_SEX_MALE`, `MOB_SEX_FEMALE` | `mob_def_t.mind`, and `mob_sex_t.sex`, which is `entity_store_t`'s sex too |

A mob's rows of `mob_parts` and `mob_drops` are its own first, then its male's, then its female's, so `MOB_<NAME>_FIRST_PART` and `part_count` are its own body only. A passive mob's `despawn_far` is 0. `min_height` and `max_height` are -1250 and 1250 when a file gives none.

- **Row 0 of each array belongs to none,** so that no array is ever empty.
- **A decimal is printed as it was written,** as in the block tables, so the tool never rounds.
