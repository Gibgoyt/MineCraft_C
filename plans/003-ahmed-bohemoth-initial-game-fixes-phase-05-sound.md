# 003-ahmed-bohemoth-initial-game-fixes — phase 5: Basic sound

**Status:** not started   (updated 2026-11-04)

## Goal

Mining makes a crunch that matches the block: while you dig, when a block breaks, and when you place one. Sound plays alongside everything else on your desktop, there's a volume setting, and with no sound server the game plays silently and carries on.

**Ahmed's words:** "basic sound needs to be added please, get the basic crunch sounds from whereever please when mining different blocks"

Plan 001 left audio out of the first release, so this is new work. Music, footsteps, and mob and ambient sounds aren't part of this phase.

## Getting sound out

**Checked on this machine, 2026-11-04:** PipeWire 1.6.9 runs the audio. `pactl` reports `PulseAudio (on PipeWire 1.6.9)`, at 48000 Hz stereo, with the default sink on the WD19 dock. `/run/user/1000/pulse/native` exists and is open to Ahmed.

**Approach:** our own PulseAudio native protocol client over that Unix socket, the same idea as `run/src/display/x11/`: our code, no libpulse.

**Why not the sound card directly:** raw ALSA ioctls on `/dev/snd/pcmC0D0p` would need the device PipeWire uses. Either the game or the desktop would lose its sound, and it would skip the desktop's volume and output choice.

**Decided by Ahmed, 2026-11-04: sound uses a second socket.** It's the second exception to the no-sockets rule, recorded in `plans/CODING_STANDARD.md` section 1. It's local only, with no TCP, and only `run/src/audio/pulse/index.c` may use it. The first is the X server's, in `run/src/display/x11/index.c`.

**Unknowns, settled in stage 1:** no official protocol document turned up. The wire format would come from reading PulseAudio's source and a Rust reimplementation, used as reference only. Whether `pipewire-pulse` behaves the same as PulseAudio for this is unconfirmed.

## Where the sounds come from

**First choice: Kenney's Impact Sounds.** Its page states CC0, with 130 files. That page doesn't list materials or the file format, so whether it covers stone, wood, dirt, gravel and the rest is **unconfirmed** until it's downloaded and heard.

**If a material is missing:** another CC0 source, checking each file's licence, or a crunch we synthesize ourselves at compile time. No Minecraft sounds.

**Same path as textures:** cloned or downloaded to `/tmp/mc_c_assets/`, licence checked, converted one time to raw PCM (`ffmpeg` is installed, but isn't needed by the build), and committed in our own format. So there's no OGG decoder to write, and builds need no network.

```
sounds/<GROUP>/<NAME>/index.{yaml,mcsnd}, README.md
sounds/ATTRIBUTION.md
```

## Which block makes which sound

One readable field in each block's YAML, like everything else there:

```yaml
# config/blocks/dirt/index.yaml
sound: dirt
```

Material families: `stone`, `wood`, `dirt`, `gravel`, `sand`, `grass`, `leaves`, `snow`, `glass`, `metal`. Each has a few short variants, picked at random so repeated hits don't sound identical.

| When | Sound |
|---|---|
| Each few ticks while mining | A short hit from that block's family |
| Block breaks | A fuller crunch |
| Block placed | A short thud |

Sounds fade with distance and pan left or right, since mobs and explosions will want that later.

## Design

- **New system:** `run/src/audio/` (Owner), with `audio/mixer` and `audio/pulse/wire`, split the way `display/x11` is, so the protocol can be tested with no server.
- **Thread:** one `audio` thread takes the spare in `config/machine.yaml` (`threads:`), leaving none reserved for the OS. **To check that this doesn't cost frames.**
- **From the game:** `player/interaction` posts events on a single-producer ring, as elsewhere. No locks, and no heap in the hot path.
- **Mixing:** about 8 voices, 16-bit stereo at 48000 Hz, in scalar C. A NASM kernel only if `bench/` shows it's worth it, per the standard.
- **Embedding:** an `incbin` blob, as textures do.
- **Settings:** a volume slider in `run/src/ui/screen_settings/`, saved in `settings.yaml`.

## Todo

**Stage 1: can we talk to it?**

- [ ] Write a throwaway spike under `/tmp/mc_c_spikes/pulse/`: connect, authenticate, open a stream, send a short tone. Agents build it, and **Ahmed runs it and listens**
- [ ] Record whether it worked and the latency, here under Issues. If it fails, stop and bring the options to Ahmed

**Stage 2: sounds**

- [ ] Download the pack to `/tmp/mc_c_assets/`, confirm the licence and contents, and choose files per family
- [ ] Add `compile/src/sound_pack/` and `sounds/`, with attribution, and embed through `asm/src/embed_assets/`
- [ ] Add `sound:` to `compile/src/game_tables/blocks/` and to every block in `config/blocks/`, refusing a block with none

**Stage 3: playing**

- [ ] Add `run/src/audio/pulse/wire/` with unit tests that need no server, like `tests/run/display/x11/`
- [ ] Add `run/src/audio/pulse/`, `run/src/audio/mixer/` and `run/src/audio/`, with allowance in `standards_lint` for `run/src/audio/pulse/index.c` only
- [ ] Post events from `run/src/player/interaction/` for digging, breaking and placing
- [ ] Add the volume setting, and make no sound server mean silence and one log line, never a failure
- [ ] Test the mixer by writing its output to memory and checking the samples, so `ctest` makes no sound
- [ ] Update `plans/REPO_STRUCTURE.md`

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_audio.*|compile_sound_pack|compile_game_tables|run_player_interaction|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

**Only Ahmed can check how it sounds.** Agents can test the bytes but can't hear them, and must not play sound on his desktop while testing:

    ./build/release/bin/minecraft_c --windowed

Mine stone, wood, dirt, sand and leaves, and listen. Change the volume. Play something else at the same time, to confirm both are heard.

## Commits

Appended as they land:

## Issues found

**Open for Ahmed:**

1. Is a thread from the spare OK, leaving none reserved for the OS?

**Sources:** [Kenney Impact Sounds](https://kenney.nl/assets/impact-sounds), [pulseaudio Rust crate](https://docs.rs/crate/pulseaudio/latest), [PulseAudio protocol-native.h](https://code.delx.au/pulseaudio/blob/81447ed392e57f822e09cf648cb16732a3e3773f:/src/protocol-native.h).
