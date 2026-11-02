/* See README.md. */
#include "texture_pack/manifest/index.h"

#include "yaml/index.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TP_MANIFEST_MAX_BYTES (1ull << 22)
#define TP_COMMIT_HEX_DIGITS 40u

typedef struct tp_licence_row_t {
    const char *id;   /* 0 */
    const char *name; /* 8 */
    const char *url;  /* 16 */
} tp_licence_row_t;
static_assert(sizeof(tp_licence_row_t) == 24);

/* The only licences an import may be under. All allow copying, adapting and selling, so NC and ND do not belong here. */
static const tp_licence_row_t tp_licences[] = {
    {"CC0-1.0", "Creative Commons Zero 1.0 Universal", "https://creativecommons.org/publicdomain/zero/1.0/"},
    {"CC-BY-4.0", "Creative Commons Attribution 4.0 International", "https://creativecommons.org/licenses/by/4.0/"},
    {"CC-BY-SA-3.0", "Creative Commons Attribution-ShareAlike 3.0 Unported",
     "https://creativecommons.org/licenses/by-sa/3.0/"},
    {"CC-BY-SA-4.0", "Creative Commons Attribution-ShareAlike 4.0 International",
     "https://creativecommons.org/licenses/by-sa/4.0/"},
};
enum { TP_LICENCE_COUNT = sizeof(tp_licences) / sizeof(tp_licences[0]) };

static const char *const tp_groups[] = {"blocks", "items", "mobs", "ui"};
enum { TP_GROUP_COUNT = sizeof(tp_groups) / sizeof(tp_groups[0]) };

/* What every helper below needs. */
typedef struct tp_load_t {
    tp_arena_t *arena;               /* 0 */
    const char *path;                /* 8 */
    const yaml_document_t *document; /* 16 */
} tp_load_t;
static_assert(sizeof(tp_load_t) == 24);

static bool tp_load_report(const tp_load_t *load, const yaml_error_t *error) {
    return tp_fail("%s:%u:%u: %s", load->path, error->line, error->column, error->message);
}

[[gnu::format(printf, 3, 4)]]
static bool tp_load_complain(const tp_load_t *load, const yaml_node_t *node, const char *format, ...) {
    char message[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    return tp_fail("%s:%u:%u: %s", load->path, node->line, node->column, message);
}

/* Refuses any key not listed, so that a typo cannot silently mean nothing. */
static bool tp_load_known_keys(const tp_load_t *load, const yaml_node_t *map, const char *const *keys, uint32_t count) {
    if (map->kind != YAML_KIND_MAP) {
        return tp_load_complain(load, map, "expected a mapping");
    }
    for (const yaml_node_t *child = yaml_first_child(load->document, map); child != nullptr;
         child = yaml_next_sibling(load->document, child)) {
        bool known = false;
        for (uint32_t i = 0; i < count && !known; i++) {
            known =
                strlen(keys[i]) == child->key_length && memcmp(child->borrowed_key, keys[i], child->key_length) == 0;
        }
        if (!known) {
            return tp_load_complain(load, child, "unknown key '%.*s'", (int32_t)child->key_length,
                                    (const char *)child->borrowed_key);
        }
    }
    return true;
}

/* A scalar, as printable ASCII and NUL-terminated. */
static bool tp_load_text(const tp_load_t *load, const yaml_node_t *node, const char **out) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        return tp_load_report(load, &error);
    }
    const char *copy = tp_arena_text(load->arena, text, length);
    if (length == 0 || strlen(copy) != length || !tp_text_is_plain(copy)) {
        return tp_load_complain(load, node, "expected non-empty printable ASCII");
    }
    *out = copy;
    return true;
}

static bool tp_load_required(const tp_load_t *load, const yaml_node_t *map, const char *key, const char **out) {
    const yaml_node_t *node = yaml_map_get(load->document, map, key);
    if (node == nullptr) {
        return tp_load_complain(load, map, "'%s' is required here", key);
    }
    return tp_load_text(load, node, out);
}

