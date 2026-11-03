# config/mobs

One directory per mob, and to see what a mob is, open its one file. To add one: make `config/mobs/<name>/index.yaml`, and build. `compile/src/game_tables/mobs/` turns these into `tables/mobs/`, and refuses a field it does not know or a reference to something missing, with the line. A mob's `kind` in `run/src/entity/store/` is its `mob_id_t`.

The numbers are the published ones, as near as this game's terms allow.

## Fields

| Field | Needed | Values |
|---|---|---|
| `name` | yes | The directory's name: lower case, digits, underscores. Not `none`, which is the empty row |
| `texture` | yes | `mobs/<name>`, whose `index.mctex` must exist |
| `texture_width`, `texture_height` | yes | 1 to 1024: what the parts' `uv` is in |
| `health` | yes | Above 0, up to 1024: in half hearts |
| `speed` | yes | Above 0, up to 1: blocks per tick on plain ground, as `entity_store_wish_set` takes it |
| `half_width` | yes | Above 0, up to 2: blocks from the middle to a side of its box |
| `height` | yes | Above 0, up to 4: blocks |
| `drops` | yes | A list of rows, or `[]` for nothing. Below |
| `spawn` | yes | Below |
| `despawn` | yes | Below |
| `parts` | yes | 1 to 6 boxes. Below |

### A drop row

`{ item: bone, count: 0, max: 2, chance: 1 }`. Each row is rolled on its own, as a block's are, except that `count` may be 0.

| Field | Needed | Values |
|---|---|---|
| `item` | yes | An item with `config/items/<item>/index.yaml` |
| `count` | yes | 0 to 64 |
| `max` | | `count` to 64: gives a count from `count` to `max` |
| `chance` | | Above 0, up to 1. Default 1 |

Only read here: the stages that kill things, 15.4 to 15.7, roll them.

### `spawn`

What `run/src/mob/spawn/` asks of a place. Its README says how the rules go together.

| Field | Needed | Values |
|---|---|---|
| `weight` | yes | 0 to 1000: how often it is picked among the mobs. 0 never comes by itself |
| `pack_min`, `pack_max` | yes | 1 to 16, in that order: how many come together |
| `max_light` | yes | 0 to 15: the brightest place it can come to, by block light or by what the sky gives at that time of day. A place passes at or under a roll from 0 to this, so the darker the likelier |
| `max_block_light` | yes | 0 to 15: the most block light it can stand, whatever the sky |
| `min_distance` | yes | 0 to 128: never nearer the player than this, in blocks |
| `never_on` | | Blocks it never stands on though they are whole and opaque: a list of names. Default none |

### `despawn`

| Field | Needed | Values |
|---|---|---|
| `near` | yes | 1 to 1024: within this many blocks of the player its idle count starts over |
| `far` | yes | Above `near` and above `spawn.min_distance`, up to 1024: past this it goes at once |
| `idle_ticks` | yes | 0 to 1000000: past `near`, how long before it may go |
| `one_in` | yes | 1 to 1000000: after that, its chance each tick is one in this |

### A part

One box of its body, read by `run/src/entity/animation/`, whose header says what each number does. Axes are forward, left, up, in a model's pixels, 16 to a block.

| Field | Needed | Values |
|---|---|---|
| `size` | yes | Three numbers above 0, up to 64 |
| `pivot` | yes | Three numbers, -64 to 64: from the middle of the soles of the feet |
| `offset` | yes | Three numbers, -64 to 64: the box's middle from its pivot |
| `uv` | yes | Two whole numbers, 0 to 1024: the corner of its box-mapped unwrap |
| `moves` | | `none`, `head` (turns by the head's yaw and pitch) or `arm` (swings to attack and rises to aim). Default `none` |
| `rest_pitch` | | -7 to 7: radians. Default 0 |
| `walk_swing` | | -7 to 7: radians at a full stride. Default 0 |
