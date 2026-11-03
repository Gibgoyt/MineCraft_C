# 001-ahmed-bohemoth-minecraft-from-scratch — phase 16, stage 1: World files

**Status:** done   (updated 2026-11-03)

## Goal

World files exist from seed plus changed-section deltas, with a versioned header and crash-safe rename.

**Wave:** W9. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-16-saving.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/save/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/save/world/`
- `tests/run/save/world/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `save` Owner tier: `run/src/save/index.c` with paired `save_init` and `save_deinit`, owning the regions and arenas its parts borrow
- [x] Implement delta files with a versioned header in `run/src/save/world/index.c`
- [x] Write through a crash-safe rename
- [x] Test reload byte for byte, and kill during save, registered as `run_save_world`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_save_world$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Wiring outside the owned paths

- `run/include/main/index.h`, `run/src/main/index.c`: `save_t save` in `app_t`; `save_init`, `save_deinit` and `game_save_set` with `--menu`.
- `run/include/game/index.h`, `run/src/game/index.c`: `game_save_set`, `borrowed_save` in `game_t` and in each column job; a job writes what is saved over what it generates; `game_sim_step` captures a column before unloading it, where Stage 07.3 left a note for this stage.
- `run/src/game/session/index.c`: `save_open` before `game_open`; `save_world_flush` before `game_close`; `save_close` after.
- READMEs of those three; `plans/REPO_STRUCTURE.md`.

## Results

    tools/test-or-build.sh cmake --build --preset debug                                        no errors, lint and recursion check clean
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_save_world$' ...          Passed, 22.6 s: 23 tests
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' ...          Passed
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^clang_format$' ...            Passed
    pgrep -a minecraft_c                                                                       nothing

Also run: `run_game`, `run_settings`, `run_settings_session`, `run_types` and `run_ui_menu` passed with this wiring, and `run_save_world` passed under the `sanitize` preset, 30.6 s. One random-kill run: 24 kills over 448 saves, 18 of them leaving files behind, each world then equal to the model of exactly one save.

Whether the tests can fail: with `save_world_commit_write` changed to retire its set and publish its manifest in two steps instead of one, `readers_on_threads_never_miss_a_section_between_a_set_and_its_file` failed (`wrong` not 0). The change was then taken back.

## Commits

Appended as they land:

- `f7e78c7` Keep a world on disk as its seed and the sections that changed, with versioned headers and a crash-safe rename.

## Issues found

1. **Expected:** the test rig's square window all streamed in. **Actual:** `CHECK failed: column != nullptr` and `WORLD_EDIT_STATUS_NOT_LOADED`, as the map streams a circle of `table_radius`, not its square, so corners are never offered. **Done:** the rig opens a table large enough for the square to fit inside its circle. The test was wrong, not the map.
2. **Expected:** only structs that go to disk need `alignof` and every `offsetof`. **Actual:** `struct_no_offsetof_assert`, 51 breaches: `lint_catalog_struct_is_shared` goes by path, so every struct under `run/include/save/` counts as shared. **Done:** asserted all of them, the in-memory ones too. Stage 16.2's headers will meet the same rule.
3. **Process slip:** twice the test binary ran directly and not through `tools/test-or-build.sh`: the mutation run above, and once more by mistake under `timeout 100` just before the first commit, with its output thrown away, so it counts for nothing here. Everything reported under Results went through the wrapper.
4. **Left for Stage 16.2, by its scope:** a save happens only at a world's close, and when a set fills up as columns leave. A kill during play loses everything since the world opened. `save_world_capture_dirty`, `save_world_commit_begin` and `save_world_commit_write` are split so that its autosave can run the write on a worker. It must also keep `game_column_leaving_save`'s own write from running during one of its own, and the phase gate's "at most one autosave interval" is not met until then.
5. **Not verified: a power cut.** `SIGKILL` tests show a process's death at every step and at random moments, which leaves the page cache intact. Whether the data really reaches the platter depends on the order of `fsync`s, which is by reading the code: temporary file, rename, directory, then manifest through `file_write_atomic`. `strace` is not installed here to show the order of the calls, and no test here can cut the power.
6. **Product gaps found, not fixed, out of this stage's scope:**
   - The far field (`render_lod_sample_terrain`) samples terrain and ignores deltas, as it ignores live edits, so a changed place looks as generated from past full detail.
   - Fluids and other scheduled ticks are not saved. A section read back has its blocks with nothing scheduled, as a generated one has, so water that was flowing stops until something next to it changes.
   - What is in a chest or a furnace is not a block, and Stage 16.2's list (player, inventory, entity, time) does not name it. Reopening a world keeps the chest and empties it. **Escalated to Ahmed** by being here: `run/src/item/container/` needs a home in a save.
   - A world refused at open, for another seed or damage, ends the whole program with why in the log and its files untouched, as any `game_open` failure already does in `game_session_advance`. A message on the menu would be kinder.
7. **Size:** `tests/run/save/world/index.c` is 1856 lines, under the hard limit of 2000 and past the split at 1500, as are four other tests. Left as one file, since it tests one directory.
