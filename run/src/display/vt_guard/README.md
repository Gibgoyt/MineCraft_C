# run/src/display/vt_guard

**Purpose:** takes the console for KMS and makes sure it comes back: `K_OFF`, `VT_PROCESS` switching, `KD_GRAPHICS` through `display/flip`, a crash handler, a watchdog, an emergency key combination, and `--crash-test` to prove them.
**Tier:** Arena. It takes one block from the arena it is given, at `_init`, for the signal dispositions and the crash handler's stack.
**Owns:** the guardian process and our end of its pipe, while armed. It owns no memory beyond that block.
**Lifetime:** Process.
**Threads:** main. The handlers may run on any thread, and only set flags or run the restore, which is safe from anywhere.
**Public functions:**

- `display_vt_guard_init`, `display_vt_guard_deinit`: a pair. `_init` touches nothing; `_deinit` gives back what is still owed.
- `display_vt_guard_take_console`: everything below, in order, ending in `display_flip_take_over`.
- `display_vt_guard_give_back`: the normal way out.
- `display_vt_guard_pet`, `display_vt_guard_service`: once a frame. The first tells the watchdog we are alive, the second does the VT switching.
- `display_vt_guard_keys_take`: pure; what one key event asks for.
- `display_vt_guard_draw_notice`: writes the emergency exit across the top of a buffer.
- `display_vt_guard_crash_test_from_name`, `display_vt_guard_crash_now`: `--crash-test`.

**May be called by:** `display`, `render/frame` (Stage 08.4).
**Depends on:** `display/device`, `display/flip`, `arena`, `time`, `log`.

## Getting out

| What | How | Works when |
|---|---|---|
| **Emergency exit** | **Hold Ctrl+Alt, press Backspace** | Any time our VT is on screen, however stuck the program is. Needs a keyboard under `/dev/input` that this user can read |
| Watchdog | By itself, after 8 seconds without a frame | Program hung or stopped, with or without a keyboard |
| Switch VT | Ctrl+Alt+F1 to F12 | The program is alive enough to answer `SIGUSR1`; if it is not, the watchdog ends it |
| Time limit | By itself: `--test-pattern` ends 20 seconds after start | Always, unless hung, and then the watchdog |
| From elsewhere | `pkill minecraft_c` over SSH | Runs the full restore. `pkill -9` does too, through the guardian |

The console says all this for 3 seconds before it goes dark, and the combination stays on screen in a red band across the top of the test pattern.

If no keyboard can be opened, the notice says so in capitals, and `K_OFF` is **not** used, since it would leave nothing to get out with: Ctrl+C and the kernel's own Ctrl+Alt+Fn keep working.

Magic SysRq is not part of this. It is `16` on this machine (`/proc/sys/kernel/sysrq`), which allows sync only. `Alt+SysRq+K` would need `sudo sysctl kernel.sysrq=1`, and that is Ahmed's choice to make.

## Two layers

**In this process:** a handler on the signals below, through the one `borrowed_` static the lint allows, on its own stack so that a stack overflow can run it. It restores everything, says so, and dies of the same signal, so the exit status and core dump stay true: `SIGSEGV SIGBUS SIGFPE SIGILL SIGABRT SIGSYS SIGTRAP SIGXCPU SIGXFSZ SIGALRM SIGVTALRM SIGPROF SIGSTKFLT`. `SIGTERM SIGINT SIGHUP SIGQUIT` are `display`'s, as a request to stop.

**The guardian:** a second process, forked before anything is taken, ended by the closing of a pipe. So it cannot be left behind. It covers what a handler cannot:

| Case | What happens |
|---|---|
| `SIGKILL`, or a stack overflow on a thread with no alternate stack, or a fault inside the handler | The pipe closes without the program having said "done", so the guardian restores from its own copy of the state |
| Memory scribbled over | The same: the guardian's copy was made at fork |
| Hang, or `SIGSTOP` | No pet for 8 s: `SIGTERM`, a second later `SIGKILL`, a second later the restore regardless |
| Stuck in a driver call | The same. It may not be able to kill the program, but it can still give the console back |
| Ctrl+Alt+Backspace | The same as a hang, immediately |

It shares the card and console descriptors, so it can do the whole restore, and it reads the keyboards itself, so a stuck program does not stop the keys. It stays in the session, because `KDSETMODE` and friends need the tty as controlling terminal, but takes a process group of its own, because `timeout` signals its whole group. It ignores every signal it can, except that `SIGKILL` of it directly loses this layer: the program's pets then fail quietly and the handler still works.

The restore is a list of steps, each claimed once by an atomic counter, so any thread may run it and a fault in one step resumes at the next. Order: the CRTC, `DROP_MASTER`, `KD_TEXT` (all three in `display_flip_restore`); keyboard mode; `VT_RELDISP(1)` in case a switch is waiting on an answer that is never coming; `VT_MODE`.

## VT switching

`K_OFF` also stops the kernel seeing Ctrl+Alt+Fn, so the guardian reads those keys and asks for `VT_ACTIVATE`. Then, as for a switch from anywhere:

1. Kernel: `SIGUSR1`. `display_vt_guard_service`: drain, the saved CRTC, `DROP_MASTER`, `VT_RELDISP(1)`. The other VT appears.
2. While away, it pets the watchdog and touches nothing.
3. Kernel: `SIGUSR2`. `VT_RELDISP(VT_ACKACQ)`, `SET_MASTER` with retries, `SETCRTC` to the front buffer.

The keyboards are read whichever VT is showing, so every key that means something is checked against `VT_GETSTATE` first: Ctrl+Alt+Backspace in X does nothing here.

## Why the keyboard here and not `input`

`input` (Stage 05.3) is for the game, and runs in the program. This has to work when the program does not. **It relies on nobody using `EVIOCGRAB`**, which would hide the keys from it.

## What has been checked

Every path above has run in `tests/run/display/vt_guard/`, against a fake kernel, in forked children that really die. `real_open_keyboards` and the real `KD*`/`VT_*` calls have not run at all: tests may not touch a real console or a real keyboard. That is Ahmed's run, in `plans/*-phase-05-stage-2-vt-guard.md`.
