# run/src/display

**Purpose:** the two display backends: KMS, which takes the display from a text console, picks an output, shows pixels and gives it all back; and `headless`, which has no display at all.
**Tier:** Owner.
**Owns:** one `heap_region_t`, with `arena_setup` over it, which `display/mode` and `display/vt_guard` allocate from at init. The parts it starts:
`device`, `mode`, `flip`, `vt_guard`. Apart from those, one more region and arena in `display_offscreen_t`, which `display/headless` allocates from.
**Lifetime:** Process.
**Threads:** main. The signal handler only sets a flag.
**Public functions:**

- `display_config_from_machine`: the `display:` keys of `config/machine.yaml`.
- `display_init`, `display_deinit`: a pair. `_init` refuses first (see below) and changes nothing on screen; `_deinit` gives
  back whatever is still owed. `_init` takes a `display_ops_t`, so a test can run all of it against a fake card.
- `display_offscreen_config_from_machine`, `display_offscreen_init`, `display_offscreen_deinit`: a pair and its defaults. They reserve for and start the no-display backend, and touch no card and no console. How to use it is in `headless/README.md`.
- `display_test_pattern_run`: `--test-pattern`. Bars and a moving square, flipped at the mode's rate until it ends by itself
  or a signal stops it, then the display goes back and the flip timings are logged.
- `display_test_pattern_set_cursor`: adds a cursor square, placed each frame by a function it is given, and a second function
  called once on coming back from another VT, to throw away what waited meanwhile. `main` joins these to `input`, so that this
  system does not include that one.

**May be called by:** `main`, `game`.
**Depends on:** `heap`, `arena`, `log`, `time`, generated `machine/index.h`.

## Refusing

`display_init` runs in this order, and stops at the first that says no. A display server keeps the display in every case.

| Check | Touches | Refused when |
|---|---|---|
| Environment | Nothing | `DISPLAY` or `WAYLAND_DISPLAY` is set |
| `fstat` of stdin | Nothing | Not a character device of major 4, minor 1 to 63: pipes, `/dev/null`, pseudo terminals, `/dev/tty0` |
| `VT_GETSTATE`, `KDGETMODE` on stdin | Reads only | Not the visible console, or not in `KD_TEXT` |
| Opening the card | The card, for the first time | `EBUSY`, `EINVAL`, `EACCES`, `EPERM`, or the file is missing |
| `DRM_IOCTL_SET_MASTER` | Becomes master | Another client is master |

The order matters, because opening a card that has no master makes the opener its master, and on `nvidia-drm` that can
reach the driver's own ownership: see the second gate check in `plans/*-phase-01-stage-2-kms-flip.md`. So under X or in a
pipe nothing here ever opens the card. Exit status 6 means one of these said no.

## What the test pattern does and does not protect

All of it runs behind `display/vt_guard`: see its README for the emergency exit (Ctrl+Alt+Backspace), VT switching, the crash
handler, the watchdog and the guardian process. `SIGTERM`, `SIGINT`, `SIGHUP` and `SIGQUIT` end the loop and run the full
restore. It also ends by itself 20 seconds after the process started, below the `timeout 30` it is run under. `--crash-test`
kills it on purpose after 240 frames, to prove the console comes back.