static bool tp_load_pack_path(const tp_load_t *load, const yaml_node_t *node, const char **out) {
    if (!tp_load_text(load, node, out)) {
        return false;
    }
    if (!tp_path_is_safe(*out)) {
        return tp_load_complain(load, node, "'%s' is not a safe relative path", *out);
    }
    return true;
}

static bool tp_load_evidence(const tp_load_t *load, const yaml_node_t *pack, tp_manifest_t *out) {
    const yaml_node_t *evidence = yaml_map_get(load->document, pack, "evidence");
    if (evidence == nullptr || evidence->kind != YAML_KIND_SEQ || evidence->child_count == 0) {
        return tp_load_complain(load, pack, "'evidence' must list where the pack itself states its licence");
    }
    out->evidence_count = evidence->child_count;
    out->evidence = tp_arena_alloc(load->arena, sizeof(out->evidence[0]) * evidence->child_count);
    uint32_t next = 0;
    for (const yaml_node_t *item = yaml_first_child(load->document, evidence); item != nullptr;
         item = yaml_next_sibling(load->document, item)) {
        static const char *const keys[] = {"file", "contains"};
        if (!tp_load_known_keys(load, item, keys, 2u)) {
            return false;
        }
        const yaml_node_t *file = yaml_map_get(load->document, item, "file");
        if (file == nullptr) {
            return tp_load_complain(load, item, "'file' is required here");
        }
        if (!tp_load_pack_path(load, file, &out->evidence[next].file) ||
            !tp_load_required(load, item, "contains", &out->evidence[next].contains)) {
            return false;
        }
        next++;
    }
    return true;
}

