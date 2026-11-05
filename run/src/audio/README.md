# run/src/audio

**Purpose:** sounds while you play: mining, breaking and placing. The sim posts an event on a ring, one thread mixes, and what comes out goes to the desktop's sound server. With no server the game plays silently and carries on.
**Tier:** Owner. Calls `heap_reserve` once, at start, and only for a run with sound. The ring, voices and connection are in `audio_state_t` at the start of that region: `audio_t`, under `app_t`, is its handle, as main's stack has no room for them.
**Owns:** `owned_region` and its arena, the thread, and through `audio/pulse` the connection.
**Lifetime:** Process.
**Threads:** the `audio` thread, on the spare CPU from `threads:` in `config/machine.yaml`, runs all of it. `audio_post` is the one thread holding the map. `audio_volume_set` is any.
**Public functions:**

- `audio_init`, `audio_deinit`: a pair. `_init` cannot fail: everything that can go wrong comes to silence and one log line, as no sound is worth not playing for.
- `audio_post`: a sound for the thread to start. Never waits and touches no heap.
- `audio_volume_set`, `audio_volume_get`: what the Settings slider says, and one sound to judge it by.
- `audio_connected`: whether a server is taking what is sent.
- `audio_display_is_private`: whether a `DISPLAY` is a test's own.

**May be called by:** `main`, and `game`, which posts what the hands did and passes on the volume.
**Depends on:** `arena`, `file`, `heap`, `log`, `thread`, `time`, `simd/embed_assets` for the sounds, `display/x11/wire` for what a `DISPLAY` says, `audio/mixer`, `audio/pulse`.
**Parts:** `mixer`, voices to samples in memory; `pulse`, the server's protocol; `pulse/wire`, that protocol as bytes.

## When there is sound

`main` asks for `AUDIO_OUTPUT_SERVER` only for a game a person started on a terminal and looks at on their own desktop or text console: walking or the menu, not `--headless`, not with stdin from anything but a terminal, not on an X display from `:80` up, which is where agents' private `Xvfb`s are, and not with `--no-sound`. A window needs a terminal, so an agent testing one has a made-up terminal: the display number is what keeps that from Ahmed's speakers. So no test, script or agent following the rules can ever be heard. `--headless --sound-file <path>` writes what would have played to a file, as raw s16le stereo at 48000 Hz at the speed it would play, which proves a mined block reaches the mixer with no sound at all.

## With no server

The thread looks, finds none, logs one line and looks again every 5 seconds, without saying so. Until then, events are thrown away as they come, so nothing mined a minute ago plays when a server comes. If a server goes away the same thing happens, with the same one line.

Connecting and everything that could wait on a server happens on the audio thread. No frame waits on it.
