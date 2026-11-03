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
- `58451ab` Save fluid levels with the blocks so a reloaded world does not flood, keep the block tick count, and spill a lost chest before a save.
- `6853ca5` Wake what a save held waiting, show saved changes in the far field, keep a refused world from ending the game, check what a power cut leaves against every fsync, add a thread sanitizer preset, and let the release build link.

## Issues found

1. **Expected:** the test rig's square window all streamed in. **Actual:** `CHECK failed: column != nullptr` and `WORLD_EDIT_STATUS_NOT_LOADED`, as the map streams a circle of `table_radius`, not its square, so corners are never offered. **Done:** the rig opens a table large enough for the square to fit inside its circle. The test was wrong, not the map.
2. **Expected:** only structs that go to disk need `alignof` and every `offsetof`. **Actual:** `struct_no_offsetof_assert`, 51 breaches: `lint_catalog_struct_is_shared` goes by path, so every struct under `run/include/save/` counts as shared. **Done:** asserted all of them, the in-memory ones too. Stage 16.2's headers will meet the same rule.
3. **Process slip:** twice the test binary ran directly and not through `tools/test-or-build.sh`: the mutation run above, and once more by mistake under `timeout 100` just before the first commit, with its output thrown away, so it counts for nothing here. Everything reported under Results went through the wrapper.
4. **Left for Stage 16.2, by its scope:** a save happens only at a world's close, and when a set fills up as columns leave. A kill during play loses everything since the world opened. `save_world_capture_dirty`, `save_world_commit_begin` and `save_world_commit_write` are split so that its autosave can run the write on a worker. It must also keep `game_column_leaving_save`'s own write from running during one of its own, and the phase gate's "at most one autosave interval" is not met until then.
5. **Not verified at the time: a power cut; see issue 9 for what has since been checked.** `SIGKILL` tests show a process's death at every step and at random moments, which leaves the page cache intact. Whether the data really reaches the platter depends on the order of `fsync`s, which is by reading the code: temporary file, rename, directory, then manifest through `file_write_atomic`. `strace` is not installed here to show the order of the calls, and no test here can cut the power.
6. **Product gaps found, not fixed, out of this stage's scope:**
   - The far field (`render_lod_sample_terrain`) samples terrain and ignores deltas, as it ignores live edits, so a changed place looks as generated from past full detail.
   - Fluids and other scheduled ticks are not saved. A section read back has its blocks with nothing scheduled, as a generated one has, so water that was flowing stops until something next to it changes.
   - What is in a chest or a furnace is not a block, and Stage 16.2's list (player, inventory, entity, time) does not name it. Reopening a world keeps the chest and empties it. **Escalated to Ahmed** by being here: `run/src/item/container/` needs a home in a save.
   - A world refused at open, for another seed or damage, ends the whole program with why in the log and its files untouched, as any `game_open` failure already does in `game_session_advance`. A message on the menu would be kinder.
7. **Size:** `tests/run/save/world/index.c` is 1856 lines, under the hard limit of 2000 and past the split at 1500, as are four other tests. Left as one file, since it tests one directory.
8. **Found at the phase gate: issue 6's fluids flooded, rather than stopped.** Expected, as issue 6 says: flowing water stops on a reload. Actual, by reading `world_tick_fluid_level`: a fluid voxel with no level saved is a source, so every flowing voxel would come back a source and the water would spread. Done in `58451ab`: `SAVE_WORLD_VERSION` 2, and a record can end in its section's levels, read back by `world_tick_section_levels_restore`. Also found then: a level changing with no block changing told no one, so the save missed it: `tick_level_save_flag`. Tests `a_record_can_end_in_levels_and_levels_no_fluid_has_are_refused`, `flowing_water_comes_back_with_its_levels_goes_on_and_does_not_flood`.
9. **Gate check: what became of issues 5 and 6.**
   - **Power cut (issue 5): checked as far as anything but pulling the plug can.** New ctest `run_save_power`, see `tests/run/save/power/README.md`: it records each `open`, `mkdir`, `rename`, `unlink` and `fsync` of a world made and saved 6 times, standing in front of libc's in that one program, and opens all a file system keeping only what `fsync` promises could leave. Result: 92 calls, 1082 cuts, none broken, on the code as fixed here. **It found a real defect.** Expected: a world reported made is there after a power cut. Actual, with `file_make_dirs` as it was: `cut after op 6 (fsync dir /saves/w): keeps 0 of 1 changes, none of the others': 1 broken`: the parent of a new directory was never synced, so `saves/<world>` could vanish with all its saves, though each said it had finished. Done: `file_make_one_dir` syncs the parent of what it makes. A real pull of the plug, past any disk's cache, stays with Ahmed: see the phase file.
   - **Whether it can fail:** each of these was applied, built, run and taken back: no parent sync; no `fsync` of a delta; no directory sync before the manifest; no `fsync` in `file_write_atomic`; no directory sync in `file_write_atomic`; the old state forgotten before the blocks; the old delta removed before the manifest. All 7 caught, each at a named cut.
   - **Far field (issue 6): fixed.** Expected: a changed place looks changed from far off. Actual: `render_lod_sample_terrain` only. Done: `render_lod_overlay_set` with `save_world_far_line` writes what is saved over each sample, on the workers. `save_world_capture_*` tells a ring of what has been captured, `game_far_changes_take` takes it before each `render_lod_frame`, and `render_lod_column_changed` has each level make that column again, showing the old until the new is ready. An edit reaches the far field when it is captured: at the next autosave, within 30 s of world time, or when its column leaves. Until then it is drawn block by block, but for the ring the map keeps past `full_detail`. A sample stands for its whole voxel, so a coarse level shows a one-block change only if a sample lands on it, as for anything else out there. A game with no save, which is every mode but `--menu`, still shows only terrain. Test `the_far_field_shows_what_a_save_holds_and_makes_again_what_is_captured`: on the card, a tower raises the finest level's top from 308 to 756, stays from the files alone after a reopen, and goes back to 308 when dug out. Mutations caught: no overlay; not told; a column being remade not counted as shown (`shown_side` 11 to 0).
   - **A world refused at open (issue 6): fixed.** Expected, once it is a thing a person can meet: the program goes on. Actual: exit 5 with why only in a log nobody under KMS can read. Done: `game_session_world_refused` goes back to the list, with `Could not open it: <why>` on it from `ui_menu_message_set`. `game_open` failing is still the end of the run, as that is the card or the memory. Test `a_world_refused_at_open_is_said_on_the_list_and_another_can_be_played`, on the built program: a bad manifest is refused and left byte for byte, the next world on the list opens, it exits 0, and a screenshot of the list differs from the same one before. `run_save_world`'s check of another seed expected a non-zero exit, and now expects 0: that premise changed on purpose. Mutations caught: nothing said; refusal fatal again (exit 5).
   - **Fluids and scheduled ticks (issue 6): see Stage 16.2's issue 9.**
