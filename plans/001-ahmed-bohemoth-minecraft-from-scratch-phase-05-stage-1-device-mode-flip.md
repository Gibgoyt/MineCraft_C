# 001-ahmed-bohemoth-minecraft-from-scratch — phase 5, stage 1: Device, mode and flip

**Status:** in progress: written and passing against a fake kernel, never run on a real card; awaits Ahmed's run below   (updated 2026-11-02)

## Goal

Raw-ioctl card open, connector and mode pick, dumb buffers and page flip exist. Proof: Ahmed sees a test pattern on a VT.

**Wave:** W4. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-05-display-input.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/display/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/display/device/`
- `run/{src,include}/display/mode/`
- `run/{src,include}/display/flip/`
- `tests/run/display/mode/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `display` Owner tier: `run/src/display/index.c` with paired `display_init` and `display_deinit`, owning the regions and arenas its parts borrow
- [ ] Open the card from `machine.yaml` using raw ioctls and kernel UAPI headers only, in `run/src/display/device/index.c` (written; passes against the fake kernel; opening a real card is Ahmed's run)
- [x] Pick connector and mode in `run/src/display/mode/index.c`, honoring `connector: auto`
- [ ] Create dumb buffers and page flip in `run/src/display/flip/index.c`, using the Stage 01.2 results (written; passes against the fake kernel; Stage 01.2 has no results yet, only its source and lessons; real flips are Ahmed's run)
- [ ] Add a `--test-pattern` mode (written; the whole loop passes against the fake kernel; seeing the pattern is Ahmed's run)
- [x] Fail fast with a clear message when run from inside X
- [x] Test mode picking with fixture data in `tests/run/display/mode/index.c`, registered as `run_display_mode`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_display_mode$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing
    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 30 build/debug/bin/minecraft_c --test-pattern

## How Ahmed runs it

Build first, from the integration branch, with `tools/test-or-build.sh cmake --build --preset debug`. Then press Ctrl+Alt+F3, log in, and from the repo root:

    timeout 30 build/debug/bin/minecraft_c --test-pattern 2>&1 | tee /tmp/mc_c_test_pattern.txt; echo "exit=${PIPESTATUS[0]}"
    pgrep -a minecraft_c    # must print nothing

What you should see: eight colour bars over a grey ramp, a thin white border around the edge, and an orange square moving back and forth. It ends by itself 20 seconds after it started, the text console comes back, and it prints the flip timings. Do not switch VT during the run. To choose the output, add `--connector HDMI-A-1` or `--connector eDP-1`; to choose the card, `--card /dev/dri/card0`.

| Exit | Meaning | Next step |
|---|---|---|
| 0 | It ran and gave the display back | Check by eye that the picture was right and the console came back. Compare the `rate` line with the mode's refresh |
| 6 | Refused, and nothing on the display was changed. The line says why | If it names a display server owning the card: stop it by hand, run again, start it again. Agents never do this |
| 7 | KMS failed partway. The `display:` or `test pattern:` line names the call and the error | Record it in this file. This is the result the run exists to find |
| 124 or 137 | It did not end by itself, which is a bug | Record it. If the screen stays dark, Ctrl+Alt+F1 or F2 should reach X |

A crash leaves the console in graphics mode until Stage 05.2: see issue 3 below.

## Results

**No run on hardware. Nothing about a real card, real flips or the console coming back has been observed.** Everything below ran on the fake kernel in `tests/run/display/mode/fixtures/fake_card.h`, or got no further than refusing. `/dev/dri/card*` was never opened and no KD or VT ioctl was made on a real tty.

Run 2026-11-02, gcc 16.2.1, after merging the integration branch (with `math` and `simd` in it):

| Command | Result |
|---|---|
| `tools/test-or-build.sh cmake --build --preset debug` | Clean; `standards_lint: no recursion` |
| `ctest --preset debug`, everything, including `run_display_mode`, `standards_lint`, `clang_format`, `run_types` | 31 of 31 passed |
| `build/debug/bin/tests/run_display_mode build/debug/bin/minecraft_c` | 73 tests, 981 checks, 0 failed |
| Sanitize preset, `run_display_mode` | Passed. Release preset, `run_display_mode`: passed |
| `DISPLAY=:0 timeout 10 build/debug/bin/minecraft_c --test-pattern --card /nonexistent/mc_c_no_such_card </dev/null` | `display: refused: DISPLAY is set, so this is running inside X ... The card was not opened`, `heap at exit: 0 reserved`, exit 6 |
| The same with `WAYLAND_DISPLAY` set and `DISPLAY` unset | `refused: WAYLAND_DISPLAY is set`, exit 6 |
| The same with both unset | `refused: stdin is not a Linux virtual console`, exit 6 |
| `timeout 10 build/debug/bin/minecraft_c </dev/null` (the default run) | Core runtime only, nothing live at exit, exit 0 |
| `pgrep -a minecraft_c`, `pgrep -af "$PWD/build"` | Both print nothing, rc 1 |

Read from sysfs only, no device opened: `/sys/module/nvidia_drm/version` is `615.71.09`, and its `modeset` and `fbdev` parameters are both `Y`, on kernel `7.2.7-arch1-1`. So the driver offers KMS on this machine. Whether its dumb buffers and flips work is still what Ahmed's run finds out.

**Do the tests catch anything?** Ten changes were made to the code in turn, and the test program was run against each, on debug. Nine failed straight away: the restore order swapped, `DISPLAY` not looked at, `display_init` ignoring a refusal, `modeset_done` not set, no 1 Hz tolerance, the front buffer never swapping, the square never erased, `SET_MASTER` failure ignored, a free CRTC not preferred. The tenth, a short event length not stopping the parser, passed, which was a gap in the test: issue 7. It fails now. All were put back; `git status` showed no change to the source after.

**Why the card cannot be reached in those runs.** The refusal message for stdin or `DISPLAY` comes from a check that runs before `open_card`. A run that got past the check would have printed the refusal for a card that will not open, since that path does not exist, and the tests assert the message that says it did not get that far.

## Commits

Appended as they land:

- `0775443` Add the KMS display backend with raw-ioctl card, mode and flip parts behind a replaceable kernel layer, a refusal before the card is opened under a display server, and --test-pattern.
- `dd1bba2` Record the Stage 05.1 results and issues, and make the display test catch an event length shorter than its header.

## Issues found

1. **Noted ahead of this stage by the Phase 03 gate check.** `game_runs_the_core_runtime_and_ends_with_nothing_live`, `game_exits_with_its_own_status_on_a_leak`, and `game_ignores_a_missing_feature_the_build_does_not_use` in `tests/run/types/index.c` run `minecraft_c` in its default mode, or with only a `--pretend-*` flag. That is harmless while no mode opens a display. Once this stage makes KMS the default, those ctest runs must move to headless, or a test would try to take the card. Not started: this stage's code does not exist yet.
   - **Outcome, 2026-11-02:** this stage did not make KMS the default. Only `--test-pattern` opens the display, and the default run still starts just the core runtime, so those three ctest runs stay harmless and were not moved. Whichever stage first gives `minecraft_c` a default mode that opens the display (08.4 at the latest) owes this change, and needs the headless backend from Stage 05.4 to do it. The comment on `main_run_test_pattern` says that only this flag opens the display.

2. **Stage 01.2 has no results to use, and the spike itself has never run on hardware.**
   - **Expected:** the todo says to create dumb buffers and flip "using the Stage 01.2 results".
   - **Actual:** Stage 01.2 is still `in progress`; its Results section says "No run on hardware yet". By instruction for this run, later stages proceed on the assumption that dumb buffers, modeset and page-flip events work on `nvidia-drm`. Nothing has confirmed that.
   - **Done:** used the spike's source and lessons instead: the guard before opening, the restore order, `modeset_done` set before the attempt, nothing printed while the console is dark, the deadline counted from process start, refresh computed from timings, and the tier order. The risk entry in Stage 01.2 stays open, and also covers this stage. `--test-pattern` answers the same question as the spike, so Ahmed's run of either answers it for both.

3. **A crash does not restore the console in this stage. That is a limit and not an oversight.**
   - **Expected:** the phase's goal is that the console is always restored, and the spike's own crash handler restored it.
   - **Actual:** a crash handler needs a `borrowed_` static, and `compile/src/standards_lint/catalog/` allows those only in `run/src/display/vt_guard/index.c`, which is Stage 05.2's. So `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL` and `SIGABRT` here die without the restore, with the VT left in `KD_GRAPHICS`. `SIGKILL` can never be handled. `SIGTERM`, `SIGINT`, `SIGHUP` and `SIGQUIT` do run the full restore, tested. `VT_PROCESS` is missing too, so a VT switch mid-run is only noticed, within about 120 frames, and then ends the run.
   - **Done:** left to Stage 05.2 as planned. `display_flip_restore` uses only ioctls and flags, so that its handler can call it, and it runs once. The risk during Ahmed's run is stated under the table in "How Ahmed runs it", and the 20-second self-limit keeps `timeout` from being what ends it. `display_device_console_*` is what 05.2 is to take over.

4. **The fake kernel is written from reading the UAPI and from memory of the kernel, not from a driver.**
   - **Expected:** a fake is only as good as what it is modelled on.
   - **Actual:** its answers (modes copied only if all of them fit, as many CRTCs copied as fit, `EBUSY` on a flip with one pending) come from what I know of `drm_mode_getconnector` and `drm_mode_getresources`. The kernel source is not on this machine, and they were not checked against it. What `nvidia-drm` answers is not modelled at all.
   - **Done:** left as it is, and said so here and in `tests/run/display/mode/README.md`. A disagreement would show on Ahmed's run as exit 7 naming the call.
   - **Tried on 2026-11-02, and refused:** fetching `drm_connector.c`, `drm_mode_config.c` and `nvidia-drm-drv.c` with `curl` from GitHub into `/tmp/mc_c_assets/kms_refs/`, to check the fake against them. The worktree guard refused each `curl` (`this command names git in a form too complex to verify`). Not worked around. The fake stays unchecked.

5. **Wiring outside the owned paths.**
   - `run/src/main/index.c`, `run/src/main/README.md`, `run/include/main/index.h`: the `--test-pattern`, `--card` and `--connector` flags, `display_t` in `app_t` (704 bytes more: 448 to 1152 once merged with `simd` and `math`), `display_deinit` in `app_deinit`, and exit codes 6 and 7. The smallest wiring that lets the module be called.
   - `plans/REPO_STRUCTURE.md`: one row under `tests/`, for what `run_display_mode` covers.
   - **Not edited:** the phase file's todo, since it is not this stage's file. Its box for Stage 05.1 stays open until Ahmed's run.

6. **Two choices the plan does not make.**
   - **`--card` and `--connector` flags.** The stage asks for neither. They are here because the spike found them needed (two connectors are connected on this machine, and `auto` may not pick the one Ahmed is looking at), and because a test that gives a path that does not exist cannot reach a card even if the check before it broke.
   - **Refresh within a tier.** The spike takes the fastest mode. This takes the nearest to `display.refresh_hz` once that is met, so that a 240 Hz panel runs at the 120 the yaml asks for. So on a monitor over 120 Hz the two may report different modes. The tier order itself, including a fast mode of another size beating a slow one at 1080p, is the spike's, kept so that the runs agree. It is a judgement call that a game with a fixed render size may want reversed, and it is one line in `display_mode_tier`.

7. **Defects found by running this stage's own tests. All fixed.**
   - `a_sequence_of_zero_says_nothing_and_a_wrap_is_not_a_gap` failed: `FAIL ... (1 of 3 checks)`. A driver going from sequence 0 to a real one was counted as a missed vblank, because only the new sequence was checked for 0. Now both are.
   - The sanitize preset failed with `AddressSanitizer: use-after-poison` at `tests/harness/index.c:243`. The test's fault, not the program's: it built arenas on the test arena and never called `arena_deinit`, so the harness's clearing hit memory the arena had poisoned. Tests now call it.
   - The mutation "a length under 8 does not stop the event parser" passed all the tests. The first test written for it passed too, since the event it placed at offset 4 overwrote the very length it was testing. `malformed_event_blocks_end_quietly` now lays the bytes out so that stepping by 4 finds a flip event, and that mutation fails it.
   - `tests/run/display/mode/index.c` reached 1817 lines, over the standard's 1500-line mark. The fake kernel moved to `fixtures/fake_card.h`, leaving 1401.

8. **Merging the integration branch conflicted, and the display test could crash.**
   - **Merge:** `plans/REPO_STRUCTURE.md`, `run/include/main/index.h` and `run/src/main/README.md` conflicted with the Stage 04 work, since both sides added to `app_t`. All were kept: `job`, `simd`, `display`, `math`. `sizeof(app_t)` is now 1152, and its `static_assert` says so. `run/src/main/index.c` merged by itself; one comment line over length was rewrapped.
   - **Expected:** `run_display_mode`, run by hand with no argument, fails its checks. **Actual:** `Segmentation fault`, rc 139, after `CHECK failed: t->arg_count == 1`. `REQUIRE` inside the helper `run_game_pattern` ended only the helper, so the test went on to read an uninitialized `test_process_t`. ctest always passes the argument, so it never saw this. The inside-X test could also return with `DISPLAY` still set.
   - **Done:** the helper returns whether the program ran, and each test requires that after putting the environment back. By hand with no argument it now exits 1 and does not crash. Checks went from 984 to 981 as a result. No other helper in the file has a `REQUIRE`.
