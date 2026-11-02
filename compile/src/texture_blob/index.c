/*
 * The Owner of the tool: maps its one arena, lists textures/, lays the blob out from every texture's sizes, and fills it. See
 * README.md. The mip part makes the chains. Each texture is read twice, once to plan and once to fill, because the size of a
 * section is not known until all of them have been read, and the arena goes back after each texture.
 */
#include "texture_blob/index.h"

#include "texture_blob/mip/index.h"
#include "texture_pack/mctex/index.h"
#include "texture_pack/output/index.h"
#include "texture_pack/support/index.h"
#include "texture_vector/mcvec/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* Reserved, not used: only touched pages cost anything. */
#define TB_ARENA_BYTES (1ull << 33)
#define TB_MCTEX_BYTES (MCTEX_MAX_PIXEL_BYTES + sizeof(mctex_header_t))
/* .mcvec has a header, a palette, and 5 bytes per pixel, where .mctex has 4: twice the pixel limit is more than enough. */
#define TB_MCVEC_BYTES (MCTEX_MAX_PIXEL_BYTES * 2ull + sizeof(mcvec_header_t) + MCVEC_MAX_PALETTE * 4ull)

static_assert(TEXTURE_BLOB_ALPHA_OPAQUE == MCVEC_ALPHA_OPAQUE);
static_assert(TEXTURE_BLOB_ALPHA_CUTOUT == MCVEC_ALPHA_CUTOUT);
static_assert(TEXTURE_BLOB_ALPHA_TRANSLUCENT == MCVEC_ALPHA_TRANSLUCENT);
static_assert(TEXTURE_BLOB_ALPHA_CLASS_COUNT == MCVEC_ALPHA_CLASS_COUNT);
static_assert(TEXTURE_BLOB_EMISSIVE_NONE == MCVEC_EMISSIVE_NONE);
static_assert(TEXTURE_BLOB_EMISSIVE_PARTIAL == MCVEC_EMISSIVE_PARTIAL);
static_assert(TEXTURE_BLOB_EMISSIVE_FULL == MCVEC_EMISSIVE_FULL);
static_assert(TEXTURE_BLOB_EMISSIVE_CLASS_COUNT == MCVEC_EMISSIVE_CLASS_COUNT);
static_assert(TEXTURE_BLOB_MAX_DIMENSION == MCTEX_MAX_DIMENSION);
static_assert(TEXTURE_BLOB_MAX_FRAMES == MCTEX_MAX_FRAMES);
static_assert(TEXTURE_BLOB_MAX_PALETTE == MCVEC_MAX_PALETTE);

typedef struct tb_arguments_t {
    const char *textures; /* 0 */
    const char *vectors;  /* 8 */
    const char *out;      /* 16 */
} tb_arguments_t;
static_assert(sizeof(tb_arguments_t) == 24);

/* One texture, read and checked. Borrows from the arena. */
typedef struct tb_loaded_t {
    mctex_view_t pixels;  /* 0 */
    mcvec_view_t vectors; /* 32 */
    uint64_t source_hash; /* 144 */
} tb_loaded_t;
static_assert(sizeof(tb_loaded_t) == 152);

static bool tb_arguments_parse(int32_t argc, char **argv, tb_arguments_t *out) {
    memset(out, 0, sizeof(*out));
    for (int32_t i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return false;
        }
        if (strcmp(argv[i], "--textures") == 0) {
            out->textures = argv[i + 1];
        } else if (strcmp(argv[i], "--vectors") == 0) {
            out->vectors = argv[i + 1];
        } else if (strcmp(argv[i], "--out") == 0) {
            out->out = argv[i + 1];
        } else {
            return false;
        }
    }
    return out->textures != nullptr && out->vectors != nullptr && out->out != nullptr;
}

