# tests/fuzz

**Purpose:** fuzzers of the three things read from files nobody can vouch for: YAML (`yaml`), PNG and DEFLATE (`png`) and saves (`save`), on a coverage-guided mutator of our own (`support`), with a test that it would find something (`planted`).
**Tier:** test.
**Owns:** nothing outside its test arena and its temporary directories.
**Lifetime:** One run.
**Threads:** main, and a child per run.
**Public functions:** none; programs.
**May be called by:** ctest, for 20 s each, and a person, for as long as they like.
**Depends on:** `tests/harness/`, and what each one tests. Only the `sanitize` preset builds them, from `cmake/hardening.cmake`.

## Running them

    tools/test-or-build.sh cmake --build --preset sanitize
    tools/test-or-build.sh timeout 700 build/sanitize/bin/fuzz_yaml --seconds 600 --max-crashes 16 --crash-dir /tmp/mc_fuzz_crashes

Each takes:

| Flag | Meaning | Default |
|---|---|---|
| `--seconds <n>` | How long | 30 |
| `--seed <n>` | Which run this is: the same seed tries the same inputs, up to what a clock changes | 1 |
| `--max-execs <n>` | Stop after this many inputs | No limit |
| `--hang-seconds <n>` | One input running this long is a failure | 60 |
| `--max-crashes <n>` | Go on after a failure, with the next seed, until this many | 1 |
| `--crash-dir <dir>` | Where the inputs at fault go | `/tmp/mc_fuzz_crashes` |
| `--replay <file>` | Run that one input, in this process, under a debugger if wanted | |

Run from the top of the tree, or the seeds that are files will not be found, and it starts from less. A failure prints what the sanitizer said, the path of the input and the command that runs it again.

**What it cannot see:** ASan knows the buffers `support` poisons, and what `arena` poisons: all of an arena that has not been handed out. So a buffer given to a function is watched to the byte, and an overrun past an arena's last allocation shows. One from one allocation into the next does not, nor one inside a struct. Each target's README says what its oracles hold.
