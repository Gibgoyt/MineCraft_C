/* See README.md. The Owner of sound: its memory, its thread, and the ring into it. */
#include "audio/index.h"

#include "display/x11/wire/index.h"
#include "log/index.h"
#include "simd/embed_assets/index.h"
#include "time/index.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint32_t {
    AUDIO_REGION_BYTES = sizeof(audio_state_t) + AUDIO_PULSE_ARENA_BYTES,
    AUDIO_RETRY_MS = 5000u,                     /* A server that went away may be back, as when PipeWire restarts. */
    AUDIO_FILE_FRAMES = PULSE_WIRE_RATE / 100u, /* 10 ms */
    AUDIO_PREVIEW_FAMILY = SOUND_FAMILY_STONE,
    AUDIO_PREVIEW_KIND = SOUND_KIND_PLACE,
};

enum : int32_t {
    /* How long the thread sleeps with nothing asked of it, and so how late a sound can be on top of what the server holds. */
    AUDIO_WAKE_MS = 5,
};

static const char AUDIO_STREAM_NAME[] = "MineCraft_C";

/* ---- The thread -------------------------------------------------------------------------------------------------- */

/* What the sim has posted goes to the mixer, or with nowhere for it to be heard, nowhere: playing, later, what was mined a minute ago would be wrong. */
static void audio_events_take(audio_state_t *state, bool heard) {
    uint32_t tail = atomic_load_explicit(&state->ring.tail, memory_order_relaxed);
    const uint32_t head = atomic_load_explicit(&state->ring.head, memory_order_acquire);
    while (tail != head) {
        const audio_event_t event = state->ring.events[tail % AUDIO_RING_EVENTS];
        tail++;
        if (heard) {
            (void)audio_mixer_play(&state->mixer, event.family, event.kind, event.gain_left, event.gain_right);
        }
    }
    atomic_store_explicit(&state->ring.tail, tail, memory_order_release);
    if (atomic_exchange_explicit(&state->preview, 0u, memory_order_relaxed) != 0u && heard) {
        (void)audio_mixer_play(&state->mixer, AUDIO_PREVIEW_FAMILY, AUDIO_PREVIEW_KIND, AUDIO_MIXER_GAIN_ONE,
                               AUDIO_MIXER_GAIN_ONE);
    }
}

/* Everything the server has asked for, mixed and sent. False if that is over. */
static bool audio_server_step(audio_state_t *state) {
    audio_pulse_t *const pulse = &state->pulse;
    if (!audio_pulse_service(pulse, AUDIO_WAKE_MS)) {
        return false;
    }
    audio_events_take(state, true);
    for (uint32_t frames = audio_pulse_frames_owed(pulse); frames != 0u; frames = audio_pulse_frames_owed(pulse)) {
        audio_mixer_render(&state->mixer, audio_pulse_samples(pulse), frames,
                           atomic_load_explicit(&state->volume_percent, memory_order_relaxed));
        if (!audio_pulse_send(pulse, frames)) {
            return false;
        }
    }
    return true;
}

static bool audio_server_open(audio_state_t *state) {
    if (state->output == AUDIO_OUTPUT_FDS) {
        const audio_pulse_open_t open = {.cookie_path = nullptr,
                                         .name = AUDIO_STREAM_NAME,
                                         .borrowed_stop = &state->stop,
                                         .muted = 0u,
                                         ._pad0 = {0}};
        return audio_pulse_attach(&state->pulse, state->read_fd, state->write_fd, &open);
    }
    const audio_pulse_open_t open = {.cookie_path = state->has_cookie_path != 0u ? state->cookie_path : nullptr,
                                     .name = AUDIO_STREAM_NAME,
                                     .borrowed_stop = &state->stop,
                                     .muted = 0u,
                                     ._pad0 = {0}};
    return audio_pulse_connect(&state->pulse, state->socket_path, &open);
}

/* The one line, however many times it looks. */
static void audio_log_silent(audio_state_t *state) {
    if (state->logged_silent == 0u) {
        state->logged_silent = 1u;
        LOG_INFO("audio: no sound server, so the game plays silently: %s", state->pulse.why);
    }
}

