# run/src/mob/passive

**Purpose:** what an animal does: it stands and strolls, keeps near others of its kind, runs from a blow for a few seconds, never strikes, and leaves its kind's drops and its sex's when it dies. Every mob whose file says `mind: passive` (`config/mobs/README.md`), male or female alike. Plan 005 phase 4, stage 1.
**Tier:** Arena (depth 2): `mob_passive_start` takes one array from the arena its Owner gives.
**Owns:** nothing. The array is borrowed from the arena.
**Lifetime:** World.
**Threads:** the sim's, as the store is.
**Public functions** (`run/include/mob/passive/index.h`):

- `mob_passive_start`: nothing going on, from a seed and an arena.
- `mob_passive_tick`: one tick of every animal. `mob_minds_tick` calls it after the zombies, so `run/src/game/` needs nothing new.
- `mob_passive_body_of`: what one animal is up to, by its index, starting over a slot that was another body's.
- `mob_passive_sex_of`: a mob's male or female row in `mob_sexes`, or `nullptr`.
- `mob_passive_yields_to`, `mob_passive_take`: whether an animal yields to what is held, and taking it: wool to shears, milk to a bucket.
- `mob_passive_fleece_shown`: whether a sheep's wool is drawn.

**May be called by:** `run/src/mob/`, `run/src/save/state/`, which saves the bodies, `run/src/game/`, which shears, milks and draws the wool, and tests.
**Depends on:** its sibling `mob/path` and the view type of `mob/zombie`, `entity/store`, `entity/item_drop`, `item/stack`, `terrain/rng`, `arena`, libm, and the generated `tables/mobs/`.

## What it does

Each tick, for each body whose kind's `mind` is `MOB_MIND_PASSIVE`, from the end of the store:

1. **Health at 0 or less:** rolls each row of its kind's `drops`, then each of its sex's, by itself, into the item store, and goes.
2. **Waiting for its column:** nothing, and no time passes for it.
3. **Every yield's wait** (`yield_wait`) runs down by one, to 0, and what it gives by itself whose wait is out, a hen's egg, is laid at its feet.
4. **Health lower than a tick ago:** a hit, a fall or lava. It runs for `MOB_PASSIVE_FLEE_TICKS`, straight away from where the player (`mob_zombie_view_t`) stood then, at `MOB_PASSIVE_FLEE_SPEED` times its speed, hopping what it runs into. Males run too (Ahmed's default: none fights back). Its stroll is dropped.
5. **Otherwise:** it stands, and about every six seconds sets off for somewhere near by `mob/path`, as a zombie strolls. If others of its kind stand within `MOB_PASSIVE_HERD_RANGE` and their middle is further than `MOB_PASSIVE_HERD_FAR` along x or y, the stroll goes back to within `MOB_PASSIVE_HERD_SPREAD` of that middle instead. So a herd stays a herd without walking in step.

It never returns a blow: `mob_minds_tick`'s is the zombies' alone.

| Number | Value | Why |
|---|---|---|
| `MOB_PASSIVE_FLEE_TICKS` | 100 | The published panic of 5 seconds |
| `MOB_PASSIVE_FLEE_SPEED` | 1.5 | Between the published panic goals: 2 for a cow, 1.25 for a chicken |
| `MOB_PASSIVE_STROLL_ONE_IN`, `_ACROSS`, `_UP`, `_TICKS`, `_NODES` | 120, 10, 3, 200, 256 | A zombie's stroll, less high |
| `MOB_PASSIVE_HERD_RANGE`, `_FAR`, `_SPREAD` | 16, 8, 3 | Not published: what keeps a herd of 2 to 4 together on open ground |

## What an animal gives alive (stage 3)

The files say who gives what, in their sex's `yields` (`config/mobs/README.md`): only a ewe's or a ram's wool, a cow's or a nanny's milk and a hen's egg. Males of the rest give nothing.

- `mob_passive_yields_to(store, index, tool)`: whether a yield of its sex is taken with what is held, ready or not.
- `mob_passive_take`: the first such yield that is ready rolls its count, from `count` to `max`, and its wait starts over at `every`. `run/src/game/` calls it for a press of the right button on an animal, and puts what it gave where it goes. The tally's `given_count` counts what was given, not saved.
- **Laying:** each tick, after the waits run down, a yield with no tool whose wait is out (a hen's egg) is left at its feet in the item store, and waits `every` again. A body met for the first time has every such wait at `every`, so a hen does not lay the moment she comes; one taken with a tool starts ready, so a new sheep can be shorn at once.
- `mob_passive_fleece_shown`: a part with `MOB_PART_FLEECE`, a sheep's wool, is drawn while its sex's first yield is ready, so a shorn sheep shows its skin until the wool grows back. It only looks.

All of it lives in `yield_wait`, saved with the world, so a shorn sheep is still shorn and a hen as far from her egg after a reopen.

## What stage 3 built on

`mob_passive_body_t` is saved with the world, in the state file's `SAVE_STATE_SECTION_PASSIVE` (`run/src/save/state/`), whole, so a field added to it is kept too: grow `_pad0` into it and keep the asserts. What is there for living yields:

- `yield_wait[i]`, by the order of the body's sex's `yields` in its file (`mob_sexes[...].first_yield + i` in `mob_yields`): ticks until that yield can be had again, run down here, 0 meaning now. A new body starts with every one at 0, so a new sheep can be shorn at once.
- `mob_yield_t.tool` is the item that must be held (shears, a bucket), and `ITEM_ID_NONE` for one given by itself, such as a hen's egg: stage 3 lays it when the wait is 0, and sets the wait to `every` again. Taking one with a tool sets it to `every` too.
- `mob_passive_sex_of(kind, sex)` finds the row, and `store->borrowed_sex[index]` says which. Only a female's row should carry milk or eggs, as the files decide.