static int32_t tb_compare_text(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Every "group/name" under textures/, in an order that does not depend on the file system. */
static bool tb_list_textures(tp_arena_t *arena, const char *textures, const char ***out_names, uint32_t *out_count) {
    const char **names = nullptr;
    uint64_t found = 0;
    uint64_t capacity = 0;
    for (uint32_t pass = 0; pass < 2u; pass++) {
        found = 0;
        for (uint32_t g = 0; g < tp_output_group_count(); g++) {
            const char *group = tp_output_group_at(g);
            const char *path = tp_arena_format(arena, "%s/%s", textures, group);
            DIR *listing = opendir(path);
            if (listing == nullptr) {
                return tp_fail("%s: cannot be listed", path);
            }
            for (struct dirent *item = readdir(listing); item != nullptr; item = readdir(listing)) {
                struct stat info;
                const char *child = tp_arena_format(arena, "%s/%s", path, item->d_name);
                if (item->d_name[0] != '.' && stat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
                    if (found < capacity) {
                        names[found] = tp_arena_format(arena, "%s/%s", group, item->d_name);
                    }
                    found++;
                }
            }
            closedir(listing);
        }
        if (found == 0u || found > TEXTURE_BLOB_MAX_TEXTURES) {
            return tp_fail("%s: holds %" PRIu64 " textures, and a blob has 1 to %u", textures, found,
                           TEXTURE_BLOB_MAX_TEXTURES);
        }
        if (pass == 0u) {
            capacity = found;
            names = tp_arena_alloc(arena, capacity * sizeof(const char *));
        }
    }
    if (found != capacity) {
        return tp_fail("%s: changed while it was listed", textures);
    }
    qsort(names, found, sizeof(const char *), tb_compare_text);
    *out_names = names;
    *out_count = (uint32_t)found;
    return true;
}

static bool tb_read_or_say(tp_arena_t *arena, const char *path, uint64_t limit, uint8_t **out_bytes,
                           uint64_t *out_length) {
    const tp_read_status_t status = tp_read_path(arena, path, limit, out_bytes, out_length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: missing", path);
    }
    return status == TP_READ_OK;
}

/* Vector data made from other pixels than these would light and shade the wrong texture, so that is refused. */
static bool tb_load(tp_arena_t *arena, const tb_arguments_t *arguments, const char *name, tb_loaded_t *out) {
    const char *mctex_path = tp_arena_format(arena, "%s/%s/index.mctex", arguments->textures, name);
    const char *mcvec_path = tp_arena_format(arena, "%s/%s/index.mcvec", arguments->vectors, name);
    uint8_t *mctex = nullptr;
    uint8_t *mcvec = nullptr;
    uint64_t mctex_length = 0;
    uint64_t mcvec_length = 0;
    if (!tb_read_or_say(arena, mctex_path, TB_MCTEX_BYTES, &mctex, &mctex_length) ||
        !tb_read_or_say(arena, mcvec_path, TB_MCVEC_BYTES, &mcvec, &mcvec_length)) {
        return false;
    }
    const mctex_status_t pixels = mctex_parse(mctex, mctex_length, &out->pixels);
    if (pixels != MCTEX_OK) {
        return tp_fail("%s: %s", mctex_path, mctex_status_text(pixels));
    }
    const mcvec_status_t vectors = mcvec_parse(mcvec, mcvec_length, &out->vectors);
    if (vectors != MCVEC_OK) {
        return tp_fail("%s: %s", mcvec_path, mcvec_status_text(vectors));
    }
    mctex_header_t header;
    memcpy(&header, mctex, sizeof(header));
    out->source_hash = header.pixel_hash;
    const mcvec_header_t *vector_header = &out->vectors.header;
    if (vector_header->source_hash != header.pixel_hash || vector_header->width != out->pixels.width ||
        vector_header->height != out->pixels.height || vector_header->frame_count != out->pixels.frame_count) {
        return tp_fail("%s: was not made from %s; run texture_vector vectors", mcvec_path, mctex_path);
    }
    return true;
}

static void tb_entry_fill(const tb_loaded_t *loaded, const char *name, uint32_t name_offset,
                          texture_blob_entry_t *out) {
    const mcvec_header_t *vector = &loaded->vectors.header;
    const uint64_t name_length = strlen(name);
    memset(out, 0, sizeof(*out));
    out->name_hash = mctex_hash((const uint8_t *)name, name_length);
    out->source_hash = loaded->source_hash;
    out->pixels_bytes = tb_mip_chain_bytes(vector->width, vector->height, vector->frame_count, out->level_offset);
    out->vector_bytes = vector->palette_count * 4ull + loaded->vectors.pixel_count * 5u;
    out->name_offset = name_offset;
    out->width = vector->width;
    out->height = vector->height;
    out->frame_count = vector->frame_count;
    out->palette_count = vector->palette_count;
    out->visible_pixels = vector->visible_pixels;
    out->emissive_pixels = vector->emissive_pixels;
    out->name_length = (uint16_t)name_length;
    out->level_count = (uint8_t)texture_blob_level_count(vector->width, vector->height);
    out->alpha_class = vector->alpha_class;
    out->emissive_class = vector->emissive_class;
    out->emissive_level = vector->emissive_level;
    out->palette_exact = vector->palette_exact;
    out->luma_min = vector->luma_min;
    out->luma_max = vector->luma_max;
    out->luma_mean = vector->luma_mean;
    memcpy(out->mean_rgba, vector->mean_rgba, sizeof(out->mean_rgba));
}

/* Reads every texture for its sizes, and fixes where everything goes. */
static bool tb_plan(tp_arena_t *arena, const tb_arguments_t *arguments, const char **names, uint32_t count,
                    texture_blob_entry_t *entries, texture_blob_header_t *out) {
    uint64_t name_bytes = 0;
    uint64_t pixels_end = 0;
    uint64_t vectors_end = 0;
    bool good = true;
    for (uint32_t id = 0; id < count; id++) {
        const uint64_t mark = arena->used;
        tb_loaded_t loaded;
        const uint64_t name_length = strlen(names[id]);
        if (name_length > UINT16_MAX) {
            good = tp_fail("%s: this name is too long", names[id]);
        } else if (tb_load(arena, arguments, names[id], &loaded)) {
            tb_entry_fill(&loaded, names[id], (uint32_t)name_bytes, &entries[id]);
            entries[id].pixels_offset = texture_blob_align_up(pixels_end, TEXTURE_BLOB_TEXTURE_ALIGNMENT);
            entries[id].vector_offset = texture_blob_align_up(vectors_end, TEXTURE_BLOB_TEXTURE_ALIGNMENT);
            pixels_end = entries[id].pixels_offset + entries[id].pixels_bytes;
            vectors_end = entries[id].vector_offset + entries[id].vector_bytes;
        } else {
            good = false;
        }
        name_bytes += name_length + 1u;
        arena->used = mark;
    }
    const uint64_t names_offset = sizeof(texture_blob_header_t) + (uint64_t)count * sizeof(texture_blob_entry_t);
    const uint64_t pixels_offset = texture_blob_align_up(names_offset + name_bytes, TEXTURE_BLOB_ALIGNMENT);
    const uint64_t vectors_offset = texture_blob_align_up(pixels_offset + pixels_end, TEXTURE_BLOB_TEXTURE_ALIGNMENT);
    *out = (texture_blob_header_t){.magic = TEXTURE_BLOB_MAGIC,
                                   .version = TEXTURE_BLOB_VERSION,
                                   .texture_count = count,
                                   .name_bytes = (uint32_t)name_bytes,
                                   .total_bytes = vectors_offset + vectors_end,
                                   .entries_offset = sizeof(texture_blob_header_t),
                                   .names_offset = names_offset,
                                   .pixels_offset = pixels_offset,
                                   .vectors_offset = vectors_offset,
                                   .pixels_bytes = pixels_end,
                                   .vectors_bytes = vectors_end};
    return good;
}

static bool tb_fill_texture(tp_arena_t *arena, const tb_arguments_t *arguments, const char *name,
                            const texture_blob_entry_t *entry, uint8_t *out_pixels, uint8_t *out_vector) {
    tb_loaded_t loaded;
    if (!tb_load(arena, arguments, name, &loaded)) {
        return false;
    }
    const tb_mip_source_t source = {.borrowed_pixels = loaded.pixels.borrowed_pixels,
                                    .width = entry->width,
                                    .height = entry->height,
                                    .frame_count = entry->frame_count,
                                    .alpha_class = entry->alpha_class,
                                    .hole_rgb = {entry->mean_rgba[0], entry->mean_rgba[1], entry->mean_rgba[2]}};
    tb_mip_build(&source, out_pixels);
    const uint64_t pixel_count = loaded.vectors.pixel_count;
    uint8_t *at = out_vector;
    memcpy(at, loaded.vectors.borrowed_palette, entry->palette_count * 4ull);
    at += entry->palette_count * 4ull;
    memcpy(at, loaded.vectors.borrowed_indices, pixel_count);
    at += pixel_count;
    memcpy(at, loaded.vectors.borrowed_height, pixel_count);
    at += pixel_count;
    memcpy(at, loaded.vectors.borrowed_normal, pixel_count * 2u);
    at += pixel_count * 2u;
    memcpy(at, loaded.vectors.borrowed_emissive, pixel_count);
    return true;
}

/* The arena zeroes what it gives, so every gap between sections and textures is zero. It aligns to 16, and nothing here needs
 * more: the embedding aligns the bytes it links in. */
static bool tb_build(tp_arena_t *arena, const tb_arguments_t *arguments, const char **names, uint32_t count) {
    texture_blob_entry_t *entries = tp_arena_alloc(arena, (uint64_t)count * sizeof(texture_blob_entry_t));
    texture_blob_header_t header;
    if (!tb_plan(arena, arguments, names, count, entries, &header)) {
        return false;
    }
    uint8_t *blob = tp_arena_alloc(arena, header.total_bytes);
    memcpy(blob + header.entries_offset, entries, (uint64_t)count * sizeof(texture_blob_entry_t));
    uint8_t *name_section = blob + header.names_offset;
    for (uint32_t id = 0; id < count; id++) {
        memcpy(name_section + entries[id].name_offset, names[id], (uint64_t)entries[id].name_length + 1u);
        const uint64_t mark = arena->used;
        if (!tb_fill_texture(arena, arguments, names[id], &entries[id],
                             blob + header.pixels_offset + entries[id].pixels_offset,
                             blob + header.vectors_offset + entries[id].vector_offset)) {
            return false;
        }
        arena->used = mark;
    }
    header.payload_hash = mctex_hash(blob + sizeof(header), header.total_bytes - sizeof(header));
    memcpy(blob, &header, sizeof(header));
    if (!tp_write_if_changed(arena, arguments->out, blob, header.total_bytes, nullptr)) {
        return false;
    }
    printf("texture_blob: %u textures, %" PRIu64 " bytes: %" PRIu64 " of pixels with mips, %" PRIu64
           " of vector data\n",
           count, header.total_bytes, header.pixels_bytes, header.vectors_bytes);
    return true;
}

int32_t main(int32_t argc, char **argv) {
    tb_arguments_t arguments;
    if (!tb_arguments_parse(argc, argv, &arguments)) {
        fprintf(stderr, "usage: texture_blob --textures <directory> --vectors <directory> --out <file>\n");
        return TEXTURE_BLOB_EXIT_USAGE;
    }
    void *owned_region =
        mmap(nullptr, TB_ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (owned_region == MAP_FAILED) {
        perror("texture_blob: mmap");
        return TEXTURE_BLOB_EXIT_USAGE;
    }
    tp_arena_t arena = {.borrowed_base = owned_region, .capacity = TB_ARENA_BYTES, .used = 0};
    const char **names = nullptr;
    uint32_t count = 0;
    const bool good =
        tb_list_textures(&arena, arguments.textures, &names, &count) && tb_build(&arena, &arguments, names, count);
    munmap(owned_region, TB_ARENA_BYTES);
    return good ? TEXTURE_BLOB_EXIT_OK : TEXTURE_BLOB_EXIT_REFUSED;
}
