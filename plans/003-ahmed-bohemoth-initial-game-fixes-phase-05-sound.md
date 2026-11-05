# 003-ahmed-bohemoth-initial-game-fixes — phase 5: Basic sound

**Status:** in progress: stage 1 proved with silence, and Ahmed still has to hear the tone; stages 2 and 3 not started   (updated 2026-11-05)

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

- [x] Write a throwaway spike under `/tmp/mc_c_spikes/pulse/`: connect, authenticate, open a stream, send a short tone. Agents build it. Source kept under "Stage 1 spike" below, since `/tmp` doesn't survive a reboot
- [x] Prove the protocol on the real server with silence only: `pulse_silence` connected, authenticated, opened a muted stream, sent 192000 zero bytes, drained and closed
- [ ] **Ahmed runs `pulse_tone` and listens** (commands under "Stage 1 spike"). No agent has run it
- [x] Record whether it worked and the latency, here under Issues. If it fails, stop and bring the options to Ahmed

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

## Stage 1 spike

The source below builds two programs. Without `SPIKE_TONE` there is no tone code: the stream opens muted at volume 0, and every buffer is checked to be zero before it's sent. With it, one second of a 480 Hz triangle at about -20 dBFS plays at normal stream volume.

**Silence, safe for agents:**

    mkdir -p /tmp/mc_c_spikes/pulse
    awk '/^```c$/{f=1;next} /^```$/{f=0} f' plans/003-ahmed-bohemoth-initial-game-fixes-phase-05-sound.md > /tmp/mc_c_spikes/pulse/index.c
    tools/test-or-build.sh timeout 120 gcc -std=c23 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion -Wshadow -Wvla -Wpadded -Werror -march=skylake -mtune=skylake -O2 -fno-tree-vectorize -fno-tree-slp-vectorize -o /tmp/mc_c_spikes/pulse/pulse_silence /tmp/mc_c_spikes/pulse/index.c
    tools/test-or-build.sh timeout -k 3 30 /tmp/mc_c_spikes/pulse/pulse_silence

**Tone, Ahmed only, because it makes a sound:** the same `gcc` line with `-DSPIKE_TONE=1` and `-o /tmp/mc_c_spikes/pulse/pulse_tone`, then:

    timeout -k 3 30 /tmp/mc_c_spikes/pulse/pulse_tone

