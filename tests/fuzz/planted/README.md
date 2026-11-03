# tests/fuzz/planted

**Purpose:** ctest `fuzz_planted`: that the fuzzer finds what is there. Three defects are planted, a read one byte past the end, a hang and an oracle's refusal, each behind branches that cannot be passed by chance, and each must be found, kept and, but for the hang, made to happen again by `--replay`. Without it, a fuzz that found nothing would prove nothing.
**Tier:** test.
**Owns:** a directory under `/tmp`, removed at the end.
**Lifetime:** One run.
**Threads:** main, and the copies of itself it waits for.
**Public functions:** none; a program.
**May be called by:** ctest.
**Depends on:** `tests/fuzz/support/`.
