# tests/fuzz/support

**Purpose:** the fuzzer itself: coverage from `-fsanitize-coverage=trace-pc`, a corpus, mutation, and a supervisor process that keeps the input at fault.
**Tier:** test.
**Owns:** nothing outside the test arena it is handed. Two `memfd`s, for one call of `fuzz_run`.
**Lifetime:** One run.
**Threads:** main, and one child at a time. A target must not start threads: the coverage map is not atomic, and they would not survive the `fork`.
**Public functions** (`tests/fuzz/support/index.h`):

- `fuzz_run`: runs the program's arguments against a `fuzz_target_t`.
- `fuzz_seed_add`, `fuzz_seed_files`: first inputs.
- `fuzz_fail`: an oracle's way to say that the code under test is wrong.
- `fuzz_buffer_make`, `fuzz_buffer_fit`, `fuzz_buffer_hold`: memory ASan watches to the byte.
- `fuzz_file_read`, `fuzz_get_u32`, `fuzz_get_u64`, `fuzz_put_u32`, `fuzz_put_u64`, `fuzz_rng_next`, `fuzz_rng_below`: what targets need.

**May be called by:** the other programs in `tests/fuzz/`.
**Depends on:** `tests/harness/`, ASan's interface, and glibc's `fork`, `memfd_create`, `dirent`.

## How it goes

`fuzz_run` forks. The child does all of the fuzzing, and the parent only watches it.

The child first runs each seed, then takes an input out of the corpus, stacks one to eight mutations on it, lets the target's `fix` put right what would stop it at the door, and runs it. An input that made any edge run a new size of tick count, as AFL counts them, goes in the corpus. The child's stdout and stderr are a `memfd`, emptied before each input, so that when it dies the log holds only that input's doing.

Before each input the child writes it and how far it has got to the other `memfd`. That is what the parent reads when the child dies, by a report, a signal or `fuzz_fail`, and when it has not moved for `--hang-seconds`. It writes the input to `--crash-dir`, shows the log and the command to run it again. So nothing about a report depends on the dying process being able to do anything.

A child ends through `exit`, so that LeakSanitizer looks at it. A leak found there has no one input to blame.

Mutations: a bit, a byte, a number that often means trouble, a small change to a number, a run removed, copied, overwritten or set to one byte, the end cut off, a word of the format, a run of another input. One in 64 starts from nothing.