```c
/* Throwaway spike, plan 003 phase 5 stage 1: speak the PulseAudio native protocol over the Unix socket, with no libpulse.
 * Built without SPIKE_TONE, there is no tone code at all: the stream is opened muted at volume 0 and every byte sent is
 * checked to be zero. Built with -DSPIKE_TONE=1 it plays one quiet second of a triangle wave, for Ahmed to run and hear. */
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum : uint32_t {
    CMD_ERROR = 0u, CMD_TIMEOUT = 1u, CMD_REPLY = 2u, CMD_CREATE_PLAYBACK_STREAM = 3u, CMD_DELETE_PLAYBACK_STREAM = 4u,
    CMD_AUTH = 8u, CMD_SET_CLIENT_NAME = 9u, CMD_DRAIN_PLAYBACK_STREAM = 12u, CMD_GET_PLAYBACK_LATENCY = 14u,
    CMD_REQUEST = 61u, CMD_OVERFLOW = 62u, CMD_UNDERFLOW = 63u, CMD_STREAM_KILLED = 64u, CMD_STARTED = 86u,
    CONTROL_CHANNEL = 0xFFFFFFFFu, NO_INDEX = 0xFFFFFFFFu, DEFAULT_ATTR = 0xFFFFFFFFu,
    PROTOCOL_VERSION = 35u, VERSION_MASK = 0x0000FFFFu,
    DESCRIPTOR_BYTES = 20u, COOKIE_BYTES = 256u, COMMAND_BYTES = 4096u, MAX_PACKET_BYTES = 1u << 16,
    SAMPLE_S16LE = 3u, RATE = 48000u, CHANNELS = 2u, FRAME_BYTES = 4u, BYTES_PER_SECOND = RATE * FRAME_BYTES,
    TOTAL_BYTES = BYTES_PER_SECOND, TARGET_BYTES = BYTES_PER_SECOND / 40u, /* 25 ms */
    FRONT_LEFT = 1u, FRONT_RIGHT = 2u, WATCHDOG_SECONDS = 20u,
};
enum : int32_t { POLL_MS = 20, REPLY_MS = 5000 };

static const char TAG_U32 = 'L', TAG_U8 = 'B', TAG_S64 = 'r', TAG_USEC = 'U', TAG_TIMEVAL = 'T', TAG_STRING = 't',
                  TAG_NULL = 'N', TAG_ARB = 'x', TAG_TRUE = '1', TAG_FALSE = '0', TAG_SPEC = 'a', TAG_MAP = 'm',
                  TAG_VOLUME = 'v', TAG_PROPS = 'P';

typedef struct {
    uint8_t bytes[COMMAND_BYTES];
    uint32_t used;
    uint8_t failed;
    uint8_t reserved[3];
} writer_t;

typedef struct {
    const uint8_t *bytes;
    uint32_t size;
    uint32_t at;
    uint8_t failed;
    uint8_t reserved[7];
} reader_t;

static int32_t g_fd = -1;
static uint8_t g_inbox[MAX_PACKET_BYTES];
static uint32_t g_inbox_bytes;
static uint32_t g_tag = 1u;
static uint32_t g_channel;
static uint32_t g_owed;
static uint32_t g_underflows;
static uint32_t g_overflows;
static uint32_t g_requests;
static int64_t g_first_write_ns;
static int64_t g_started_ns;

static int64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + (int64_t)t.tv_nsec;
}

[[noreturn]] static void die(const char *what) {
    printf("result=FAILED at=%s errno=%d (%s)\n", what, errno, strerror(errno));
    exit(1);
}

static void put_be32(uint8_t *to, uint32_t v) {
    to[0] = (uint8_t)(v >> 24); to[1] = (uint8_t)(v >> 16); to[2] = (uint8_t)(v >> 8); to[3] = (uint8_t)v;
}
static uint32_t get_be32(const uint8_t *from) {
    return ((uint32_t)from[0] << 24) | ((uint32_t)from[1] << 16) | ((uint32_t)from[2] << 8) | (uint32_t)from[3];
}

static void w_raw(writer_t *w, const void *data, uint32_t size) {
    if (size > COMMAND_BYTES - w->used) { w->failed = 1u; return; }
    memcpy(&w->bytes[w->used], data, size);
    w->used += size;
}
static void w_tag(writer_t *w, char tag) { w_raw(w, &tag, 1u); }
static void w_be32(writer_t *w, uint32_t v) { uint8_t b[4]; put_be32(b, v); w_raw(w, b, 4u); }
static void w_u8(writer_t *w, uint8_t v) { w_tag(w, TAG_U8); w_raw(w, &v, 1u); }
static void w_u32(writer_t *w, uint32_t v) { w_tag(w, TAG_U32); w_be32(w, v); }
static void w_bool(writer_t *w, bool v) { w_tag(w, v ? TAG_TRUE : TAG_FALSE); }
static void w_null(writer_t *w) { w_tag(w, TAG_NULL); }
static void w_string(writer_t *w, const char *s) { w_tag(w, TAG_STRING); w_raw(w, s, (uint32_t)strlen(s) + 1u); }
static void w_arb(writer_t *w, const void *data, uint32_t size) { w_tag(w, TAG_ARB); w_be32(w, size); w_raw(w, data, size); }
static void w_command(writer_t *w, uint32_t command, uint32_t tag) { w->used = 0u; w->failed = 0u; w_u32(w, command); w_u32(w, tag); }

static bool r_tag(reader_t *r, char tag) {
    if (r->failed != 0u || r->at >= r->size || r->bytes[r->at] != (uint8_t)tag) { r->failed = 1u; return false; }
    r->at += 1u;
    return true;
}
static uint32_t r_be32(reader_t *r) {
    if (r->failed != 0u || r->size - r->at < 4u) { r->failed = 1u; return 0u; }
    uint32_t v = get_be32(&r->bytes[r->at]);
    r->at += 4u;
    return v;
}
static uint64_t r_be64(reader_t *r) { uint64_t hi = r_be32(r); return (hi << 32) | r_be32(r); }
static uint32_t r_u32(reader_t *r) { return r_tag(r, TAG_U32) ? r_be32(r) : 0u; }
static uint64_t r_usec(reader_t *r) { return r_tag(r, TAG_USEC) ? r_be64(r) : 0u; }
static int64_t r_s64(reader_t *r) { return r_tag(r, TAG_S64) ? (int64_t)r_be64(r) : 0; }
static bool r_bool(reader_t *r) {
    if (r->failed == 0u && r->at < r->size && r->bytes[r->at] == (uint8_t)TAG_TRUE) { r->at += 1u; return true; }
    (void)r_tag(r, TAG_FALSE);
    return false;
}
static void r_timeval(reader_t *r) { if (r_tag(r, TAG_TIMEVAL)) { (void)r_be32(r); (void)r_be32(r); } }
static void r_skip(reader_t *r, uint32_t size) {
    if (r->failed != 0u || r->size - r->at < size) { r->failed = 1u; return; }
    r->at += size;
}
static const char *r_string(reader_t *r) {
    if (r->failed == 0u && r->at < r->size && r->bytes[r->at] == (uint8_t)TAG_NULL) { r->at += 1u; return "(null)"; }
    if (!r_tag(r, TAG_STRING)) { return "(bad)"; }
    const char *s = (const char *)&r->bytes[r->at];
    const uint8_t *end = memchr(s, 0, r->size - r->at);
    if (end == nullptr) { r->failed = 1u; return "(bad)"; }
    r->at += (uint32_t)(end - (const uint8_t *)s) + 1u;
    return s;
}

static void send_packet(uint32_t channel, const void *payload, uint32_t size) {
    uint8_t descriptor[DESCRIPTOR_BYTES] = {0};
    put_be32(&descriptor[0], size);
    put_be32(&descriptor[4], channel);
    struct iovec parts[2] = {{descriptor, DESCRIPTOR_BYTES}, {(void *)payload, size}};
    struct msghdr message = {.msg_iov = parts, .msg_iovlen = 2u};
    uint64_t left = (uint64_t)DESCRIPTOR_BYTES + size;
    while (left > 0u) {
        ptrdiff_t sent = sendmsg(g_fd, &message, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) { continue; }
            die("sendmsg");
        }
        left -= (uint64_t)sent;
        while (sent > 0 && message.msg_iovlen > 0u) {
            if ((uint64_t)sent >= message.msg_iov->iov_len) {
                sent -= (ptrdiff_t)message.msg_iov->iov_len;
                message.msg_iov += 1;
                message.msg_iovlen -= 1u;
            } else {
                message.msg_iov->iov_base = (uint8_t *)message.msg_iov->iov_base + sent;
                message.msg_iov->iov_len -= (uint64_t)sent;
                sent = 0;
            }
        }
    }
}
static void send_command(const writer_t *w) {
    if (w->failed != 0u) { errno = EMSGSIZE; die("command too large"); }
    send_packet(CONTROL_CHANNEL, w->bytes, w->used);
}

static void read_exact(uint8_t *to, uint32_t size) {
    uint32_t have = 0u;
    while (have < size) {
        ptrdiff_t got = recv(g_fd, &to[have], size - have, 0);
        if (got == 0) { errno = ECONNRESET; die("server closed the connection"); }
        if (got < 0) { if (errno == EINTR) { continue; } die("recv"); }
        have += (uint32_t)got;
    }
}

/* One packet from the server, or false when nothing came within wait_ms. Leaves it in g_inbox. */
static bool next_packet(int32_t wait_ms) {
    struct pollfd p = {g_fd, POLLIN, 0};
    int32_t ready = poll(&p, 1u, wait_ms);
    if (ready < 0) { if (errno == EINTR) { return false; } die("poll"); }
    if (ready == 0) { return false; }
    uint8_t descriptor[DESCRIPTOR_BYTES];
    read_exact(descriptor, DESCRIPTOR_BYTES);
    uint32_t size = get_be32(&descriptor[0]);
    if (size > MAX_PACKET_BYTES) { errno = EMSGSIZE; die("server packet too large"); }
    if (get_be32(&descriptor[4]) != CONTROL_CHANNEL) { errno = EPROTO; die("data packet on a playback connection"); }
    read_exact(g_inbox, size);
    g_inbox_bytes = size;
    return true;
}

/* Everything the server sends unasked. */
static void handle_unasked(uint32_t command, reader_t *r) {
    switch (command) {
    case CMD_REQUEST:
        if (r_u32(r) == g_channel) { g_owed += r_u32(r); g_requests += 1u; }
        break;
    case CMD_STARTED:
        if (g_started_ns == 0) { g_started_ns = now_ns(); }
        break;
    case CMD_UNDERFLOW: g_underflows += 1u; break;
    case CMD_OVERFLOW: g_overflows += 1u; break;
    case CMD_STREAM_KILLED: errno = ECANCELED; die("stream killed by the server");
    default:
        printf("note: unasked command %" PRIu32 " ignored\n", command);
        break;
    }
}

/* Pumps packets until the reply to tag arrives; the reader then sits just after the command and tag. */
static void wait_reply(uint32_t tag, reader_t *r, const char *what) {
    int64_t deadline = now_ns() + (int64_t)REPLY_MS * 1000000;
    while (now_ns() < deadline) {
        if (!next_packet(POLL_MS)) { continue; }
        *r = (reader_t){.bytes = g_inbox, .size = g_inbox_bytes};
        uint32_t command = r_u32(r);
        uint32_t got_tag = r_u32(r);
        if (r->failed != 0u) { errno = EPROTO; die("malformed packet"); }
        if (got_tag == tag && command == CMD_REPLY) { return; }
        if (got_tag == tag && command == CMD_ERROR) {
            printf("server_error_code=%" PRIu32 "\n", r_u32(r));
            errno = EPROTO;
            die(what);
        }
        if (got_tag == tag && command == CMD_TIMEOUT) { errno = ETIMEDOUT; die(what); }
        if (command != CMD_REPLY) { handle_unasked(command, r); }
    }
    errno = ETIMEDOUT;
    die(what);
}

#ifdef SPIKE_TONE
/* 480 Hz triangle at about -20 dBFS, starting and ending at zero so there is no click. */
static void fill_pcm(uint8_t *pcm, uint32_t size, uint32_t first_byte) {
    for (uint32_t at = 0u; at + FRAME_BYTES <= size; at += FRAME_BYTES) {
        uint32_t phase = (((first_byte + at) / FRAME_BYTES) + 25u) % 100u;
        int32_t value = phase < 50u ? -3000 + (int32_t)phase * 120 : 3000 - ((int32_t)phase - 50) * 120;
        uint16_t bits = (uint16_t)(int16_t)value;
        pcm[at] = (uint8_t)bits; pcm[at + 1u] = (uint8_t)(bits >> 8);
        pcm[at + 2u] = (uint8_t)bits; pcm[at + 3u] = (uint8_t)(bits >> 8);
    }
}
static const bool g_silent = false;
#else
static void fill_pcm(uint8_t *pcm, uint32_t size, uint32_t first_byte) {
    (void)first_byte;
    memset(pcm, 0, size);
    for (uint32_t at = 0u; at < size; at += 1u) {
        if (pcm[at] != 0u) { abort(); }
    }
}
static const bool g_silent = true;
#endif

static void connect_socket(void) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    int32_t wrote = runtime != nullptr
        ? snprintf(address.sun_path, sizeof address.sun_path, "%s/pulse/native", runtime)
        : snprintf(address.sun_path, sizeof address.sun_path, "/run/user/%u/pulse/native", getuid());
    if (wrote <= 0 || (uint64_t)wrote >= sizeof address.sun_path) { errno = ENAMETOOLONG; die("socket path"); }
    g_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (g_fd < 0) { die("socket"); }
    if (connect(g_fd, (struct sockaddr *)&address, sizeof address) != 0) { die("connect"); }
    printf("socket=%s\n", address.sun_path);
}

/* The server's own cookie file, as libpulse sends it. Never printed; cleared after use. */
static bool load_cookie(uint8_t *cookie) {
    memset(cookie, 0, COOKIE_BYTES);
    const char *home = getenv("HOME");
    if (home == nullptr) { return false; }
    char path[512];
    int32_t wrote = snprintf(path, sizeof path, "%s/.config/pulse/cookie", home);
    if (wrote <= 0 || (uint64_t)wrote >= sizeof path) { return false; }
    FILE *file = fopen(path, "rb");
    if (file == nullptr) { return false; }
    uint64_t got = fread(cookie, 1u, COOKIE_BYTES, file);
    (void)fclose(file);
    return got == COOKIE_BYTES;
}

static void authenticate(bool zero_cookie) {
    uint8_t cookie[COOKIE_BYTES] = {0};
    bool have = !zero_cookie && load_cookie(cookie);
    static writer_t w;
    uint32_t tag = g_tag++;
    w_command(&w, CMD_AUTH, tag);
    w_u32(&w, PROTOCOL_VERSION); /* no shm or memfd bits: samples travel in the socket */
    w_arb(&w, cookie, COOKIE_BYTES);
    send_command(&w);
    memset(cookie, 0, COOKIE_BYTES);
    memset(w.bytes, 0, COMMAND_BYTES);
    reader_t r;
    wait_reply(tag, &r, "auth");
    uint32_t reply = r_u32(&r);
    if (r.failed != 0u) { errno = EPROTO; die("auth reply"); }
    printf("auth=ok cookie=%s server_version=%" PRIu32 " server_flags=0x%08" PRIx32 "\n",
           have ? "file" : "zeros", reply & VERSION_MASK, reply & ~VERSION_MASK);
}

static void set_client_name(void) {
    static const char KEY[] = "application.name", VALUE[] = "mc_c pulse spike";
    static writer_t w;
    uint32_t tag = g_tag++;
    w_command(&w, CMD_SET_CLIENT_NAME, tag);
    w_tag(&w, TAG_PROPS);
    w_string(&w, KEY);
    w_u32(&w, (uint32_t)sizeof VALUE);
    w_arb(&w, VALUE, (uint32_t)sizeof VALUE);
    w_null(&w);
    send_command(&w);
    reader_t r;
    wait_reply(tag, &r, "set_client_name");
    printf("client_index=%" PRIu32 "\n", r_u32(&r));
}

static void create_stream(void) {
    static const char KEY[] = "media.name", VALUE[] = "mc_c pulse spike";
    static writer_t w;
    uint32_t tag = g_tag++;
    w_command(&w, CMD_CREATE_PLAYBACK_STREAM, tag);
    uint8_t spec[6];
    spec[0] = SAMPLE_S16LE; spec[1] = CHANNELS;
    put_be32(&spec[2], RATE);
    w_tag(&w, TAG_SPEC); w_raw(&w, spec, 6u);
    const uint8_t map[3] = {CHANNELS, FRONT_LEFT, FRONT_RIGHT};
    w_tag(&w, TAG_MAP); w_raw(&w, map, 3u);
    w_u32(&w, NO_INDEX);
    w_null(&w); /* the default sink */
    w_u32(&w, DEFAULT_ATTR); /* maxlength */
    w_bool(&w, false); /* corked */
    w_u32(&w, TARGET_BYTES); /* tlength */
    w_u32(&w, DEFAULT_ATTR); /* prebuf */
    w_u32(&w, DEFAULT_ATTR); /* minreq */
    w_u32(&w, 0u); /* sync id */
    const uint32_t volume = g_silent ? 0u : 0x10000u; /* muted, or the normal 100% */
    uint8_t cvolume[1u + CHANNELS * 4u];
    cvolume[0] = CHANNELS;
    put_be32(&cvolume[1], volume);
    put_be32(&cvolume[5], volume);
    w_tag(&w, TAG_VOLUME); w_raw(&w, cvolume, (uint32_t)sizeof cvolume);
    for (uint32_t i = 0u; i < 7u; i += 1u) { w_bool(&w, false); } /* no_remap .. variable_rate */
    w_bool(&w, g_silent); /* muted */
    w_bool(&w, true); /* adjust_latency */
    w_tag(&w, TAG_PROPS);
    w_string(&w, KEY);
    w_u32(&w, (uint32_t)sizeof VALUE);
    w_arb(&w, VALUE, (uint32_t)sizeof VALUE);
    w_null(&w);
    w_bool(&w, g_silent); /* volume_set */
    w_bool(&w, false); /* early_requests */
    w_bool(&w, g_silent); /* muted_set */
    w_bool(&w, false); /* dont_inhibit_auto_suspend */
    w_bool(&w, false); /* fail_on_suspend */
    w_bool(&w, false); /* relative_volume */
    w_bool(&w, false); /* passthrough */
    w_u8(&w, 0u); /* no format list */
    send_command(&w);
    reader_t r;
    int64_t asked = now_ns();
    wait_reply(tag, &r, "create_playback_stream");
    g_channel = r_u32(&r);
    uint32_t index = r_u32(&r);
    g_owed = r_u32(&r);
    uint32_t maxlength = r_u32(&r), tlength = r_u32(&r), prebuf = r_u32(&r), minreq = r_u32(&r);
    r_skip(&r, 7u); /* sample spec */
    r_skip(&r, 4u); /* channel map */
    uint32_t sink = r_u32(&r);
    const char *sink_name = r_string(&r);
    bool suspended = r_bool(&r);
    uint64_t sink_usec = r_usec(&r);
    if (r.failed != 0u) { errno = EPROTO; die("create reply"); }
    printf("stream=ok channel=%" PRIu32 " index=%" PRIu32 " create_ms=%" PRId64 "\n", g_channel, index, (now_ns() - asked) / 1000000);
    printf("attr maxlength=%" PRIu32 " tlength=%" PRIu32 " prebuf=%" PRIu32 " minreq=%" PRIu32 " first_request=%" PRIu32 "\n", maxlength, tlength, prebuf, minreq, g_owed);
    printf("sink=%" PRIu32 " name=%s suspended=%d configured_sink_usec=%" PRIu64 "\n", sink, sink_name, suspended ? 1 : 0, sink_usec);
}

static uint32_t ask_latency(void) {
    static writer_t w;
    uint32_t tag = g_tag++;
    w_command(&w, CMD_GET_PLAYBACK_LATENCY, tag);
    w_u32(&w, g_channel);
    w_tag(&w, TAG_TIMEVAL); w_be32(&w, 0u); w_be32(&w, 0u);
    send_command(&w);
    return tag;
}

static void simple_stream_command(uint32_t command, const char *what) {
    static writer_t w;
    uint32_t tag = g_tag++;
    w_command(&w, command, tag);
    w_u32(&w, g_channel);
    send_command(&w);
    reader_t r;
    wait_reply(tag, &r, what);
    printf("%s=ok\n", what);
}

static void stream_the_second(void) {
    static uint8_t pcm[MAX_PACKET_BYTES];
    uint32_t sent = 0u;
    uint32_t pending_latency = 0u;
    uint64_t samples = 0u, sum_usec = 0u, min_usec = UINT64_MAX, max_usec = 0u;
    int64_t last_ask = 0;
    int64_t deadline = now_ns() + (int64_t)WATCHDOG_SECONDS * 500000000;
    while (sent < TOTAL_BYTES && now_ns() < deadline) {
        while (g_owed >= FRAME_BYTES && sent < TOTAL_BYTES) {
            uint32_t size = g_owed < TOTAL_BYTES - sent ? g_owed : TOTAL_BYTES - sent;
            size = size < MAX_PACKET_BYTES ? size : MAX_PACKET_BYTES;
            size -= size % FRAME_BYTES;
            fill_pcm(pcm, size, sent);
            if (g_first_write_ns == 0) { g_first_write_ns = now_ns(); }
            send_packet(g_channel, pcm, size);
            sent += size;
            g_owed -= size;
        }
        if (pending_latency == 0u && now_ns() - last_ask > 50000000) {
            pending_latency = ask_latency();
            last_ask = now_ns();
        }
        if (!next_packet(POLL_MS)) { continue; }
        reader_t r = {.bytes = g_inbox, .size = g_inbox_bytes};
        uint32_t command = r_u32(&r);
        uint32_t tag = r_u32(&r);
        if (command == CMD_REPLY && tag == pending_latency) {
            uint64_t sink_usec = r_usec(&r);
            (void)r_usec(&r);
            bool playing = r_bool(&r);
            r_timeval(&r);
            r_timeval(&r);
            int64_t write_index = r_s64(&r), read_index = r_s64(&r);
            if (r.failed != 0u) { errno = EPROTO; die("latency reply"); }
            pending_latency = 0u;
            if (playing && write_index >= read_index) {
                uint64_t total = sink_usec + (uint64_t)(write_index - read_index) * 1000000u / BYTES_PER_SECOND;
                samples += 1u; sum_usec += total;
                min_usec = total < min_usec ? total : min_usec;
                max_usec = total > max_usec ? total : max_usec;
            }
        } else if (command == CMD_ERROR && tag == pending_latency) {
            printf("server_error_code=%" PRIu32 "\n", r_u32(&r));
            errno = EPROTO;
            die("get_playback_latency");
        } else if (command != CMD_REPLY) {
            handle_unasked(command, &r);
        }
    }
    if (sent < TOTAL_BYTES) { errno = ETIMEDOUT; die("server did not ask for the whole second"); }
    if (pending_latency != 0u) { reader_t r; wait_reply(pending_latency, &r, "get_playback_latency"); }
    printf("sent_bytes=%" PRIu32 " silent=%d requests=%" PRIu32 " underflows=%" PRIu32 " overflows=%" PRIu32 "\n",
           sent, g_silent ? 1 : 0, g_requests, g_underflows, g_overflows);
    if (g_started_ns != 0) { printf("first_write_to_started_ms=%" PRId64 "\n", (g_started_ns - g_first_write_ns) / 1000000); }
    if (samples > 0u) {
        printf("latency_usec samples=%" PRIu64 " avg=%" PRIu64 " min=%" PRIu64 " max=%" PRIu64 "\n", samples, sum_usec / samples, min_usec, max_usec);
    }
}

int32_t main(int32_t argc, char **argv) {
    alarm(WATCHDOG_SECONDS);
    setvbuf(stdout, nullptr, _IOLBF, 0u);
    bool zero_cookie = argc > 1 && strcmp(argv[1], "--zero-cookie") == 0;
    printf("mode=%s\n", g_silent ? "silence (muted, volume 0, all samples zero)" : "tone (480 Hz, about -20 dBFS, 1 s)");
    connect_socket();
    authenticate(zero_cookie);
    set_client_name();
    create_stream();
    stream_the_second();
    simple_stream_command(CMD_DRAIN_PLAYBACK_STREAM, "drain");
    /* Independent of what the server reports: the last sample has played when the drain returns, so this less one second
     * of audio is what the pipe really added, startup included. */
    printf("first_write_to_drained_ms=%" PRId64 " audio_ms=%" PRIu32 "\n", (now_ns() - g_first_write_ns) / 1000000, TOTAL_BYTES / (BYTES_PER_SECOND / 1000u));
    simple_stream_command(CMD_DELETE_PLAYBACK_STREAM, "delete_stream");
    if (close(g_fd) != 0) { die("close"); }
    printf("result=OK\n");
    return 0;
}
```

