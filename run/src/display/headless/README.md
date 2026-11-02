# run/src/display/headless

**Purpose:** the no-display backend: one frame in ordinary memory, a timed script of key and mouse events, and PNG screenshots at chosen ticks. It is how an agent or a test drives the program and looks at the result, because only Ahmed can see KMS.
**Tier:** Arena. Everything comes from the `arena_t` its parent passes in; `display_offscreen_init` in `run/src/display/` owns that.
**Owns:** nothing on the heap of its own. The frame, the script's text and events, and the PNG buffer are all arena memory.
**Lifetime:** Process.
**Threads:** the one calling `display_headless_run`, and it runs the scene on that thread too.
**Public functions:**

- `display_headless_arena_bytes`: how much arena `_init` will ask for.
- `display_headless_init`, `display_headless_deinit`: a pair. `_init` reads and parses the script, and refuses a bad one by line number.
- `display_headless_run`: runs every tick through a `display_headless_scene_t`.
- `display_headless_screenshot`: writes the frame as a PNG now, for a caller outside a script.

**May be called by:** `display` (the Owner), `main`, `game`.
**Depends on:** `arena`, `file`, `log`, `display/flip` (`display_buffer_t`), `display/headless/{png,script}`.

**Parts:** `png` (the encoder), `script` (the language), `demo` (what is drawn until `game` exists).

It opens no `/dev/dri` card and no `/dev/input` device, and the code path has no way to. So it is safe with a desktop running.

## Running it

    timeout 60 build/debug/bin/minecraft_c --headless --script tests/run/display/headless/fixtures/tour.script \
        --screenshot-dir /tmp/mc_shots --width 640 --height 360

Then look at `/tmp/mc_shots/*.png`. Always run it under `timeout`. One line per screenshot goes to stderr, then a count of frames. `--headless` needs a script, or `--ticks <n>` to run `n` ticks with no input.

| Flag | Meaning | Default |
|---|---|---|
| `--script <path>` | The input script | None |
| `--screenshot-dir <dir>` | Where screenshots go, made if missing | None; a script with screenshots must have it |
| `--ticks <n>` | Run exactly `n` ticks. Refused if that is shorter than the script, or if the script has its own `end` | Until the script's last line |
| `--width <w>`, `--height <h>` | Frame size, 1 to 16384 each | `display:` in `config/machine.yaml`, 1920x1080 today |

Exit status 0 means every tick ran and every screenshot is on disk. Status 2 is a bad command line and 8 a bad script, a failed scene or a screenshot that could not be written, with the reason on stderr.

## Script language

One action per line. `#` starts a comment. Blank lines are fine.

    <when> key <name> down|up           a key
    <when> button <name> down|up        a mouse button: left right middle side extra
    <when> move <dx> <dy>               relative mouse motion, whole numbers up to 1000000 either way
    <when> wheel <steps>                wheel steps, up to 1000 either way; positive is away from the player
    <when> screenshot [name.png]        the frame of that tick; the default name is tick_00000040.png
    <when> end                          the last tick; nothing may follow

`<when>` is one of:

| Form | Meaning |
|---|---|
| `40` | Tick 40. Ticks count from 0 |
| `+5` | 5 ticks after the line before |
| `1500ms` | The tick at this time, rounded down, at `game.tick_hz` from `config/machine.yaml` (20 today: tick 30) |
| `+250ms` | The same, after the line before |

Rules, each an error naming the line if broken:

- Time may not go backwards. Lines on one tick apply in file order.
- A key name is a letter or digit, `f1` to `f12`, `space enter escape tab backspace delete insert home end pageup pagedown up down left right lshift rshift lctrl rctrl lalt ralt capslock minus equal comma dot slash semicolon apostrophe grave leftbrace rightbrace backslash`, in any case, or `code:<n>` for any evdev code below 768.
- A screenshot's name uses letters, digits, `_ - .`, ends in `.png` and does not start with `.`, so it cannot leave `--screenshot-dir`. No two screenshots may share a name. At most 1024 screenshots, 65536 events and 1 MiB of script.
- A run ends after the tick of `end`, otherwise after the script's last line, unless `--ticks` gives more.

An example:

    # Walk forward, look up and to the right, and mine.
    0      screenshot before.png
    2      key w down
    +3     move 60 -40               # tick 5
    5      screenshot mining.png     # shows this tick's input, whatever the order of these lines
    5      button left down
    1500ms key w up                  # tick 30 at 20 Hz
    +0     button left up
    +10    screenshot                # tick 40, as tick_00000040.png
    +1     end

## What a tick does

For each tick from 0, in this order, with nothing between ticks:

1. All of the tick's events go into `display_script_state_t`: held keys, added-up mouse and wheel.
2. `scene->tick`, then `scene->draw` on the frame.
3. The tick's screenshots are written, so a screenshot always shows the input of its own tick. Writing is atomic, through `file_write_atomic`.

**A run does not wait for the wall clock.** Ticks follow one another as fast as they can be done and nothing reads a clock, so the same script at the same size gives byte-identical PNGs: the files can be compared directly, which is what a golden image test needs. A tick is a tick of simulated time, not 50 ms of real time.

## Driving real gameplay

`main` runs the scene in `display/headless/demo` for now. `game` passes its own scene when it exists (Stage 08.4): `tick` steps the simulation, `draw` renders into the `display_buffer_t` it is handed, which is the same type as a KMS back buffer. Scenes read `tick->state` for what is held and `tick->events` for what changed. `code` is evdev's own `KEY_*` or `BTN_*`, and `move` and `wheel` carry what `REL_X`, `REL_Y` and `REL_WHEEL` would, so the code that turns these into player input is the same code that handles Stage 05.3's real events.

## Screenshots

8-bit RGB, no alpha, filter 0 on every row, stored DEFLATE, one IDAT. 1080p is 6.2 MB. Any PNG reader opens them. See `png/README.md`.
