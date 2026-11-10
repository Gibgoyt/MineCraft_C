/*
 * See README.md. Mojang's pack is untrusted and not ours to publish: only decoded pixels are taken from it, and they are
 * written only into the copy of textures/ the caller names, which git ignores.
 */
#include "texture_pack/default_pack/index.h"

#include "texture_pack/mctex/index.h"
#include "texture_pack/output/index.h"
#include "texture_pack/pack/index.h"
#include "yaml/index.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define TP_DEFAULT_MAP_MAX_BYTES (1ull << 22)
#define TP_DEFAULT_PIN_MAX_BYTES 256u
#define TP_DEFAULT_TEXT_BYTES (1ull << 16)
#define TP_DEFAULT_MCTEX_BYTES (MCTEX_MAX_PIXEL_BYTES + sizeof(mctex_header_t))
#define TP_DEFAULT_SHA1_HEX_DIGITS 40u
#define TP_DEFAULT_MAX_TINTS 32u
#define TP_DEFAULT_MAX_STACK 32u
#define TP_DEFAULT_MAX_TEXTURES 4096u
/* Where a client jar keeps its textures, below the directory the downloader unpacked into. */
#define TP_DEFAULT_TEXTURE_ROOT "assets/minecraft/textures"
#define TP_DEFAULT_PIN_FILE "mc_c_pin.txt"

/* A named colour that grey art is multiplied by, as Minecraft's biome colours do when it draws. */
typedef struct tp_tint_t {
    const char *borrowed_name; /* 0 */
    uint8_t rgb[3];            /* 8 */
    uint8_t _pad0[5];          /* 11 */
} tp_tint_t;
static_assert(sizeof(tp_tint_t) == 16);

/* One of our textures, and what the map says becomes of it. */
typedef struct tp_default_entry_t {
    const char *borrowed_to;        /* 0: group/name, as in textures/ */
    const char *borrowed_from;      /* 8: a file in the pack, or nullptr for keep or stack */
    const char *borrowed_over;      /* 16: an opaque file in the pack to composite on, or nullptr */
    const char *borrowed_keep;      /* 24: why ours stays, or nullptr */
    const char **borrowed_stack;    /* 32: files stacked top to bottom as frames, or nullptr */
    const tp_tint_t *borrowed_tint; /* 40: or nullptr */
    uint32_t stack_count;           /* 48 */
    uint32_t frames;                /* 52: the strip, after any crop, is this many frames */
    uint32_t crop[4];               /* 56: x, y, width, height, used when has_crop */
    uint32_t line;                  /* 72: in the map */
    uint8_t has_crop;               /* 76 */
    uint8_t seen;                   /* 77: the tree has this texture */
    uint8_t _pad0[2];               /* 78 */
} tp_default_entry_t;
static_assert(sizeof(tp_default_entry_t) == 80);

typedef struct tp_default_map_t {
    const char *borrowed_path;            /* 0 */
    const char *borrowed_id;              /* 8 */
    const char *borrowed_title;           /* 16 */
    const char *borrowed_version;         /* 24 */
    const char *borrowed_client_sha1;     /* 32 */
    const char *borrowed_licence;         /* 40 */
    tp_tint_t *borrowed_tints;            /* 48 */
    tp_default_entry_t *borrowed_entries; /* 56 */
    uint32_t tint_count;                  /* 64 */
    uint32_t entry_count;                 /* 68 */
} tp_default_map_t;
static_assert(sizeof(tp_default_map_t) == 72);

typedef struct tp_default_load_t {
    tp_arena_t *arena;               /* 0 */
    const char *path;                /* 8 */
    const yaml_document_t *document; /* 16 */
} tp_default_load_t;
static_assert(sizeof(tp_default_load_t) == 24);

