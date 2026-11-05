# tests/run/audio/pulse/wire

**Purpose:** holds `run/src/audio/pulse/wire/`, the sound server's protocol as bytes, to what the protocol is: every command against bytes written out by hand, and the replies the real server gave in stage 1. ctest `run_audio_pulse_wire`.
**Tier:** test. Needs no server and no socket, so it can never make a sound.
