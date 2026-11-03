# run/src/mob/zombie

**Purpose:** what a zombie does: it notices what is in view, chases it, strikes it within reach, and leaves what it carried when it dies.
**Tier:** Arena (depth 2): `mob_zombie_start` takes one array from the arena its Owner gives.
**Owns:** nothing. The array is borrowed from the arena.
**Lifetime:** World.
**Threads:** the sim's, as the store is.
**Public functions** (`run/include/mob/zombie/index.h`):

- `mob_zombie_start`: nothing going on, from a seed and an arena.
- `mob_zombie_tick`: one tick of every zombie, returning the blow its target takes.
- `mob_zombie_line_is_clear`: whether one place can see another.
- `mob_zombie_in_reach`: whether a body can strike what is in view.

**May be called by:** `run/src/mob/`, and tests.
**Depends on:** its sibling `mob/path`, `entity/store`, `entity/item_drop`, `entity/animation`, `item/stack`, `physics/collision`, `world/map`, `terrain/rng`, `arena`, libm, and the generated `tables/mobs/`.

## What it does

Each tick, for each body whose `kind` is `MOB_ID_ZOMBIE`, from the end of the store:

1. **Health at 0 or less:** rolls each row of its `drops` in `config/mobs/zombie/` by itself, puts them in the item store, and goes. Nothing here lowers health: Stage 15.7's blows and anything else write `borrowed_health`, and the next tick sees it. A body the spawn rules take away leaves nothing.
2. **Waiting for its column:** nothing, and no time passes for it.
3. **Not chasing:** one tick in ten it looks round, and takes up the chase if view is alive, in range, near its own height, and in sight. Otherwise it stands, and about every six seconds sets off for somewhere near by the same paths, which is what brings it within sight on ground that is not flat.
4. **Chasing:** it gives up if view has died, left its range, or gone unseen too long. Within reach and in sight it stands and faces it. Otherwise it walks the way `mob/path` last gave it, and asks again when view has moved a block, or the way has run out or stuck, and its turn has come. Its head turns to view.
5. **Striking:** within reach, in sight by a ray cast then, with its wait over.

The way it gets is to view's cell, so that it arrives within reach of anything standing in that cell, wherever in it.

It knows nothing of players: `run/src/game/` gives it a `mob_zombie_view_t`, and gives what it returns to `player_survival_hurt`, as `PLAYER_SURVIVAL_CAUSE_ATTACK`, so armor and the time a body cannot be hurt after a hit work as for any other blow. However many struck in one tick count as one, since a second blow of the same size would do nothing anyway.

## The numbers

| Name | Value | Published as |
|---|---|---|
| `health`, `speed`, the drops | `config/mobs/zombie/` | The mob's own file |
| `MOB_ZOMBIE_DAMAGE` | 3 | Normal difficulty's: this game has no others |
| `MOB_ZOMBIE_ATTACK_TICKS` | 20 | A melee goal's wait |
| `MOB_ZOMBIE_FOLLOW_RANGE` | 35 | `follow_range`; looked at in three dimensions |
| `MOB_ZOMBIE_NOTICE_UP` | 4 | How far up and down it looks for a target |
| `MOB_ZOMBIE_NOTICE_ONE_IN` | 10 | Its chance per tick of looking |
| `MOB_ZOMBIE_UNSEEN_TICKS` | 60 | How long a target goal lasts out of sight |
| `MOB_ZOMBIE_ATTACK_REACH` | 0.828 | The attack box's sideways growth |
| `MOB_ZOMBIE_EYE` | 1.74 | Its eye height |
| `MOB_ZOMBIE_STROLL_ONE_IN`, `_ACROSS`, `_UP` | 120, 10, 7 | A random stroll's chance per tick and its reach |
| `MOB_ZOMBIE_REPATH_*` | 4 + 0 to 6, + 5 past 16 blocks, + 10 past 32 | A melee goal's wait between paths. The further 10 after a way that did not arrive is the older games' penalty |

Not published: `MOB_ZOMBIE_SIGHT_EVERY`, which saves rays while it chases, with each zombie on its own tick; `MOB_ZOMBIE_STROLL_TICKS` and `_NODES`, which bound a stroll; and the limits on how its head turns.

## Sight

`mob_zombie_line_is_clear` walks the cells between two eyes and tests the line against what each cell's block collides with, as the published game does. `physics/raycast` is not used because it stops at what can be aimed at, and a zombie sees through tall grass.

## What this does not do

It has no knockback, and nothing hurts it: both are Stage 15.7. It does not break doors, burn or call for help. Rows of `drops` do not depend on who killed it, though the rare ones do in the published game. Nothing draws it: see Stage 15.4's Issues.
