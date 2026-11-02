# run/src/display/device

**Purpose:** the thin layer over the kernel: `display_ops_t`, the check that the display may be taken at all, and the open card.
**Tier:** Arena. It allocates nothing. It maps dumb buffers, which is device memory and the standard's one exception to
"only the heap maps" (`plans/CODING_STANDARD.md` section 4).
**Owns:** the card's file descriptor, and being its master.
**Lifetime:** Process.
**Threads:** main. `display_device_drop_master` and `display_device_console_text` are also safe in a signal handler.
**Public functions:**

- `display_device_real_ops`: the table of real system calls. Tests pass their own.
- `display_device_check_console`, `display_device_refusal_text`: may we try, and if not, what to tell the person.
- `display_device_init`, `display_device_deinit`: a pair. Opens the card, takes master, checks for dumb buffers.
- `display_device_ioctl`: an ioctl on the card, retried a bounded number of times on `EINTR` and `EAGAIN`.
- `display_device_map`, `display_device_unmap`: dumb buffers.
- `display_device_wait_readable`, `display_device_read`: the card's event stream.
- `display_device_console_graphics`, `display_device_console_text`, `display_device_console_is_visible`: `KD_GRAPHICS` and back.
  `display/vt_guard` adds `K_OFF` and `VT_PROCESS` around them.
- `display_device_drop_master`, `display_device_set_master`.

The table's `open_keyboards` is the one call in it that opens anything under `/dev/input`, read-only and never grabbed, and only
`display/vt_guard` calls it, after every refusal has passed.

**May be called by:** `display`, `display/mode`, `display/flip`, `display/vt_guard`.
**Depends on:** `log`, kernel UAPI headers.
