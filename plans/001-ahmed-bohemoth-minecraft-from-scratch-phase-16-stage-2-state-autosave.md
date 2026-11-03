# 001-ahmed-bohemoth-minecraft-from-scratch — phase 16, stage 2: State and autosave

**Status:** done   (updated 2026-11-03)

## Goal

Player, inventory, entity and time persistence exists, with background autosave and no frame hitch.

**Wave:** W9. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-16-saving.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/save/state/`
- `run/{src,include}/save/autosave/`
- `tests/run/save/state/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Persist player, inventory, entity and time state in `run/src/save/state/index.c`
- [x] Implement background autosave on workers in `run/src/save/autosave/index.c`
- [x] Measure frame time during autosave
- [x] Register test `run_save_state`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_save_state$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Wiring outside the owned paths

- `run/include/save/index.h`, `run/src/save/index.c`: `save_t` holds `state`, `autosave` and a second arena, `arena_state`; `save_open` opens both; `save_close` waits for a write under way; the region grows by `SAVE_STATE_ARENA_BYTES`.
- `run/include/main/index.h`: `app_t`'s size assert, 15552 to 15808, for that.
- `run/include/game/index.h`, `run/src/game/index.c`: `game_save_finish`; `game_open` takes up a kept player, time and the rest, and starts the autosave; `game_sim_step` calls `save_autosave_step`; `game_column_leaving_save` no longer writes, and asks the autosave instead.
- `run/src/game/session/index.c`: `game_save_finish` in place of `save_world_flush` as a world closes, and one line of autosave numbers in the log.
- From the phase gate. `run/include/world/tick/index.h`, `run/src/world/tick/index.c`: levels to and from a save, `world_tick_now_set`, `world_tick_column_saved`. `run/include/item/index.h`, `run/src/item/index.c`: `item_world_edits_read`. `run/{include,src}/render/lod/`: the overlay and making a column again. `run/src/game/settle/index.c`: `game_far_changes_take`. `run/{include,src}/ui/menu/`: `ui_menu_message_set`. `run/src/file/index.c`: parents synced. `run/include/thread/index.h`: `thread_fence_seq_cst`, used by `run/src/job/index.c` and `run/src/player/camera/index.c`. `cmake/flags.cmake`, `cmake/check_link.cmake`, `CMakeLists.txt`, `CMakePresets.json`: the `tsan` preset. `tests/run/world/tick/`, `tests/run/save/power/`, which is new. `plans/REPO_STRUCTURE.md`, `plans/CODING_STANDARD.md`.
- `run/include/item/index.h`: a comment that said nothing is saved until this stage.
- READMEs of `game`, `save` and `save/world`; `plans/REPO_STRUCTURE.md`.
- **Two other stages' tests, whose premise this stage changes on purpose. See issue 2.** `tests/run/settings/fixtures/again.script`, `tests/run/ui/menu/index.c`.

Added beyond the todo's list, in the owned paths, because leaving them out loses items: chests, furnaces, crafting grids and what the pointer carries. Stage 16.1's issue 6 had escalated chests and furnaces. Also a second ctest, `run_save_state_game`, so that `run_save_state` needs no card.

## Results

Debug preset, through `tools/test-or-build.sh`, at the commit below apart from the doc edits made since:

    cmake --build --preset debug                                   no errors, lint and recursion check clean
    ctest -R '^run_save_state$'                                    Passed, 2.87 s: 9 tests
    ctest -R '^run_save_state_game$'                               Passed, 16.09 s: 3 tests, on the card
    ctest -R '^standards_lint$'                                    Passed
    ctest -R '^clang_format$'                                      Passed
    pgrep -a minecraft_c                                           nothing
    Whole debug suite, ctest with no filter                        94 of 94 passed
    sanitize preset: run_save_state, run_save_state_game, run_save_world   all passed, no sanitizer report

### Frame time during autosave

Debug build, real threaded game on the card at 640x360, render distance 4, a player digging, no vsync, one run. 4 s with no save coming, then 8 s with a save every 4 ticks, 150 times oftener than the real 30 s:

| | Count | Median | 99th | Longest |
|---|---|---|---|---|
| `game_loop_frame`, no saves | 1875 | 0.058 ms | 0.106 ms | 0.144 ms |
| `game_loop_frame`, during saves | 3748 | 0.058 ms | 0.107 ms | 0.206 ms |
| Between new pictures, no saves | 1870 | 2.125 ms | 2.204 ms | 4.333 ms |
| Between new pictures, during saves | 3747 | 2.125 ms | 2.203 ms | 2.361 ms |

40 saves, 0 failed, 54 sections written, 6 blocks dug. The longest save took 0.677 ms of the sim and 1.296 ms of a worker. `game_loop_frame` alone leaves out the gpu thread's drawing, which is why the gap between pictures is measured too.