[[gnu::format(printf, 3, 4)]]
static bool tp_default_complain(const tp_default_load_t *load, const yaml_node_t *node, const char *format, ...) {
    char message[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    return tp_fail("%s:%u:%u: %s", load->path, node->line, node->column, message);
}

static bool tp_default_known_keys(const tp_default_load_t *load, const yaml_node_t *map, const char *const *keys,
                                  uint32_t count) {
    if (map->kind != YAML_KIND_MAP) {
        return tp_default_complain(load, map, "expected a mapping");
    }
    for (const yaml_node_t *child = yaml_first_child(load->document, map); child != nullptr;
         child = yaml_next_sibling(load->document, child)) {
        bool known = false;
        for (uint32_t i = 0; i < count && !known; i++) {
            known =
                strlen(keys[i]) == child->key_length && memcmp(child->borrowed_key, keys[i], child->key_length) == 0;
        }
        if (!known) {
            return tp_default_complain(load, child, "unknown key '%.*s'", (int32_t)child->key_length,
                                       (const char *)child->borrowed_key);
        }
    }
    return true;
}

static bool tp_default_text(const tp_default_load_t *load, const yaml_node_t *node, const char **out) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        return tp_fail("%s:%u:%u: %s", load->path, error.line, error.column, error.message);
    }
    const char *copy = tp_arena_text(load->arena, text, length);
    if (length == 0 || strlen(copy) != length || !tp_text_is_plain(copy)) {
        return tp_default_complain(load, node, "expected non-empty printable ASCII");
    }
    *out = copy;
    return true;
}

static bool tp_default_required(const tp_default_load_t *load, const yaml_node_t *map, const char *key,
                                const char **out) {
    const yaml_node_t *node = yaml_map_get(load->document, map, key);
    if (node == nullptr) {
        return tp_default_complain(load, map, "'%s' is required here", key);
    }
    return tp_default_text(load, node, out);
}

static bool tp_default_pack_path(const tp_default_load_t *load, const yaml_node_t *node, const char **out) {
    if (!tp_default_text(load, node, out)) {
        return false;
    }
    if (!tp_path_is_safe(*out)) {
        return tp_default_complain(load, node, "'%s' is not a safe relative path", *out);
    }
    return true;
}

static bool tp_default_hex_digit(char c, uint8_t *out) {
    if (c >= '0' && c <= '9') {
        *out = (uint8_t)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
        *out = (uint8_t)(c - 'a' + 10);
    } else {
        return false;
    }
    return true;
}

static bool tp_default_load_pack(const tp_default_load_t *load, const yaml_node_t *pack, tp_default_map_t *out) {
    static const char *const keys[] = {"id", "title", "version", "client_sha1", "licence"};
    if (!tp_default_known_keys(load, pack, keys, sizeof(keys) / sizeof(keys[0])) ||
        !tp_default_required(load, pack, "id", &out->borrowed_id) ||
        !tp_default_required(load, pack, "title", &out->borrowed_title) ||
        !tp_default_required(load, pack, "version", &out->borrowed_version) ||
        !tp_default_required(load, pack, "client_sha1", &out->borrowed_client_sha1) ||
        !tp_default_required(load, pack, "licence", &out->borrowed_licence)) {
        return false;
    }
    if (!tp_name_is_safe(out->borrowed_id, strlen(out->borrowed_id)) ||
        !tp_name_is_safe(out->borrowed_version, strlen(out->borrowed_version))) {
        return tp_default_complain(load, pack, "the id and the version must be safe names");
    }
    bool hex = strlen(out->borrowed_client_sha1) == TP_DEFAULT_SHA1_HEX_DIGITS;
    for (uint32_t i = 0; hex && i < TP_DEFAULT_SHA1_HEX_DIGITS; i++) {
        uint8_t digit = 0;
        hex = tp_default_hex_digit(out->borrowed_client_sha1[i], &digit);
    }
    if (!hex) {
        return tp_default_complain(load, pack, "client_sha1 must be 40 lowercase hex digits, so that it pins");
    }
    return true;
}

