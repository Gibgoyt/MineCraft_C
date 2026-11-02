/*
 * The Owner of the import: maps the tool's one arena, and runs the steps in the order that makes a bad pack harmless.
 * Everything is checked and every texture decoded before the first file is written. See README.md.
 */
#include "texture_pack/index.h"

#include "texture_pack/manifest/index.h"
#include "texture_pack/output/index.h"
#include "texture_pack/pack/index.h"
#include "texture_pack/support/index.h"
#include "texture_pack/verify/index.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* Reserved, not used: only touched pages cost anything. */
#define TP_ARENA_BYTES (1ull << 32)

typedef struct tp_arguments_t {
    const char *pack;     /* 0 */
    const char *manifest; /* 8 */
    const char *textures; /* 16 */
    uint8_t importing;    /* 24 */
    uint8_t _pad0[7];     /* 25 */
} tp_arguments_t;
static_assert(sizeof(tp_arguments_t) == 32);

static bool tp_arguments_parse(int32_t argc, char **argv, tp_arguments_t *out) {
    memset(out, 0, sizeof(*out));
    if (argc < 2) {
        return false;
    }
    if (strcmp(argv[1], "import") == 0) {
        out->importing = 1;
    } else if (strcmp(argv[1], "verify") != 0) {
        return false;
    }
    for (int32_t i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return false;
        }
        if (strcmp(argv[i], "--pack") == 0 && out->importing != 0) {
            out->pack = argv[i + 1];
        } else if (strcmp(argv[i], "--manifest") == 0) {
            out->manifest = argv[i + 1];
        } else if (strcmp(argv[i], "--textures") == 0) {
            out->textures = argv[i + 1];
        } else {
            return false;
        }
    }
    return out->manifest != nullptr && out->textures != nullptr && (out->importing == 0 || out->pack != nullptr);
}

/* Every texture, decoded for nothing but to find out that it can be. */
static bool tp_import_check_all(tp_arena_t *arena, const char *pack, const tp_manifest_t *manifest) {
    bool good = true;
    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        const uint64_t mark = arena->used;
        tp_image_t image;
        good = tp_pack_load_entry(arena, pack, &manifest->entries[i], &image) && good;
        arena->used = mark;
    }
    return good;
}

static bool tp_import(tp_arena_t *arena, const tp_arguments_t *arguments, const tp_manifest_t *manifest) {
    tp_coverage_t coverage;
    if (!tp_pack_check_commit(arena, arguments->pack, manifest) ||
        !tp_pack_check_licence(arena, arguments->pack, manifest) ||
        !tp_pack_check_coverage(arena, arguments->pack, manifest, &coverage) ||
        !tp_import_check_all(arena, arguments->pack, manifest)) {
        return tp_fail("refused, and nothing was written");
    }
    uint32_t written = 0;
    if (!tp_make_directories(arena, arguments->textures) || !tp_output_groups(arena, arguments->textures, &written)) {
        return false;
    }
    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        const uint64_t mark = arena->used;
        tp_image_t image;
        if (!tp_pack_load_entry(arena, arguments->pack, &manifest->entries[i], &image) ||
            !tp_output_texture(arena, arguments->textures, manifest, &manifest->entries[i], &image, &written)) {
            return false;
        }
        arena->used = mark;
    }
    if (!tp_output_attribution(arena, arguments->textures, manifest, &coverage, &written)) {
        return false;
    }
    printf("texture_pack: %s at %s, %s: %u files, %u imported, %u skipped, %u licence statements; %u output files "
           "changed\n",
           manifest->id, manifest->commit, manifest->licence, coverage.files, coverage.imported, coverage.skipped,
           coverage.evidence, written);
    return true;
}

int32_t main(int32_t argc, char **argv) {
    tp_arguments_t arguments;
    if (!tp_arguments_parse(argc, argv, &arguments)) {
        fprintf(stderr, "usage: texture_pack import --pack <clone> --manifest <import.yaml> --textures <directory>\n"
                        "       texture_pack verify --manifest <import.yaml> --textures <directory>\n");
        return TEXTURE_PACK_EXIT_USAGE;
    }
    void *owned_region =
        mmap(nullptr, TP_ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (owned_region == MAP_FAILED) {
        perror("texture_pack: mmap");
        return TEXTURE_PACK_EXIT_USAGE;
    }
    tp_arena_t arena = {.borrowed_base = owned_region, .capacity = TP_ARENA_BYTES, .used = 0};

    tp_manifest_t manifest;
    bool good = tp_manifest_load(&arena, arguments.manifest, &manifest);
    if (good && arguments.importing != 0) {
        good = tp_import(&arena, &arguments, &manifest);
    }
    if (good) {
        /* An import proves itself with the same check the committed tree gets. */
        tp_verify_counts_t counts;
        good = tp_verify_tree(&arena, arguments.textures, &manifest, &counts);
        if (good) {
            printf("texture_pack: verified %s: %u imported textures, %u from other sources\n", arguments.textures,
                   counts.imported, counts.other);
        }
    }
    munmap(owned_region, TP_ARENA_BYTES);
    return good ? TEXTURE_PACK_EXIT_OK : TEXTURE_PACK_EXIT_REFUSED;
}