## Commits

Appended as they land:

## Issues found

**Stage 1 result, 2026-11-05: the protocol works against the real `pipewire-pulse` 1.6.9, with silence only. Nobody has heard anything yet.**

- **Expected:** our own client can connect, authenticate, open a stream, send samples and close. **Actual:** all of it worked on the first run, every time (4 runs on the real server). `result=OK`, `server_version=35`, 192000 bytes sent, `requests` 92 to 93, `underflows=0`, `overflows=0`, and drain and delete both replied. Afterwards `pactl list short sink-inputs` printed nothing.
- **Silence, the evidence:** 3 of the 4 runs went through a private relay that forwarded a data packet only after checking every byte was zero: `data_bytes=192000 all_data_bytes_zero=True`. The fourth, direct, was the same binary after those passed. `pactl` during the run showed `Mute: yes` and `Volume: front-left: 0 / 0% / -inf dB, front-right: 0 / 0% / -inf dB`. The relay's refusal path was never triggered, as no agent ran `pulse_tone`. An `objdump` search for the tone's amplitude constant found nothing in either build, so that check proves nothing, and the absence of tone code rests on the `#ifdef`.
- **Latency:** the server reports 24.08 ms (range 24.00 to 24.08, `sink_usec` plus the bytes queued). Measured here, first write to end of drain took 1012 to 1054 ms for 1000 ms of audio, so 12 to 54 ms including startup; start took 4 to 46 ms. That's when the server says the audio has played, with the stream muted, not a measurement at the speaker. Good enough for block sounds if the tone confirms it.
- **Ahmed's listening check is still open.** The stage's real question, whether sound comes out, can't be settled by an agent. Stage 2 and 3 can go ahead on the protocol result, but a silent `pulse_tone` would send this back to the options.

