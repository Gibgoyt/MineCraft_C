/*
 * ctest compile_game_tables. See README.md. Arguments come from test.cmake:
 *   tool config textures generated_tables work_dir
 * The first half reads the tables the build generated from the real config/blocks/. The second runs the tool on small trees of
 * its own, one mistake at a time, and holds each refusal to its line.
 */
#include "harness/index.h"
#include "tables/blocks/index.h"
#include "yaml/index.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GTT_PATH_BYTES 1024u
#define GTT_FILE_BYTES (1u << 20)
#define GTT_TIMEOUT_MS 20000u

static const char *gtt_arg(test_context_t *t, int32_t index) {
    if (index >= t->arg_count) {
        test_check(t, false, __FILE__, __LINE__, "test.cmake gives five arguments");
        return "";
    }
    return t->borrowed_args[index];
}

static char *gtt_path(test_context_t *t, const char *first, const char *second) {
    char *path = test_alloc(t, GTT_PATH_BYTES);
    snprintf(path, GTT_PATH_BYTES, "%s/%s", first, second);
    return path;
}

static uint32_t gtt_find(const char *name) {
    for (uint32_t b = 0; b < BLOCK_COUNT; b++) {
        if (strcmp(block_defs[b].name, name) == 0) {
            return b;
        }
    }
    return UINT32_MAX;
}

/* A whole file, NUL-terminated, or nullptr. */
static char *gtt_read(test_context_t *t, const char *path, uint64_t *out_length) {
    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        return nullptr;
    }
    char *text = test_alloc(t, GTT_FILE_BYTES);
    const uint64_t length = fread(text, 1, GTT_FILE_BYTES - 1u, file);
    fclose(file);
    if (out_length != nullptr) {
        *out_length = length;
    }
    return text;
}

static void gtt_write(test_context_t *t, const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    CHECK(file != nullptr);
    if (file != nullptr) {
        fputs(text, file);
        fclose(file);
    }
}

/* mkdir -p, with no recursion. */
static void gtt_mkdirs(test_context_t *t, const char *path) {
    char *copy = test_alloc(t, GTT_PATH_BYTES);
    snprintf(copy, GTT_PATH_BYTES, "%s", path);
    for (char *c = copy + 1; *c != '\0'; c++) {
        if (*c == '/') {
            *c = '\0';
            mkdir(copy, 0777);
            *c = '/';
        }
    }
    mkdir(copy, 0777);
}

/* textures/vector.yaml, parsed. */
static bool gtt_load_vector_yaml(test_context_t *t, yaml_document_t *document) {
    uint64_t length = 0;
    const char *text = gtt_read(t, gtt_path(t, gtt_arg(t, 2), "vector.yaml"), &length);
    if (text == nullptr) {
        return false;
    }
    yaml_storage_t storage = {
        .nodes = test_alloc(t, (length + 1u) * sizeof(yaml_node_t)),
        .strings = test_alloc(t, length + 1u),
        .node_capacity = (uint32_t)(length + 1u),
        .string_capacity = (uint32_t)length,
    };
    yaml_error_t error = {0};
    return yaml_parse(document, (const uint8_t *)text, length, storage, &error);
}

/* ---- The real tables ---------------------------------------------------------------------------------------------- */

TEST(about_120_blocks_with_air_first) {
    CHECK(BLOCK_COUNT >= 110u && BLOCK_COUNT <= 140u);
    CHECK_EQ_U64(BLOCK_ID_AIR, 0u);
    CHECK_STR_EQ(block_defs[BLOCK_ID_AIR].name, "air");
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].shape, BLOCK_SHAPE_NONE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].flags, BLOCK_FLAG_REPLACEABLE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].faces[BLOCK_FACE_TOP], BLOCK_NO_TEXTURE);
}

