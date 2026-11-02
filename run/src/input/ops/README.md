# run/src/input/ops

**Purpose:** every system call the input system makes, behind one table, so that a test can replace all of them.
**Tier:** Arena; allocates nothing.
**Owns:** nothing. A descriptor it returns belongs to the caller.
**Lifetime:** Process.
**Threads:** main.
**Public functions:**

- `input_ops_real`: the table of real calls.

**May be called by:** `input`, `input/discovery`, `input/events`, and tests.
**Depends on:** glibc only.

There is no `EVIOCGRAB` here, on purpose. Stage 05.2's `K_OFF` is what stops keys reaching the console, and a grab that outlived
a crash would leave the machine with no keyboard.
