/* See README.md. */
#include "ui/menu/catalog/index.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char UI_MENU_META_FILE[] = "world.meta";

enum : uint32_t {
    UI_MENU_SUFFIX_MAX = 9999u, /* tries for a free directory, before saying there is none */
};

static bool ui_menu_catalog_is_space(char c) {
    return c == ' ' || c == '\t';
}

uint64_t ui_menu_catalog_seed_from_text(const char *text, uint64_t if_blank) {
    const char *begin = text;
    while (ui_menu_catalog_is_space(*begin)) {
        begin++;
    }
    uint64_t length = strlen(begin);
    while (length > 0u && ui_menu_catalog_is_space(begin[length - 1u])) {
        length--;
    }
    if (length == 0u) {
        return if_blank;
    }
    const bool negative = begin[0] == '-';
    const uint64_t first = negative ? 1u : 0u;
    const uint64_t limit = negative ? (uint64_t)1 << 63 : UINT64_MAX;
    bool number = length > first;
    uint64_t value = 0u;
    for (uint64_t i = first; i < length && number; i++) {
        const uint64_t digit = (uint64_t)(uint8_t)begin[i] - (uint64_t)'0';
        if (digit > 9u || value > (limit - digit) / 10u) {
            number = false;
        } else {
            value = value * 10u + digit;
        }
    }
    if (number) {
        return negative ? 0u - value : value;
    }
    /* FNV-1a. A seed is a promise of the same world for ever, so this must never change. */
    uint64_t hash = 0xCBF29CE484222325ull;
    for (uint64_t i = 0u; i < length; i++) {
        hash ^= (uint8_t)begin[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

bool ui_menu_catalog_name_clean(const char *name, char out[UI_MENU_NAME_BYTES]) {
    const char *begin = name;
    while (ui_menu_catalog_is_space(*begin)) {
        begin++;
    }
    uint64_t length = strlen(begin);
    if (length >= UI_MENU_NAME_BYTES) {
        length = UI_MENU_NAME_BYTES - 1u;
    }
    while (length > 0u && ui_menu_catalog_is_space(begin[length - 1u])) {
        length--;
    }
    memmove(out, begin, length);
    out[length] = '\0';
    return length > 0u;
}

void ui_menu_catalog_directory_name(const char *name, char out[UI_MENU_NAME_BYTES]) {
    uint32_t length = 0u;
    for (const char *c = name; *c != '\0' && length + 1u < UI_MENU_NAME_BYTES; c++) {
        const bool keep =
            (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
        out[length++] = keep ? *c : '_';
    }
    out[length] = '\0';
    if (length == 0u) {
        memcpy(out, "world", sizeof("world"));
    }
}

/* The one line that starts at *cursor, without its newline, which it must have. */
static bool ui_menu_catalog_line(const char *bytes, uint64_t length, uint64_t *cursor, const char **out_line,
                                 uint64_t *out_length) {
    if (*cursor >= length) {
        return false;
    }
    const char *const line = bytes + *cursor;
    const char *const end = memchr(line, '\n', length - *cursor);
    if (end == nullptr) {
        return false;
    }
    *out_line = line;
    *out_length = (uint64_t)(end - line);
    *cursor += *out_length + 1u;
    return true;
}

static bool ui_menu_catalog_starts(const char *line, uint64_t length, const char *prefix, uint64_t prefix_length) {
    return length >= prefix_length && memcmp(line, prefix, prefix_length) == 0;
}

bool ui_menu_catalog_meta_parse(const char *bytes, uint64_t length, ui_menu_world_t *out) {
    char header[32];
    const int32_t header_length = snprintf(header, sizeof(header), "minecraft_c world %" PRIu32, UI_MENU_META_VERSION);
    uint64_t cursor = 0u;
    const char *line = nullptr;
    uint64_t line_length = 0u;
    if (!ui_menu_catalog_line(bytes, length, &cursor, &line, &line_length) || line_length != (uint64_t)header_length ||
        memcmp(line, header, line_length) != 0) {
        return false;
    }
    if (!ui_menu_catalog_line(bytes, length, &cursor, &line, &line_length) ||
        !ui_menu_catalog_starts(line, line_length, "seed ", 5u) || line_length == 5u) {
        return false;
    }
    uint64_t seed = 0u;
    for (uint64_t i = 5u; i < line_length; i++) {
        const uint64_t digit = (uint64_t)(uint8_t)line[i] - (uint64_t)'0';
        if (digit > 9u || seed > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        seed = seed * 10u + digit;
    }
    if (!ui_menu_catalog_line(bytes, length, &cursor, &line, &line_length) ||
        !ui_menu_catalog_starts(line, line_length, "name ", 5u) || line_length == 5u ||
        line_length - 5u >= UI_MENU_NAME_BYTES || cursor != length) {
        return false;
    }
    for (uint64_t i = 5u; i < line_length; i++) {
        if ((uint8_t)line[i] < 0x20u || (uint8_t)line[i] == 0x7Fu) {
            return false;
        }
    }
    out->seed = seed;
    memcpy(out->name, line + 5u, line_length - 5u);
    out->name[line_length - 5u] = '\0';
    return true;
}

/* saves/directory, or saves/directory/world.meta. */
static bool ui_menu_catalog_path(char out[FILE_PATH_BYTES], const char *saves, const char *directory, bool meta) {
    const int32_t written = meta ? snprintf(out, FILE_PATH_BYTES, "%s/%s/%s", saves, directory, UI_MENU_META_FILE)
                                 : snprintf(out, FILE_PATH_BYTES, "%s/%s", saves, directory);
    return written > 0 && written < FILE_PATH_BYTES;
}

file_status_t ui_menu_catalog_create(const char *saves, const char *name, uint64_t seed, ui_menu_world_t *out) {
    memset(out, 0, sizeof(*out));
    if (!ui_menu_catalog_name_clean(name, out->name)) {
        return FILE_ERROR_IO;
    }
    out->seed = seed;
    char wanted[UI_MENU_NAME_BYTES];
    ui_menu_catalog_directory_name(out->name, wanted);
    char path[FILE_PATH_BYTES];
    bool free_found = false;
    for (uint32_t suffix = 1u; suffix <= UI_MENU_SUFFIX_MAX && !free_found; suffix++) {
        if (suffix == 1u) {
            memcpy(out->directory, wanted, sizeof(wanted));
        } else {
            (void)snprintf(out->directory, sizeof(out->directory), "%s-%" PRIu32, wanted, suffix);
        }
        if (!ui_menu_catalog_path(path, saves, out->directory, false)) {
            return FILE_ERROR_PATH_TOO_LONG;
        }
        uint64_t bytes = 0u;
        const file_status_t status = file_path_size(path, &bytes);
        if (status == FILE_ERROR_NOT_FOUND) {
            free_found = true;
        } else if (status != FILE_OK) {
            return status;
        }
    }
    if (!free_found) {
        return FILE_ERROR_EXISTS;
    }
    const file_status_t made = file_make_dirs(path);
    if (made != FILE_OK) {
        return made;
    }
    if (!ui_menu_catalog_path(path, saves, out->directory, true)) {
        return FILE_ERROR_PATH_TOO_LONG;
    }
    char meta[UI_MENU_META_BYTES];
    const int32_t length = snprintf(meta, sizeof(meta), "minecraft_c world %" PRIu32 "\nseed %" PRIu64 "\nname %s\n",
                                    UI_MENU_META_VERSION, out->seed, out->name);
    return file_write_atomic(path, meta, (uint64_t)length);
}

static int32_t ui_menu_catalog_compare(const void *a, const void *b) {
    const ui_menu_world_t *const left = a;
    const ui_menu_world_t *const right = b;
    const int32_t by_name = strcasecmp(left->name, right->name);
    return by_name != 0 ? by_name : strcmp(left->directory, right->directory);
}

file_status_t ui_menu_catalog_read(const char *saves, ui_menu_world_t *worlds, uint32_t capacity, uint32_t *out_count,
                                   uint32_t *out_skipped) {
    *out_count = 0u;
    *out_skipped = 0u;
    DIR *const handle = opendir(saves);
    if (handle == nullptr) {
        switch (errno) {
        case ENOENT:
            return FILE_OK;
        case ENOTDIR:
            return FILE_ERROR_EXISTS;
        case EACCES:
        case EPERM:
            return FILE_ERROR_ACCESS;
        case ENAMETOOLONG:
            return FILE_ERROR_PATH_TOO_LONG;
        default:
            return FILE_ERROR_IO;
        }
    }
    uint32_t count = 0u;
    uint32_t skipped = 0u;
    char path[FILE_PATH_BYTES];
    char meta[UI_MENU_META_BYTES];
    for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        const uint64_t name_length = strlen(entry->d_name);
        if (!ui_menu_catalog_path(path, saves, entry->d_name, true)) {
            skipped++;
            continue;
        }
        uint64_t length = 0u;
        const file_status_t status = file_read_whole(path, meta, sizeof(meta), &length);
        if (status == FILE_ERROR_NOT_FOUND) {
            continue;
        }
        ui_menu_world_t world;
        memset(&world, 0, sizeof(world));
        if (status != FILE_OK || name_length >= UI_MENU_DIRECTORY_BYTES ||
            !ui_menu_catalog_meta_parse(meta, length, &world) || count == capacity) {
            skipped++;
            continue;
        }
        memcpy(world.directory, entry->d_name, name_length + 1u);
        worlds[count++] = world;
    }
    (void)closedir(handle);
    qsort(worlds, count, sizeof(ui_menu_world_t), ui_menu_catalog_compare);
    *out_count = count;
    *out_skipped = skipped;
    return FILE_OK;
}