**What stage 3 needs to know, all seen on the real server unless marked:**

- Asked for `tlength` 4800 (25 ms); the server gave 3600 (18.75 ms), `prebuf` 2404, `minreq` 1200, `maxlength` 4194304. So use the attributes in the reply, not the ones requested.
- The stream takes s16le 2ch 48000 Hz as asked, though the server's default is `float32le`.
- The server sends small requests: about 2064 bytes each, about 93 per second. Sending only what it asks for gave no underflow.
- **Cookie:** `pipewire-pulse` accepted 256 zero bytes as well as the real file, so it doesn't check it. Send the file when it's there, zeros otherwise. Real PulseAudio does check, and that **isn't tested**: only `pipewire-pulse` is on this machine.
- **Command numbers:** every one in the spike matches `src/pulsecore/native-common.h` in PulseAudio's source, read as reference only. `REQUEST` (61) and `STARTED` (86) were also seen live; `OVERFLOW`, `UNDERFLOW` and `PLAYBACK_STREAM_KILLED` were never sent by the server, so the handling of those three hasn't run.
- **No server:** with no socket, `connect` fails with `ENOENT` and the spike exits 1 without hanging. The game must turn that into silence and one log line.

**Where the plan differs from the machine:**

- **Expected** (section "Getting sound out", checked 2026-11-04): default sink on the WD19 dock. **Actual** on 2026-11-05: `alsa_output.pci-0000_01_00.1.hdmi-stereo`. Default sinks change, which is why the client asks for the default (`null` sink name) and names none. **Done:** nothing needed; the listening check has to use whatever Ahmed's default is at the time.
- **Expected** (todo): agents build a tone and Ahmed listens. **Actual:** the workflow forbids any sound from agents and asked for proof by silence, so the single spike became two builds. **Done:** split the todo into what agents proved and what Ahmed still has to do.
- **Refused command:** the first attempt to write and compile the spike in one long shell command was refused as too complex to check against the worktree. **Done:** as the refusal said, ran plain separate commands. Nothing was bypassed.

**Open for Ahmed:**

1. Is a thread from the spare OK, leaving none reserved for the OS?
2. Does `pulse_tone` make a clean one-second tone? See "Stage 1 spike". This box stays open until he's listened.

**Sources:** [Kenney Impact Sounds](https://kenney.nl/assets/impact-sounds), [pulseaudio Rust crate](https://docs.rs/crate/pulseaudio/latest), [PulseAudio protocol-native.h](https://code.delx.au/pulseaudio/blob/81447ed392e57f822e09cf648cb16732a3e3773f:/src/protocol-native.h).