/* The block of the manifest that names the pack. */
static bool tp_load_pack(const tp_load_t *load, const yaml_node_t *pack, tp_manifest_t *out) {
    static const char *const keys[] = {"id", "title", "url", "commit", "licence", "authors", "evidence"};
    if (!tp_load_known_keys(load, pack, keys, sizeof(keys) / sizeof(keys[0])) ||
        !tp_load_required(load, pack, "id", &out->id) || !tp_load_required(load, pack, "title", &out->title) ||
        !tp_load_required(load, pack, "url", &out->url) || !tp_load_required(load, pack, "commit", &out->commit) ||
        !tp_load_required(load, pack, "licence", &out->licence)) {
        return false;
    }
    if (!tp_name_is_safe(out->id, strlen(out->id))) {
        return tp_load_complain(load, pack, "the pack id '%s' is not a safe name", out->id);
    }
    if (strncmp(out->url, "https://", 8) != 0) {
        return tp_load_complain(load, pack, "the pack url must be https://");
    }
    bool hex = strlen(out->commit) == TP_COMMIT_HEX_DIGITS;
    for (uint32_t i = 0; hex && i < TP_COMMIT_HEX_DIGITS; i++) {
        const char c = out->commit[i];
        hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
    if (!hex) {
        return tp_load_complain(load, pack, "the commit must be a full 40-digit lowercase hash, so that it pins");
    }
    for (uint32_t i = 0; i < TP_LICENCE_COUNT; i++) {
        if (strcmp(out->licence, tp_licences[i].id) == 0) {
            out->licence_name = tp_licences[i].name;
            out->licence_url = tp_licences[i].url;
        }
    }
    if (out->licence_name == nullptr) {
        return tp_load_complain(load, pack, "licence '%s' is not on this tool's allowlist of open licences",
                                out->licence);
    }

    const yaml_node_t *authors = yaml_map_get(load->document, pack, "authors");
    if (authors == nullptr || authors->kind != YAML_KIND_SEQ || authors->child_count == 0) {
        return tp_load_complain(load, pack, "'authors' must be a non-empty list: the licence requires the credit");
    }
    out->author_count = authors->child_count;
    out->authors = tp_arena_alloc(load->arena, sizeof(out->authors[0]) * authors->child_count);
    uint32_t next_author = 0;
    for (const yaml_node_t *item = yaml_first_child(load->document, authors); item != nullptr;
         item = yaml_next_sibling(load->document, item)) {
        if (!tp_load_text(load, item, &out->authors[next_author])) {
            return false;
        }
        next_author++;
    }
    return tp_load_evidence(load, pack, out);
}

static bool tp_load_entry(const tp_load_t *load, const yaml_node_t *item, tp_entry_t *entry) {
    static const char *const keys[] = {"from", "over", "to", "frames"};
    if (!tp_load_known_keys(load, item, keys, sizeof(keys) / sizeof(keys[0]))) {
        return false;
    }
    entry->line = item->line;
    const yaml_node_t *from = yaml_map_get(load->document, item, "from");
    if (from == nullptr) {
        return tp_load_complain(load, item, "'from' is required here");
    }
    if (!tp_load_pack_path(load, from, &entry->from)) {
        return false;
    }
    const yaml_node_t *over = yaml_map_get(load->document, item, "over");
    if (over != nullptr && !tp_load_pack_path(load, over, &entry->over)) {
        return false;
    }
    if (!tp_load_required(load, item, "to", &entry->to)) {
        return false;
    }
    const char *slash = strchr(entry->to, '/');
    if (slash == nullptr) {
        return tp_load_complain(load, item, "'to' must be <group>/<name>, and '%s' is not", entry->to);
    }
    entry->group = tp_arena_text(load->arena, entry->to, (uint64_t)(slash - entry->to));
    entry->name = slash + 1;
    bool group_known = false;
    for (uint32_t i = 0; i < TP_GROUP_COUNT; i++) {
        group_known = group_known || strcmp(entry->group, tp_groups[i]) == 0;
    }
    if (!group_known) {
        return tp_load_complain(load, item, "'%s' is not a group: blocks, items, mobs or ui", entry->group);
    }
    if (entry->name[0] == '\0') {
        return tp_load_complain(load, item, "'to' has an empty name");
    }
    for (const char *c = entry->name; *c != '\0'; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) {
            return tp_load_complain(load, item, "a texture name is [a-z0-9_]+, and '%s' is not", entry->name);
        }
    }
    entry->frames = 1;
    const yaml_node_t *frames = yaml_map_get(load->document, item, "frames");
    if (frames != nullptr) {
        int64_t count = 0;
        yaml_error_t error = {0};
        if (!yaml_get_int64(frames, &count, &error)) {
            return tp_load_report(load, &error);
        }
        if (count < 1 || count > 256) {
            return tp_load_complain(load, frames, "'frames' must be from 1 to 256");
        }
        entry->frames = (uint32_t)count;
    }
    if (entry->over != nullptr && entry->frames != 1) {
        return tp_load_complain(load, item, "'over' works on one frame, and this entry has %u", entry->frames);
    }
    return true;
}

static bool tp_load_skips(const tp_load_t *load, const yaml_node_t *skips, tp_manifest_t *out) {
    if (skips->kind != YAML_KIND_SEQ) {
        return tp_load_complain(load, skips, "'skip' must be a list");
    }
    uint32_t total = 0;
    for (const yaml_node_t *group = yaml_first_child(load->document, skips); group != nullptr;
         group = yaml_next_sibling(load->document, group)) {
        static const char *const keys[] = {"why", "files"};
        if (!tp_load_known_keys(load, group, keys, 2u)) {
            return false;
        }
        const yaml_node_t *files = yaml_map_get(load->document, group, "files");
        if (files == nullptr || files->kind != YAML_KIND_SEQ) {
            return tp_load_complain(load, group, "'files' must be a list");
        }
        total += files->child_count;
    }
    out->skips = tp_arena_alloc(load->arena, sizeof(out->skips[0]) * (uint64_t)(total + 1u));
    for (const yaml_node_t *group = yaml_first_child(load->document, skips); group != nullptr;
         group = yaml_next_sibling(load->document, group)) {
        const char *why = nullptr;
        if (!tp_load_required(load, group, "why", &why)) {
            return false;
        }
        const yaml_node_t *files = yaml_map_get(load->document, group, "files");
        for (const yaml_node_t *file = yaml_first_child(load->document, files); file != nullptr;
             file = yaml_next_sibling(load->document, file)) {
            tp_skip_t *skip = &out->skips[out->skip_count];
            if (!tp_load_pack_path(load, file, &skip->file)) {
                return false;
            }
            skip->why = why;
            out->skip_count++;
        }
    }
    return true;
}