TEST(dirt_drops_dirt) {
    const block_def_t *dirt = &block_defs[BLOCK_ID_DIRT];
    CHECK_STR_EQ(dirt->name, "dirt");
    CHECK_EQ_U64(dirt->drop_count, 1u);
    const block_drop_t *drop = &block_drops[dirt->first_drop];
    CHECK_STR_EQ(block_drop_items[drop->item].name, "dirt");
    CHECK_EQ_U64(block_drop_items[drop->item].block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(drop->count_min, 1u);
    CHECK_EQ_U64(drop->count_max, 1u);
    CHECK(drop->chance == 1.0f32);
    CHECK(dirt->hardness == 0.5f32);
    CHECK_EQ_U64(dirt->tool, BLOCK_TOOL_SHOVEL);

    /* And the file says so plainly: this is the line a reader will look for. */
    char *path = gtt_path(t, gtt_arg(t, 1), "blocks/dirt/index.yaml");
    const char *text = gtt_read(t, path, nullptr);
    REQUIRE(text != nullptr);
    CHECK_CONTAINS(text, "drops: [{ item: dirt, count: 1 }]\n");
}

TEST(what_the_files_say_is_in_the_table) {
    /* Stone gives cobblestone, and only to a pickaxe. */
    const block_def_t *stone = &block_defs[BLOCK_ID_STONE];
    CHECK_STR_EQ(block_drop_items[block_drops[stone->first_drop].item].name, "cobblestone");
    CHECK_EQ_U64(block_drop_items[block_drops[stone->first_drop].item].block, BLOCK_ID_COBBLESTONE);
    CHECK_EQ_U64(stone->tool, BLOCK_TOOL_PICKAXE);
    CHECK_EQ_U64(stone->tier, BLOCK_TIER_WOOD);
    CHECK(stone->hardness == 1.5f32);
    CHECK_EQ_U64(stone->flags, BLOCK_FLAG_SOLID);
    CHECK_EQ_U64(stone->opacity, 15u);

    /* An item that is no block. */
    const block_def_t *coal = &block_defs[BLOCK_ID_COAL_ORE];
    CHECK_STR_EQ(block_drop_items[block_drops[coal->first_drop].item].name, "coal");
    CHECK_EQ_U64(block_drop_items[block_drops[coal->first_drop].item].block, BLOCK_NO_ID);

    /* A range, a lower tier and several rows with chances. */
    const block_drop_t *lapis = &block_drops[block_defs[BLOCK_ID_LAPIS_ORE].first_drop];
    CHECK_EQ_U64(lapis->count_min, 4u);
    CHECK_EQ_U64(lapis->count_max, 9u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_DIAMOND_ORE].tier, BLOCK_TIER_IRON);
    CHECK_EQ_U64(block_defs[BLOCK_ID_OBSIDIAN].tier, BLOCK_TIER_DIAMOND);
    const block_def_t *gravel = &block_defs[BLOCK_ID_GRAVEL];
    REQUIRE(gravel->drop_count == 2u);
    CHECK(block_drops[gravel->first_drop].chance == 0.9f32);
    CHECK_STR_EQ(block_drop_items[block_drops[gravel->first_drop + 1u].item].name, "flint");
    CHECK(block_drops[gravel->first_drop + 1u].chance == 0.1f32);
    CHECK((gravel->flags & BLOCK_FLAG_FALLS) != 0u);

    /* Unbreakable, and nothing dropped. */
    const block_def_t *bedrock = &block_defs[BLOCK_ID_BEDROCK];
    CHECK((bedrock->flags & BLOCK_FLAG_UNBREAKABLE) != 0u);
    CHECK(bedrock->hardness == -1.0f32);
    CHECK_EQ_U64(bedrock->drop_count, 0u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_GLASS].drop_count, 0u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_GLASS].opacity, 0u);

    /* Shapes and their defaults, and a default overruled. */
    const block_def_t *torch = &block_defs[BLOCK_ID_TORCH];
    CHECK_EQ_U64(torch->shape, BLOCK_SHAPE_TORCH);
    CHECK_EQ_U64(torch->light, 14u);
    CHECK_EQ_U64(torch->flags, 0u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_OAK_SLAB].shape, BLOCK_SHAPE_SLAB);
    CHECK_EQ_U64(block_defs[BLOCK_ID_OAK_SLAB].flags, BLOCK_FLAG_SOLID);
    CHECK_EQ_U64(block_defs[BLOCK_ID_OAK_LEAVES].opacity, 1u);
    CHECK((block_defs[BLOCK_ID_OAK_LEAVES].flags & BLOCK_FLAG_DECAYS) != 0u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_WATER].shape, BLOCK_SHAPE_FLUID);
    CHECK_EQ_U64(block_defs[BLOCK_ID_WATER].opacity, 2u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_WATER].flags, BLOCK_FLAG_REPLACEABLE);

    /* x east, y north, z up, and bit 0 is the positive side, so a ray's step picks a face with no branch. */
    CHECK_EQ_U64(BLOCK_FACE_WEST, 0u);
    CHECK_EQ_U64(BLOCK_FACE_EAST, 1u);
    CHECK_EQ_U64(BLOCK_FACE_SOUTH, 2u);
    CHECK_EQ_U64(BLOCK_FACE_NORTH, 3u);
    CHECK_EQ_U64(BLOCK_FACE_BOTTOM, 4u);
    CHECK_EQ_U64(BLOCK_FACE_TOP, 5u);

    /* Faces land where they were named: the furnace's front is its north, and side covers the other three. */
    const uint16_t *faces = block_defs[BLOCK_ID_FURNACE].faces;
    CHECK_STR_EQ(block_texture_names[faces[BLOCK_FACE_NORTH]], "blocks/furnace_front");
    CHECK_STR_EQ(block_texture_names[faces[BLOCK_FACE_SOUTH]], "blocks/furnace_side");
    CHECK_STR_EQ(block_texture_names[faces[BLOCK_FACE_EAST]], "blocks/furnace_side");
    CHECK_STR_EQ(block_texture_names[faces[BLOCK_FACE_WEST]], "blocks/furnace_side");
    CHECK_STR_EQ(block_texture_names[faces[BLOCK_FACE_TOP]], "blocks/furnace_top");
    CHECK_STR_EQ(block_texture_names[faces[BLOCK_FACE_BOTTOM]], "blocks/furnace_bottom");
    const uint16_t *grass = block_defs[BLOCK_ID_GRASS_BLOCK].faces;
    CHECK_STR_EQ(block_texture_names[grass[BLOCK_FACE_TOP]], "blocks/grass_top");
    CHECK_STR_EQ(block_texture_names[grass[BLOCK_FACE_BOTTOM]], "blocks/dirt");
}

