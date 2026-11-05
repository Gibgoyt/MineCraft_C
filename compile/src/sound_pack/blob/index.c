/* Lays the blob out as run/include/types/sound_blob/index.h says, fills it, and proves it with the reader's own check. */
#include "sound_pack/blob/index.h"

#include "types/sound_blob/index.h"

#include <string.h>

/* The largest magnitude of any sample. -32768 is 32768, which a uint16_t holds. */
static uint16_t sb_peak(const int16_t *samples, uint32_t count) {
    int32_t peak = 0;
    for (uint32_t i = 0; i < count; i++) {
        const int32_t size = samples[i] < 0 ? -(int32_t)samples[i] : (int32_t)samples[i];
        peak = size > peak ? size : peak;
    }
    return (uint16_t)peak;
}

bool sound_pack_blob_write(tp_arena_t *arena, const sound_tree_t *tree, const char *path) {
    const uint64_t entries_end = sizeof(sound_blob_header_t) + (uint64_t)tree->sound_count * sizeof(sound_blob_entry_t);
    const uint64_t samples_offset = sound_blob_align_up(entries_end, SOUND_BLOB_ALIGNMENT);
    uint64_t samples_bytes = 0;
    for (uint32_t i = 0; i < tree->sound_count; i++) {
        samples_bytes = sound_blob_align_up(samples_bytes, SOUND_BLOB_SOUND_ALIGNMENT);
        samples_bytes += (uint64_t)tree->borrowed_sounds[i].sample_count * sizeof(int16_t);
    }
    const uint64_t total = samples_offset + samples_bytes;

    /* The arena is zeroed, which is what the gaps must hold, and aligned to 16 only, so the blob's start is aligned by hand. */
    uint8_t *raw = tp_arena_alloc(arena, total + SOUND_BLOB_ALIGNMENT);
    uint8_t *blob = raw + (sound_blob_align_up((uintptr_t)raw, SOUND_BLOB_ALIGNMENT) - (uintptr_t)raw);
    sound_blob_entry_t *entries = (sound_blob_entry_t *)(void *)(blob + sizeof(sound_blob_header_t));
    uint64_t at = 0;
    for (uint32_t i = 0; i < tree->sound_count; i++) {
        const sound_tree_sound_t *sound = &tree->borrowed_sounds[i];
        at = sound_blob_align_up(at, SOUND_BLOB_SOUND_ALIGNMENT);
        const uint64_t bytes = (uint64_t)sound->sample_count * sizeof(int16_t);
        memcpy(blob + samples_offset + at, sound->borrowed_samples, bytes);
        entries[i] = (sound_blob_entry_t){
            .samples_offset = at,
            .sample_count = sound->sample_count,
            .peak = sb_peak(sound->borrowed_samples, sound->sample_count),
            .family = sound->family,
            .kind = sound->kind,
            .variant = sound->variant,
        };
        at += bytes;
    }
    const sound_blob_header_t header = {
        .magic = SOUND_BLOB_MAGIC,
        .version = SOUND_BLOB_VERSION,
        .sound_count = tree->sound_count,
        .sample_rate = SOUND_BLOB_SAMPLE_RATE,
        .total_bytes = total,
        .payload_hash = sound_blob_hash(blob + sizeof(sound_blob_header_t), total - sizeof(sound_blob_header_t)),
        .entries_offset = sizeof(sound_blob_header_t),
        .samples_offset = samples_offset,
        .samples_bytes = samples_bytes,
    };
    memcpy(blob, &header, sizeof(header));

    sound_blob_view_t view;
    const sound_blob_status_t status = sound_blob_check(blob, total, &view);
    if (status != SOUND_BLOB_OK) {
        return tp_fail("the blob made from this tree fails its own check, with status %u", (uint32_t)status);
    }
    return tp_write_if_changed(arena, path, blob, total, nullptr);
}