The sim's part of one save, from `run_save_state`, debug: 0.400 ms with few things (126760 bytes); with every store full (1583744 bytes) 0.333 ms, 2.210 ms with 64 sections changed, 16.446 ms with 512. A tick is 50 ms. These vary by run: an earlier one had 0.260 ms and 5.703 ms for the first and last, with cells placed differently. Not measured: a release build, a real-time 1080p run, or a spinning disk: `/tmp` here is memory, so a worker's time says little about a real disk, though nothing but a worker waits for it.

### The gate: a kill loses at most an interval

- Killed at each of the six steps of a save, with the state on disk before the manifest: blocks and state were one save or the other every time.
- 12 random `SIGKILL`s across 265 saves at a save every 4 ticks: what was read back was never older than the last save reported finished, nor newer than the last started, and its blocks were exactly those of its tick. The most ticks lost was 5, when a write was under way.
- The built program killed in play: state at 600 ticks at save 1, 606 after running on, with what was dug and nothing past reading.
- Reload byte for byte: a filled rig, and the real game after digging, each saved again with nothing ticked, gave the same file. 40 further ticks from the reloaded rig gave the same bytes as from the original.

### Whether the tests can fail

Each was applied, run, and taken back, and the tree confirmed clean afterwards:

| Mutation | What caught it |
|---|---|
| Blocks written before the state | Both kill tests, and the disk test |
| A retried save takes a fresh image | The disk test |
| Minds not put back | The byte-for-byte test |
| The slot check skipped | The forged numbers test |
| The sim waits for the write | The stuck write test, which hangs until its 100 s limit: caught by the limit and not by a check |

## Commits

Appended as they land:

- `03624ea` Keep the player, inventory, entities and time beside each save of the blocks, and save them all on a worker every 30 seconds of world time.
- `4654786` Record what state and autosave do, what they cost a frame and what a kill loses.

## Issues found

1. **An earlier attempt's work could not be picked up, so this was written again.** Expected: `git merge --no-edit worktree-wf_916eee71-48c-139` brings in its work. Actual: `Already up to date`; its report says it never committed, and the work stayed as uncommitted files in its worktree. `git -C` on that worktree was refused: "this command redirects git to the shared checkout via -C". The brief also says never to touch another worktree, so its files were not read or copied, by any means, and they are still there, unread. Done: this implementation is new. It used that attempt's scripts and logs in this session's scratchpad for design and wiring, including most of its `run/src/game/` wiring. Measurements here are from this code, not from its report. Its worktree can be discarded or compared by whoever looks after them.
2. **Two tests' premise changes on purpose.** Expected: a world opened again starts as new. Actual, after the wiring, as designed: it goes on. `run_settings`: `CHECK failed: fresh_4 <= 2u` at 941 thousandths and `fresh_2` at 831, as `again.script` turned the look a second time on a look now kept; the move is taken out. `run_ui_menu`: `world_a == world_b: first difference at byte 52`, as the light has moved on; now fewer than a thousandth of the bytes may be more than 16 off, and it measures 30 of 922218. That check is looser than it was, and it still fails on another place or look. These are outside the owned paths, and are here because leaving them red would be worse. `run_save_world` was kept passing unedited by giving the state its own arena.
3. **Refused commands.** The guard refused, as too complex to verify or as leaving the worktree, four of my commands: the `git -C` above, a compound read-only inspection, a `find` using a shell variable, and a compound that wrote a README and configured. None was retried in a form meant to get around it: the `git -C` was dropped, and the others were re-run as plain, separate commands inside this worktree.
4. **Mutation runs left temporary directories.** A test that dies in a `REQUIRE` or a hang never reaches its cleanup: `/tmp/mc_save_state_EJNPz1` and `oKTW5D`, dated to the second by those runs, were removed by name. `/tmp/mc_save_state_JMaPOo`, dated 06:58, is older than this work, is not mine, and was left. A `REQUIRE` failing with workers running also crashes the harness's teardown: the first mutation exited 139 after its checks had already failed. Only a failing test does this.
5. **Process slip.** `clang-format -i` on the files this stage touched ran directly, not through `tools/test-or-build.sh`. Every build and test went through it.
6. **Product gaps, left alone, for Ahmed to weigh.**
   - Scheduled block ticks in `world/tick` are not saved, so water flowing when a world closes stops until something next to it changes. Stage 16.1 found this and the stage's list does not name it.
   - An edit made after `item_world_tick` in the same tick a save is captured reaches the file as a block change the item stores never heard of. A creeper's blast or `world_tick_step` taking a chest in exactly that tick leaves its record and loses its items without a spill. It needs that one tick in 600. Not reproduced: found by reading the order of `game_sim_tick`.
   - What a mind last heard from a path search is not saved, so a mind asks again after a reload. So "going on the same" is tested with no mind ticking, and the minds' own state is compared only as bytes.
   - With more dirty sections than one set holds (64 MiB or 32768), one save holds part of what changed, and the rest follows at once. That one case can mix two moments. Not tested: reaching it takes more changes than a test here makes.
   - A carried stack on the pointer stays invisible after a reload until a screen opens. It is not lost.
   - `look_word` is 0 until the first frame sends it. No session saves before a frame, so this cannot reach a file from the game.
   - A state file refused at open stops the whole program with why in the log, as any `game_open` failure does: Stage 16.1's note on a kinder message applies. So the checks must never refuse what real play makes. That has been run through about 12 s of real play and short built-program runs, not hours: Stage 18.1's soak should look at it.
   - A world with blocks saved and no state file starts its state afresh and logs it, rather than refusing. A crash cannot cause that, by the write order, but a deleted file would silently lose the inventory but for that log line. Chosen so that a world saved by `save_world_flush` stays openable.
