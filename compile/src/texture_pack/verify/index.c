/* See README.md. */
#include "texture_pack/verify/index.h"

#include "texture_pack/mctex/index.h"
#include "texture_pack/output/index.h"
#include "yaml/index.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#define TP_VERIFY_YAML_BYTES (1ull << 20)
#define TP_VERIFY_README_BYTES (1ull << 20)
#define TP_VERIFY_ATTRIBUTION_BYTES (1ull << 23)
#define TP_VERIFY_MCTEX_BYTES (MCTEX_MAX_PIXEL_BYTES + sizeof(mctex_header_t))

static bool tp_verify_is_directory(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool tp_verify_text_is(const yaml_document_t *document, const yaml_node_t *root, const char *path,
                              const char *expected) {
    const yaml_node_t *node = yaml_path_get(document, root, path);
    return node != nullptr && yaml_text_equals(node, expected);
}

static bool tp_verify_number_is(const yaml_document_t *document, const yaml_node_t *root, const char *path,
                                uint32_t expected) {
    int64_t value = 0;
    yaml_error_t error = {0};
    const yaml_node_t *node = yaml_path_get(document, root, path);
    return node != nullptr && yaml_get_int64(node, &value, &error) && value == (int64_t)expected;
}

/* One imported directory: its yaml says what the manifest says, its pixels are whole and the size the yaml says. */
static bool tp_verify_imported(tp_arena_t *arena, const char *directory, const tp_manifest_t *manifest,
                               const tp_entry_t *entry, const yaml_document_t *document, const yaml_node_t *root) {
    bool good = true;
    if (!tp_verify_text_is(document, root, "name", entry->name) ||
        !tp_verify_text_is(document, root, "group", entry->group) ||
        !tp_verify_text_is(document, root, "source.pack", manifest->id) ||
        !tp_verify_text_is(document, root, "source.commit", manifest->commit) ||
        !tp_verify_text_is(document, root, "source.licence", manifest->licence) ||
        !tp_verify_text_is(document, root, "source.file", entry->from)) {
        good = tp_fail("%s/index.yaml: does not say what the manifest says about it", directory);
    }
    const yaml_node_t *over = yaml_path_get(document, root, "source.over");
    const bool over_matches =
        entry->over != nullptr ? (over != nullptr && yaml_text_equals(over, entry->over)) : over == nullptr;
    if (!over_matches) {
        good = tp_fail("%s/index.yaml: source.over does not match the manifest", directory);
    }

    uint8_t *file = nullptr;
    uint64_t length = 0;
    const char *path = tp_arena_format(arena, "%s/index.mctex", directory);
    const tp_read_status_t status = tp_read_path(arena, path, TP_VERIFY_MCTEX_BYTES, &file, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: missing", path);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    mctex_view_t view;
    const mctex_status_t parsed = mctex_parse(file, length, &view);
    if (parsed != MCTEX_OK) {
        return tp_fail("%s: %s", path, mctex_status_text(parsed));
    }
    if (view.frame_count != entry->frames) {
        good = tp_fail("%s: holds %u frames, and the manifest says %u", path, view.frame_count, entry->frames);
    }
    if (!tp_verify_number_is(document, root, "size.width", view.width) ||
        !tp_verify_number_is(document, root, "size.height", view.height) ||
        !tp_verify_number_is(document, root, "size.frames", view.frame_count)) {
        good = tp_fail("%s/index.yaml: its size is not what index.mctex holds (%ux%u, %u frames)", directory,
                       view.width, view.height, view.frame_count);
    }
    return good;
}

/* One directory under a group. Any directory needs its two files; one from the pack must also match the manifest. */
static bool tp_verify_directory(tp_arena_t *arena, const char *textures, const char *group, const char *name,
                                const tp_manifest_t *manifest, uint8_t *seen, tp_verify_counts_t *out) {
    const uint64_t mark = arena->used;
    const char *directory = tp_arena_format(arena, "%s/%s/%s", textures, group, name);
    bool good = true;
    uint8_t *readme = nullptr;
    uint64_t readme_length = 0;
    const tp_read_status_t readme_status = tp_read_path(arena, tp_arena_format(arena, "%s/README.md", directory),
                                                        TP_VERIFY_README_BYTES, &readme, &readme_length);
    if (readme_status == TP_READ_MISSING) {
        good = tp_fail("%s: has no README.md", directory);
    } else if (readme_status != TP_READ_OK) {
        good = false;
    }

    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status =
        tp_read_path(arena, tp_arena_format(arena, "%s/index.yaml", directory), TP_VERIFY_YAML_BYTES, &text, &length);
    if (status == TP_READ_MISSING) {
        arena->used = mark;
        return tp_fail("%s: has no index.yaml", directory);
    }
    if (status != TP_READ_OK) {
        arena->used = mark;
        return false;
    }
    const yaml_storage_t storage = {.nodes = tp_arena_alloc(arena, sizeof(yaml_node_t) * (length + 1u)),
                                    .strings = tp_arena_alloc(arena, length + 1u),
                                    .node_capacity = (uint32_t)(length + 1u),
                                    .string_capacity = (uint32_t)length};
    yaml_document_t document;
    yaml_error_t error = {0};
    if (!yaml_parse(&document, text, length, storage, &error)) {
        arena->used = mark;
        return tp_fail("%s/index.yaml:%u:%u: %s", directory, error.line, error.column, error.message);
    }
    const yaml_node_t *root = yaml_root(&document);
    if (tp_verify_text_is(&document, root, "source.kind", "pack")) {
        const tp_entry_t *entry = nullptr;
        uint32_t position = 0;
        for (uint32_t i = 0; i < manifest->entry_count && entry == nullptr; i++) {
            if (strcmp(manifest->entries[i].group, group) == 0 && strcmp(manifest->entries[i].name, name) == 0) {
                entry = &manifest->entries[i];
                position = i;
            }
        }
        if (entry == nullptr) {
            good = tp_fail("%s: says it came from the pack, and the manifest has no such texture", directory);
        } else {
            seen[position] = 1;
            good = tp_verify_imported(arena, directory, manifest, entry, &document, root) && good;
            if (readme != nullptr && !tp_bytes_contain(readme, readme_length, manifest->licence)) {
                good = tp_fail("%s/README.md: does not state the licence", directory);
            }
        }
        out->imported++;
    } else {
        out->other++;
    }
    arena->used = mark;
    return good;
}

static bool tp_verify_attribution(tp_arena_t *arena, const char *textures, const tp_manifest_t *manifest) {
    const uint64_t mark = arena->used;
    const char *path = tp_arena_format(arena, "%s/ATTRIBUTION.md", textures);
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(arena, path, TP_VERIFY_ATTRIBUTION_BYTES, &text, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: missing, and the licence requires it", path);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    bool good = true;
    const char *const required[] = {manifest->licence, manifest->licence_url, manifest->commit, manifest->url,
                                    manifest->title};
    for (uint32_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (!tp_bytes_contain(text, length, required[i])) {
            good = tp_fail("%s: does not state %s", path, required[i]);
        }
    }
    for (uint32_t i = 0; i < manifest->author_count; i++) {
        if (!tp_bytes_contain(text, length, manifest->authors[i])) {
            good = tp_fail("%s: does not credit %s", path, manifest->authors[i]);
        }
    }
    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        const char *row = tp_arena_format(arena, "| `%s` | `%s` |", manifest->entries[i].to, manifest->entries[i].from);
        if (!tp_bytes_contain(text, length, row)) {
            good = tp_fail("%s: has no row for %s", path, manifest->entries[i].to);
        }
    }
    arena->used = mark;
    return good;
}

/* A directory at the top is either a group, or a mistake that the game would never read. */
static bool tp_verify_top_level(tp_arena_t *arena, const char *textures) {
    DIR *top = opendir(textures);
    if (top == nullptr) {
        return tp_fail("%s: cannot be listed", textures);
    }
    bool good = true;
    struct dirent *item = readdir(top);
    while (item != nullptr) {
        bool is_group = item->d_name[0] == '.';
        for (uint32_t g = 0; g < tp_output_group_count(); g++) {
            is_group = is_group || strcmp(item->d_name, tp_output_group_at(g)) == 0;
        }
        const uint64_t mark = arena->used;
        if (!is_group && tp_verify_is_directory(tp_arena_format(arena, "%s/%s", textures, item->d_name))) {
            good = tp_fail("%s/%s: not one of the groups blocks, items, mobs, ui", textures, item->d_name);
        }
        arena->used = mark;
        item = readdir(top);
    }
    closedir(top);
    return good;
}

static bool tp_verify_group(tp_arena_t *arena, const char *textures, const char *group, const tp_manifest_t *manifest,
                            uint8_t *seen, tp_verify_counts_t *out) {
    const uint64_t mark = arena->used;
    const char *group_path = tp_arena_format(arena, "%s/%s", textures, group);
    bool good = true;
    struct stat info;
    if (stat(tp_arena_format(arena, "%s/README.md", group_path), &info) != 0) {
        good = tp_fail("%s: has no README.md", group_path);
    }
    DIR *listing = opendir(group_path);
    if (listing == nullptr) {
        arena->used = mark;
        return tp_fail("%s: cannot be listed", group_path);
    }
    struct dirent *item = readdir(listing);
    while (item != nullptr) {
        const uint64_t item_mark = arena->used;
        if (item->d_name[0] != '.' &&
            tp_verify_is_directory(tp_arena_format(arena, "%s/%s", group_path, item->d_name))) {
            good = tp_verify_directory(arena, textures, group, item->d_name, manifest, seen, out) && good;
        }
        arena->used = item_mark;
        item = readdir(listing);
    }
    closedir(listing);
    arena->used = mark;
    return good;
}

bool tp_verify_tree(tp_arena_t *arena, const char *textures, const tp_manifest_t *manifest, tp_verify_counts_t *out) {
    const uint64_t mark = arena->used;
    memset(out, 0, sizeof(*out));
    bool good = tp_verify_attribution(arena, textures, manifest);
    good = tp_verify_top_level(arena, textures) && good;
    uint8_t *seen = tp_arena_alloc(arena, (uint64_t)manifest->entry_count + 1u);
    for (uint32_t g = 0; g < tp_output_group_count(); g++) {
        good = tp_verify_group(arena, textures, tp_output_group_at(g), manifest, seen, out) && good;
    }
    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        if (seen[i] == 0) {
            good = tp_fail("%s/%s: the manifest imports it, and it is not there as an imported texture", textures,
                           manifest->entries[i].to);
        }
    }
    arena->used = mark;
    return good;
}