static bool tp_default_load_tints(const tp_default_load_t *load, const yaml_node_t *tints, tp_default_map_t *out) {
    if (tints->kind != YAML_KIND_MAP || tints->child_count > TP_DEFAULT_MAX_TINTS) {
        return tp_default_complain(load, tints, "'tints' must be a mapping of at most %u names to RRGGBB",
                                   TP_DEFAULT_MAX_TINTS);
    }
    out->borrowed_tints = tp_arena_alloc(load->arena, sizeof(out->borrowed_tints[0]) * (tints->child_count + 1u));
    for (const yaml_node_t *child = yaml_first_child(load->document, tints); child != nullptr;
         child = yaml_next_sibling(load->document, child)) {
        tp_tint_t *tint = &out->borrowed_tints[out->tint_count];
        tint->borrowed_name = tp_arena_text(load->arena, child->borrowed_key, child->key_length);
        const char *value = nullptr;
        if (!tp_default_text(load, child, &value)) {
            return false;
        }
        bool good = strlen(value) == 6u;
        for (uint32_t i = 0; good && i < 3u; i++) {
            uint8_t high = 0;
            uint8_t low = 0;
            good = tp_default_hex_digit(value[2u * i], &high) && tp_default_hex_digit(value[2u * i + 1u], &low);
            tint->rgb[i] = (uint8_t)(high * 16u + low);
        }
        if (!good) {
            return tp_default_complain(load, child, "a tint is six lowercase hex digits, RRGGBB");
        }
        out->tint_count++;
    }
    return true;
}

static bool tp_default_uint(const tp_default_load_t *load, const yaml_node_t *node, uint32_t low, uint32_t high,
                            uint32_t *out) {
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        return tp_fail("%s:%u:%u: %s", load->path, error.line, error.column, error.message);
    }
    if (value < (int64_t)low || value > (int64_t)high) {
        return tp_default_complain(load, node, "expected a whole number from %u to %u", low, high);
    }
    *out = (uint32_t)value;
    return true;
}

/* The optional parts of an entry that shape the image: crop, frames, tint and over. */
static bool tp_default_load_shape(const tp_default_load_t *load, const yaml_node_t *item, const tp_default_map_t *map,
                                  tp_default_entry_t *entry) {
    entry->frames = 1u;
    const yaml_node_t *frames = yaml_map_get(load->document, item, "frames");
    if (frames != nullptr && !tp_default_uint(load, frames, 1u, MCTEX_MAX_FRAMES, &entry->frames)) {
        return false;
    }
    const yaml_node_t *crop = yaml_map_get(load->document, item, "crop");
    if (crop != nullptr) {
        if (crop->kind != YAML_KIND_SEQ || crop->child_count != 4u) {
            return tp_default_complain(load, crop, "'crop' is [x, y, width, height]");
        }
        uint32_t next = 0;
        for (const yaml_node_t *part = yaml_first_child(load->document, crop); part != nullptr;
             part = yaml_next_sibling(load->document, part)) {
            if (!tp_default_uint(load, part, next < 2u ? 0u : 1u, MCTEX_MAX_DIMENSION * MCTEX_MAX_FRAMES,
                                 &entry->crop[next])) {
                return false;
            }
            next++;
        }
        entry->has_crop = 1u;
    }
    const yaml_node_t *tint = yaml_map_get(load->document, item, "tint");
    if (tint != nullptr) {
        const char *name = nullptr;
        if (!tp_default_text(load, tint, &name)) {
            return false;
        }
        for (uint32_t i = 0; i < map->tint_count && entry->borrowed_tint == nullptr; i++) {
            if (strcmp(map->borrowed_tints[i].borrowed_name, name) == 0) {
                entry->borrowed_tint = &map->borrowed_tints[i];
            }
        }
        if (entry->borrowed_tint == nullptr) {
            return tp_default_complain(load, tint, "'%s' is not one of the tints named under 'tints'", name);
        }
    }
    const yaml_node_t *over = yaml_map_get(load->document, item, "over");
    if (over != nullptr && !tp_default_pack_path(load, over, &entry->borrowed_over)) {
        return false;
    }
    if (entry->borrowed_over != nullptr && entry->frames != 1u) {
        return tp_default_complain(load, item, "'over' works on one frame, and this entry has %u", entry->frames);
    }
    return true;
}

