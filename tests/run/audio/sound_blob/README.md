# tests/run/audio/sound_blob

**Purpose:** ctest `run_audio_sound_blob`: the sound blob linked into the program is the one the build made, and every block that can be mined finds a sound of each kind in it.
**Tier:** none; tests are not tiered.
**Owns:** nothing. Memory comes from the harness's test arena.
**Lifetime:** Process.
**Threads:** one.
**Public functions:** none. It is the ctest `run_audio_sound_blob`, on `tests/harness/`, with its argument from `test.cmake`.
**May be called by:** ctest.
**Depends on:** `tests/harness/`, `asm/src/embed_assets/`, the generated block table and `build/<preset>/generated/sounds/`. It needs no server and no display, and it makes no sound.
