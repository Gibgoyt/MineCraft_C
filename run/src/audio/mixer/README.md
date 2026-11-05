# run/src/audio/mixer

**Purpose:** turns "play this family's break sound here" into samples: up to 8 voices of mono sound from the blob, panned, added up, turned down by the volume and held to 16 bits. It writes to memory and knows nothing of where that goes, so `ctest` can check every sample and make no sound.
**Tier:** Arena by depth, and it allocates nothing: voices are in its struct, and samples are the blob's.
**Owns:** nothing. It borrows the blob, which lasts as long as the process.
**Lifetime:** Process.
**Threads:** audio, alone. A test's own, in a test.
**Public functions:**

- `audio_mixer_start`: nothing playing.
- `audio_mixer_play`: starts a sound, at random among that family and kind's, but not the one it played last.
- `audio_mixer_playing`: how many are.
- `audio_mixer_render`: the next frames. Silence past the last sound, so it can always be asked.
- `audio_mixer_gains`: what a place in the world comes to for each ear. Pure, and for whoever posts a sound.

**May be called by:** `audio`.
**Depends on:** `types/sound_blob`.

## Choices

- **Whole numbers past the gains.** 32768 is a gain of 1, and two shifts leave a voice at gain 1 and full volume exactly the sample it was.
- **Nine sounds at once:** the ninth takes the voice nearest its end. It is cut short, which can click, and 8 are rarely all in use: a hit lasts at most 170 ms, a break 700 ms.
- **The slider** is a square: half way is a quarter of the amplitude, as an ear hears a straight line as hardly changing until the end.
- **Pan** takes at most half of the far ear, so a block on your left is still in the right speaker. Distance loses nothing up to 6 blocks, past a body's reach of about 5, and everything at 32.
- **Scalar C.** The standard asks for a NASM kernel only if a benchmark shows it's worth it. Eight voices take about 0.2% of the thread's time in a debug build, 37.6 ms for 20 s of sound: `run_audio_mixer` prints it.