/* With a server, or looking for one. What is mixed stays silence while there is none. */
static void audio_server_run(audio_state_t *state) {
    int64_t next_look_ns = 0;
    while (atomic_load_explicit(&state->stop, memory_order_relaxed) == 0u) {
        if (atomic_load_explicit(&state->connected, memory_order_relaxed) != 0u) {
            if (!audio_server_step(state)) {
                atomic_store_explicit(&state->connected, 0u, memory_order_relaxed);
                audio_log_silent(state);
                audio_pulse_close(&state->pulse);
                /* Nothing more can be heard of what was playing. */
                for (uint32_t i = 0u; i < AUDIO_MIXER_VOICES; i++) {
                    state->mixer.voices[i].borrowed_samples = nullptr;
                }
                next_look_ns = time_now_ns() + (int64_t)state->retry_ms * 1000000;
            }
            continue;
        }
        audio_events_take(state, false);
        const int64_t now_ns = time_now_ns();
        if (now_ns >= next_look_ns) {
            if (audio_server_open(state)) {
                atomic_store_explicit(&state->connected, 1u, memory_order_relaxed);
                continue;
            }
            audio_log_silent(state);
            next_look_ns = now_ns + (int64_t)state->retry_ms * 1000000;
        }
        time_sleep_ns((int64_t)AUDIO_WAKE_MS * 1000000);
    }
    audio_pulse_close(&state->pulse);
    atomic_store_explicit(&state->connected, 0u, memory_order_relaxed);
}

/* At the speed it would play, with the same mixer, so that a file holds what a person would have heard. */
static void audio_file_run(audio_state_t *state) {
    static_assert((uint32_t)AUDIO_FILE_FRAMES <= (uint32_t)AUDIO_PULSE_PCM_FRAMES);
    int16_t *const samples = audio_pulse_samples(&state->pulse);
    int64_t due_ns = time_now_ns();
    bool writing = true;
    while (atomic_load_explicit(&state->stop, memory_order_relaxed) == 0u) {
        audio_events_take(state, writing);
        due_ns += (int64_t)AUDIO_FILE_FRAMES * 1000000000 / PULSE_WIRE_RATE;
        if (writing) {
            audio_mixer_render(&state->mixer, samples, AUDIO_FILE_FRAMES,
                               atomic_load_explicit(&state->volume_percent, memory_order_relaxed));
            const file_status_t status =
                file_write_all(&state->file, samples, AUDIO_FILE_FRAMES * PULSE_WIRE_FRAME_BYTES);
            if (status != FILE_OK) {
                LOG_WARN("audio: could not write what would have played: %s", file_status_name(status));
                writing = false;
            }
        }
        time_sleep_until_ns(due_ns);
    }
}

static void audio_thread(void *borrowed_argument) {
    audio_state_t *const state = borrowed_argument;
    if (state->output == AUDIO_OUTPUT_FILE) {
        audio_file_run(state);
    } else {
        audio_server_run(state);
    }
}

/* ---- Start and stop ---------------------------------------------------------------------------------------------- */

/* The server's and its cookie's places, from what the environment says, on the main thread as nothing else changes it there. */
static bool audio_paths_read(audio_state_t *state, const audio_options_t *options) {
    if (options->socket_path != nullptr) {
        (void)memccpy(state->socket_path, options->socket_path, 0, AUDIO_PATH_BYTES - 1u);
    } else if (!pulse_wire_socket_path(state->socket_path, AUDIO_PATH_BYTES, getenv("PULSE_SERVER"),
                                       getenv("XDG_RUNTIME_DIR"), (uint32_t)getuid())) {
        LOG_INFO("audio: PULSE_SERVER is not a socket on this machine, so the game plays silently");
        return false;
    }
    if (options->cookie_path != nullptr) {
        (void)memccpy(state->cookie_path, options->cookie_path, 0, AUDIO_PATH_BYTES - 1u);
        state->has_cookie_path = 1u;
    } else {
        state->has_cookie_path = pulse_wire_cookie_path(state->cookie_path, AUDIO_PATH_BYTES, getenv("PULSE_COOKIE"),
                                                        getenv("XDG_CONFIG_HOME"), getenv("HOME"))
                                     ? 1u
                                     : 0u;
    }
    return true;
}

static bool audio_start_output(audio_state_t *state, const audio_options_t *options) {
    state->file = (file_t){.fd = -1, ._pad0 = {0}};
    switch (options->output) {
    case AUDIO_OUTPUT_SERVER:
        return audio_paths_read(state, options);
    case AUDIO_OUTPUT_FILE: {
        const file_status_t status = file_open(&state->file, options->file_path, FILE_MODE_WRITE);
        if (status != FILE_OK) {
            LOG_INFO("audio: cannot write to %s, so nothing is kept: %s", options->file_path, file_status_name(status));
            return false;
        }
        return true;
    }
    case AUDIO_OUTPUT_FDS:
        state->read_fd = options->read_fd;
        state->write_fd = options->write_fd;
        return true;
    case AUDIO_OUTPUT_NONE:
        break;
    }
    return false;
}

bool audio_display_is_private(const char *display) {
    uint32_t number = 0u;
    return display != nullptr && wire_display_number(display, &number) && number >= AUDIO_PRIVATE_DISPLAY_FIRST;
}

