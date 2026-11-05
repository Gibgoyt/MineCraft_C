# tests/run/audio/pulse

**Purpose:** ctest `run_audio_pulse`: the sound server's client, against a stand-in on a thread, over two pipes, that keeps every sample in memory. Hello, the cookie, what is sent arriving exactly, what it asks for, notices, and each way a server can refuse, go silent, go away or talk nonsense. No server at the socket path is a plain failure.
**Tier:** test. As ctest runs it, nothing connects to anything real, so it can never make a sound.

The stand-in uses pipes, as `standards_lint` allows sockets in `run/src/audio/pulse/index.c` alone, and not in tests.

By hand, and never by ctest, the same program shows the real client on the real server, with silence alone:

    tools/test-or-build.sh timeout -k 3 60 build/debug/bin/tests/run_audio_pulse --real-server-silence

It opens the stream muted at volume 0, and checks every sample is 0 before sending it.
