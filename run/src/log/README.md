# run/src/log

**Purpose:** one line per message, written whole, with its level and where it came from.
**Tier:** Owner by depth. It reserves nothing; each line is built in a fixed stack buffer.
**Owns:** nothing. It borrows the descriptor it writes to.
**Lifetime:** Process. It works before anything is initialized, so it has no `_init`.
**Threads:** any. Lines do not mix, because each is one `write`. Not for hot paths: it formats and makes a syscall.
**Public functions:**

- `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`: printf-style, and checked by the compiler as printf is. Cast floats
  at the call: `LOG_INFO("%.2f ms", (libc_double_t)elapsed)`.
- `log_set_level`, `log_get_level`: the lowest level written.
- `log_set_fd`: where lines go; starts at stderr. Tests point it at a pipe.
- `log_format_line`: formats without writing.
- `log_write`: what the macros call.

**May be called by:** everything in `run/`.
**Depends on:** glibc.
