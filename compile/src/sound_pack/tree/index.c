/*
 * Reads sounds/<family>/<name>/ for every sound, and refuses anything it does not understand. A problem does not stop the
 * reading, so one build shows all of them. Directories are read in whatever order the system gives, and the slots put them in
 * order, so the result does not depend on it. See README.md.
 */
#include "sound_pack/tree/index.h"

#include "sound_pack/mcsnd/index.h"
#include "sound_pack/synth/index.h"
#include "types/sound_blob/index.h"
#include "yaml/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define ST_MAX_YAML_BYTES (1ull << 16)
#define ST_MAX_ATTRIBUTION_BYTES (1ull << 22)
#define ST_MAX_MCSND_BYTES (sizeof(mcsnd_header_t) + SOUND_BLOB_MAX_SAMPLES * 2ull)
#define ST_LICENCE "CC0-1.0"

static const char *const ST_SOUND_FIELDS[] = {"name", "family", "source", "recipe"};
enum { ST_SOUND_FIELD_COUNT = sizeof(ST_SOUND_FIELDS) / sizeof(ST_SOUND_FIELDS[0]) };
static const char *const ST_SOURCE_FIELDS[] = {"kind", "origin", "file", "url", "licence"};
enum { ST_SOURCE_FIELD_COUNT = sizeof(ST_SOURCE_FIELDS) / sizeof(ST_SOURCE_FIELDS[0]) };
static const char *const ST_RECIPE_FIELDS[] = {"seed",    "ms",          "grains",  "grain_ms",   "low_hz", "high_hz",
                                               "tone_hz", "overtone_hz", "tone_ms", "tone_level", "level"};
enum { ST_RECIPE_FIELD_COUNT = sizeof(ST_RECIPE_FIELDS) / sizeof(ST_RECIPE_FIELDS[0]) };

typedef struct st_context_t {
    tp_arena_t *borrowed_arena;  /* 0 */
    const char *borrowed_path;   /* 8: the yaml being read, for reporting */
    const uint8_t *attribution;  /* 16: sounds/ATTRIBUTION.md, nullptr if it is missing */
    uint64_t attribution_length; /* 24 */
    uint32_t problems;           /* 32 */
    uint8_t _pad0[4];            /* 36 */
} st_context_t;
static_assert(sizeof(st_context_t) == 40);

[[gnu::format(printf, 4, 5)]]
static void st_report_at(st_context_t *context, uint32_t line, uint32_t column, const char *format, ...) {
    fprintf(stderr, "%s:%u:%u: ", context->borrowed_path, line, column);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    context->problems++;
}

[[gnu::format(printf, 2, 3)]]
static void st_report(st_context_t *context, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    context->problems++;
}

static void st_report_yaml(st_context_t *context, const yaml_error_t *error) {
    st_report_at(context, error->line, error->column, "%s", error->message);
}

static bool st_is_file(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool st_is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

/* Reports every key of map that the list does not hold: a typo must not quietly mean something else. */
static void st_reject_unknown(st_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                              const char *what, const char *const *fields, uint32_t field_count) {
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        bool known = false;
        for (uint32_t i = 0; i < field_count && !known; i++) {
            const uint64_t length = strlen(fields[i]);
            known = child->key_length == length && memcmp(child->borrowed_key, fields[i], length) == 0;
        }
        if (!known) {
            st_report_at(context, child->line, child->column, "unknown field '%.*s' in %s", (int32_t)child->key_length,
                         (const char *)child->borrowed_key, what);
        }
    }
}

/* A NUL-terminated copy of a scalar, or nullptr with the problem reported. */
static const char *st_read_text(st_context_t *context, const yaml_node_t *node) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        st_report_yaml(context, &error);
        return nullptr;
    }
    return tp_arena_text(context->borrowed_arena, text, length);
}

