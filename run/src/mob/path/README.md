# run/src/mob/path

**Purpose:** finds a body a way over the blocks: bounded A\*, run on workers in batches, with answers cached until an edit spoils them.
**Tier:** Arena (depth 2): `mob_path_start` takes its arrays once from the arena its Owner gives, and a job takes its scratch from the worker's Job arena.
**Owns:** nothing. It holds its arrays as `borrowed_`, for as long as the Owner keeps that.
**Lifetime:** World for its arrays, Job for a worker's scratch.
**Threads:** `sim` for everything but `mob_path_search`. During `mob_path_tick` workers run searches too, reading the map, which the sim is not writing because it is in that call.
**Public functions** (`run/include/mob/path/index.h`):

- `mob_path_start`, `mob_path_stop`: nothing asked and nothing known; wait for any job still out.
- `mob_path_ask_for_body`: the usual ask, for a body in the store.
- `mob_path_request`: ask. A body has one ask at a time, and a new one replaces it, keeping its turn.
- `mob_path_tick`: answer what the budget allows, and forget what edits spoiled.
- `mob_path_found`: a body's last answer.
- `mob_path_follow`: one tick of walking that answer: wish, jump and yaw.
- `mob_path_search`: one search, on any thread, with no state but its scratch.
- `mob_path_stats_read`: the counters.

**May be called by:** `run/src/mob/`, and its sibling parts, the mobs of Stages 15.4 to 15.6.
**Depends on:** `arena`, `assert`, `job`, `entity/store`, `physics/collision`, `world/map`, `world/edit`, `world/column`, the block tables, libm, and `sched_yield`.

## Where a body can be

A node is the cell of a body's feet. The body is taken as at most one cell wide, standing in the middle of it, which covers every mob in the plan.

| Cell | To a body |
|---|---|
| Air, or anything not solid | Passes; stands if there is a block under it |
| Water, ladder, vine | Passes and stands, whatever is under it. Water costs more |
| Slab | Stands on top, feet half a block up |
| Stairs, and any other solid block | Stands on top. Stairs are a block here because a body in the middle of the cell ends on their top half |
| Lava, cactus, a column not loaded | Nothing. The last is what `physics` calls a wall too |

The moves, with what each costs in hundredths of a block's walk:

| Move | Needs | Cost |
|---|---|---|
| Straight, on the level | Room there | 100 |
| Diagonal | Room there and at both corners: a body cannot cut one | 141 |
| Up half a block (a slab, or off one) | Room above here and there. `entity_store_tick`'s 0.6 step walks it | 100 + 30 |
| Up a block (a jump) | The same. A jump reaches 1.25 | 100 + 60 |
| Down, up to the ask's `drop` | Room out there at the height it is, all the way down | 100 + 5 per half block |
| Up or down, in water or on a ladder | Room there | 100 up, 30 down |
| Into water | | 150 more |

Costs are whole numbers, so that nothing depends on rounding, and the heuristic is octile distance plus the height's cost, never more than the moves it stands for. So the first time the goal comes off the heap the way is the cheapest in reach, and no node is ever opened twice. Two nodes in the heap never tie: lower `f`, then higher `g`, then the older.

## The bounds

- `nodes` in the ask, at most `MOB_PATH_NODES_MAX`: places written down.
- `MOB_PATH_REACH_XY` and `MOB_PATH_REACH_Z`: how far from where it starts.
- `MOB_PATH_POINTS_MAX`: a longer way keeps its start.

Reaching any of them gives `MOB_PATH_STATUS_PARTIAL`, with the way to the nearest place written down, or `MOB_PATH_STATUS_NONE` if that is where it stands. So does a goal that cannot be reached, and a body can walk as near as it gets. Ask again from there.

A start or goal in the air, or in a body just above its ground, is moved down to the ground under it, up to `MOB_PATH_SETTLE_BLOCKS`: what is being chased may be jumping.

## On workers

`mob_path_tick` runs on the sim and answers in this order:

1. Reads the edit log, and forgets what that spoils.
2. Walks the queue in order. What the cache answers is answered. What it cannot joins the round, up to `MOB_PATH_ROUND_SEARCHES` and `MOB_PATH_ROUND_NODES`, once per different ask.
3. Publishes the round through one atomic cursor, sends up to `MOB_PATH_JOBS_MAX` jobs, and then claims searches itself until none is left. It waits only for those already running.
4. Keeps every answer, in the round's order, whoever ran it, and walks the queue again. Whoever is left keeps its place at the front of the next tick.

**Why the sim searches too.** A queue of workers can be busy with slow terrain, and `job_group_wait` would stall the tick behind it. With the cursor the sim never waits for a job that has not started: with every worker busy, it runs all of the round itself, and `MOB_PATH_ROUND_NODES` is what bounds that. A job that starts late finds nothing to claim, or helps with whichever round is on, and either is right, so only `mob_path_stop` waits for the group.

**What the budget comes to.** Measured on the 10900K, release, with the budget spent in full on searches for a goal that cannot be reached, which is the worst there is: 8192 nodes took 1.2 to 2.0 ms with workers and 4.4 to 5.1 ms alone, on a machine shared with other builds, with one run alone at 9.4 ms. So with workers a round leaves most of the plan's 5 ms sim tick, and in the rare tick with four such searches and every worker busy it can use all of that or more. `MOB_PATH_ROUND_NODES` is the number to lower if Stage 15's `bench_mobs` says so. `run_mob_path` prints these. The usual search is far cheaper, and a full budget buys four of the dearest searches a tick, or 80 a second.

**Why the map is safe to read.** It is the sim's, and the sim is inside this call for as long as anything may claim a search. The cursor's release store, and the acquire of whoever claims, carry that across.

**Why the workers change nothing.** A search depends only on its ask and the cells it reads, each goes to one thread, and the sim keeps the results in the order of the round. The same world and the same calls give the same bytes from 0 workers or 16.

## The cache

`MOB_PATH_CACHE_ENTRIES` answers, the longest unused going first. An ask is answered from there two ways:

- **Exactly**: the same start, goal, budget, body and fall.
- **By the rest of a way:** the same goal, budget, body and fall, on a complete way passing through where this body stands. The way on from any point of a cheapest way is a cheapest way, so a body that walks a path and asks again costs nothing, and neither does a second one behind it.

What spoils an answer:

- **A block.** Every search writes down a box holding every cell it read, whatever it found, so a change anywhere else cannot change its answer. `mp_edits_read` keeps its own cursor on `world_edit`'s log and drops what contains a changed block. A change that does not matter to any search, as grass for a flower, is skipped. More than `MOB_PATH_EDITS_MAX` at once empties the cache instead. **So a block written around `world_edit` is not noticed.** `world_edit_block_set` is how the game changes one; only a column just committed is written directly, and that is the next point.
- **A column.** The log does not say when one comes or goes, and a partial answer may have stopped at what was not there. When the map's `change_count` has moved, a complete answer is kept if every step of it still stands, and any other is dropped. A kept one may have a shorter way through a column new since; it is still a way.

## Walking it

`mob_path_follow` steers a body from one point's middle to the next through `entity_store_wish_set` and `entity_store_jump`, jumping where the next point is more than a step up, and always in water, or on a ladder going up. So it needs no rule of its own for how a body moves. It says when it has arrived, when a partial way has ended, and when `MOB_PATH_STUCK_TICKS` have gone by with no point reached: another mob in the way, or a block placed since. Asking again is up to the mob.

Being shoved off course is not a problem, as it steers to the next point from wherever it is.

## Memory

`MOB_PATH_BYTES`, about 1.7 MiB, checked by the Owner's `static_assert`, most of it never touched. A worker's scratch is 288 KiB of its Job arena, touched as far as a search gets, and allocated by a job only if it claims a search. Nothing here touches the heap after `mob_init`.