static bool tp_default_load_entry(const tp_default_load_t *load, const yaml_node_t *item, const tp_default_map_t *map,
                                  tp_default_entry_t *entry) {
    static const char *const keys[] = {"to", "from", "stack", "keep", "over", "crop", "frames", "tint"};
    if (!tp_default_known_keys(load, item, keys, sizeof(keys) / sizeof(keys[0])) ||
        !tp_default_required(load, item, "to", &entry->borrowed_to)) {
        return false;
    }
    entry->line = item->line;
    if (!tp_path_is_safe(entry->borrowed_to) || strchr(entry->borrowed_to, '/') == nullptr) {
        return tp_default_complain(load, item, "'to' must be <group>/<name>");
    }
    const yaml_node_t *from = yaml_map_get(load->document, item, "from");
    const yaml_node_t *stack = yaml_map_get(load->document, item, "stack");
    const yaml_node_t *keep = yaml_map_get(load->document, item, "keep");
    if ((from != nullptr ? 1 : 0) + (stack != nullptr ? 1 : 0) + (keep != nullptr ? 1 : 0) != 1) {
        return tp_default_complain(load, item, "an entry has exactly one of 'from', 'stack' and 'keep'");
    }
    if (keep != nullptr) {
        if (item->child_count != 2u) {
            return tp_default_complain(load, item, "a 'keep' entry has only 'to' and 'keep'");
        }
        return tp_default_text(load, keep, &entry->borrowed_keep);
    }
    if (from != nullptr && !tp_default_pack_path(load, from, &entry->borrowed_from)) {
        return false;
    }
    if (stack != nullptr) {
        if (stack->kind != YAML_KIND_SEQ || stack->child_count < 2u || stack->child_count > TP_DEFAULT_MAX_STACK) {
            return tp_default_complain(load, stack, "'stack' lists 2 to %u files, one frame each",
                                       TP_DEFAULT_MAX_STACK);
        }
        entry->borrowed_stack = tp_arena_alloc(load->arena, sizeof(entry->borrowed_stack[0]) * stack->child_count);
        for (const yaml_node_t *file = yaml_first_child(load->document, stack); file != nullptr;
             file = yaml_next_sibling(load->document, file)) {
            if (!tp_default_pack_path(load, file, &entry->borrowed_stack[entry->stack_count])) {
                return false;
            }
            entry->stack_count++;
        }
    }
    if (!tp_default_load_shape(load, item, map, entry)) {
        return false;
    }
    if (stack != nullptr && (entry->frames != 1u || entry->has_crop != 0u || entry->borrowed_over != nullptr)) {
        return tp_default_complain(load, item, "a 'stack' takes no frames, crop or over: each file is one frame");
    }
    return true;
}