/* The field must be there, and be the text expected. */
static void st_require_text(st_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                            const char *key, const char *expected) {
    const yaml_node_t *node = yaml_map_get(document, map, key);
    if (node == nullptr) {
        st_report_at(context, map->line, map->column, "missing field '%s'", key);
        return;
    }
    const char *text = st_read_text(context, node);
    if (text != nullptr && strcmp(text, expected) != 0) {
        st_report_at(context, node->line, node->column, "%s '%s' is not its directory's, '%s'", key, text, expected);
    }
}

/* A required whole number that fits 32 bits. The recipe's own check then says what it may be. */
static void st_read_u32(st_context_t *context, const yaml_document_t *document, const yaml_node_t *map, const char *key,
                        uint32_t *out) {
    const yaml_node_t *node = yaml_map_get(document, map, key);
    if (node == nullptr) {
        st_report_at(context, map->line, map->column, "missing field '%s' in recipe", key);
        return;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        st_report_yaml(context, &error);
    } else if (value < 0 || value > (int64_t)UINT32_MAX) {
        st_report_at(context, node->line, node->column, "%s is %" PRId64 ", and must not be negative or huge", key,
                     value);
    } else {
        *out = (uint32_t)value;
    }
}

static bool st_read_recipe(st_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                           sound_synth_recipe_t *recipe) {
    const uint32_t problems_before = context->problems;
    if (map->kind != YAML_KIND_MAP) {
        st_report_at(context, map->line, map->column, "recipe must be a mapping");
        return false;
    }
    st_reject_unknown(context, document, map, "recipe", ST_RECIPE_FIELDS, ST_RECIPE_FIELD_COUNT);
    uint32_t seed = 0;
    st_read_u32(context, document, map, "seed", &seed);
    recipe->seed = seed;
    st_read_u32(context, document, map, "ms", &recipe->ms);
    st_read_u32(context, document, map, "grains", &recipe->grains);
    st_read_u32(context, document, map, "grain_ms", &recipe->grain_ms);
    st_read_u32(context, document, map, "low_hz", &recipe->low_hz);
    st_read_u32(context, document, map, "high_hz", &recipe->high_hz);
    st_read_u32(context, document, map, "tone_hz", &recipe->tone_hz);
    st_read_u32(context, document, map, "overtone_hz", &recipe->overtone_hz);
    st_read_u32(context, document, map, "tone_ms", &recipe->tone_ms);
    st_read_u32(context, document, map, "tone_level", &recipe->tone_level);
    st_read_u32(context, document, map, "level", &recipe->level);
    if (context->problems != problems_before) {
        return false;
    }
    const char *problem = sound_synth_recipe_problem(recipe);
    if (problem != nullptr) {
        st_report_at(context, map->line, map->column, "%s", problem);
        return false;
    }
    return true;
}

/* A recording may come in only with its licence, its source and its credit. */
static void st_check_recorded_source(st_context_t *context, const yaml_document_t *document,
                                     const yaml_node_t *source) {
    const char *const required[] = {"origin", "file", "url"};
    for (uint32_t i = 0; i < 3u; i++) {
        const yaml_node_t *node = yaml_map_get(document, source, required[i]);
        if (node == nullptr) {
            st_report_at(context, source->line, source->column, "a recording needs '%s' in source", required[i]);
        } else {
            const char *text = st_read_text(context, node);
            if (text != nullptr && !tp_text_is_plain(text)) {
                st_report_at(context, node->line, node->column, "%s is not plain text", required[i]);
            }
        }
    }
    const yaml_node_t *licence = yaml_map_get(document, source, "licence");
    if (licence == nullptr) {
        st_report_at(context, source->line, source->column, "a recording needs 'licence' in source");
    } else {
        const char *text = st_read_text(context, licence);
        if (text != nullptr && strcmp(text, ST_LICENCE) != 0) {
            st_report_at(context, licence->line, licence->column,
                         "licence '%s' is not %s, the only one sounds may have", text, ST_LICENCE);
        }
    }
    const yaml_node_t *origin = yaml_map_get(document, source, "origin");
    if (origin != nullptr) {
        const char *text = st_read_text(context, origin);
        if (text != nullptr && context->attribution != nullptr &&
            !tp_bytes_contain(context->attribution, context->attribution_length, text)) {
            st_report_at(context, origin->line, origin->column, "origin '%s' is not credited in ATTRIBUTION.md", text);
        }
    }
}

