/* The .mcsnd format: see README.md. Plain enough that there is little to get wrong. */
#include "sound_pack/mcsnd/index.h"

#include "types/sound_blob/index.h"

#include <string.h>

mcsnd_status_t mcsnd_encode(uint8_t *out, uint64_t capacity, const uint8_t *samples, uint64_t sample_bytes) {
    if (sample_bytes == 0u || sample_bytes % 2u != 0u || sample_bytes / 2u > SOUND_BLOB_MAX_SAMPLES) {
        return MCSND_BAD_SIZE;
    }
    if (capacity < sizeof(mcsnd_header_t) || capacity - sizeof(mcsnd_header_t) < sample_bytes) {
        return MCSND_NO_ROOM;
    }
    const mcsnd_header_t header = {
        .magic = MCSND_MAGIC,
        .version = MCSND_VERSION,
        .sample_rate = SOUND_BLOB_SAMPLE_RATE,
        .sample_count = (uint32_t)(sample_bytes / 2u),
        .sample_hash = sound_blob_hash(samples, sample_bytes),
    };
    memcpy(out, &header, sizeof(header));
    memcpy(out + sizeof(header), samples, sample_bytes);
    return MCSND_OK;
}

mcsnd_status_t mcsnd_parse(const uint8_t *file, uint64_t length, mcsnd_view_t *view) {
    memset(view, 0, sizeof(*view));
    if (length < sizeof(mcsnd_header_t)) {
        return MCSND_TRUNCATED;
    }
    mcsnd_header_t header;
    memcpy(&header, file, sizeof(header));
    if (header.magic != MCSND_MAGIC) {
        return MCSND_BAD_MAGIC;
    }
    if (header.version != MCSND_VERSION) {
        return MCSND_BAD_VERSION;
    }
    if (header.sample_rate != SOUND_BLOB_SAMPLE_RATE) {
        return MCSND_BAD_RATE;
    }
    if (header.sample_count == 0u || header.sample_count > SOUND_BLOB_MAX_SAMPLES) {
        return MCSND_BAD_SIZE;
    }
    if (header._pad0 != 0u) {
        return MCSND_BAD_PADDING;
    }
    const uint64_t sample_bytes = (uint64_t)header.sample_count * 2u;
    const uint64_t present = length - sizeof(header);
    if (present < sample_bytes) {
        return MCSND_TRUNCATED;
    }
    if (present > sample_bytes) {
        return MCSND_TRAILING;
    }
    const uint8_t *samples = file + sizeof(header);
    if (sound_blob_hash(samples, sample_bytes) != header.sample_hash) {
        return MCSND_BAD_HASH;
    }
    view->borrowed_samples = samples;
    view->sample_count = header.sample_count;
    return MCSND_OK;
}

const char *mcsnd_status_text(mcsnd_status_t status) {
    switch (status) {
    case MCSND_OK:
        return "it is good";
    case MCSND_TRUNCATED:
        return "it is shorter than its header says";
    case MCSND_BAD_MAGIC:
        return "it does not start with MCSN";
    case MCSND_BAD_VERSION:
        return "its version is not one this tool reads";
    case MCSND_BAD_RATE:
        return "its sample rate is not 48000";
    case MCSND_BAD_SIZE:
        return "it has no samples, an odd byte, or over five seconds of them";
    case MCSND_BAD_PADDING:
        return "its padding is not zero";
    case MCSND_TRAILING:
        return "it has bytes after its samples";
    case MCSND_BAD_HASH:
        return "its samples do not match their hash";
    case MCSND_NO_ROOM:
        return "the buffer is too small";
    }
    return "its status is unknown";
}