static bool tp_default_map_load(tp_arena_t *arena, const char *path, tp_default_map_t *out) {
    memset(out, 0, sizeof(*out));
    out->borrowed_path = path;
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(arena, path, TP_DEFAULT_MAP_MAX_BYTES, &text, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: no such map", path);
    }
    if (status != TP_READ_OK) {
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
    const tp_default_load_t load = {.arena = arena, .path = path, .document = &document};
    const yaml_node_t *root = yaml_root(&document);
    static const char *const keys[] = {"pack", "tints", "textures"};
    if (!tp_default_known_keys(&load, root, keys, 3u)) {
        return false;
    }
    const yaml_node_t *pack = yaml_map_get(&document, root, "pack");
    if (pack == nullptr) {
        return tp_default_complain(&load, root, "'pack' is required");
    }
    if (!tp_default_load_pack(&load, pack, out)) {
        return false;
    }
    const yaml_node_t *tints = yaml_map_get(&document, root, "tints");
    if (tints != nullptr && !tp_default_load_tints(&load, tints, out)) {
        return false;
    }
    const yaml_node_t *textures = yaml_map_get(&document, root, "textures");
    if (textures == nullptr || textures->kind != YAML_KIND_SEQ || textures->child_count > TP_DEFAULT_MAX_TEXTURES) {
        return tp_default_complain(&load, root, "'textures' must be a list of at most %u", TP_DEFAULT_MAX_TEXTURES);
    }
    out->borrowed_entries = tp_arena_alloc(arena, sizeof(out->borrowed_entries[0]) * (textures->child_count + 1u));
    for (const yaml_node_t *item = yaml_first_child(&document, textures); item != nullptr;
         item = yaml_next_sibling(&document, item)) {
        tp_default_entry_t *entry = &out->borrowed_entries[out->entry_count];
        if (!tp_default_load_entry(&load, item, out, entry)) {
            return false;
        }
        for (uint32_t i = 0; i < out->entry_count; i++) {
            if (strcmp(out->borrowed_entries[i].borrowed_to, entry->borrowed_to) == 0) {
                return tp_fail("%s:%u: '%s' is also named by line %u", path, entry->line, entry->borrowed_to,
                               out->borrowed_entries[i].line);
            }
        }
        out->entry_count++;
    }
    return true;
}

/* The downloader writes "<version> <sha1>\n" beside what it unpacked, only after the jar matched the pin. */
static bool tp_default_check_pin(tp_arena_t *arena, const char *pack, const tp_default_map_t *map) {
    uint8_t *bytes = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status =
        tp_read_contained(arena, pack, TP_DEFAULT_PIN_FILE, TP_DEFAULT_PIN_MAX_BYTES, &bytes, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s has no %s: unpack it with tools/fetch_minecraft_default.sh", pack, TP_DEFAULT_PIN_FILE);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    const char *expected = tp_arena_format(arena, "%s %s\n", map->borrowed_version, map->borrowed_client_sha1);
    if (length != strlen(expected) || memcmp(bytes, expected, length) != 0) {
        return tp_fail("%s/%s does not name version %s with client SHA-1 %s, which %s pins", pack, TP_DEFAULT_PIN_FILE,
                       map->borrowed_version, map->borrowed_client_sha1, map->borrowed_path);
    }
    return true;
}

/* Every texture directory in the tree must be named by the map once, and every name in the map must be in the tree. */
static bool tp_default_check_tree(tp_arena_t *arena, const char *textures, tp_default_map_t *map) {
    bool good = true;
    for (uint32_t g = 0; g < tp_output_group_count(); g++) {
        const char *group = tp_output_group_at(g);
        const char *directory = tp_arena_format(arena, "%s/%s", textures, group);
        DIR *handle = opendir(directory);
        if (handle == nullptr) {
            return tp_fail("%s: cannot open it", directory);
        }
        for (struct dirent *item = readdir(handle); item != nullptr; item = readdir(handle)) {
            const char *yaml_path = tp_arena_format(arena, "%s/%s/index.yaml", directory, item->d_name);
            struct stat info;
            if (item->d_name[0] == '.' || stat(yaml_path, &info) != 0) {
                continue;
            }
            const char *to = tp_arena_format(arena, "%s/%s", group, item->d_name);
            bool named = false;
            for (uint32_t i = 0; i < map->entry_count && !named; i++) {
                if (strcmp(map->borrowed_entries[i].borrowed_to, to) == 0) {
                    map->borrowed_entries[i].seen = 1u;
                    named = true;
                }
            }
            if (!named) {
                good = tp_fail("%s: the map does not say what becomes of %s; give it a 'from' or a 'keep'",
                               map->borrowed_path, to);
            }
        }
        closedir(handle);
    }
    for (uint32_t i = 0; i < map->entry_count; i++) {
        if (map->borrowed_entries[i].seen == 0u) {
            good = tp_fail("%s:%u: %s is not a texture in %s", map->borrowed_path, map->borrowed_entries[i].line,
                           map->borrowed_entries[i].borrowed_to, textures);
        }
    }
    return good;
}

/* The size of ours, from its index.mctex, which the new one must match. */
static bool tp_default_ours(tp_arena_t *arena, const char *textures, const char *to, mctex_view_t *out) {
    const uint64_t mark = arena->used;
    const char *path = tp_arena_format(arena, "%s/%s/index.mctex", textures, to);
    uint8_t *file = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(arena, path, TP_DEFAULT_MCTEX_BYTES, &file, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: missing", path);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    const mctex_status_t parsed = mctex_parse(file, length, out);
    arena->used = mark;
    out->borrowed_pixels = nullptr;
    if (parsed != MCTEX_OK) {
        return tp_fail("%s: %s", path, mctex_status_text(parsed));
    }
    return true;
}

/* A rectangle of a decoded image, which must lie inside it. */
static bool tp_default_crop(tp_arena_t *arena, const char *file, const uint32_t crop[4], tp_image_t *image) {
    if ((uint64_t)crop[0] + crop[2] > image->width || (uint64_t)crop[1] + crop[3] > image->height) {
        return tp_fail("%s: is %ux%u, and the crop [%u, %u, %u, %u] goes outside it", file, image->width, image->height,
                       crop[0], crop[1], crop[2], crop[3]);
    }
    const uint64_t bytes = (uint64_t)crop[2] * crop[3] * 4u;
    uint8_t *pixels = tp_arena_alloc(arena, bytes);
    for (uint32_t row = 0; row < crop[3]; row++) {
        memcpy(pixels + (uint64_t)row * crop[2] * 4u,
               image->borrowed_pixels + (((uint64_t)crop[1] + row) * image->width + crop[0]) * 4u,
               (uint64_t)crop[2] * 4u);
    }
    image->borrowed_pixels = pixels;
    image->pixel_bytes = bytes;
    image->width = crop[2];
    image->height = crop[3];
    return true;
}

/* Grey times the colour, rounded to nearest, in integers so that it is the same on any machine. Alpha is kept. */
static void tp_default_tint(const tp_tint_t *tint, tp_image_t *image) {
    for (uint64_t i = 0; i < image->pixel_bytes; i += 4u) {
        for (uint32_t c = 0; c < 3u; c++) {
            image->borrowed_pixels[i + c] =
                (uint8_t)(((uint32_t)image->borrowed_pixels[i + c] * tint->rgb[c] + 127u) / 255u);
        }
    }
}

static bool tp_default_stack(tp_arena_t *arena, const char *root, const tp_default_entry_t *entry, tp_image_t *out) {
    tp_image_t first;
    if (!tp_pack_decode_image(arena, root, entry->borrowed_stack[0], &first)) {
        return false;
    }
    uint64_t total = 0;
    if (!mctex_pixel_bytes(first.width, first.height, entry->stack_count, &total)) {
        return tp_fail("%s: %u frames of %ux%u are over the .mctex limits", entry->borrowed_to, entry->stack_count,
                       first.width, first.height);
    }
    uint8_t *pixels = tp_arena_alloc(arena, total);
    memcpy(pixels, first.borrowed_pixels, first.pixel_bytes);
    for (uint32_t i = 1; i < entry->stack_count; i++) {
        tp_image_t frame;
        if (!tp_pack_decode_image(arena, root, entry->borrowed_stack[i], &frame)) {
            return false;
        }
        if (frame.width != first.width || frame.height != first.height) {
            return tp_fail("%s is %ux%u, and %s before it is %ux%u", entry->borrowed_stack[i], frame.width,
                           frame.height, entry->borrowed_stack[0], first.width, first.height);
        }
        memcpy(pixels + first.pixel_bytes * i, frame.borrowed_pixels, frame.pixel_bytes);
    }
    *out = (tp_image_t){.borrowed_pixels = pixels,
                        .pixel_bytes = total,
                        .width = first.width,
                        .height = first.height,
                        .frames = entry->stack_count,
                        ._pad0 = {0}};
    return true;
}

/* Reads, crops, tints, composites and splits one entry, then holds it to the size of ours. */
static bool tp_default_load_image(tp_arena_t *arena, const char *root, const char *textures, const char *map_path,
                                  const tp_default_entry_t *entry, tp_image_t *out) {
    if (entry->borrowed_stack != nullptr) {
        if (!tp_default_stack(arena, root, entry, out)) {
            return false;
        }
    } else {
        if (!tp_pack_decode_image(arena, root, entry->borrowed_from, out) ||
            (entry->has_crop != 0u && !tp_default_crop(arena, entry->borrowed_from, entry->crop, out))) {
            return false;
        }
        if (entry->borrowed_tint != nullptr) {
            tp_default_tint(entry->borrowed_tint, out);
        }
        if (entry->borrowed_over != nullptr) {
            tp_image_t base;
            if (!tp_pack_decode_image(arena, root, entry->borrowed_over, &base) ||
                (entry->has_crop != 0u && !tp_default_crop(arena, entry->borrowed_over, entry->crop, &base))) {
                return false;
            }
            if (base.width != out->width || base.height != out->height) {
                return tp_fail("%s is %ux%u, and %s under it is %ux%u", entry->borrowed_from, out->width, out->height,
                               entry->borrowed_over, base.width, base.height);
            }
            for (uint64_t i = 3u; i < base.pixel_bytes; i += 4u) {
                if (base.borrowed_pixels[i] != 255u) {
                    return tp_fail("%s is not fully opaque, so it cannot be the base of %s", entry->borrowed_over,
                                   entry->borrowed_from);
                }
            }
            tp_pack_composite_over_opaque(out->borrowed_pixels, base.borrowed_pixels, out->pixel_bytes);
        }
        if (out->height % entry->frames != 0u) {
            return tp_fail("%s: a height of %u does not divide into %u frames", entry->borrowed_to, out->height,
                           entry->frames);
        }
        out->height /= entry->frames;
        out->frames = entry->frames;
    }
    mctex_view_t ours;
    if (!tp_default_ours(arena, textures, entry->borrowed_to, &ours)) {
        return false;
    }
    /* The game samples any size, but a screen lays out its pictures by ours, and a skin by its layout. */
    if (out->width != ours.width || out->height != ours.height) {
        return tp_fail("%s:%u: %s comes out %ux%u, and ours is %ux%u; crop it, or keep ours", map_path, entry->line,
                       entry->borrowed_to, out->width, out->height, ours.width, ours.height);
    }
    /* Only the crack's frames are drawn one by one. An animation may become a still, which is its frame 0. */
    if (out->frames != ours.frame_count && out->frames != 1u) {
        return tp_fail("%s:%u: %s comes out as %u frames, and ours has %u", map_path, entry->line, entry->borrowed_to,
                       out->frames, ours.frame_count);
    }
    if (out->frames != ours.frame_count && strcmp(entry->borrowed_to, "blocks/crack") == 0) {
        return tp_fail("the crack needs all %u of its frames", ours.frame_count);
    }
    return true;
}

static const char *tp_default_file_text(tp_arena_t *arena, const tp_default_entry_t *entry) {
    if (entry->borrowed_from != nullptr) {
        return entry->borrowed_from;
    }
    tp_builder_t files = tp_builder_begin(arena, TP_DEFAULT_TEXT_BYTES);
    for (uint32_t i = 0; i < entry->stack_count; i++) {
        tp_builder_add(&files, "%s%s", i == 0 ? "" : ", ", entry->borrowed_stack[i]);
    }
    return files.borrowed_text;
}

static bool tp_default_write(tp_arena_t *arena, const char *textures, const tp_default_map_t *map,
                             const tp_default_entry_t *entry, const tp_image_t *image, uint32_t *out_written) {
    const char *directory = tp_arena_format(arena, "%s/%s", textures, entry->borrowed_to);
    const uint64_t file_bytes = sizeof(mctex_header_t) + image->pixel_bytes;
    uint8_t *file = tp_arena_alloc(arena, file_bytes);
    const mctex_status_t encoded = mctex_encode(file, file_bytes, image->width, image->height, image->frames,
                                                image->borrowed_pixels, image->pixel_bytes);
    if (encoded != MCTEX_OK) {
        return tp_fail("%s: %s", entry->borrowed_to, mctex_status_text(encoded));
    }
    const char *slash = strchr(entry->borrowed_to, '/');
    const char *files = tp_default_file_text(arena, entry);
    tp_builder_t yaml = tp_builder_begin(arena, TP_DEFAULT_TEXT_BYTES);
    tp_builder_add(&yaml, "# Written by compile/src/texture_pack import-default from %s. Never commit it.\n",
                   map->borrowed_path);
    tp_builder_add(&yaml, "name: %s\ngroup: %.*s\n", slash + 1, (int32_t)(slash - entry->borrowed_to),
                   entry->borrowed_to);
    tp_builder_add(&yaml, "size: { width: %u, height: %u, frames: %u }\n", image->width, image->height, image->frames);
    tp_builder_add(&yaml, "source:\n  kind: pack\n  pack: %s\n  version: \"%s\"\n  licence: \"%s\"\n", map->borrowed_id,
                   map->borrowed_version, map->borrowed_licence);
    tp_builder_add(&yaml, "  file: \"%s/%s\"\n", TP_DEFAULT_TEXTURE_ROOT, files);

    tp_builder_t readme = tp_builder_begin(arena, TP_DEFAULT_TEXT_BYTES);
    tp_builder_add(&readme, "# %s\n\n", entry->borrowed_to);
    tp_builder_add(&readme, "From %s %s, `%s/%s`. %s.\n\n", map->borrowed_title, map->borrowed_version,
                   TP_DEFAULT_TEXTURE_ROOT, files, map->borrowed_licence);
    tp_builder_add(&readme, "Changed from the original: converted from PNG to `index.mctex`");
    if (entry->has_crop != 0u) {
        tp_builder_add(&readme, ", cropped to [%u, %u, %u, %u]", entry->crop[0], entry->crop[1], entry->crop[2],
                       entry->crop[3]);
    }
    if (entry->borrowed_tint != nullptr) {
        tp_builder_add(&readme, ", tinted %s (%02x%02x%02x) as Minecraft's biome colour would",
                       entry->borrowed_tint->borrowed_name, entry->borrowed_tint->rgb[0], entry->borrowed_tint->rgb[1],
                       entry->borrowed_tint->rgb[2]);
    }
    if (entry->borrowed_over != nullptr) {
        tp_builder_add(&readme, ", composited over `%s`", entry->borrowed_over);
    }
    tp_builder_add(&readme, ", as %u frame%s.\n\nThis copy is in a git-ignored tree. Never commit it.\n", image->frames,
                   image->frames == 1u ? "" : "s");

    const char *paths[] = {tp_arena_format(arena, "%s/index.mctex", directory),
                           tp_arena_format(arena, "%s/index.yaml", directory),
                           tp_arena_format(arena, "%s/README.md", directory)};
    const void *contents[] = {file, yaml.borrowed_text, readme.borrowed_text};
    const uint64_t lengths[] = {file_bytes, yaml.used, readme.used};
    for (uint32_t i = 0; i < 3u; i++) {
        bool wrote = false;
        if (!tp_write_if_changed(arena, paths[i], contents[i], lengths[i], &wrote)) {
            return false;
        }
        *out_written += wrote ? 1u : 0u;
    }
    return true;
}

bool tp_default_import(tp_arena_t *arena, const char *pack, const char *map_path, const char *textures) {
    tp_default_map_t map;
    if (!tp_default_map_load(arena, map_path, &map) || !tp_default_check_pin(arena, pack, &map) ||
        !tp_default_check_tree(arena, textures, &map)) {
        return tp_fail("refused, and nothing was written");
    }
    const char *root = tp_arena_format(arena, "%s/%s", pack, TP_DEFAULT_TEXTURE_ROOT);
    /* Pass 0 decodes and checks everything; pass 1 writes. */
    uint32_t imported = 0;
    uint32_t kept = 0;
    uint32_t written = 0;
    for (uint32_t pass = 0; pass < 2u; pass++) {
        bool good = true;
        for (uint32_t i = 0; i < map.entry_count; i++) {
            const tp_default_entry_t *entry = &map.borrowed_entries[i];
            if (entry->borrowed_keep != nullptr) {
                kept += pass == 0u ? 1u : 0u;
                continue;
            }
            const uint64_t mark = arena->used;
            tp_image_t image;
            if (!tp_default_load_image(arena, root, textures, map.borrowed_path, entry, &image)) {
                good = false;
            } else if (pass == 1u) {
                if (!tp_default_write(arena, textures, &map, entry, &image, &written)) {
                    return false;
                }
                imported++;
            }
            arena->used = mark;
        }
        if (!good) {
            return tp_fail("refused, and nothing was written");
        }
    }
    printf("texture_pack: %s %s into %s: %u textures imported, %u of ours kept; %u files changed\n", map.borrowed_id,
           map.borrowed_version, textures, imported, kept, written);
    return true;
}