7. **Not verified at the time: a power cut,** for the same reason as Stage 16.1's issue 5: `SIGKILL` keeps the page cache. The order of syncs is by reading the code. **Also not verified then:** a thread sanitizer run, as the sanitize preset is ASan and UBSan. Both since done: Stage 16.1's issue 9 and issue 10 here.
8. **Size:** `run/src/game/index.c` is 1626 lines, past the split at 1500 and under the 2000 limit. It was 1565 before this stage, also past it. Left as it is, since a split of it belongs to no one stage. Now 1676 with the gate's wiring, and `tests/run/world/tick/index.c` 1801 with its tests.
9. **Gate check: what was waiting for a tick.** Expected: a reload goes on. Actual, after `58451ab` woke flowing voxels only: a source or sand placed and saved before its first tick waited until something next to it changed. Done: `world_tick_column_saved`, called by `game_column_fill` for every column of a world with a save, wakes what has something to do as it stands, in a saved section or touching one, and remembers those sections, so that a neighbour arriving later wakes along the seam, whichever came first. The sim's cost: 0.396 ms for one saved section and 27.090 ms for all 48 of a column in a debug build, 0.033 ms and 2.737 ms in release, one run of each. Waits are not kept, so a retry due at a delay starts over, and what a tick's limits had pushed on starts in its turn: a wait is at most a few ticks longer after a reload. Tests: six in `run_world_sim`, `what_was_waiting_for_its_first_tick_goes_on_after_a_reload` in `run_save_state_game`. Mutations caught: the game not calling it (sand stays, water does not spread); no seam (`pending` 0, not 1); every fluid woken (`pending` 4096, not 1156).
10. **Gate check: threads.** Expected: a thread sanitizer can be run. Actual: no preset, and GCC refuses `atomic_thread_fence` under it (`-Werror=tsan`), which `standards_lint` will not let a `-Wno-` get past. Done: preset `tsan`, and `thread_fence_seq_cst`, a locked instruction on a local in that build alone. Those fences order a store against a later load, and all that hands memory over is the acquires and releases, so the sanitizer misses no edge for it. Run on `tsan`, through the wrapper, against the built program under `tsan` too: `run_save_power`, `run_save_state`, `run_save_world`, and of `run_save_state_game` the far field, the real game's threads during saves, a refused world and the built program killed in play, each by `TEST_FILTER`, on the final tree; its other tests on the tree just before the far field's ring moved to the arena. **No data race reported in any of them.** `run_save_world` exits 1 there, with one test failing that is not a race: `killed_at_any_moment_it_loses_at_most_the_save_in_progress` wants kills to land in saves (`interrupted > 0u`, `confirmed > SW_KILL_ROUNDS`), and under the sanitizer the children get too few saves done before their kill. So that test's own point goes unproved on this preset; it passes in debug, sanitize and release, and left as it is, as the preset is run by hand. `run_save_state_game`'s far field test takes over 10 minutes under it, past the preset's limit of 300 s, so it ran under its own timeout with `TEST_FILTER`. Whether it can fail: with the autosave's `writing` made relaxed, it gave 97 reports and exit 66, and 0 once taken back.
11. **Found at the phase gate and fixed in `58451ab`, logged here as that commit did not.** With tests in `run_save_state` and `run_save_state_game`:
    - The block tick count started at 0 on each open while the world's ticks went on: random ticks' numbers repeated. Done: `world_tick_now_set`.
    - A chest or furnace an edit took after `item_world_tick` in the very tick of a save reached the file as gone, with its items in no record's reach. This is the second gap of issue 6. Done: `item_world_edits_read` from `game_save_items_settle` just before a save spills them. Test `the_game_keeps_what_flows_the_tick_count_and_what_a_chest_gone_this_tick_held`.
    - Issue 6's first gap, as a flood: Stage 16.1's issue 8, with the level that changed and told no one.
