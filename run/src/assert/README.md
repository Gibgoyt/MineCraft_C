# run/src/assert

**Purpose:** assertions that name the file, line, function and expression, then abort.
**Tier:** Owner by depth. It reserves nothing.
**Owns:** nothing.
**Lifetime:** Process.
**Threads:** any. It takes no lock and allocates nothing.
**Public functions:**

- `ASSERT(condition)`: checked unless `NDEBUG`; then not evaluated.
- `ASSERT_ALWAYS(condition)`: checked in every preset.
- `assert_fail(file, line, function, expression)`: what they call; writes one line to stderr and aborts.

**May be called by:** everything in `run/`.
**Depends on:** glibc.
