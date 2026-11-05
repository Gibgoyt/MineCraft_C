# tests/compile/sound_pack

**Purpose:** ctest `compile_sound_pack`: proves that recipes make the sounds they should, that the committed `sounds/` gives every family every kind, that a blob comes out the same every time, and that each mistake in a tree is refused.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `compile_sound_pack`, on `tests/harness/`, with its arguments from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `compile/src/sound_pack/` (run as a process, and `synth` and `mcsnd` linked directly), and `cp` and `rm` from the system. It needs no server, no display and no network, and it makes no sound: everything is bytes in memory.

What this can't tell you is how any of it sounds. Brightness here is counted in zero crossings, which is a rough measure and says only that the families differ in the direction they were meant to.

The blob linked into the game is checked by ctest `run_audio_sound_blob`, in `tests/run/audio/sound_blob/`.
