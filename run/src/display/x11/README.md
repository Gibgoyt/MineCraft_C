# run/src/display/x11

**Purpose:** a window on the local X server, so the game runs on a normal desktop such as XFCE. No libX11 or xcb: our own protocol bytes over the server's Unix socket, with frames in MIT-SHM shared memory.
**Tier:** Arena. Takes what it needs from `display`'s arena.
**Owns:** the shared memory: two segments shared with the server and a spare only this process maps (plan 008 phase 02; 8.3 MB each at 1920x1080, 24.9 MB in all), the one connection, and the window on the server, which goes when the connection closes.
**Lifetime:** Process.
**Threads:** main.
**Public functions:**

- `display_x11_init`, `display_x11_deinit`: a pair. `_init` logs why if it fails, and leaves nothing behind.
- `display_x11_arena_bytes`: what `_init` takes from the arena.
- `display_x11_service`: reads what has come without waiting.
- `display_x11_poll`: hands input to a `display_x11_sink_t`, which `main` joins to `input`.
- `display_x11_put_note`, `display_x11_completion_take`: the segments' bookkeeping apart from the socket, for `display_x11_present`, reading a completion, and tests.
- `display_x11_free_buffer`, `display_x11_present`: frames into the segments in turn, never waiting for the server while one is free (plan 008 phase 02, below). Plan 008 phase 01: `put_sent_ns` is when the last put was sent, `put_done_ns` when its `ShmCompletion` was read (0 while it is out), `wait_from_ns` when `display_x11_free_buffer` was last called and `wait_ns` what it took, and phase 02's `puts_skipped` how many frames went into the spare. `game/session` hands them to `game/watch` each frame, for the frame log and F3: the X server's copy of the picture is its own work on the same card, and these say how long it took.

**May be called by:** `display`.
**Depends on:** `arena`, `log`, `time`, `display/flip` (`display_buffer_t`), `display/x11/wire`.
**Parts:** `wire`, the protocol as bytes.

This is the one place sockets are allowed: a Unix socket to a server on this machine. A `DISPLAY` naming another machine is refused, so there is still no TCP anywhere. Plain `shmget` memory is allowed here for the same reason the dumb buffer is in `display/device`: it is shared with something outside the process, not heap.

## Two segments (plan 008 phase 02)

An `ShmPutImage` asks the server to copy from shared memory into the window, and the server answers with an `ShmCompletion` naming the segment (`wire_shm_completion_segment`, bytes 12 to 15). Until then the memory must not be written. With one segment main waited for each completion before drawing the next frame, so a slow copy held main's whole loop (cause C6 in plan 008).

Now there are two, `buffers[0]` and `buffers[1]`, put in turn, each freed by its own completion (`put_pending[i]`). `display_x11_free_buffer` reads what has come without waiting and gives the one not put last if both are free, the other if only that one is. If both are still with the server it gives `DISPLAY_X11_SPARE`: `display_window_run` still calls the frame source, so main takes its picture and keeps its rhythm, into the spare, which is never put, and counts the frame in `puts_skipped`. The window shows the last frame one tick longer, as it would have anyway. If the older of the two has had no completion for five seconds, that is a failure, as it was with one: skipping must not hide a stuck server.

`tests/run/display/x11/` checks this with the server's answers played by the test.

## Using the pointer

While the window has focus the pointer is held in it and hidden, and is warped back to the middle before it can reach an edge. The game reads the difference, so it gets movement the same way it does from evdev.

Key codes are X keycodes minus 8, which is the evdev code. Alt+Tab takes the focus away, which lets the pointer go and lets go of every key; clicking the window takes it back. The click itself does not reach the game.

Each time the window manager takes the focus away, `focus_losses` goes up by one. The game's session reads it each frame and opens the pause screen (plan 005 phase 7). A focus event caused by a grab, such as the keyboard grab XFCE takes while Alt+Tab is held, does not count: its mode, in byte 8 of the event, says Grab or Ungrab (`wire_focus_is_real`). Until plan 005 phase 7 this read byte 1, the detail, as the mode.

## Limits

- Movement goes through the desktop's pointer acceleration, which KMS with evdev does not. Turn it down in XFCE's mouse settings if aiming feels uneven.
- No vsync: the frame rate is paced by a timer, and the desktop's compositor decides about tearing. `display_window_run` says when it will next ask for a frame in `display_t.next_take_ns`, for the gpu thread's start rule (`run/src/game/pace/`).
- One fixed window size, chosen at start.
