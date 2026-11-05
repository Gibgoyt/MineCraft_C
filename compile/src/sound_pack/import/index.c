/* A person converts a recording with ffmpeg, once, and this commits the result in our own format. The build never runs it. See README.md. */
#include "sound_pack/import/index.h"

#include "sound_pack/mcsnd/index.h"
#include "sound_pack/tree/index.h"
#include "types/sound_blob/index.h"

#include <string.h>

#define SI_MAX_RAW_BYTES (SOUND_BLOB_MAX_SAMPLES * 2ull)
#define SI_LICENCE "CC0-1.0"
#define SI_TEXT_BYTES (1ull << 14)

/* Plain, and safe inside double quotes in yaml and on one line of a README. */
static bool si_text_is_quotable(const char *text) {
    return text[0] != '\0' && tp_text_is_plain(text) && strchr(text, '"') == nullptr && strchr(text, '\\') == nullptr;
}

static bool si_family_is_known(const char *family) {
    for (uint32_t i = 1; i < SOUND_FAMILY_COUNT; i++) {
        if (strcmp(family, sound_family_name(i)) == 0) {
            return true;
        }
    }
    return false;
}

static bool si_check(const sound_import_t *import) {
    uint8_t kind = 0;
    uint8_t variant = 0;
    if (!si_family_is_known(import->family)) {
        return tp_fail("family is not one of the sound families");
    }
    if (!sound_tree_parse_name(import->name, &kind, &variant)) {
        return tp_fail("name '%s' is not <hit|break|place>_<1 to %u>", import->name, SOUND_BLOB_MAX_VARIANTS);
    }
    if (strcmp(import->licence, SI_LICENCE) != 0) {
        return tp_fail("licence is not %s, the only one sounds may have", SI_LICENCE);
    }
    if (!si_text_is_quotable(import->origin) || !si_text_is_quotable(import->file) ||
        !si_text_is_quotable(import->url) || !si_text_is_quotable(import->changes)) {
        return tp_fail("origin, file, url and changes must be plain text with no quote or backslash");
    }
    return true;
}

bool sound_pack_import(tp_arena_t *arena, const sound_import_t *import) {
    if (!si_check(import)) {
        return false;
    }
    uint8_t *raw = nullptr;
    uint64_t raw_bytes = 0;
    if (tp_read_path(arena, import->raw, SI_MAX_RAW_BYTES, &raw, &raw_bytes) != TP_READ_OK) {
        return tp_fail("%s: cannot be read, or holds over five seconds", import->raw);
    }
    const uint64_t capacity = sizeof(mcsnd_header_t) + raw_bytes;
    uint8_t *file = tp_arena_alloc(arena, capacity);
    const mcsnd_status_t status = mcsnd_encode(file, capacity, raw, raw_bytes);
    if (status != MCSND_OK) {
        return tp_fail("%s: %s", import->raw, mcsnd_status_text(status));
    }

    const char *directory = tp_arena_format(arena, "%s/%s/%s", import->sounds, import->family, import->name);
    tp_builder_t yaml = tp_builder_begin(arena, SI_TEXT_BYTES);
    tp_builder_add(&yaml, "# Written by compile/src/sound_pack import. A re-import replaces it.\n");
    tp_builder_add(&yaml, "name: %s\nfamily: %s\n", import->name, import->family);
    tp_builder_add(&yaml, "source:\n  kind: recorded\n  origin: \"%s\"\n  file: \"%s\"\n  url: \"%s\"\n  licence: %s\n",
                   import->origin, import->file, import->url, import->licence);
    tp_builder_t readme = tp_builder_begin(arena, SI_TEXT_BYTES);
    tp_builder_add(&readme, "# sounds/%s/%s\n\n", import->family, import->name);
    tp_builder_add(&readme, "From %s, file `%s`: %s\n\nLicence: %s. See `sounds/ATTRIBUTION.md`.\n\n", import->origin,
                   import->file, import->url, import->licence);
    tp_builder_add(&readme,
                   "Changed from the original: converted to mono 16-bit at 48000 Hz, in `index.mcsnd`; %s.\n\n",
                   import->changes);
    tp_builder_add(&readme, "Written by `compile/src/sound_pack`, which `sounds/README.md` describes.\n");

    return tp_make_directories(arena, directory) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.mcsnd", directory), file, capacity, nullptr) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.yaml", directory), yaml.borrowed_text, yaml.used,
                               nullptr) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/README.md", directory), readme.borrowed_text,
                               readme.used, nullptr);
}