TEST(every_row_is_whole) {
    uint32_t next_drop = 0;
    for (uint32_t b = 0; b < BLOCK_COUNT; b++) {
        const block_def_t *block = &block_defs[b];
        REQUIRE(block->name != nullptr);
        CHECK_EQ_U64(gtt_find(block->name), b);
        if (b > 1u) {
            CHECK(strcmp(block_defs[b - 1u].name, block->name) < 0);
        }
        CHECK_EQ_U64(block->first_drop, next_drop);
        next_drop += block->drop_count;
        for (uint32_t d = 0; d < block->drop_count; d++) {
            const block_drop_t *drop = &block_drops[block->first_drop + d];
            CHECK(drop->item < BLOCK_DROP_ITEM_COUNT);
            CHECK(drop->count_min >= 1u && drop->count_min <= drop->count_max);
            CHECK(drop->chance > 0.0f32 && drop->chance <= 1.0f32);
        }
        if (b != BLOCK_ID_AIR) {
            for (uint32_t face = 0; face < 6u; face++) {
                CHECK(block->faces[face] < BLOCK_TEXTURE_COUNT);
            }
        }
    }
    CHECK_EQ_U64(next_drop, BLOCK_DROP_COUNT);
    for (uint32_t i = 0; i < BLOCK_DROP_ITEM_COUNT; i++) {
        const uint16_t block = block_drop_items[i].block;
        if (block != BLOCK_NO_ID) {
            CHECK_STR_EQ(block_defs[block].name, block_drop_items[i].name);
        }
    }
}

