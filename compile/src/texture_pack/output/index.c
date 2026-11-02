/* See README.md. Every file here is a pure function of the manifest and the pixels, so an import is repeatable. */
#include "texture_pack/output/index.h"

#include "texture_pack/mctex/index.h"

#include <string.h>

#define TP_OUTPUT_TEXT_BYTES (1ull << 16)
#define TP_OUTPUT_ATTRIBUTION_BYTES (1ull << 22)

typedef struct tp_group_row_t {
    const char *name; /* 0 */
    const char *what; /* 8 */
} tp_group_row_t;
static_assert(sizeof(tp_group_row_t) == 16);

static const tp_group_row_t tp_output_group_rows[] = {
    {"blocks", "Faces, animated strips and crack stages of blocks placed in the world."},
    {"items", "Flat icons of things held in the inventory."},
    {"mobs", "Skins of things that move: mobs, the player, carts."},
    {"ui", "Screens, the hotbar, bars and icons, and the sun and moon."},
};
enum { TP_GROUP_ROW_COUNT = sizeof(tp_output_group_rows) / sizeof(tp_output_group_rows[0]) };

uint32_t tp_output_group_count(void) {
    return TP_GROUP_ROW_COUNT;
}

const char *tp_output_group_at(uint32_t position) {
    return position < TP_GROUP_ROW_COUNT ? tp_output_group_rows[position].name : nullptr;
}

static void tp_output_authors(tp_builder_t *builder, const tp_manifest_t *manifest) {
    for (uint32_t i = 0; i < manifest->author_count; i++) {
        tp_builder_add(builder, "%s%s", i == 0 ? "" : ", ", manifest->authors[i]);
    }
}

static void tp_output_changes(tp_builder_t *builder, const tp_entry_t *entry) {
    tp_builder_add(builder, "Changed from the original: converted from PNG to `index.mctex`");
    if (entry->frames > 1u) {
        tp_builder_add(builder, ", with its vertical strip recorded as %u frames", entry->frames);
    }
    if (entry->over != nullptr) {
        tp_builder_add(builder, ", and composited over `%s` from the same pack, which makes it fully opaque",
                       entry->over);
    } else {
        tp_builder_add(builder, ", with every pixel value kept");
    }
    tp_builder_add(builder, ".");
}

bool tp_output_texture(tp_arena_t *arena, const char *textures, const tp_manifest_t *manifest, const tp_entry_t *entry,
                       const tp_image_t *image, uint32_t *out_written) {
    const uint64_t mark = arena->used;
    const char *directory = tp_arena_format(arena, "%s/%s", textures, entry->to);
    if (!tp_make_directories(arena, directory)) {
        return false;
    }

    const uint64_t file_bytes = sizeof(mctex_header_t) + image->pixel_bytes;
    uint8_t *file = tp_arena_alloc(arena, file_bytes);
    const mctex_status_t encoded = mctex_encode(file, file_bytes, image->width, image->height, image->frames,
                                                image->borrowed_pixels, image->pixel_bytes);
    if (encoded != MCTEX_OK) {
        return tp_fail("%s: %s", entry->to, mctex_status_text(encoded));
    }
    bool wrote = false;
    if (!tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.mctex", directory), file, file_bytes, &wrote)) {
        return false;
    }
    *out_written += wrote ? 1u : 0u;

    tp_builder_t yaml = tp_builder_begin(arena, TP_OUTPUT_TEXT_BYTES);
    tp_builder_add(&yaml,
                   "# Written by compile/src/texture_pack from textures/import.yaml. A re-import replaces it.\n");
    tp_builder_add(&yaml, "name: %s\ngroup: %s\n", entry->name, entry->group);
    tp_builder_add(&yaml, "size: { width: %u, height: %u, frames: %u }\n", image->width, image->height, image->frames);
    tp_builder_add(&yaml, "source:\n  kind: pack\n  pack: %s\n  commit: \"%s\"\n  licence: %s\n  file: %s\n",
                   manifest->id, manifest->commit, manifest->licence, entry->from);
    if (entry->over != nullptr) {
        tp_builder_add(&yaml, "  over: %s\n", entry->over);
    }
    if (!tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.yaml", directory), yaml.borrowed_text, yaml.used,
                             &wrote)) {
        return false;
    }
    *out_written += wrote ? 1u : 0u;

    tp_builder_t readme = tp_builder_begin(arena, TP_OUTPUT_TEXT_BYTES);
    tp_builder_add(&readme, "# textures/%s\n\n", entry->to);
    tp_builder_add(&readme, "From [%s](%s), file `%s`, commit `%s`.\n\nBy ", manifest->title, manifest->url,
                   entry->from, manifest->commit);
    tp_output_authors(&readme, manifest);
    tp_builder_add(&readme, ". Licence: [%s](%s). See `textures/ATTRIBUTION.md`.\n\n", manifest->licence,
                   manifest->licence_url);
    tp_output_changes(&readme, entry);
    tp_builder_add(&readme, "\n\nWritten by `compile/src/texture_pack`, which `textures/README.md` describes.\n");
    if (!tp_write_if_changed(arena, tp_arena_format(arena, "%s/README.md", directory), readme.borrowed_text,
                             readme.used, &wrote)) {
        return false;
    }
    *out_written += wrote ? 1u : 0u;
    arena->used = mark;
    return true;
}

