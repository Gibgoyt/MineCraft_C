# run/src/audio/pulse

**Purpose:** a client of the desktop's sound server, so that game sounds play alongside everything else. No libpulse: our own protocol bytes over the server's Unix socket. Samples travel in the socket, with no shared memory.
**Tier:** Arena. Takes its buffers from `audio`'s arena.
**Owns:** the one connection, and through it the one stream, which goes when the connection closes.
**Lifetime:** Process, reconnected as the server comes and goes.
**Threads:** audio, alone.
**Public functions:**

- `audio_pulse_start`: takes its buffers.
- `audio_pulse_connect`, `audio_pulse_close`: a pair. `_connect` says why if it fails, and leaves nothing open.
- `audio_pulse_attach`: the same over two descriptors already joined to a server, for a test's stand-in. The lint allows sockets in this one file, so a test has pipes.
- `audio_pulse_service`: reads what the server has said, waiting up to a limit.
- `audio_pulse_frames_owed`, `audio_pulse_samples`, `audio_pulse_send`: what the server has asked for, and where to put it.

**May be called by:** `audio`.
**Depends on:** `arena`, `file`, `time`, `audio/pulse/wire`.
**Parts:** `wire`, the protocol as bytes.

This is the second of the two local sockets `plans/CODING_STANDARD.md` section 1 allows, decided by Ahmed on 2026-11-04. The socket path comes from `audio/pulse/wire`, which refuses a `PULSE_SERVER` naming another machine, so there is still no TCP anywhere.

## What it sends

The server asks for bytes, about 90 times a second, and gets exactly those. Between sounds, they are zeros: a stream that stopped would be an underrun at the next crunch. The volume on the stream is left to the server, so the desktop's mixer can turn the game up and down, and the game's own slider is in `audio/mixer`.

`muted` opens the stream at volume 0, muted, so that a check against the real server has two guards on its silence: that, and whatever the caller sends, which is its own to keep zero.
