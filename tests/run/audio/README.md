# tests/run/audio

**Purpose:** ctest `run_audio`: the whole of `run/src/audio/`, thread and ring and all, joined to the stand-in in `fixtures/` or to a file. A sound posted comes out whole and soon, at volume 0 it is zeros, with no server there is silence and one log line, a server going away is the same, and it takes nothing it does not give back. Then the program itself mines, with its sound in a file.
**Tier:** test.
**Owns:** nothing past each test.
**Threads:** main, the `audio` thread, and the stand-in's.
**Depends on:** `tests/harness/`, `fixtures/`, a card for the test of the program, which is `--headless`.

By hand, and never by ctest, the real thread shows it stays up on the real server, with nothing posted, volume 0, and so silence alone:

    tools/test-or-build.sh timeout -k 3 120 build/debug/bin/tests/run_audio build/debug/bin/minecraft_c --real-server-silence

As ctest runs it, no test here connects to a real sound server, and none can make a sound: the stand-in keeps samples in memory, the no-server test names a path with nothing there, and the program runs `--headless` with stdin from `/dev/null`, either of which `main` takes as no sound. So does an X display from `:80` up, which this holds to its range.
