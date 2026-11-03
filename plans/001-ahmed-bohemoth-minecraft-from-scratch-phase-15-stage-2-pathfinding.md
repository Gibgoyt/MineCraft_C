# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 2: Pathfinding

**Status:** done   (updated 2026-11-03)

## Goal

Bounded A* pathfinding, batched on workers, with path caching exists.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/mob/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/mob/path/`
- `tests/run/mob/path/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `mob` Owner tier: `run/src/mob/index.c` with paired `mob_init` and `mob_deinit`, owning the regions and arenas its parts borrow
- [x] Implement bounded A* in `run/src/mob/path/index.c`
- [x] Batch on workers, and cache paths
- [x] Register test `run_mob_path`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Results

All run on 2026-11-03.

- Debug build: clean, `standards_lint` clean, no recursion.
- `run_mob_path`: passed in debug, release and sanitize: 41 tests, 0 failed, 413230 checks in release.
- `standards_lint`, `clang_format`, `run_game`, `run_entity_drop`, `run_entity_store`, `run_player_interaction`: passed. The full ctest suite was not run.
- Against a separate shortest-path search in the test, over 20 random worlds: 387 ways the same cost, 13 with no way in either.
- Through 4000 random steps of edits and asks: 1787 exact cache answers were byte for byte what a search run then gave, and 189 answers by the rest of a way cost what a search run then found. 2001 entries were spoiled by edits or map changes on the way.
- Same bytes from workers as from none, with 443 to 472 of 512 searches on workers. All workers held busy: the tick still finished, with 0 on workers.
- Real bodies through `entity_tick` walked what it found: round a wall, over blocks, off a ledge, up a slab and stairs, through a pool, up a ladder, stuck on a new wall and round it, and 16 in a crowd.
- Release, on a machine shared with other agents' builds, a whole round's budget of 8192 nodes spent on a goal that cannot be reached: 1.2 to 2.0 ms on workers; alone, as with every worker busy, 4.4 to 5.1 ms, with one run of 9.4 ms. So that worst case can reach or pass the plan's 5 ms sim tick. Not yet judged by `bench_mobs`, which has no AI until Stages 15.4 to 15.6.

Nothing asks for a path in the game yet: Stage 15.4's zombie is the first. In the game itself only opening, ticking with nothing asked and closing has run, through `--walk` in `run_entity_drop`.

## Wiring outside the owned paths

- `run/include/main/index.h`, `run/src/main/index.c`, `run/src/main/README.md`: `mob_t mob` in `app_t`, whose `sizeof` assert went from 13248 to 13696, with `mob_init` and `mob_deinit` in the order and every failure path.
- `run/include/game/index.h`, `run/src/game/index.c`, `run/src/game/README.md`: `game_mob_set`, `mob_open`, `mob_close`, and `mob_tick` before the bodies move, under `--walk`. `borrowed_mob` took 8 bytes of `_pad3`, so `game_t` stays 1152.
- `plans/REPO_STRUCTURE.md`: the row for `tests/run/mob/path/`.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_mob_path$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `2a120a7` Add the mob Owner tier and bounded A* pathfinding, run on workers in batches with a cache that edits spoil.

## Issues found

1. **The plan does not say what happens to a tick when workers are busy.** Expected: batches on workers with `job_group_wait`. Found: a group wait would stall the sim behind whatever terrain jobs are queued ahead, since the shared queue is first in, first out. Done: the sim publishes a round through an atomic cursor and claims searches too, so it waits only for searches already running. See `run/src/mob/path/README.md`. `with_every_worker_busy_a_tick_runs_it_all_itself_and_does_not_wait` holds every worker and shows it.
2. **The first round budget was too big.** Expected: 32768 nodes a tick to fit the 5 ms sim tick. Measured in release: 2.75 ms on workers, 17.3 ms alone. Done: lowered to 8192. Alone it can still reach about 5 ms, as Results says; `MOB_PATH_ROUND_NODES` is the number to lower if `bench_mobs` wants less.
3. **A block written around `world_edit` is not noticed by the cache.** Nothing in the game does this but a column just committed, which `change_count` covers. `tests/run/physics/fixtures/test_world.h` writes directly, so tests here use `world_edit_block_set` for changes after a search. Written down in the module's README. Left as it is.
4. **A cached whole way can miss a shorter way through a column loaded since.** It is still a way, every step of it checked to stand, and it goes when an edit touches its box or the entry is pushed out. Left as it is, and said in the README.
5. **The moves know a body as one cell wide, standing in the middle of it.** Every mob of the plan fits; a wider one would need more. Stairs count as a whole block, so a stair's low half is not used, and nothing jumps a gap. Each only makes a way longer or missing, never one a body cannot walk.
6. **`pgrep -a minecraft_c` in this file's Verification cannot print nothing while other agents test**, as it is not scoped to a worktree: it showed another agent's `build/debug/bin/minecraft_c --headless --menu ...`, left alone. Scoped to this worktree, `pgrep -af "$PWD/build"` printed nothing.
