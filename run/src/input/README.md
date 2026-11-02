# run/src/input

**Purpose:** raw keyboard and mouse input from evdev: finds the devices, follows hot-plug, and adds up each poll into one frame.
**Tier:** Owner.
**Owns:** one `heap_region_t`, with `arena_setup` over it, which `input/events` and `input/discovery` allocate from at init. The
parts it starts: `events`, `discovery`. `ops` is a table and needs no starting.
**Lifetime:** Process.
**Threads:** main.
**Public functions:**

- `input_init`, `input_deinit`: a pair. `_init` logs what it found; no devices is a warning, since one may yet be plugged in.
- `input_poll`: the frame since the last poll. Never blocks, allocates or logs, so it can run while the console is dark.
- `input_report`: logs what the polls counted. For after the console is back.
- `input_cursor_move`: moves a position by a frame's mouse movement, inside a rectangle.
- `input_test_pattern_cursor`: what `--test-pattern` draws its cursor square from. Esc ends the run; holding a key or button changes the square's colour.
- `input_test_pattern_discard`: throws away what waited while another VT was on screen. See below.

**May be called by:** `main`, `game`.
**Depends on:** `heap`, `arena`, `log`.

## When it may run

Reading `/dev/input` reads the keys of whoever is at the machine, inside X too. So `input_init` is only called with the real
directory by a program that holds the console: `main` calls it for `--test-pattern`, after `display_init` said OK, and no sooner.
Under X, in a pipe or under ctest, `display_init` refuses first and this never runs, which `run_input` checks on the built
program. Tests give `input_init` a directory of their own, and either a fake kernel or real calls on FIFOs in a temporary
directory. Neither reaches a real device.

It does not grab anything: see `input/ops`.

## The console's own keyboard

During `--test-pattern`, `display/vt_guard` has the console in `K_OFF`, so keys reach this code and not the shell. The guard's
emergency exit reads the keyboards for itself, in another process, and uses nothing here.

## Another VT on screen

Nothing here is polled while another VT is showing, but the evdev queues go on filling. What they hold on the way back was typed
at something else, so `input_test_pattern_discard` takes it in without acting on it: an Esc pressed in X does not end the run. It
reads it because the state of what is held has to stay right, and it keeps nothing else.
