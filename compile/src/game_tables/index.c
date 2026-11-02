/*
 * The Owner of the tool: reads the command line, maps its one arena, and runs each table's part. See README.md.
 */
#include "game_tables/index.h"

#include "game_tables/biomes/index.h"
#include "game_tables/blocks/index.h"
#include "game_tables/items/index.h"
#include "game_tables/recipes/index.h"
#include "texture_pack/support/index.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* Reserved, not used: only touched pages cost anything. */
#define GT_ARENA_BYTES (1ull << 30)

typedef struct gt_arguments_t {
    const char *config;   /* 0 */
    const char *textures; /* 8 */
    const char *out;      /* 16 */
} gt_arguments_t;
static_assert(sizeof(gt_arguments_t) == 24);

static bool gt_arguments_parse(int32_t argc, char **argv, gt_arguments_t *out) {
    memset(out, 0, sizeof(*out));
    for (int32_t i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return false;
        }
        if (strcmp(argv[i], "--config") == 0) {
            out->config = argv[i + 1];
        } else if (strcmp(argv[i], "--textures") == 0) {
            out->textures = argv[i + 1];
        } else if (strcmp(argv[i], "--out") == 0) {
            out->out = argv[i + 1];
        } else {
            return false;
        }
    }
    return out->config != nullptr && out->textures != nullptr && out->out != nullptr;
}

int32_t main(int32_t argc, char **argv) {
    gt_arguments_t arguments;
    if (!gt_arguments_parse(argc, argv, &arguments)) {
        fprintf(stderr, "usage: game_tables --config <directory> --textures <directory> --out <directory>\n");
        return GAME_TABLES_EXIT_USAGE;
    }
    void *owned_region =
        mmap(nullptr, GT_ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (owned_region == MAP_FAILED) {
        perror("game_tables: mmap");
        return GAME_TABLES_EXIT_USAGE;
    }
    tp_arena_t arena = {.borrowed_base = owned_region, .capacity = GT_ARENA_BYTES, .used = 0};
    /* Blocks first, since items and biomes name them. Each part looks at the others' files, not at their tables. */
    const bool good = game_tables_blocks_build(&arena, arguments.config, arguments.textures, arguments.out) &&
                      game_tables_items_build(&arena, arguments.config, arguments.textures, arguments.out) &&
                      game_tables_recipes_build(&arena, arguments.config, arguments.out) &&
                      game_tables_biomes_build(&arena, arguments.config, arguments.out);
    munmap(owned_region, GT_ARENA_BYTES);
    return good ? GAME_TABLES_EXIT_OK : GAME_TABLES_EXIT_REFUSED;
}
