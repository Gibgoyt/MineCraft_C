# tests/run/file

**Purpose:** ctest `run_file`. Tests `run/src/file/`, including what ordinary use never reaches: short counts and signals
on pipes, a full disk through `/dev/full`, a file size limit through `RLIMIT_FSIZE`, and a writer killed in the middle of
`file_write_atomic`.
**Tier:** test.
**Owns:** nothing outside the test arena, and files under the scratch directory it is given.
**Lifetime:** One run.
**Threads:** main, short-lived threads it joins, and child processes it kills or waits for.
**Public functions:** none; a test program.
**May be called by:** ctest. `test.cmake` passes `build/<preset>/test_tmp/run_file` as the scratch directory.
**Depends on:** `tests/harness/`, `run/src/file/`.

The kill test proves that a process crash cannot leave a mix. A power cut is what the `fsync`s are for, and no test here
can prove that.
