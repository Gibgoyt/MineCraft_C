# run/src/display/flip

**Purpose:** two dumb buffers, the modeset, page flips and their events, and giving the display back. Also the test pattern's pixels.
**Tier:** Arena. It allocates nothing from the arena: the buffers are device memory, mapped through `display/device`.
**Owns:** the two `display_buffer_t`: each one's dumb buffer handle, framebuffer and mapping.
**Lifetime:** Process.
**Threads:** main. `display_flip_restore` is also safe in a signal handler.
**Public functions:**

- `display_flip_init`, `display_flip_deinit`: a pair. Makes the buffers; removes them, after restoring if that is still owed.
- `display_flip_take_over`: `KD_GRAPHICS`, then `SETCRTC` to buffer 0.
- `display_flip_back`, `display_flip_submit`, `display_flip_wait`, `display_flip_take_events`, `display_flip_drain`: the loop.
- `display_flip_restore`: the saved CRTC, then `DROP_MASTER`, then `KD_TEXT`, once.
- `display_flip_suspend`, `display_flip_resume`: a VT switch away and back, for `display/vt_guard`.
- `display_flip_assume_taken`: for the guardian's copy of this struct, made before the takeover.
- `display_flip_pattern_pixel`, `display_flip_pattern_fill`, `display_flip_pattern_frame`: the test pattern.

**May be called by:** `display`, `display/vt_guard`, and `render/frame` for presenting (Stage 08.4).
**Depends on:** `display/device`, `display/mode`, `time`, `log`.

## Lessons kept from the KMS spike (Stage 01.2)

- The console shows nothing between `take_over` and `restore`, so failures in that time are recorded and logged afterwards.
- `modeset_done` is set before `SETCRTC`, since a failed one may still have disturbed the CRTC.
- The back buffer is only written with no flip pending: the last event proved it is off screen.
- Dumb buffer memory is written and never read, and is repainted as little as possible.
- The restore order is the CRTC, then master, then `KD_TEXT`. A saved CRTC that was off, or has nothing to go back on, goes back
  to off, and a failed restore turns it off so that fbcon can set it up again.

The spike has not yet run on real hardware, and neither has this. Whether `nvidia-drm` accepts these calls is what Ahmed's run
is for.