TEST(every_texture_is_on_disk_and_something_keeps_it) {
    /* vector.yaml's needs is what `texture_vector check` holds the tree to, so a re-import cannot lose these. */
    yaml_document_t document;
    REQUIRE(gtt_load_vector_yaml(t, &document));
    const yaml_node_t *needs = yaml_path_get(&document, yaml_root(&document), "needs.blocks");
    REQUIRE(needs != nullptr);
    for (uint32_t i = 0; i < BLOCK_TEXTURE_COUNT; i++) {
        const char *name = block_texture_names[i];
        char *relative = test_alloc(t, GTT_PATH_BYTES);
        snprintf(relative, GTT_PATH_BYTES, "%s/index.mctex", name);
        struct stat info;
        CHECK(stat(gtt_path(t, gtt_arg(t, 2), relative), &info) == 0);
        bool needed = false;
        for (const yaml_node_t *row = yaml_first_child(&document, needs); row != nullptr && !needed;
             row = yaml_next_sibling(&document, row)) {
            needed = yaml_text_equals(row, name + strlen("blocks/"));
        }
        if (!needed) {
            fprintf(stderr, "not under needs.blocks in vector.yaml: %s\n", name);
        }
        CHECK(needed);
    }
}

TEST(a_blocks_light_is_what_its_texture_gives) {
    /* textures/vector.yaml says which textures shine for the renderer, and the tables say which blocks light a room. */
    yaml_document_t document;
    REQUIRE(gtt_load_vector_yaml(t, &document));
    const yaml_node_t *materials = yaml_map_get(&document, yaml_root(&document), "materials");
    REQUIRE(materials != nullptr);
    yaml_error_t error = {0};
    uint32_t lit = 0;
    for (uint32_t b = 0; b < BLOCK_COUNT; b++) {
        int64_t brightest = 0;
        for (uint32_t face = 0; face < 6u && b != BLOCK_ID_AIR; face++) {
            const char *name = block_texture_names[block_defs[b].faces[face]];
            for (const yaml_node_t *row = yaml_first_child(&document, materials); row != nullptr;
                 row = yaml_next_sibling(&document, row)) {
                const yaml_node_t *emissive = yaml_map_get(&document, row, "emissive");
                int64_t level = 0;
                if (emissive != nullptr && yaml_text_equals(yaml_map_get(&document, row, "texture"), name) &&
                    yaml_get_int64(emissive, &level, &error) && level > brightest) {
                    brightest = level;
                }
            }
        }
        if (block_defs[b].light != brightest) {
            fprintf(stderr, "%s: table light %u, texture %ld\n", block_defs[b].name, block_defs[b].light, brightest);
        }
        CHECK_EQ_I64(block_defs[b].light, brightest);
        lit += block_defs[b].light != 0u ? 1u : 0u;
    }
    CHECK(lit >= 3u);
}