/* Its samples, from the file a recording has. */
static const int16_t *st_load_recording(st_context_t *context, const char *path, uint32_t *out_count) {
    uint8_t *bytes = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(context->borrowed_arena, path, ST_MAX_MCSND_BYTES, &bytes, &length);
    if (status != TP_READ_OK) {
        st_report(context, "%s: a recording needs this file, and it cannot be read", path);
        return nullptr;
    }
    mcsnd_view_t view;
    const mcsnd_status_t parsed = mcsnd_parse(bytes, length, &view);
    if (parsed != MCSND_OK) {
        st_report(context, "%s: %s", path, mcsnd_status_text(parsed));
        return nullptr;
    }
    /* The file's samples have no alignment promise, and the blob copies these as bytes anyway. */
    int16_t *samples = tp_arena_alloc(context->borrowed_arena, (uint64_t)view.sample_count * 2u);
    memcpy(samples, view.borrowed_samples, (uint64_t)view.sample_count * 2u);
    *out_count = view.sample_count;
    return samples;
}

/* The scratch goes back to the arena, and only the samples stay. */
static const int16_t *st_render(st_context_t *context, const sound_synth_recipe_t *recipe, uint32_t *out_count) {
    tp_arena_t *arena = context->borrowed_arena;
    const uint32_t count = sound_synth_sample_count(recipe);
    int16_t *samples = tp_arena_alloc(arena, (uint64_t)count * sizeof(int16_t));
    const uint64_t mark = arena->used;
    float64_t *work = tp_arena_alloc(arena, (uint64_t)count * sizeof(float64_t));
    sound_synth_render(recipe, samples, work);
    arena->used = mark;
    *out_count = count;
    return samples;
}