bool tp_output_groups(tp_arena_t *arena, const char *textures, uint32_t *out_written) {
    const uint64_t mark = arena->used;
    for (uint32_t i = 0; i < TP_GROUP_ROW_COUNT; i++) {
        const char *directory = tp_arena_format(arena, "%s/%s", textures, tp_output_group_rows[i].name);
        if (!tp_make_directories(arena, directory)) {
            return false;
        }
        const char *text =
            tp_arena_format(arena, "# textures/%s\n\n%s One directory per texture. See `textures/README.md`.\n",
                            tp_output_group_rows[i].name, tp_output_group_rows[i].what);
        bool wrote = false;
        if (!tp_write_if_changed(arena, tp_arena_format(arena, "%s/README.md", directory), text, strlen(text),
                                 &wrote)) {
            return false;
        }
        *out_written += wrote ? 1u : 0u;
    }
    arena->used = mark;
    return true;
}

bool tp_output_attribution(tp_arena_t *arena, const char *textures, const tp_manifest_t *manifest,
                           const tp_coverage_t *coverage, uint32_t *out_written) {
    const uint64_t mark = arena->used;
    tp_builder_t text = tp_builder_begin(arena, TP_OUTPUT_ATTRIBUTION_BYTES);
    tp_builder_add(&text, "# Texture attribution\n\n");
    tp_builder_add(&text,
                   "Written by `compile/src/texture_pack` from `textures/import.yaml`. A re-import replaces it.\n\n");
    tp_builder_add(&text, "This file ships with anything that ships these textures. No Mojang textures and no "
                          "decompiled code are used anywhere in this repository. A texture with no row below comes "
                          "from this project's own procedural recipes, which its `index.yaml` holds.\n\n");
    tp_builder_add(&text, "## %s\n\n", manifest->title);
    tp_builder_add(&text, "- **Authors:** ");
    tp_output_authors(&text, manifest);
    tp_builder_add(&text, "\n- **Source:** %s\n", manifest->url);
    tp_builder_add(&text, "- **Commit:** `%s`\n", manifest->commit);
    tp_builder_add(&text, "- **Licence:** %s, %s, %s\n", manifest->licence, manifest->licence_name,
                   manifest->licence_url);
    tp_builder_add(&text,
                   "- **Coverage:** %u files in the pack: %u imported, %u deliberately left out, %u holding its "
                   "licence statement. The reasons are in `textures/import.yaml`.\n\n",
                   coverage->files, coverage->imported, coverage->skipped, coverage->evidence);
    tp_builder_add(&text, "The textures listed below are adaptations of that work, and are licensed to you under the "
                          "same licence. There is no warranty, as the licence says. Each was converted from PNG to "
                          "our own `.mctex` format, and renamed. Any further change is stated in its row.\n\n");
    tp_builder_add(&text, "| Texture | File in the pack | Further change |\n|---|---|---|\n");
    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        const tp_entry_t *entry = &manifest->entries[i];
        tp_builder_add(&text, "| `%s` | `%s` | ", entry->to, entry->from);
        if (entry->over != nullptr) {
            tp_builder_add(&text, "composited over `%s`", entry->over);
        } else if (entry->frames > 1u) {
            tp_builder_add(&text, "%u animation frames", entry->frames);
        } else {
            tp_builder_add(&text, "none");
        }
        tp_builder_add(&text, " |\n");
    }
    bool wrote = false;
    if (!tp_write_if_changed(arena, tp_arena_format(arena, "%s/ATTRIBUTION.md", textures), text.borrowed_text,
                             text.used, &wrote)) {
        return false;
    }
    *out_written += wrote ? 1u : 0u;
    arena->used = mark;
    return true;
}