/* A file listed twice would make the coverage count a lie, and two entries writing one directory would lose one. */
static bool tp_load_no_duplicates(const tp_manifest_t *manifest) {
    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        const tp_entry_t *entry = &manifest->entries[i];
        for (uint32_t j = i + 1u; j < manifest->entry_count; j++) {
            if (strcmp(entry->to, manifest->entries[j].to) == 0) {
                return tp_fail("%s:%u: '%s' is also written by line %u", manifest->path, manifest->entries[j].line,
                               entry->to, entry->line);
            }
            if (strcmp(entry->from, manifest->entries[j].from) == 0) {
                return tp_fail("%s:%u: '%s' is also imported by line %u", manifest->path, manifest->entries[j].line,
                               entry->from, entry->line);
            }
        }
        for (uint32_t j = 0; j < manifest->skip_count; j++) {
            if (strcmp(entry->from, manifest->skips[j].file) == 0) {
                return tp_fail("%s:%u: '%s' is both imported and skipped", manifest->path, entry->line, entry->from);
            }
        }
    }
    for (uint32_t i = 0; i < manifest->skip_count; i++) {
        for (uint32_t j = i + 1u; j < manifest->skip_count; j++) {
            if (strcmp(manifest->skips[i].file, manifest->skips[j].file) == 0) {
                return tp_fail("%s: '%s' is skipped twice", manifest->path, manifest->skips[i].file);
            }
        }
    }
    return true;
}

bool tp_manifest_load(tp_arena_t *arena, const char *path, tp_manifest_t *out) {
    memset(out, 0, sizeof(*out));
    out->path = path;
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t read_status = tp_read_path(arena, path, TP_MANIFEST_MAX_BYTES, &text, &length);
    if (read_status == TP_READ_MISSING) {
        return tp_fail("%s: no such manifest", path);
    }
    if (read_status != TP_READ_OK) {
        return false;
    }
    const yaml_storage_t storage = {.nodes = tp_arena_alloc(arena, sizeof(yaml_node_t) * (length + 1u)),
                                    .strings = tp_arena_alloc(arena, length + 1u),
                                    .node_capacity = (uint32_t)(length + 1u),
                                    .string_capacity = (uint32_t)length};
    yaml_document_t document;
    yaml_error_t error = {0};
    if (!yaml_parse(&document, text, length, storage, &error)) {
        return tp_fail("%s:%u:%u: %s", path, error.line, error.column, error.message);
    }
    const tp_load_t load = {.arena = arena, .path = path, .document = &document};
    const yaml_node_t *root = yaml_root(&document);
    static const char *const keys[] = {"pack", "textures", "skip"};
    if (!tp_load_known_keys(&load, root, keys, 3u)) {
        return false;
    }
    const yaml_node_t *pack = yaml_map_get(&document, root, "pack");
    if (pack == nullptr) {
        return tp_load_complain(&load, root, "'pack' is required");
    }
    if (!tp_load_pack(&load, pack, out)) {
        return false;
    }
    const yaml_node_t *textures = yaml_map_get(&document, root, "textures");
    if (textures == nullptr || textures->kind != YAML_KIND_SEQ) {
        return tp_load_complain(&load, root, "'textures' must be a list");
    }
    out->entry_count = textures->child_count;
    out->entries = tp_arena_alloc(arena, sizeof(out->entries[0]) * (uint64_t)(textures->child_count + 1u));
    uint32_t next = 0;
    for (const yaml_node_t *item = yaml_first_child(&document, textures); item != nullptr;
         item = yaml_next_sibling(&document, item)) {
        if (!tp_load_entry(&load, item, &out->entries[next])) {
            return false;
        }
        next++;
    }
    const yaml_node_t *skips = yaml_map_get(&document, root, "skip");
    if (skips != nullptr && !tp_load_skips(&load, skips, out)) {
        return false;
    }
    return tp_load_no_duplicates(out);
}