static void st_read_sound(st_context_t *context, const char *directory, const char *family, const char *name,
                          sound_tree_sound_t *sound) {
    tp_arena_t *arena = context->borrowed_arena;
    const char *yaml_path = tp_arena_format(arena, "%s/index.yaml", directory);
    const char *mcsnd_path = tp_arena_format(arena, "%s/index.mcsnd", directory);
    context->borrowed_path = yaml_path;
    if (!st_is_file(tp_arena_format(arena, "%s/README.md", directory))) {
        st_report(context, "%s/README.md: missing, and a sound must say where it came from", directory);
    }
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(arena, yaml_path, ST_MAX_YAML_BYTES, &text, &length);
    if (status != TP_READ_OK) {
        st_report(context, "%s: missing or unreadable", yaml_path);
        return;
    }
    yaml_storage_t storage = {
        .nodes = tp_arena_alloc(arena, (length + 1u) * sizeof(yaml_node_t)),
        .strings = tp_arena_alloc(arena, length + 1u),
        .node_capacity = (uint32_t)(length + 1u),
        .string_capacity = (uint32_t)length,
    };
    yaml_document_t document;
    yaml_error_t error = {0};
    if (!yaml_parse(&document, text, length, storage, &error)) {
        st_report_yaml(context, &error);
        return;
    }
    const yaml_node_t *root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        st_report_at(context, 1, 1, "a sound's file must be a mapping");
        return;
    }
    st_reject_unknown(context, &document, root, "a sound", ST_SOUND_FIELDS, ST_SOUND_FIELD_COUNT);
    st_require_text(context, &document, root, "name", name);
    st_require_text(context, &document, root, "family", family);

    const yaml_node_t *source = yaml_map_get(&document, root, "source");
    if (source == nullptr || source->kind != YAML_KIND_MAP) {
        st_report_at(context, root->line, root->column, "a sound needs a mapping 'source'");
        return;
    }
    st_reject_unknown(context, &document, source, "source", ST_SOURCE_FIELDS, ST_SOURCE_FIELD_COUNT);
    const yaml_node_t *kind = yaml_map_get(&document, source, "kind");
    const char *kind_text = kind != nullptr ? st_read_text(context, kind) : nullptr;
    const yaml_node_t *recipe_node = yaml_map_get(&document, root, "recipe");
    if (kind_text != nullptr && strcmp(kind_text, "synth") == 0) {
        if (st_is_file(mcsnd_path)) {
            st_report(context, "%s: a sound made from a recipe must have no recording beside it", mcsnd_path);
        }
        if (recipe_node == nullptr) {
            st_report_at(context, root->line, root->column, "source kind synth needs a 'recipe'");
            return;
        }
        sound_synth_recipe_t recipe = {0};
        if (st_read_recipe(context, &document, recipe_node, &recipe)) {
            sound->borrowed_samples = st_render(context, &recipe, &sound->sample_count);
        }
    } else if (kind_text != nullptr && strcmp(kind_text, "recorded") == 0) {
        if (recipe_node != nullptr) {
            st_report_at(context, recipe_node->line, recipe_node->column, "a recording has no recipe");
        }
        st_check_recorded_source(context, &document, source);
        sound->borrowed_samples = st_load_recording(context, mcsnd_path, &sound->sample_count);
        sound->recorded = 1;
    } else if (kind != nullptr && kind_text != nullptr) {
        st_report_at(context, kind->line, kind->column, "source kind '%s' is not synth or recorded", kind_text);
    } else if (kind == nullptr) {
        st_report_at(context, source->line, source->column, "source needs a 'kind'");
    }
}

bool sound_tree_parse_name(const char *name, uint8_t *out_kind, uint8_t *out_variant) {
    for (uint32_t kind = 0; kind < SOUND_KIND_COUNT; kind++) {
        const char *word = sound_kind_name(kind);
        const uint64_t length = strlen(word);
        if (strncmp(name, word, length) == 0 && name[length] == '_' && name[length + 1u] >= '1' &&
            (uint32_t)(name[length + 1u] - '0') <= SOUND_BLOB_MAX_VARIANTS && name[length + 2u] == '\0') {
            *out_kind = (uint8_t)kind;
            *out_variant = (uint8_t)(name[length + 1u] - '0');
            return true;
        }
    }
    return false;
}

/* One slot per family, kind and variant, so that the order of reading does not matter. */
#define ST_SLOT_COUNT (SOUND_FAMILY_COUNT * SOUND_KIND_COUNT * SOUND_BLOB_MAX_VARIANTS)

static uint32_t st_slot(uint32_t family, uint32_t kind, uint32_t variant) {
    return (family * SOUND_KIND_COUNT + kind) * SOUND_BLOB_MAX_VARIANTS + (variant - 1u);
}

static void st_read_family(st_context_t *context, const char *sounds, uint8_t family, sound_tree_sound_t *slots) {
    tp_arena_t *arena = context->borrowed_arena;
    const char *family_name = sound_family_name(family);
    const char *family_path = tp_arena_format(arena, "%s/%s", sounds, family_name);
    if (!st_is_file(tp_arena_format(arena, "%s/README.md", family_path))) {
        st_report(context, "%s/README.md: missing", family_path);
    }
    DIR *listing = opendir(family_path);
    if (listing == nullptr) {
        st_report(context, "%s: family '%s' has no directory, so it would have no sound", family_path, family_name);
        return;
    }
    for (struct dirent *item = readdir(listing); item != nullptr; item = readdir(listing)) {
        const char *child = tp_arena_format(arena, "%s/%s", family_path, item->d_name);
        if (item->d_name[0] == '.' || !st_is_directory(child)) {
            continue;
        }
        uint8_t kind = 0;
        uint8_t variant = 0;
        if (!sound_tree_parse_name(item->d_name, &kind, &variant)) {
            st_report(context, "%s: a sound's directory is <hit|break|place>_<1 to %u>", child,
                      SOUND_BLOB_MAX_VARIANTS);
            continue;
        }
        sound_tree_sound_t *sound = &slots[st_slot(family, kind, variant)];
        sound->family = family;
        sound->kind = kind;
        sound->variant = variant;
        st_read_sound(context, child, family_name, item->d_name, sound);
    }
    closedir(listing);
}

