# run/src/time

**Purpose:** the one clock the program measures and paces itself by.
**Tier:** Owner by depth. It reserves nothing.
**Owns:** nothing.
**Lifetime:** Process. There is no state, so it has no `_init`.
**Threads:** any.
**Public functions:**

- `time_now_ns`: monotonic nanoseconds, the same timeline on every thread.
- `time_sleep_ns`: sleeps at least this long, through signals.
- `time_sleep_until_ns`: sleeps to an absolute reading, for fixed-rate loops.
- `time_ns_to_seconds`, `time_ns_to_ms`: for printing and for rates, not for comparing.

**May be called by:** everything in `run/` from this layer up.
**Depends on:** `assert`, `types`, glibc.