TEST(the_tool_makes_the_same_bytes_as_the_build) {
    char *out = gtt_path(t, gtt_arg(t, 4), "same_bytes");
    gtt_mkdirs(t, out);
    char *const argv[] = {(char *)gtt_arg(t, 0),
                          (char *)"--config",
                          (char *)gtt_arg(t, 1),
                          (char *)"--textures",
                          (char *)gtt_arg(t, 2),
                          (char *)"--out",
                          out,
                          nullptr};
    test_process_t run;
    test_process_run(t, argv, GTT_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
    const char *const files[] = {"blocks/index.h", "blocks/index.c"};
    for (uint32_t i = 0; i < 2u; i++) {
        uint64_t left_length = 0;
        uint64_t right_length = 0;
        const char *left = gtt_read(t, gtt_path(t, out, files[i]), &left_length);
        const char *right = gtt_read(t, gtt_path(t, gtt_arg(t, 3), files[i]), &right_length);
        REQUIRE(left != nullptr);
        REQUIRE(right != nullptr);
        CHECK_EQ_U64(left_length, right_length);
        CHECK(memcmp(left, right, left_length) == 0);
    }
}

/* ---- Small trees ------------------------------------------------------------------------------------------------ */

/* The tree: air and stone, stone's texture, and a coal item. Each case then replaces stone's file. */
static const char GTT_STONE[] = "name: stone\n"
                                "hardness: 1.5\n"
                                "tool: pickaxe\n"
                                "tier: wood\n"
                                "drops: [{ item: coal, count: 1 }]\n"
                                "texture: { all: blocks/stone }\n";

typedef struct gtt_tree_t {
    char *borrowed_root;     /* 0 */
    char *borrowed_config;   /* 8 */
    char *borrowed_textures; /* 16 */
    char *borrowed_out;      /* 24 */
    char *borrowed_stone;    /* 32: stone's yaml */
} gtt_tree_t;
static_assert(sizeof(gtt_tree_t) == 40);

static gtt_tree_t gtt_tree_make(test_context_t *t, const char *name) {
    gtt_tree_t tree;
    tree.borrowed_root = gtt_path(t, gtt_arg(t, 4), name);
    tree.borrowed_config = gtt_path(t, tree.borrowed_root, "config");
    tree.borrowed_textures = gtt_path(t, tree.borrowed_root, "textures");
    tree.borrowed_out = gtt_path(t, tree.borrowed_root, "out");
    tree.borrowed_stone = gtt_path(t, tree.borrowed_config, "blocks/stone/index.yaml");
    char *const rm[] = {(char *)"rm", (char *)"-rf", tree.borrowed_root, nullptr};
    test_process_t run;
    test_process_run(t, rm, GTT_TIMEOUT_MS, &run);
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "blocks/air"));
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "blocks/stone"));
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_textures, "blocks/stone"));
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_textures, "items/coal"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "blocks/air/index.yaml"),
              "name: air\nhardness: 0\nshape: none\ndrops: []\n");
    gtt_write(t, tree.borrowed_stone, GTT_STONE);
    gtt_write(t, gtt_path(t, tree.borrowed_textures, "blocks/stone/index.mctex"), "");
    gtt_write(t, gtt_path(t, tree.borrowed_textures, "items/coal/index.mctex"), "");
    return tree;
}

static void gtt_tree_run(test_context_t *t, const gtt_tree_t *tree, test_process_t *run) {
    char *const argv[] = {(char *)gtt_arg(t, 0),   (char *)"--config", tree->borrowed_config, (char *)"--textures",
                          tree->borrowed_textures, (char *)"--out",    tree->borrowed_out,    nullptr};
    test_process_run(t, argv, GTT_TIMEOUT_MS, run);
}

/* A whole stone file, the line:column it must be refused at, and what the message must say. */
typedef struct gtt_bad_case_t {
    const char *text;     /* 0 */
    const char *position; /* 8 */
    const char *message;  /* 16 */
} gtt_bad_case_t;
static_assert(sizeof(gtt_bad_case_t) == 24);

#define GTT_STONE_START "name: stone\nhardness: 1.5\n"
#define GTT_STONE_DROPS "drops: [{ item: coal, count: 1 }]\n"
#define GTT_STONE_TEXTURE "texture: { all: blocks/stone }\n"