/* Anything in sounds/ that is not a family is a typo, or a family this build does not know. */
static void st_reject_strangers(st_context_t *context, const char *sounds) {
    DIR *listing = opendir(sounds);
    if (listing == nullptr) {
        st_report(context, "%s: cannot be listed", sounds);
        return;
    }
    for (struct dirent *item = readdir(listing); item != nullptr; item = readdir(listing)) {
        const char *child = tp_arena_format(context->borrowed_arena, "%s/%s", sounds, item->d_name);
        if (item->d_name[0] == '.' || !st_is_directory(child)) {
            continue;
        }
        bool known = false;
        for (uint32_t family = 1; family < SOUND_FAMILY_COUNT && !known; family++) {
            known = strcmp(item->d_name, sound_family_name(family)) == 0;
        }
        if (!known) {
            st_report(context, "%s: not a sound family", child);
        }
    }
    closedir(listing);
}

bool sound_tree_load(tp_arena_t *arena, const char *sounds, sound_tree_t *out) {
    memset(out, 0, sizeof(*out));
    st_context_t context = {.borrowed_arena = arena, .borrowed_path = sounds};
    uint8_t *attribution = nullptr;
    const char *attribution_path = tp_arena_format(arena, "%s/ATTRIBUTION.md", sounds);
    if (tp_read_path(arena, attribution_path, ST_MAX_ATTRIBUTION_BYTES, &attribution, &context.attribution_length) !=
        TP_READ_OK) {
        st_report(&context, "%s: missing, and it ships with these sounds", attribution_path);
    } else {
        context.attribution = attribution;
    }
    st_reject_strangers(&context, sounds);

    sound_tree_sound_t *slots = tp_arena_alloc(arena, ST_SLOT_COUNT * sizeof(sound_tree_sound_t));
    for (uint8_t family = 1; family < SOUND_FAMILY_COUNT; family++) {
        st_read_family(&context, sounds, family, slots);
    }

    sound_tree_sound_t *sounds_out = tp_arena_alloc(arena, ST_SLOT_COUNT * sizeof(sound_tree_sound_t));
    for (uint32_t family = 1; family < SOUND_FAMILY_COUNT; family++) {
        for (uint32_t kind = 0; kind < SOUND_KIND_COUNT; kind++) {
            uint32_t present = 0;
            for (uint32_t variant = 1; variant <= SOUND_BLOB_MAX_VARIANTS; variant++) {
                const sound_tree_sound_t *sound = &slots[st_slot(family, kind, variant)];
                if (sound->variant == 0u) {
                    continue;
                }
                if (variant != present + 1u) {
                    st_report(&context, "%s/%s/%s_%u: its variants have a gap before it", sounds,
                              sound_family_name(family), sound_kind_name(kind), variant);
                }
                present++;
                if (sound->borrowed_samples != nullptr) {
                    sounds_out[out->sound_count] = *sound;
                    out->sound_count++;
                    out->recorded_count += sound->recorded;
                }
            }
            if (present == 0u) {
                st_report(&context, "%s/%s: has no %s sound", sounds, sound_family_name(family), sound_kind_name(kind));
            }
        }
    }
    out->borrowed_sounds = sounds_out;
    if (context.problems != 0u) {
        return tp_fail("%s: %u problems", sounds, context.problems);
    }
    return true;
}
