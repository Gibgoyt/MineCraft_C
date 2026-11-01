# Heavy commands go through the mutex

Every build, test, lint and benchmark in this repository runs as:

```bash
tools/test-or-build.sh cmake --build --preset debug
tools/test-or-build.sh timeout 300 ctest --preset debug --output-on-failure
```

**Why.** Parallel agents work in separate git worktrees on one machine. nvcc and LTO links are memory-heavy and the GPU
is shared with Ahmed's desktop, so only one heavy command may run at a time.

**How it works.** The wrapper runs the command under `flock` on the absolute path `/tmp/mc_c_build.lock`. The path must
stay absolute and outside the checkout, because all worktrees have to share one lock. Waiting for the lock is normal:
the wrapper says so on stderr and then blocks with no timeout of its own.

**Rules.**

- Wrap the configure step too: `tools/test-or-build.sh cmake --preset debug`. It compiles test programs with nvcc.
- Wrap a single command. To run several, call the wrapper once for each, so the lock is released between them.
- Put `timeout` inside the wrapper, not outside: `tools/test-or-build.sh timeout 300 ctest ...`. A `timeout` outside
  also counts the time spent waiting for the lock, and killing `flock` leaves the command running without the lock.
- Never nest the wrapper. The inner call would wait forever on the lock the outer call holds.
- The wrapper returns the command's exit status, so a failed build stays a failure.
- Never leave anything running. After a test, confirm with `pgrep -af "$PWD/build"`.
- Do not delete `/tmp/mc_c_build.lock` to "fix" a wait. That breaks the exclusion for everyone. Find out who holds it
  with `fuser -v /tmp/mc_c_build.lock`.

A phase file's Verification section that lists an unwrapped heavy command is a bug in that file. The one accepted
exception is work done before this wrapper existed (plan section 6).
