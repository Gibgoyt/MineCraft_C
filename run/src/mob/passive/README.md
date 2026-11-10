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

**May be called by:** `run/src/mob/`, `run/src/save/state/`, which saves the bodies, stage 3's shearing, milking and laying, and tests.
**Depends on:** its sibling `mob/path` and the view type of `mob/zombie`, `entity/store`, `entity/item_drop`, `item/stack`, `terrain/rng`, `arena`, libm, and the generated `tables/mobs/`.

## What it does

Each tick, for each body whose kind's `mind` is `MOB_MIND_PASSIVE`, from the end of the store:

1. **Health at 0 or less:** rolls each row of its kind's `drops`, then each of its sex's, by itself, into the item store, and goes.
2. **Waiting for its column:** nothing, and no time passes for it.
3. **Every yield's wait** (`yield_wait`) runs down by one, to 0.
4. **Health lower than a tick ago:** a hit, a fall or lava. It runs for `MOB_PASSIVE_FLEE_TICKS`, straight away from where the player (`mob_zombie_view_t`) stood then, at `MOB_PASSIVE_FLEE_SPEED` times its speed, hopping what it runs into. Males run too (Ahmed's default: none fights back). Its stroll is dropped.
5. **Otherwise:** it stands, and about every six seconds sets off for somewhere near by `mob/path`, as a zombie strolls. If others of its kind stand within `MOB_PASSIVE_HERD_RANGE` and their middle is further than `MOB_PASSIVE_HERD_FAR` along x or y, the stroll goes back to within `MOB_PASSIVE_HERD_SPREAD` of that middle instead. So a herd stays a herd without walking in step.

It never returns a blow: `mob_minds_tick`'s is the zombies' alone.

| Number | Value | Why |
|---|---|---|
| `MOB_PASSIVE_FLEE_TICKS` | 100 | The published panic of 5 seconds |
| `MOB_PASSIVE_FLEE_SPEED` | 1.5 | Between the published panic goals: 2 for a cow, 1.25 for a chicken |
| `MOB_PASSIVE_STROLL_ONE_IN`, `_ACROSS`, `_UP`, `_TICKS`, `_NODES` | 120, 10, 3, 200, 256 | A zombie's stroll, less high |
| `MOB_PASSIVE_HERD_RANGE`, `_FAR`, `_SPREAD` | 16, 8, 3 | Not published: what keeps a herd of 2 to 4 together on open ground |

## What stage 3 builds on

`mob_passive_body_t` is saved with the world, in the state file's `SAVE_STATE_SECTION_PASSIVE` (`run/src/save/state/`), whole, so a field added to it is kept too: grow `_pad0` into it and keep the asserts. What is there for living yields:

- `yield_wait[i]`, by the order of the body's sex's `yields` in its file (`mob_sexes[...].first_yield + i` in `mob_yields`): ticks until that yield can be had again, run down here, 0 meaning now. A new body starts with every one at 0, so a new sheep can be shorn at once.
- `mob_yield_t.tool` is the item that must be held (shears, a bucket), and `ITEM_ID_NONE` for one given by itself, such as a hen's egg: stage 3 lays it when the wait is 0, and sets the wait to `every` again. Taking one with a tool sets it to `every` too.
- `mob_passive_sex_of(kind, sex)` finds the row, and `store->borrowed_sex[index]` says which. Only a female's row should carry milk or eggs, as the files decide.
