# config/mobs

One directory per mob, and to see what a mob is, open its one file. To add one: make `config/mobs/<name>/index.yaml`, and build. `compile/src/game_tables/mobs/` turns these into `tables/mobs/`, and refuses a field it does not know or a reference to something missing, with the line. A mob's `kind` in `run/src/entity/store/` is its `mob_id_t`.

The numbers are the published ones, as near as this game's terms allow.

## The stand-in pig

`pig/` is the smallest animal that works what animals need (plan 005 phase 4 stage 1): passive, male and female (a boar with tusks, 1.2 times a sow), by day on grass in the fields. It borrows the creeper's skin and drops leather, and has weight 0, so it never comes by itself and is not kept from a save. Stage 3 gives the pig its own model, skin and meat, and a weight, with the other five animals.

## Turned off: zombie, skeleton, creeper

Since plan 005 phase 3 all three have `spawn: weight: 0`, so none comes by itself, by night or in a cave. Ahmed's words: "completely disable skeletons, creepers, and zombies, I have ideas for my own mobs". Each was 100.

Everything else is kept: their files here, the models and textures, their minds in `run/src/mob/`, and the tests that put one down on purpose (combat, arrows, blasts, the picture). A mob of Ahmed's can reuse the parts, and putting a weight back brings one back.

A world saved while they came still holds some. As it opens, `game_open` takes away every mob whose weight is 0 (`mob_spawn_retire` in `run/src/mob/spawn/`), with a line in the log for each kind, such as `game: took 4 of mob zombie out of the saved world, as it is turned off`. So **a weight of 0 also means a mob is not kept from a save**: a mob of Ahmed's that only comes some other way needs that changed first.

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
| `mind` | | `hostile` or `passive`. Default `hostile`. Below |
| `spawn` | yes | Below |
| `despawn` | unless `passive` | Below. A passive mob has none: it is never taken away |
| `parts` | yes | 1 to 16 boxes, its sex's included. Below |
| `male`, `female` | together | What a male and a female are besides the rest: name, size, boxes, drops and what it gives alive. Both or neither. Below |

### `mind`

- **`hostile`:** the zombie, the skeleton and the creeper, each with a mind of its own in `run/src/mob/`. It comes by the dark rules, in packs, under the cap of 70, and goes by `despawn`.
- **`passive`:** an animal, whose mind is `run/src/mob/passive/`: it strolls, keeps near others of its kind, runs from a blow for 5 seconds, and never strikes, male or female. It comes in herds by day under a cap of its own, 10 per 289 loaded columns as the published game's animals, and is never taken away, so it has no `despawn`. `run/src/mob/spawn/README.md` has its rules.

## An animal: `male` and `female`

One file per animal, with a male and a female in it (plan 005 phase 4). Every body of a mob with them is one or the other, half and half, kept in the entity store and saved with the world; a herd has both. Neither is needed of a mob, but if one is there so must the other be.

```yaml
male:
  name: bull                # what a male is called: lower case, digits and underscores
  scale: 1.25               # its box and every box of it, the mob's own too, this much bigger
  parts:                    # its boxes besides the mob's own, as parts are: horns
    - { size: [1, 1, 4], pivot: [6, 0, 18], offset: [5, 4, 2], uv: [22, 0], moves: head }
  drops:                    # rolled after the mob's own, as drops are
    - { item: leather, count: 1 }
female:
  name: cow
  scale: 1
  parts:                    # an udder
    - { size: [4, 6, 1], pivot: [0, 0, 11], offset: [-2, 0, -1], uv: [52, 0] }
  yields:                   # what she gives alive
    - { item: milk_bucket, count: 1, using: bucket, every: 6000 }
```

| Field | Needed | Values |
|---|---|---|
| `name` | yes | Lower case, digits and underscores: `bull`, `ewe`, `rooster` |
| `scale` | yes | Above 0, up to 2: how much bigger it is than the mob's `half_width`, `height` and parts say. A male is 15 to 30% bigger, so that the two are told apart at a distance |
| `parts` | | Boxes, as `parts` are, drawn after the mob's own. The mob's and these together are at most 16 |
| `drops` | | Rows as `drops` are, rolled after the mob's own when it dies |
| `yields` | | Up to 4 rows of what it gives alive. Below |

### A yield row

`{ item: white_wool, count: 1, max: 3, using: shears, every: 6000 }`. What stage 3 of plan 005 phase 4 gives a living animal: wool to shears, milk to a bucket, an egg now and then. Only the files say who gives what, so a male gives nothing alive by having no `yields`.

| Field | Needed | Values |
|---|---|---|
| `item` | yes | An item with `config/items/<item>/index.yaml`: what it gives |
| `count` | yes | 1 to 64 |
| `max` | | `count` to 64: gives from `count` to `max` |
| `using` | | An item: what must be held to take it, such as `shears` or `bucket`. Absent: it gives it by itself, as a hen lays an egg |
| `every` | yes | 1 to 1000000 ticks: how long until it can be had again, as wool grows back and milk refills, or between eggs |

In the tables, `mob_defs[...].first_sex` is the male's row of `mob_sexes`, and the female's is the next; 0 is none. Each row has its `scale`, its parts (`first_part`, `part_count` in `mob_parts`), drops and yields (`mob_yields`, with `tool` for `using` and `ITEM_ID_NONE` for by itself). `run/src/mob/passive/` keeps a wait for each yield of each animal, saved with the world.

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
| `min_distance` | yes | 0 to 128: never nearer the player than this, in blocks. On a world's first day an animal may come as near as 12 |
| `never_on` | | Blocks it never stands on though they are whole and opaque: a list of names. Default none |
| `min_light` | | 0 to 15, up to `max_light`: the least light it comes to, by block light or the sky's at that time of day. Default 0. 9 for an animal, which comes by day, as the published game's come above 8. A passive mob's light is not rolled: anything from `min_light` to `max_light` passes |
| `on` | | The only blocks it stands on when it comes: a list of names, such as `[grass_block]`. Default any whole opaque block |
| `biomes` | | The only biomes it comes in: names from `config/biomes/`, up to 32. Default all. `plains, sunflower_plains, meadow, forest, flower_forest, birch_forest, savanna` for the animals of the fields, the windswept ones and `grove` for goats, `stony_peaks, jagged_peaks, frozen_peaks, snowy_slopes` for mountain goats |
| `min_height`, `max_height` | | -1250 to 1250, in that order: blocks above the sea its feet may be at when it comes. Default all of the world: a mountain goat comes only high up |

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