void audio_init(audio_t *audio, const audio_options_t *options, const thread_topology_t *topology) {
    memset(audio, 0, sizeof(*audio));
    if (options->output == AUDIO_OUTPUT_NONE || MC_THREADS_AUDIO == 0) {
        return;
    }
    sound_blob_view_t view;
    const sound_blob_status_t blob = sound_blob_check(embed_assets_sound_blob, embed_assets_sound_blob_bytes, &view);
    if (blob != SOUND_BLOB_OK) {
        LOG_INFO("audio: the sounds built in are damaged (%" PRIu32 "), so the game plays silently", (uint32_t)blob);
        return;
    }
    if (!heap_reserve(AUDIO_REGION_BYTES, &audio->owned_region)) {
        LOG_INFO("audio: no memory for it, so the game plays silently");
        return;
    }
    audio_state_t *const state = (audio_state_t *)(void *)audio->owned_region.owned_base;
    memset(state, 0, sizeof(*state));
    arena_init(&state->arena, "audio", audio->owned_region.owned_base + sizeof(*state),
               audio->owned_region.bytes - sizeof(*state));
    audio_pulse_start(&state->pulse, &state->arena);
    /* Which sound of a family comes next is the only thing this seed says, and nothing else in the game hears of it. */
    audio_mixer_start(&state->mixer, &view, (uint64_t)time_now_ns());
    const uint32_t volume =
        options->volume_percent < AUDIO_MIXER_VOLUME_MAX ? options->volume_percent : AUDIO_MIXER_VOLUME_MAX;
    atomic_store_explicit(&state->volume_percent, volume, memory_order_relaxed);
    state->retry_ms = options->retry_ms != 0u ? options->retry_ms : AUDIO_RETRY_MS;
    state->output = options->output;
    if (audio_start_output(state, options)) {
        if (thread_start(&state->thread, topology, THREAD_ROLE_AUDIO, 0u, audio_thread, state)) {
            audio->borrowed_state = state;
            return;
        }
        LOG_INFO("audio: could not start its thread, so the game plays silently");
        if (options->output == AUDIO_OUTPUT_FILE) {
            (void)file_close(&state->file);
        }
    }
    arena_deinit(&state->arena);
    heap_release(&audio->owned_region);
}

void audio_deinit(audio_t *audio) {
    audio_state_t *const state = audio->borrowed_state;
    if (state == nullptr) {
        return;
    }
    atomic_store_explicit(&state->stop, 1u, memory_order_relaxed);
    thread_join(&state->thread);
    if (state->output == AUDIO_OUTPUT_FILE && file_close(&state->file) != FILE_OK) {
        LOG_WARN("audio: could not finish the file of what would have played");
    }
    audio->played_count = state->mixer.played_count;
    audio->dropped_count = state->ring.dropped_count;
    LOG_DEBUG("audio: %" PRIu64 " sounds played, %" PRIu64 " took another's voice, %" PRIu64 " posts found no room",
              audio->played_count, state->mixer.stolen_count, audio->dropped_count);
    audio->borrowed_state = nullptr;
    arena_deinit(&state->arena);
    heap_release(&audio->owned_region);
}

/* ---- From the rest of the game ----------------------------------------------------------------------------------- */

bool audio_post(audio_t *audio, uint32_t family, uint32_t kind, uint16_t gain_left, uint16_t gain_right) {
    audio_state_t *const state = audio->borrowed_state;
    if (state == nullptr) {
        return false;
    }
    const uint32_t head = atomic_load_explicit(&state->ring.head, memory_order_relaxed);
    const uint32_t tail = atomic_load_explicit(&state->ring.tail, memory_order_acquire);
    if (head - tail >= AUDIO_RING_EVENTS) {
        state->ring.dropped_count++;
        return false;
    }
    state->ring.events[head % AUDIO_RING_EVENTS] = (audio_event_t){.gain_left = gain_left,
                                                                   .gain_right = gain_right,
                                                                   .family = (uint8_t)family,
                                                                   .kind = (uint8_t)kind,
                                                                   ._pad0 = {0}};
    atomic_store_explicit(&state->ring.head, head + 1u, memory_order_release);
    return true;
}

void audio_volume_set(audio_t *audio, uint32_t volume_percent, bool preview) {
    audio_state_t *const state = audio->borrowed_state;
    if (state == nullptr) {
        return;
    }
    const uint32_t held = volume_percent < AUDIO_MIXER_VOLUME_MAX ? volume_percent : AUDIO_MIXER_VOLUME_MAX;
    atomic_store_explicit(&state->volume_percent, held, memory_order_relaxed);
    if (preview) {
        atomic_store_explicit(&state->preview, 1u, memory_order_relaxed);
    }
}

uint32_t audio_volume_get(const audio_t *audio) {
    const audio_state_t *const state = audio->borrowed_state;
    return state != nullptr ? atomic_load_explicit(&state->volume_percent, memory_order_relaxed) : 0u;
}

bool audio_connected(const audio_t *audio) {
    const audio_state_t *const state = audio->borrowed_state;
    return state != nullptr && atomic_load_explicit(&state->connected, memory_order_relaxed) != 0u;
}