static const gtt_bad_case_t GTT_BAD_CASES[] = {
    /* Unknown fields, at each level. */
    {GTT_STONE_START "colour: grey\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "unknown field 'colour' in a block"},
    {GTT_STONE_START "drops: [{ item: coal, count: 1, luck: 3 }]\n" GTT_STONE_TEXTURE,
     ":3:", "unknown field 'luck' in drop"},
    {GTT_STONE_START GTT_STONE_DROPS "texture:\n  all: blocks/stone\n  shiny: blocks/stone\n",
     ":6:", "unknown field 'shiny' in texture"},
    /* Missing fields, each at the file's first key. */
    {"hardness: 1.5\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":1:", "missing field 'name'"},
    {"name: stone\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":1:", "missing field 'hardness'"},
    {GTT_STONE_START GTT_STONE_TEXTURE, ":1:", "missing field 'drops'"},
    {GTT_STONE_START GTT_STONE_DROPS, ":1:", "missing field 'texture'"},
    {GTT_STONE_START "drops: [{ count: 1 }]\n" GTT_STONE_TEXTURE, ":3:", "drop has no item"},
    {GTT_STONE_START "drops: [{ item: coal }]\n" GTT_STONE_TEXTURE, ":3:", "drop has no count"},
    /* Missing references. */
    {GTT_STONE_START GTT_STONE_DROPS "texture: { all: blocks/nothing }\n",
     ":4:", "texture 'blocks/nothing' does not exist"},
    {GTT_STONE_START GTT_STONE_DROPS "texture:\n  top: blocks/stone\n  bottom: blocks/stone\n  side: blocks/nothing\n",
     ":7:", "texture 'blocks/nothing' does not exist"},
    {GTT_STONE_START "drops:\n  - { item: coal, count: 1 }\n  - { item: nothing, count: 1 }\n" GTT_STONE_TEXTURE,
     ":5:", "drop item 'nothing' does not exist"},
    {GTT_STONE_START GTT_STONE_DROPS "texture: { all: items/coal }\n", ":4:", "is not of the form blocks/<name>"},
    {GTT_STONE_START GTT_STONE_DROPS "texture: { all: blocks/../items/coal }\n",
     ":4:", "is not of the form blocks/<name>"},
    {GTT_STONE_START GTT_STONE_DROPS "texture: { all: blocks/stone/deeper }\n",
     ":4:", "is not of the form blocks/<name>"},
    {GTT_STONE_START "drops: [{ item: air, count: 1 }]\n" GTT_STONE_TEXTURE, ":3:", "air cannot be a drop"},
    {GTT_STONE_START "drops: [{ item: \"../coal\", count: 1 }]\n" GTT_STONE_TEXTURE, ":3:", "is not a valid name"},
    /* Faces. */
    {GTT_STONE_START GTT_STONE_DROPS "texture:\n  all: blocks/stone\n  top: blocks/stone\n",
     ":5:8:", "'all' cannot go with other faces"},
    {GTT_STONE_START GTT_STONE_DROPS "texture:\n  side: blocks/stone\n  top: blocks/stone\n", ":",
     "nothing for the bottom face"},
    {GTT_STONE_START GTT_STONE_DROPS "texture: blocks/stone\n", ":4:", "texture must be a mapping"},
    /* Values. */
    {"name: rock\nhardness: 1.5\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":1:7:", "is not its directory's"},
    {"name: stone\nhardness: -1\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":2:", "must be 0 to 1000"},
    {"name: stone\nhardness: 1001\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":2:", "must be 0 to 1000"},
    {"name: stone\nhardness: 1e99\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":2:", "must be 0 to 1000"},
    {"name: stone\nhardness: 1e-99\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":2:", "must be 0 to 1000"},
    {"name: stone\nhardness: soft\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":2:", ""},
    {"name: stone\nhardness: \"unbreakable\"\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":2:", ""},
    {GTT_STONE_START "tool: spoon\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "unknown tool 'spoon'"},
    {GTT_STONE_START "tool: pickaxe\ntier: gold\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":4:", "unknown tier 'gold'"},
    {GTT_STONE_START "tier: wood\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "needs a tool that has tiers"},
    {GTT_STONE_START "tool: shears\ntier: wood\n" GTT_STONE_DROPS GTT_STONE_TEXTURE,
     ":4:", "needs a tool that has tiers"},
    {GTT_STONE_START "shape: blob\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "unknown shape 'blob'"},
    {GTT_STONE_START "shape: none\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":5:", "shape none draws nothing"},
    {GTT_STONE_START "light: 16\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "light is 16"},
    {GTT_STONE_START "opacity: -1\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "opacity is -1"},
    {GTT_STONE_START "falls: yes\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", ""},
    {GTT_STONE_START "drops: { item: coal, count: 1 }\n" GTT_STONE_TEXTURE, ":3:", "drops must be a list"},
    {GTT_STONE_START "drops: [coal]\n" GTT_STONE_TEXTURE, ":3:", "a drop must be a mapping"},
    {GTT_STONE_START "drops: [{ item: coal, count: 0 }]\n" GTT_STONE_TEXTURE, ":3:", "drop count is 0"},
    {GTT_STONE_START "drops: [{ item: coal, count: 65 }]\n" GTT_STONE_TEXTURE, ":3:", "drop count is 65"},
    {GTT_STONE_START "drops: [{ item: coal, count: 3, max: 2 }]\n" GTT_STONE_TEXTURE,
     ":3:", "drop max is below its count"},
    {GTT_STONE_START "drops: [{ item: coal, count: 1, chance: 0 }]\n" GTT_STONE_TEXTURE, ":3:", "must be above 0"},
    {GTT_STONE_START "drops: [{ item: coal, count: 1, chance: 1.5 }]\n" GTT_STONE_TEXTURE, ":3:", "must be above 0"},
    /* The file as a whole. */
    {"- just\n- a list\n", ":1:", "must be a mapping"},
    {"", ":1:", "must be a mapping"},
    {GTT_STONE_START "hardness: 2\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", ""},
    {GTT_STONE_START "drops: [{ item: coal\n", ":3:", ""},
};
enum { GTT_BAD_CASE_COUNT = sizeof(GTT_BAD_CASES) / sizeof(GTT_BAD_CASES[0]) };

TEST(a_good_tree_makes_tables) {
    const gtt_tree_t tree = gtt_tree_make(t, "good");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
    const char *header = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.h"), nullptr);
    const char *source = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.c"), nullptr);
    REQUIRE(header != nullptr);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(header, "BLOCK_ID_AIR = 0,");
    CHECK_CONTAINS(header, "BLOCK_ID_STONE = 1,");
    CHECK_CONTAINS(header, "#define BLOCK_COUNT 2u");
    CHECK_CONTAINS(source, "\"blocks/stone\",");
    /* What the file wrote, exactly: 15 tenths, not a rounded binary number. */
    CHECK_CONTAINS(source, ".hardness = 15e-1f32");
    CHECK_CONTAINS(source, "{.name = \"coal\", .block = BLOCK_NO_ID}");
}

TEST(air_stays_block_0_and_the_rest_sort_by_name) {
    const gtt_tree_t tree = gtt_tree_make(t, "order");
    /* "aaa" sorts before "air", and air must stay 0 anyway. */
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "blocks/aaa"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "blocks/aaa/index.yaml"),
              "name: aaa\nhardness: 0\ndrops: [{ item: stone, count: 1 }]\ntexture: { all: blocks/stone }\n");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    const char *header = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.h"), nullptr);
    const char *source = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.c"), nullptr);
    REQUIRE(header != nullptr);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(header, "BLOCK_ID_AIR = 0,");
    CHECK_CONTAINS(header, "BLOCK_ID_AAA = 1,");
    CHECK_CONTAINS(header, "BLOCK_ID_STONE = 2,");
    /* A drop that is a block links to it. */
    CHECK_CONTAINS(source, "{.name = \"stone\", .block = BLOCK_ID_STONE}");
}

TEST(an_item_may_be_defined_by_its_config_file) {
    const gtt_tree_t tree = gtt_tree_make(t, "item_file");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/pebble"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "items/pebble/index.yaml"), "name: pebble\n");
    gtt_write(t, tree.borrowed_stone, GTT_STONE_START "drops: [{ item: pebble, count: 1 }]\n" GTT_STONE_TEXTURE);
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
}

/* No real block has yet said false to a default it would get from its shape, so nothing else would notice this going wrong. */
TEST(a_flag_in_the_file_beats_its_shape_either_way) {
    const gtt_tree_t tree = gtt_tree_make(t, "flags");
    gtt_write(t, tree.borrowed_stone,
              GTT_STONE_START "solid: false\nreplaceable: true\nfalls: true\n" GTT_STONE_DROPS GTT_STONE_TEXTURE);
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    const char *source = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.c"), nullptr);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(source, ".flags = BLOCK_FLAG_REPLACEABLE | BLOCK_FLAG_FALLS}");

    gtt_write(t, tree.borrowed_stone,
              GTT_STONE_START "shape: fluid\nreplaceable: false\n" GTT_STONE_DROPS GTT_STONE_TEXTURE);
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    source = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.c"), nullptr);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(source, ".shape = BLOCK_SHAPE_FLUID, .light = 0, .opacity = 0, .flags = 0u}");
}

TEST(each_mistake_is_refused_at_its_line) {
    const gtt_tree_t tree = gtt_tree_make(t, "bad");
    char *expected = test_alloc(t, GTT_PATH_BYTES);
    for (uint32_t i = 0; i < GTT_BAD_CASE_COUNT; i++) {
        const gtt_bad_case_t *bad = &GTT_BAD_CASES[i];
        gtt_write(t, tree.borrowed_stone, bad->text);
        test_process_t run;
        gtt_tree_run(t, &tree, &run);
        /* Named on failure, since one test covers every case. */
        if (run.exit_code != 1 || strstr(run.output, bad->message) == nullptr) {
            fprintf(stderr, "case %u: wanted '%s' at %s, got exit %d:\n%s\n", i, bad->message, bad->position,
                    run.exit_code, run.output);
        }
        CHECK_EQ_I64(run.exit_code, 1);
        CHECK_CONTAINS(run.output, bad->message);
        snprintf(expected, GTT_PATH_BYTES, "%s%s", tree.borrowed_stone, bad->position);
        CHECK_CONTAINS(run.output, expected);
        /* Nothing is written when anything is wrong. */
        struct stat info;
        CHECK(stat(gtt_path(t, tree.borrowed_out, "blocks/index.h"), &info) != 0);
    }
}

TEST(all_the_problems_are_shown_at_once) {
    const gtt_tree_t tree = gtt_tree_make(t, "many");
    gtt_write(t, tree.borrowed_stone,
              "name: stone\nhardness: soft\ncolour: grey\ndrops: []\ntexture: { all: blocks/nothing }\n");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    char *expected = test_alloc(t, GTT_PATH_BYTES);
    snprintf(expected, GTT_PATH_BYTES, "%s:2:", tree.borrowed_stone);
    CHECK_CONTAINS(run.output, expected);
    snprintf(expected, GTT_PATH_BYTES, "%s:3:9: unknown field 'colour'", tree.borrowed_stone);
    CHECK_CONTAINS(run.output, expected);
    snprintf(expected, GTT_PATH_BYTES, "%s:5:", tree.borrowed_stone);
    CHECK_CONTAINS(run.output, expected);
    CHECK_CONTAINS(run.output, "3 problems");
}

TEST(a_tree_with_a_hole_is_refused) {
    const gtt_tree_t tree = gtt_tree_make(t, "hole");
    test_process_t run;

    /* A block's directory with no file. */
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "blocks/ghost"));
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "blocks/ghost/index.yaml: missing");
    rmdir(gtt_path(t, tree.borrowed_config, "blocks/ghost"));

    /* A directory that could not be an enum name. */
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "blocks/Bad-Name"));
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "directory name 'Bad-Name'");
    rmdir(gtt_path(t, tree.borrowed_config, "blocks/Bad-Name"));

    /* No air. */
    unlink(gtt_path(t, tree.borrowed_config, "blocks/air/index.yaml"));
    rmdir(gtt_path(t, tree.borrowed_config, "blocks/air"));
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "has no air");
}

TEST(a_bad_command_line_says_how) {
    char *const argv[] = {(char *)gtt_arg(t, 0), (char *)"--config", (char *)"x", nullptr};
    test_process_t run;
    test_process_run(t, argv, GTT_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, 2);
    CHECK_CONTAINS(run.output, "usage:");
}

TEST_MAIN()
