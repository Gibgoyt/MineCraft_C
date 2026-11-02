# run/src/file

**Purpose:** every file operation the game makes, with short counts and signals dealt with, and with failures as a status
a caller can act on, disk full above all.
**Tier:** Owner by depth. It reserves nothing: callers bring the buffers, and the only memory here is stack for paths.
**Owns:** nothing on the heap. A `file_t` holds one descriptor, and whoever opened it closes it.
**Lifetime:** Process. There is no state, so it has no `_init`. A `file_t` lasts from `file_open` to `file_close`.
**Threads:** any. One `file_t` may be shared across threads for `file_read_at` and `file_write_at` only, since the others
move the file position.
**Public functions:**

- `file_status_name`: a status as text for a log line.
- `file_open`, `file_close`: a pair; `file_close` reports a failed close, which can mean lost data.
- `file_read`, `file_read_exact`, `file_write_all`: at the file position, through short counts and signals.
- `file_read_at`, `file_write_at`: at an offset, without moving the position.
- `file_size`, `file_sync`: of an open file.
- `file_read_whole`: a whole file into the caller's buffer, including files in `/proc` and `/sys`.
- `file_write_atomic`: replaces a file so that a crash leaves old or new, never a mix.
- `file_path_size`, `file_remove`, `file_rename`, `file_make_dirs`, `file_sync_dir`: by path.

**May be called by:** everything in `run/` from this layer up.
**Depends on:** glibc.
