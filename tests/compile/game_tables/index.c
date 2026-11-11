/*
 * ctest compile_game_tables. See README.md. Arguments come from test.cmake:
 *   tool config textures generated_tables work_dir
 * The first half reads the tables the build generated from the real config/blocks/. The second runs the tool on small trees of
 * its own, one mistake at a time, and holds each refusal to its line.
 */
#include "harness/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"
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

TEST(between_110_and_170_blocks_with_air_first) {
    CHECK(BLOCK_COUNT >= 110u && BLOCK_COUNT <= 170u);
    CHECK_EQ_U64(BLOCK_ID_AIR, 0u);
    CHECK_STR_EQ(block_defs[BLOCK_ID_AIR].name, "air");
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].shape, BLOCK_SHAPE_NONE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].flags, BLOCK_FLAG_REPLACEABLE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].faces[BLOCK_FACE_TOP], BLOCK_NO_TEXTURE);
}

TEST(every_block_sounds_like_what_it_is) {
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].sound, SOUND_FAMILY_NONE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_WATER].sound, SOUND_FAMILY_NONE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_LAVA].sound, SOUND_FAMILY_NONE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_STONE].sound, SOUND_FAMILY_STONE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_IRON_ORE].sound, SOUND_FAMILY_STONE);
    CHECK_EQ_U64(block_defs[BLOCK_ID_OAK_LOG].sound, SOUND_FAMILY_WOOD);
    CHECK_EQ_U64(block_defs[BLOCK_ID_OAK_PLANKS].sound, SOUND_FAMILY_WOOD);
    CHECK_EQ_U64(block_defs[BLOCK_ID_DIRT].sound, SOUND_FAMILY_DIRT);
    CHECK_EQ_U64(block_defs[BLOCK_ID_GRAVEL].sound, SOUND_FAMILY_GRAVEL);
    CHECK_EQ_U64(block_defs[BLOCK_ID_SAND].sound, SOUND_FAMILY_SAND);
    CHECK_EQ_U64(block_defs[BLOCK_ID_GRASS_BLOCK].sound, SOUND_FAMILY_GRASS);
    CHECK_EQ_U64(block_defs[BLOCK_ID_OAK_LEAVES].sound, SOUND_FAMILY_LEAVES);
    CHECK_EQ_U64(block_defs[BLOCK_ID_SNOW_BLOCK].sound, SOUND_FAMILY_SNOW);
    CHECK_EQ_U64(block_defs[BLOCK_ID_GLASS].sound, SOUND_FAMILY_GLASS);
    /* Anything a player can break has a sound. Only air and fluids are silent, and the tool holds them to that too. */
    for (uint32_t b = 0; b < BLOCK_COUNT; b++) {
        const bool never_dug = block_defs[b].shape == BLOCK_SHAPE_NONE || block_defs[b].shape == BLOCK_SHAPE_FLUID;
        if (!never_dug && block_defs[b].sound == SOUND_FAMILY_NONE) {
            fprintf(stderr, "%s has no sound\n", block_defs[b].name);
        }
        CHECK(never_dug || block_defs[b].sound != SOUND_FAMILY_NONE);
        CHECK(block_defs[b].sound < SOUND_FAMILY_COUNT);
    }
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
    const char *const files[] = {"blocks/index.h",  "blocks/index.c",  "items/index.h", "items/index.c",
                                 "recipes/index.h", "recipes/index.c", "ores/index.h",  "ores/index.c"};
    for (uint32_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
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

/* The tree: air and stone, stone's texture, and items of coal and stone. Each case then replaces stone's file. */
static const char GTT_STONE[] = "name: stone\n"
                                "hardness: 1.5\n"
                                "tool: pickaxe\n"
                                "tier: wood\n"
                                "drops: [{ item: coal, count: 1 }]\n"
                                "texture: { all: blocks/stone }\n"
                                "sound: stone\n";

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
              "name: air\nhardness: 0\nshape: none\ndrops: []\nsound: none\n");
    gtt_write(t, tree.borrowed_stone, GTT_STONE);
    gtt_write(t, gtt_path(t, tree.borrowed_textures, "blocks/stone/index.mctex"), "");
    gtt_write(t, gtt_path(t, tree.borrowed_textures, "items/coal/index.mctex"), "");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/coal"));
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/stone"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "items/coal/index.yaml"), "name: coal\ntexture: items/coal\n");
    gtt_write(t, gtt_path(t, tree.borrowed_config, "items/stone/index.yaml"),
              "name: stone\ntexture: blocks/stone\nblock: stone\n");
    /* Two tiers of tool, plan 005 phase 6's files of config/tiers/. */
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "tiers/gold"));
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "tiers/wood"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "tiers/gold/index.yaml"),
              "name: gold\nharvests: wood\nspeed: 12000\naxe_attack_speed: 1\nhoe_attack_speed: 1\n");
    gtt_write(t, gtt_path(t, tree.borrowed_config, "tiers/wood/index.yaml"),
              "name: wood\nharvests: wood\nspeed: 2000\naxe_attack_speed: 0.8\nhoe_attack_speed: 1\n");
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
/* Last in the file, so it moves no line a case below is held to. */
#define GTT_STONE_TEXTURE "texture: { all: blocks/stone }\nsound: stone\n"

static const gtt_bad_case_t GTT_BAD_CASES[] = {
    /* A state is a word the table knows, and air has none. */
    {GTT_STONE_START "facing: sideways\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "unknown facing 'sideways'"},
    {GTT_STONE_START "half: top\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "unknown half 'top'"},
    {GTT_STONE_START "open: maybe\n" GTT_STONE_DROPS GTT_STONE_TEXTURE, ":3:", "true or false"},
    /* What it sounds like: every block must say, and only what is never dug may say none. */
    {GTT_STONE_START GTT_STONE_DROPS "texture: { all: blocks/stone }\n", ":1:", "missing field 'sound'"},
    {GTT_STONE_START GTT_STONE_DROPS "texture: { all: blocks/stone }\nsound: squeak\n",
     ":5:", "unknown sound 'squeak'"},
    {GTT_STONE_START GTT_STONE_DROPS "texture: { all: blocks/stone }\nsound: none\n", ":5:", "sound none is only for"},
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
    CHECK_CONTAINS(source, "{.name = \"coal\", .block = BLOCK_NO_ID, .item = ITEM_ID_COAL}");
}

TEST(air_stays_block_0_and_the_rest_sort_by_name) {
    const gtt_tree_t tree = gtt_tree_make(t, "order");
    /* "aaa" sorts before "air", and air must stay 0 anyway. */
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "blocks/aaa"));
    gtt_write(
        t, gtt_path(t, tree.borrowed_config, "blocks/aaa/index.yaml"),
        "name: aaa\nhardness: 0\ndrops: [{ item: stone, count: 1 }]\ntexture: { all: blocks/stone }\nsound: wood\n");
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
    CHECK_CONTAINS(source, "{.name = \"stone\", .block = BLOCK_ID_STONE, .item = ITEM_ID_STONE}");
}

TEST(an_item_may_be_defined_by_its_config_file) {
    const gtt_tree_t tree = gtt_tree_make(t, "item_file");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/pebble"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "items/pebble/index.yaml"), "name: pebble\ntexture: items/coal\n");
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
    CHECK_CONTAINS(source,
                   ".flags = BLOCK_FLAG_REPLACEABLE | BLOCK_FLAG_FALLS, .sound = SOUND_FAMILY_STONE, .states = 0u}");

    gtt_write(t, tree.borrowed_stone,
              GTT_STONE_START "shape: fluid\nreplaceable: false\n" GTT_STONE_DROPS GTT_STONE_TEXTURE);
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    source = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.c"), nullptr);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(source,
                   ".shape = BLOCK_SHAPE_FLUID, .light = 0, .opacity = 0, .flags = 0u, .sound = SOUND_FAMILY_STONE, "
                   ".states = 0u}");
}

TEST(a_state_in_the_file_is_in_the_table) {
    const gtt_tree_t tree = gtt_tree_make(t, "states");
    gtt_write(t, tree.borrowed_stone,
              GTT_STONE_START "facing: horizontal\nhalf: upper_lower\nopen: true\n" GTT_STONE_DROPS GTT_STONE_TEXTURE);
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    const char *source = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.c"), nullptr);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(source, ".states = BLOCK_STATES_FACING | BLOCK_STATES_HALF | BLOCK_STATES_OPEN}");

    gtt_write(t, tree.borrowed_stone,
              GTT_STONE_START "facing: none\nhalf: none\nopen: false\n" GTT_STONE_DROPS GTT_STONE_TEXTURE);
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    source = gtt_read(t, gtt_path(t, tree.borrowed_out, "blocks/index.c"), nullptr);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(source, ".sound = SOUND_FAMILY_STONE, .states = 0u}");
}

TEST(the_real_blocks_that_turn_say_so) {
    static const uint16_t turning[] = {
        BLOCK_ID_OAK_STAIRS, BLOCK_ID_COBBLESTONE_STAIRS,     BLOCK_ID_LADDER,
        BLOCK_ID_VINE,       BLOCK_ID_WOODWORKING_BENCH_WEST, BLOCK_ID_WOODWORKING_BENCH_EAST};
    for (uint32_t i = 0u; i < sizeof(turning) / sizeof(turning[0]); i++) {
        CHECK_EQ_U64(block_defs[turning[i]].states, BLOCK_STATES_FACING);
    }
    CHECK_EQ_U64(block_defs[BLOCK_ID_STONE].states, 0u);
    CHECK_EQ_U64(block_defs[BLOCK_ID_AIR].states, 0u);
    CHECK(BLOCK_COUNT <= BLOCK_STATE_ID_LIMIT);
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
              "name: stone\nhardness: soft\ncolour: grey\ndrops: []\ntexture: { all: blocks/nothing }\nsound: stone\n");
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

/* ---- Items: Stage 13.1 --------------------------------------------------------------------------------------------- */

TEST(item_0_is_none_and_the_rest_sort_by_name) {
    CHECK_EQ_U64(ITEM_ID_NONE, 0u);
    CHECK_STR_EQ(item_defs[ITEM_ID_NONE].name, "none");
    CHECK_EQ_U64(item_defs[ITEM_ID_NONE].max_stack, 0u);
    CHECK_EQ_U64(item_defs[ITEM_ID_NONE].block, ITEM_NO_BLOCK);
    for (uint32_t i = 2u; i < ITEM_COUNT; i++) {
        CHECK(strcmp(item_defs[i - 1u].name, item_defs[i].name) < 0);
    }
}

TEST(every_item_is_whole_and_its_texture_is_on_disk) {
    for (uint32_t i = 1u; i < ITEM_COUNT; i++) {
        const item_def_t *const item = &item_defs[i];
        CHECK(item->max_stack >= 1u && item->max_stack <= ITEM_STACK_LIMIT);
        char *const pixels = test_alloc(t, GTT_PATH_BYTES);
        snprintf(pixels, GTT_PATH_BYTES, "%s/%s/index.mctex", gtt_arg(t, 2), item->texture);
        struct stat info;
        CHECK(stat(pixels, &info) == 0);
        if (item->block != ITEM_NO_BLOCK) {
            CHECK(item->block != BLOCK_ID_AIR);
            CHECK(item->block < BLOCK_COUNT);
        }
        if (item->durability != 0u) {
            CHECK_EQ_U64(item->max_stack, 1u);
        }
        if (item->tool != BLOCK_TOOL_NONE) {
            CHECK(item->durability != 0u);
        }
    }
}

TEST(what_a_drop_names_is_an_item_of_that_name_that_places_that_block) {
    for (uint32_t i = 0u; i < BLOCK_DROP_ITEM_COUNT; i++) {
        const block_drop_item_t *const drop = &block_drop_items[i];
        REQUIRE(drop->item < ITEM_COUNT);
        CHECK(drop->item != ITEM_ID_NONE);
        CHECK_STR_EQ(item_defs[drop->item].name, drop->name);
        if (drop->block != BLOCK_NO_ID) {
            CHECK_EQ_U64(item_defs[drop->item].block, drop->block);
        }
    }
}

TEST(what_the_item_files_say_is_in_the_table) {
    const item_def_t *const dirt = &item_defs[ITEM_ID_DIRT];
    CHECK_EQ_U64(dirt->block, BLOCK_ID_DIRT);
    CHECK_EQ_U64(dirt->max_stack, 64u);
    CHECK_STR_EQ(dirt->texture, "blocks/dirt");
    CHECK_EQ_U64(item_defs[ITEM_ID_COAL].block, ITEM_NO_BLOCK);
    CHECK_STR_EQ(item_defs[ITEM_ID_COAL].texture, "items/coal");
    CHECK_EQ_U64(item_defs[ITEM_ID_SNOWBALL].max_stack, 16u);
    CHECK_EQ_U64(item_defs[ITEM_ID_WHEAT_SEEDS].block, BLOCK_ID_WHEAT_STAGE_1);

    const item_def_t *const pick = &item_defs[ITEM_ID_IRON_PICKAXE];
    CHECK_EQ_U64(pick->max_stack, 1u);
    CHECK_EQ_U64(pick->durability, 250u);
    CHECK_EQ_U64(pick->tool, BLOCK_TOOL_PICKAXE);
    CHECK_EQ_U64(pick->tier, ITEM_TIER_IRON);
    CHECK_EQ_U64(pick->harvest_tier, BLOCK_TIER_IRON);
    CHECK(pick->damage == 4.0f32);

    /* Gold says gold, and breaks what wood breaks. */
    CHECK_EQ_U64(item_defs[ITEM_ID_GOLD_PICKAXE].tier, ITEM_TIER_GOLD);
    CHECK_EQ_U64(item_defs[ITEM_ID_GOLD_PICKAXE].harvest_tier, BLOCK_TIER_WOOD);
    CHECK_EQ_U64(item_defs[ITEM_ID_SHEARS].tool, BLOCK_TOOL_SHEARS);
    CHECK_EQ_U64(item_defs[ITEM_ID_SHEARS].durability, 238u);

    CHECK_EQ_U64(item_defs[ITEM_ID_BREAD].food, 5u);
    CHECK(item_defs[ITEM_ID_BREAD].saturation == 6.0f32);
    CHECK_EQ_U64(item_defs[ITEM_ID_IRON_BOOTS].armor_slot, ITEM_ARMOR_SLOT_FEET);
    CHECK_EQ_U64(item_defs[ITEM_ID_IRON_BOOTS].armor_points, 2u);
    CHECK_EQ_U64(item_defs[ITEM_ID_DIAMOND_CHESTPLATE].armor_slot, ITEM_ARMOR_SLOT_CHEST);
    CHECK_EQ_U64(item_defs[ITEM_ID_DIAMOND_CHESTPLATE].durability, 528u);
    CHECK_EQ_U64(item_defs[ITEM_ID_DIAMOND_CHESTPLATE].armor_toughness, 2u);
    CHECK_EQ_U64(item_defs[ITEM_ID_IRON_BOOTS].armor_toughness, 0u);
}

/* A whole file for an item called pebble, the line:column it must be refused at, and what the message must say. */
#define GTT_PEBBLE "name: pebble\ntexture: items/coal\n"

static const gtt_bad_case_t GTT_BAD_ITEMS[] = {
    {GTT_PEBBLE "colour: grey\n", ":3:", "unknown field 'colour' in an item"},
    {GTT_PEBBLE "durability: 5\nstack: 1\narmor: { slot: head, points: 1, shine: 2 }\n",
     ":5:", "unknown field 'shine' in armor"},
    {"texture: items/coal\n", ":1:", "missing field 'name'"},
    {"name: pebble\n", ":1:", "missing field 'texture'"},
    {"name: gravel\ntexture: items/coal\n", ":1:", "is not its directory's"},
    {"name: pebble\ntexture: items/nothing\n", ":2:", "texture 'items/nothing' does not exist"},
    {"name: pebble\ntexture: mobs/coal\n", ":2:", "is not of the form"},
    {"name: pebble\ntexture: items/../coal\n", ":2:", "is not of the form"},
    {GTT_PEBBLE "stack: 0\n", ":3:", "stack is 0, and must be 1 to 64"},
    {GTT_PEBBLE "stack: 65\n", ":3:", "stack is 65, and must be 1 to 64"},
    {GTT_PEBBLE "block: nothing\n", ":3:", "block 'nothing' does not exist"},
    {GTT_PEBBLE "block: air\n", ":3:", "cannot place air"},
    {GTT_PEBBLE "tool: spoon\n", ":3:", "unknown tool 'spoon'"},
    {GTT_PEBBLE "tier: wood\n", ":3:", "needs a tool that has tiers"},
    {GTT_PEBBLE "tool: pickaxe\ndurability: 5\nstack: 1\n", ":3:", "tool 'pickaxe' needs a tier"},
    {GTT_PEBBLE "tool: shears\nstack: 1\n", ":1:", "a tool or armor needs a durability"},
    {GTT_PEBBLE "durability: 5\n", ":3:", "must have stack: 1"},
    {GTT_PEBBLE "durability: 0\nstack: 1\n", ":3:", "durability is 0"},
    {GTT_PEBBLE "durability: 70000\nstack: 1\n", ":3:", "durability is 70000"},
    {GTT_PEBBLE "durability: 5\nstack: 1\nblock: stone\n", ":3:", "that places a block has no durability"},
    {GTT_PEBBLE "food: 21\n", ":3:", "food is 21"},
    {GTT_PEBBLE "saturation: 2\n", ":3:", "saturation needs a food"},
    {GTT_PEBBLE "food: 2\nsaturation: 21\n", ":4:", "saturation '21' must be 0 to 20"},
    {GTT_PEBBLE "damage: 101\n", ":3:", "damage '101' must be 0 to 100"},
    {GTT_PEBBLE "armor: { slot: head }\n", ":3:", "armor needs both slot and points"},
    {GTT_PEBBLE "durability: 5\nstack: 1\narmor: { slot: head, points: 1, toughness: 21 }\n",
     ":5:", "armor toughness is 21"},
    {GTT_PEBBLE "durability: 5\nstack: 1\narmor: { slot: tail, points: 1 }\n", ":5:", "unknown armor slot 'tail'"},
    {GTT_PEBBLE "durability: 5\nstack: 1\narmor: { slot: none, points: 1 }\n", ":5:", "armor must go on a slot"},
    {GTT_PEBBLE "durability: 5\nstack: 1\narmor: { slot: head, points: 21 }\n", ":5:", "armor points is 21"},
    {GTT_PEBBLE "armor: { slot: head, points: 1 }\n", ":1:", "a tool or armor needs a durability"},
    {GTT_PEBBLE "durability: 5\nstack: 1\ntool: shears\narmor: { slot: head, points: 1 }\n",
     ":6:", "both a tool and armor"},
};
enum { GTT_BAD_ITEM_COUNT = sizeof(GTT_BAD_ITEMS) / sizeof(GTT_BAD_ITEMS[0]) };

TEST(each_bad_item_is_refused_at_its_line_and_nothing_is_written) {
    const gtt_tree_t tree = gtt_tree_make(t, "bad_items");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/pebble"));
    char *const file = gtt_path(t, tree.borrowed_config, "items/pebble/index.yaml");
    char *const table = gtt_path(t, tree.borrowed_out, "items/index.h");
    for (uint32_t i = 0u; i < GTT_BAD_ITEM_COUNT; i++) {
        gtt_write(t, file, GTT_BAD_ITEMS[i].text);
        test_process_t run;
        gtt_tree_run(t, &tree, &run);
        CHECK_EQ_I64(run.exit_code, 1);
        CHECK_CONTAINS(run.output, GTT_BAD_ITEMS[i].position);
        CHECK_CONTAINS(run.output, GTT_BAD_ITEMS[i].message);
        CHECK(access(table, F_OK) != 0);
    }
}

/* Recipes: a tree that is good until the file under test goes in beside it. */
static void gtt_recipes_add(test_context_t *t, const gtt_tree_t *tree) {
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, "recipes/ok_craft"));
    gtt_write(t, gtt_path(t, tree->borrowed_config, "recipes/ok_craft/index.yaml"),
              "name: ok_craft\nkind: shaped\npattern: [\"SC\"]\nkeys: { S: stone, C: coal }\ngives: coal\ncount: 4\n");
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, "recipes/ok_smelt"));
    gtt_write(t, gtt_path(t, tree->borrowed_config, "recipes/ok_smelt/index.yaml"),
              "name: ok_smelt\nkind: smelt\ninput: stone\ngives: coal\n");
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, "recipes/group_rock"));
    gtt_write(t, gtt_path(t, tree->borrowed_config, "recipes/group_rock/index.yaml"),
              "name: group_rock\nkind: group\nitems: [stone]\n");
}

#define GTT_SHAPED "name: bad\nkind: shaped\n"
#define GTT_GIVES "gives: coal\n"

static const gtt_bad_case_t GTT_BAD_RECIPES[] = {
    {"kind: smelt\ninput: coal\ngives: coal\n", ":1:", "missing field 'name'"},
    {"name: other\nkind: smelt\ninput: coal\ngives: coal\n", ":1:", "is not its directory's"},
    {"name: bad\n", ":1:", "missing field 'kind'"},
    {"name: bad\nkind: boiled\n", ":2:", "unknown kind 'boiled'"},
    {"name: bad\nkind: group\nitems: [coal]\n", ":2:", "only a group, has a name that starts with group_"},
    {"name: bad\nkind: smelt\ninput: coal\ngives: coal\ncolour: grey\n", ":5:", "unknown field 'colour' in a smelt"},
    {"name: bad\nkind: fuel\nitems: [coal]\nticks: 1\ngives: coal\n", ":5:", "unknown field 'gives' in a fuel"},
    {GTT_SHAPED GTT_GIVES, ":1:", "needs a pattern"},
    {GTT_SHAPED "pattern: [\"C\", \"C\", \"C\", \"C\"]\nkeys: { C: coal }\n" GTT_GIVES,
     ":3:", "a pattern has 1 to 3 rows"},
    {GTT_SHAPED "pattern: [\"CCCC\"]\nkeys: { C: coal }\n" GTT_GIVES, ":3:", "a row has 1 to 3 cells"},
    {GTT_SHAPED "pattern: [\"CC\", \"C\"]\nkeys: { C: coal }\n" GTT_GIVES, ":3:", "not as long as the first"},
    {GTT_SHAPED "pattern: [\"C \", \"C \"]\nkeys: { C: coal }\n" GTT_GIVES,
     ":3:", "an empty row or column at its edge"},
    {GTT_SHAPED "pattern: [\"  \", \"CC\"]\nkeys: { C: coal }\n" GTT_GIVES,
     ":3:", "an empty row or column at its edge"},
    {GTT_SHAPED "pattern: [\"C\"]\n" GTT_GIVES, ":1:", "needs keys"},
    {GTT_SHAPED "pattern: [\"CD\"]\nkeys: { C: coal }\n" GTT_GIVES, ":4:", "the pattern's 'D' is not in keys"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal, D: coal }\n" GTT_GIVES, ":4:", "key 'D' is not in the pattern"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal, DD: coal }\n" GTT_GIVES, ":4:", "a key is one letter"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: nothing }\n" GTT_GIVES, ":4:", "'nothing' is no item and no group"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\n", ":1:", "missing field 'gives'"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\ngives: nothing\n", ":5:", "item 'nothing' does not exist"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\ngives: group_rock\n", ":5:", "item 'group_rock' does not exist"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\n" GTT_GIVES "count: 0\n", ":6:", "count is 0"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\n" GTT_GIVES "count: 65\n", ":6:", "count is 65"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\ngives: pick\ncount: 2\n", ":6:", "more than one slot of pick"},
    {"name: bad\nkind: shapeless\ngives: coal\n", ":1:", "needs ingredients"},
    {"name: bad\nkind: shapeless\ningredients: []\ngives: coal\n", ":1:", "needs ingredients"},
    {"name: bad\nkind: shapeless\ningredients: [coal, coal, coal, coal, coal, coal, coal, coal, coal, coal]\ngives: "
     "coal\n",
     ":1:", "needs ingredients"},
    {"name: bad\nkind: shapeless\ningredients: [nothing]\ngives: coal\n", ":3:", "'nothing' is no item and no group"},
    {"name: bad\nkind: smelt\ngives: coal\n", ":1:", "missing field 'input'"},
    {"name: bad\nkind: smelt\ninput: nothing\ngives: coal\n", ":3:", "'nothing' is no item and no group"},
    {"name: bad\nkind: smelt\ninput: coal\ngives: coal\nticks: 0\n", ":5:", "ticks is 0"},
    {"name: bad\nkind: fuel\nitems: [coal]\n", ":1:", "a fuel needs ticks"},
    {"name: bad\nkind: fuel\nitems: [coal]\nticks: 70000\n", ":4:", "ticks is 70000"},
    {"name: bad\nkind: fuel\nitems: []\nticks: 1\n", ":1:", "items must be a list"},
    {"name: bad\nkind: fuel\nitems: [coal]\nticks: 1\nleaves: nothing\n", ":5:", "item 'nothing' does not exist"},
    /* Two that one grid could match, each way there is. The first is straight, the second the same mirrored. */
    {GTT_SHAPED "pattern: [\"SC\"]\nkeys: { S: stone, C: coal }\ngives: stone\n",
     ":1:", "one grid can match this and bad"},
    {GTT_SHAPED "pattern: [\"CS\"]\nkeys: { S: stone, C: coal }\ngives: stone\n",
     ":1:", "one grid can match this and bad"},
    {GTT_SHAPED "pattern: [\"RC\"]\nkeys: { R: group_rock, C: coal }\ngives: stone\n",
     ":1:", "one grid can match this and bad"},
    {"name: bad\nkind: shapeless\ningredients: [coal, stone]\ngives: stone\n",
     ":1:", "one grid can match this and bad"},
    /* A station is one of the words, and only what a grid makes has one. */
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\n" GTT_GIVES "station: shed\n", ":6:", "unknown station 'shed'"},
    {"name: bad\nkind: shapeless\ningredients: [stone]\ngives: coal\nstation: shed\n", ":5:", "unknown station 'shed'"},
    {"name: bad\nkind: smelt\ninput: coal\ngives: coal\nstation: woodworking\n",
     ":5:", "unknown smelting station 'woodworking'"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\n" GTT_GIVES "station: furnace\n",
     ":6:", "unknown station 'furnace'"},
    /* The hands' grid is 2 by 2, and what is turned off says why. */
    {GTT_SHAPED "pattern: [\"CCC\"]\nkeys: { C: coal }\n" GTT_GIVES "station: hands\n",
     ":6:", "fits their grid of 2 by 2"},
    {"name: bad\nkind: shapeless\ningredients: [coal, coal, coal, coal, coal]\ngives: coal\nstation: hands\n",
     ":5:", "fits their grid of 2 by 2"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: coal }\n" GTT_GIVES "off: \"\"\n", ":6:", "off says why it is off"},
    {GTT_SHAPED "pattern: [\"C\"]\nkeys: { C: nothing }\n" GTT_GIVES "off: \"not yet\"\n",
     ":4:", "'nothing' is no item and no group"},
    {"name: bad\nkind: group\nitems: [coal]\noff: \"no\"\n", ":4:", "unknown field 'off' in a group"},
    {"name: bad\nkind: fuel\nitems: [coal]\nticks: 1\nstation: woodworking\n",
     ":5:", "unknown field 'station' in a fuel"},
    /* Saying the table is the same as saying nothing, so this is still the one grid. */
    {GTT_SHAPED "pattern: [\"SC\"]\nkeys: { S: stone, C: coal }\ngives: stone\nstation: table\n",
     ":1:", "one grid can match this and bad"},
    /* An item smelts one way and burns one way. */
    {"name: bad\nkind: smelt\ninput: group_rock\ngives: stone\n", ":1:", "'stone' smelts here and in bad"},
    {"name: bad\nkind: fuel\nitems: [coal, coal]\nticks: 1\n", ":1:", "'coal' burns here and in bad"},
};
enum { GTT_BAD_RECIPE_COUNT = sizeof(GTT_BAD_RECIPES) / sizeof(GTT_BAD_RECIPES[0]) };

TEST(each_bad_recipe_is_refused_at_its_line_and_nothing_is_written) {
    const gtt_tree_t tree = gtt_tree_make(t, "bad_recipes");
    gtt_recipes_add(t, &tree);
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/pick"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "items/pick/index.yaml"),
              "name: pick\ntexture: items/coal\nstack: 1\n");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "recipes/bad"));
    char *const file = gtt_path(t, tree.borrowed_config, "recipes/bad/index.yaml");
    char *const table = gtt_path(t, tree.borrowed_out, "recipes/index.h");
    for (uint32_t i = 0u; i < GTT_BAD_RECIPE_COUNT; i++) {
        gtt_write(t, file, GTT_BAD_RECIPES[i].text);
        test_process_t run;
        gtt_tree_run(t, &tree, &run);
        CHECK_EQ_I64(run.exit_code, 1);
        CHECK_CONTAINS(run.output, GTT_BAD_RECIPES[i].position);
        CHECK_CONTAINS(run.output, GTT_BAD_RECIPES[i].message);
        CHECK(access(table, F_OK) != 0);
    }
}

TEST(good_recipes_make_tables_and_a_tree_with_none_needs_none) {
    const gtt_tree_t tree = gtt_tree_make(t, "good_recipes");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK(access(gtt_path(t, tree.borrowed_out, "recipes/index.h"), F_OK) != 0);

    gtt_recipes_add(t, &tree);
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "recipes/fuel_rock"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "recipes/fuel_rock/index.yaml"),
              "name: fuel_rock\nkind: fuel\nitems: [group_rock]\nticks: 7\nleaves: coal\n");
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
    uint64_t length = 0u;
    const char *const source = gtt_read(t, gtt_path(t, tree.borrowed_out, "recipes/index.c"), &length);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(source,
                   "{.name = \"ok_craft\", .cells = {2, 1, 0, 0, 0, 0, 0, 0, 0}, .gives = ITEM_ID_COAL, .count = 4");
    CHECK_CONTAINS(source, "{.name = \"ok_smelt\", .gives = ITEM_ID_COAL, .ticks = 200, .count = 1, .process = 0}");
    CHECK_CONTAINS(source, "[ITEM_ID_STONE] = 1,");
    CHECK_CONTAINS(source, "[ITEM_ID_STONE] = {.ticks = 7, .leaves = ITEM_ID_COAL},");

    /* What says nothing is the table's. A pattern another station's recipe has is no clash, as no grid is both. */
    CHECK_CONTAINS(source, ".cell_count = 2, .station = 0}");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "recipes/ok_bench"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "recipes/ok_bench/index.yaml"),
              "name: ok_bench\nkind: shaped\npattern: [\"SC\"]\nkeys: { S: stone, C: coal }\ngives: stone\n"
              "station: woodworking\n");
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
    const char *const with_bench = gtt_read(t, gtt_path(t, tree.borrowed_out, "recipes/index.c"), &length);
    REQUIRE(with_bench != nullptr);
    CHECK_CONTAINS(with_bench, ".width = 2, .height = 1, .cell_count = 2, .station = 1}");
    unlink(gtt_path(t, tree.borrowed_config, "recipes/ok_bench/index.yaml"));
    rmdir(gtt_path(t, tree.borrowed_config, "recipes/ok_bench"));

    /* Plan 005 phase 6: a smelt says its block, and what is off is checked and left out, so it may clash with nothing. */
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "recipes/ok_tan"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "recipes/ok_tan/index.yaml"),
              "name: ok_tan\nkind: smelt\ninput: coal\ngives: stone\nticks: 600\nstation: tanning_rack\n");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "recipes/ok_off"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "recipes/ok_off/index.yaml"),
              "name: ok_off\nkind: shaped\npattern: [\"SC\"]\nkeys: { S: stone, C: coal }\ngives: stone\n"
              "off: \"the same as ok_craft, and off\"\n");
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
    const char *const with_tan = gtt_read(t, gtt_path(t, tree.borrowed_out, "recipes/index.c"), &length);
    const char *const header = gtt_read(t, gtt_path(t, tree.borrowed_out, "recipes/index.h"), &length);
    REQUIRE(with_tan != nullptr);
    REQUIRE(header != nullptr);
    CHECK_CONTAINS(with_tan, "{.name = \"ok_tan\", .gives = ITEM_ID_STONE, .ticks = 600, .count = 1, .process = 2}");
    CHECK_CONTAINS(with_tan, ".process = 0}");
    CHECK_CONTAINS(with_tan, "const uint8_t recipe_process_burns[RECIPE_PROCESS_COUNT] = {1, 1, 0};");
    CHECK(strstr(with_tan, "ok_off") == nullptr);
    CHECK_CONTAINS(header, "#define RECIPE_COUNT 1u");
    CHECK_CONTAINS(header, "RECIPE_STATION_HANDS = 5,");
    CHECK_CONTAINS(header, "RECIPE_PROCESS_TANNING_RACK = 2,");
    unlink(gtt_path(t, tree.borrowed_config, "recipes/ok_tan/index.yaml"));
    rmdir(gtt_path(t, tree.borrowed_config, "recipes/ok_tan"));
    unlink(gtt_path(t, tree.borrowed_config, "recipes/ok_off/index.yaml"));
    rmdir(gtt_path(t, tree.borrowed_config, "recipes/ok_off"));

    /* A directory of recipes that makes nothing is a mistake. */
    unlink(gtt_path(t, tree.borrowed_config, "recipes/ok_smelt/index.yaml"));
    rmdir(gtt_path(t, tree.borrowed_config, "recipes/ok_smelt"));
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "there must be a crafting recipe and a smelting one");
}

TEST(an_item_named_for_a_block_must_say_so) {
    const gtt_tree_t tree = gtt_tree_make(t, "named_for_a_block");
    gtt_write(t, gtt_path(t, tree.borrowed_config, "items/stone/index.yaml"), "name: stone\ntexture: blocks/stone\n");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "item 'stone' is named for a block; say block: stone");
}

TEST(a_drop_with_no_item_file_is_refused_even_with_a_texture_or_a_block) {
    const gtt_tree_t tree = gtt_tree_make(t, "drop_without_item");
    unlink(gtt_path(t, tree.borrowed_config, "items/coal/index.yaml"));
    rmdir(gtt_path(t, tree.borrowed_config, "items/coal"));
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "drop item 'coal' does not exist");
    gtt_write(t, tree.borrowed_stone, GTT_STONE_START "drops: [{ item: stone, count: 1 }]\n" GTT_STONE_TEXTURE);
    unlink(gtt_path(t, tree.borrowed_config, "items/stone/index.yaml"));
    rmdir(gtt_path(t, tree.borrowed_config, "items/stone"));
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "drop item 'stone' does not exist");
}

TEST(a_tree_of_items_with_a_hole_is_refused) {
    const gtt_tree_t tree = gtt_tree_make(t, "item_hole");
    test_process_t run;
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/ghost"));
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "items/ghost/index.yaml: missing");
    rmdir(gtt_path(t, tree.borrowed_config, "items/ghost"));

    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/none"));
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "the name of the empty row");
    rmdir(gtt_path(t, tree.borrowed_config, "items/none"));

    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/Bad-Name"));
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "directory name 'Bad-Name'");
}

TEST(a_good_item_tree_makes_tables) {
    const gtt_tree_t tree = gtt_tree_make(t, "good_items");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/gold_pick"));
    gtt_write(
        t, gtt_path(t, tree.borrowed_config, "items/gold_pick/index.yaml"),
        "name: gold_pick\ntexture: items/coal\nstack: 1\ntool: pickaxe\ntier: gold\ndurability: 32\ndamage: 2.5\n");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
    const char *header = gtt_read(t, gtt_path(t, tree.borrowed_out, "items/index.h"), nullptr);
    const char *source = gtt_read(t, gtt_path(t, tree.borrowed_out, "items/index.c"), nullptr);
    REQUIRE(header != nullptr);
    REQUIRE(source != nullptr);
    CHECK_CONTAINS(header, "ITEM_ID_NONE = 0,");
    CHECK_CONTAINS(header, "ITEM_ID_COAL = 1,");
    CHECK_CONTAINS(header, "ITEM_ID_GOLD_PICK = 2,");
    CHECK_CONTAINS(header, "ITEM_ID_STONE = 3,");
    CHECK_CONTAINS(header, "#define ITEM_COUNT 4u");
    CHECK_CONTAINS(source, ".damage = 25e-1f32");
    CHECK_CONTAINS(source, ".tier = ITEM_TIER_GOLD, .harvest_tier = BLOCK_TIER_WOOD");
    CHECK_CONTAINS(source, ".block = BLOCK_ID_STONE");
    /* Plan 005 phase 6: the tiers come from their files, none first and then by name. */
    CHECK_CONTAINS(header, "ITEM_TIER_NONE = 0,");
    CHECK_CONTAINS(header, "ITEM_TIER_GOLD = 1,");
    CHECK_CONTAINS(header, "ITEM_TIER_WOOD = 2,");
    CHECK_CONTAINS(header, "#define ITEM_TIER_COUNT 3u");
    CHECK_CONTAINS(source,
                   "[ITEM_TIER_GOLD] = {.name = \"gold\", .axe_attack_speed = 1e0f32, .hoe_attack_speed = 1e0f32, "
                   ".speed_milli = 12000, .harvest_tier = BLOCK_TIER_WOOD},");
    CHECK_CONTAINS(source, "[ITEM_TIER_WOOD] = {.name = \"wood\", .axe_attack_speed = 8e-1f32");
}

/* A tier's file, refused at its line, and the items with it. */
TEST(each_bad_tier_is_refused_and_nothing_is_written) {
    static const gtt_bad_case_t BAD_TIERS[] = {
        {"name: wood\nharvests: wood\nspeed: 2000\naxe_attack_speed: 0.8\nhoe_attack_speed: 1\ncolour: brown\n",
         ":6:", "unknown field 'colour' in a tier"},
        {"name: wood\nharvests: copper\nspeed: 2000\naxe_attack_speed: 0.8\nhoe_attack_speed: 1\n",
         ":2:", "unknown rung 'copper'"},
        {"name: wood\nharvests: wood\nspeed: 10\naxe_attack_speed: 0.8\nhoe_attack_speed: 1\n", ":3:", "speed is 10"},
        {"name: wood\nharvests: wood\naxe_attack_speed: 0.8\nhoe_attack_speed: 1\n", ":1:", "missing field 'speed'"},
        {"name: oak\nharvests: wood\nspeed: 2000\naxe_attack_speed: 0.8\nhoe_attack_speed: 1\n",
         ":1:", "a tier's name is its directory's"},
    };
    const gtt_tree_t tree = gtt_tree_make(t, "bad_tiers");
    gtt_mkdirs(t, gtt_path(t, tree.borrowed_config, "items/pick"));
    gtt_write(t, gtt_path(t, tree.borrowed_config, "items/pick/index.yaml"),
              "name: pick\ntexture: items/coal\nstack: 1\ntool: pickaxe\ntier: copper\ndurability: 5\n");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "unknown tier 'copper'");
    gtt_write(t, gtt_path(t, tree.borrowed_config, "items/pick/index.yaml"),
              "name: pick\ntexture: items/coal\nstack: 1\ntool: pickaxe\ntier: wood\ndurability: 5\n");
    char *const table = gtt_path(t, tree.borrowed_out, "items/index.h");
    for (uint32_t i = 0u; i < sizeof(BAD_TIERS) / sizeof(BAD_TIERS[0]); i++) {
        gtt_write(t, gtt_path(t, tree.borrowed_config, "tiers/wood/index.yaml"), BAD_TIERS[i].text);
        gtt_tree_run(t, &tree, &run);
        CHECK_EQ_I64(run.exit_code, 1);
        CHECK_CONTAINS(run.output, BAD_TIERS[i].position);
        CHECK_CONTAINS(run.output, BAD_TIERS[i].message);
        CHECK(access(table, F_OK) != 0);
    }
}

/* ---- Biomes: Stage 10.6 ------------------------------------------------------------------------------------------- */

#define GTT_LAND_START "name: land_one\nwhere: land\n"
#define GTT_LAND_SURFACE "surface: { top: stone, filler: stone }\n"

/* One biome for each kind of place, so that a tree is whole. */
static void gtt_biomes_make(test_context_t *t, const gtt_tree_t *tree, const char *land) {
    static const char *const WHERES[] = {"ocean", "deep_ocean", "river", "beach", "stony_shore"};
    for (uint32_t i = 0u; i < sizeof(WHERES) / sizeof(WHERES[0]); i++) {
        char *const name = test_alloc(t, GTT_PATH_BYTES);
        snprintf(name, GTT_PATH_BYTES, "biomes/%s_one", WHERES[i]);
        gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, name));
        char *const file = test_alloc(t, GTT_PATH_BYTES);
        snprintf(file, GTT_PATH_BYTES, "%s/index.yaml", name);
        char *const text = test_alloc(t, GTT_PATH_BYTES);
        snprintf(text, GTT_PATH_BYTES,
                 "name: %s_one\nwhere: %s\nsurface:\n  top: stone\n  filler: stone\n  patches:\n"
                 "    - { block: stone, from: 0, to: 1 }\n",
                 WHERES[i], WHERES[i]);
        gtt_write(t, gtt_path(t, tree->borrowed_config, file), text);
    }
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, "biomes/under_one"));
    gtt_write(
        t, gtt_path(t, tree->borrowed_config, "biomes/under_one/index.yaml"),
        "name: under_one\nwhere: underground\nclimate: { z: [-10, 10] }\ncave: { floor: stone, ceiling: stone }\n");
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, "biomes/land_one"));
    gtt_write(t, gtt_path(t, tree->borrowed_config, "biomes/land_one/index.yaml"), land);
}

TEST(a_whole_tree_of_biomes_becomes_tables) {
    const gtt_tree_t tree = gtt_tree_make(t, "biomes_good");
    gtt_biomes_make(t, &tree,
                    GTT_LAND_START "priority: 3\nclimate: { temperature: [-0.5, 0.25], height: [10, 90] }\n"
                                   "surface:\n  top: stone\n  filler: stone\n  filler_depth: 5\n"
                                   "  steep: { block: stone, from: 1.5 }\n  high: { block: stone, from: 140 }\n"
                                   "  bands: { from: 40, depth: 14 }\n"
                                   "features: { tree_oak: 0.5, cactus: 2 }\n");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    uint64_t length = 0u;
    const char *const source = gtt_read(t, gtt_path(t, tree.borrowed_out, "biomes/index.c"), &length);
    const char *const header = gtt_read(t, gtt_path(t, tree.borrowed_out, "biomes/index.h"), &length);
    CHECK_CONTAINS(header, "BIOME_ID_LAND_ONE");
    CHECK_CONTAINS(header, "#define BIOME_COUNT 7u");
    CHECK_CONTAINS(header, "#define BIOME_UNDERGROUND_COUNT 1u");
    /* As written, never rounded. */
    CHECK_CONTAINS(source, ".temperature = {-5e-1f32, 25e-2f32}");
    CHECK_CONTAINS(source, ".height = {10e0f32, 90e0f32}");
    CHECK_CONTAINS(source, ".height = {-10e0f32, 10e0f32}");
    CHECK_CONTAINS(source, ".top = BLOCK_ID_STONE");
    CHECK_CONTAINS(source, ".steep_from = 15e-1f32");
    CHECK_CONTAINS(source, ".high_from = 140, .bands_from = 40");
    CHECK_CONTAINS(source, ".filler_depth = 5");
    CHECK_CONTAINS(source, ".bands_depth = 14");
    CHECK_CONTAINS(source, ".where = BIOME_WHERE_LAND, .priority = 3");
    CHECK_CONTAINS(source, "[BIOME_FEATURE_TREE_OAK] = 5e-1f32, [BIOME_FEATURE_CACTUS] = 2e0f32");
    CHECK_CONTAINS(source, "BIOME_ID_UNDER_ONE,\n");
}

static const gtt_bad_case_t GTT_BAD_BIOMES[] = {
    {GTT_LAND_START "colour: green\n" GTT_LAND_SURFACE, ":3:", "unknown field 'colour' in a biome"},
    {GTT_LAND_START "climate: { wetness: [0, 1] }\n" GTT_LAND_SURFACE, ":3:", "unknown field 'wetness' in climate"},
    {GTT_LAND_START "surface: { top: stone, filler: stone, shiny: stone }\n",
     ":3:", "unknown field 'shiny' in surface"},
    {GTT_LAND_START GTT_LAND_SURFACE "features: { dragons: 1 }\n", ":4:", "unknown field 'dragons' in features"},
    {"name: other\nwhere: land\n" GTT_LAND_SURFACE, ":1:", "is not its directory's"},
    {"name: land_one\n" GTT_LAND_SURFACE, ":1:", "missing field 'where'"},
    {"name: land_one\nwhere: moon\n" GTT_LAND_SURFACE, ":2:", "unknown where 'moon'"},
    {GTT_LAND_START, ":1:", "missing field 'surface'"},
    {GTT_LAND_START "surface: { top: stone }\n", ":3:", "surface must have top and filler"},
    {GTT_LAND_START "surface: { top: granite_ghost, filler: stone }\n", ":3:", "block 'granite_ghost' does not exist"},
    {GTT_LAND_START "climate: { temperature: [0.5, 0.5] }\n" GTT_LAND_SURFACE, ":3:", "with low below high"},
    {GTT_LAND_START "climate: { temperature: [0.5] }\n" GTT_LAND_SURFACE, ":3:", "must be [low, high]"},
    {GTT_LAND_START "climate: { temperature: [0, 1.5] }\n" GTT_LAND_SURFACE, ":3:", "temperature is '1.5'"},
    {GTT_LAND_START "climate: { z: [0, 1] }\n" GTT_LAND_SURFACE, ":3:", "only an underground biome goes by z"},
    {GTT_LAND_START "cave: { floor: stone, ceiling: stone }\n" GTT_LAND_SURFACE,
     ":3:", "only an underground biome has cave"},
    {GTT_LAND_START "priority: 300\n" GTT_LAND_SURFACE, ":3:", "priority is 300"},
    {GTT_LAND_START "surface: { top: stone, filler: stone, filler_depth: 12, under: stone, under_depth: 4 }\n",
     ":3:", "over 16 layers"},
    {GTT_LAND_START "surface: { top: stone, filler: stone, wet_top: stone }\n",
     ":3:", "wet_top and wet_depth go together"},
    {GTT_LAND_START "surface: { top: stone, filler: stone, under_depth: 2 }\n",
     ":3:", "under and under_depth go together"},
    {GTT_LAND_START "surface: { top: stone, filler: stone, steep: { block: stone, from: 0 } }\n",
     ":3:", "steep from must be above 0"},
    {GTT_LAND_START "surface: { top: stone, filler: stone, bands: { from: 3, depth: 0 } }\n",
     ":3:", "bands depth must be above 0"},
    {GTT_LAND_START "surface:\n  top: stone\n  filler: stone\n  patches:\n    - { block: stone, from: 0.5, to: 0.5 }\n",
     ":7:", "a patch's from must be below its to"},
    {GTT_LAND_START GTT_LAND_SURFACE "features: { cactus: 257 }\n", ":4:", "cactus is '257'"},
};

TEST(each_bad_biome_is_refused_at_its_line_and_nothing_is_written) {
    const gtt_tree_t tree = gtt_tree_make(t, "biomes_bad");
    for (uint32_t i = 0u; i < sizeof(GTT_BAD_BIOMES) / sizeof(GTT_BAD_BIOMES[0]); i++) {
        gtt_biomes_make(t, &tree, GTT_BAD_BIOMES[i].text);
        test_process_t run;
        gtt_tree_run(t, &tree, &run);
        CHECK_EQ_I64(run.exit_code, 1);
        CHECK_CONTAINS(run.output, GTT_BAD_BIOMES[i].position);
        CHECK_CONTAINS(run.output, GTT_BAD_BIOMES[i].message);
        CHECK_CONTAINS(run.output, "nothing was written");
    }
    struct stat info;
    CHECK(stat(gtt_path(t, tree.borrowed_out, "biomes/index.c"), &info) != 0);
}

TEST(a_kind_of_place_with_no_biome_is_refused) {
    const gtt_tree_t tree = gtt_tree_make(t, "biomes_hole");
    gtt_biomes_make(t, &tree, GTT_LAND_START GTT_LAND_SURFACE);
    unlink(gtt_path(t, tree.borrowed_config, "biomes/river_one/index.yaml"));
    rmdir(gtt_path(t, tree.borrowed_config, "biomes/river_one"));
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "no biome has where: river");
}

/* Mobs: a tree with a whole mob, into which each mistake goes one at a time. */
#define GTT_MOB_HEAD "name: pebble\ntexture: mobs/pebble\ntexture_width: 64\ntexture_height: 32\n"
#define GTT_MOB_BODY "health: 20\nspeed: 0.1\nhalf_width: 0.3\nheight: 1.9\n"
#define GTT_MOB_DROPS "drops: [{ item: coal, count: 0, max: 2 }]\n"
#define GTT_MOB_SPAWN                                                                                                  \
    "spawn: { weight: 1, pack_min: 1, pack_max: 2, max_light: 7, max_block_light: 0, min_distance: 24, never_on: "     \
    "[stone] }\n"
#define GTT_MOB_DESPAWN "despawn: { near: 32, far: 128, idle_ticks: 600, one_in: 800 }\n"
#define GTT_MOB_PARTS                                                                                                  \
    "parts:\n  - { size: [1, 2, 3], pivot: [0, -1.5, 3], offset: [0, 0, -1], uv: [8, 16], moves: arm }\n"
#define GTT_MOB_GOOD GTT_MOB_HEAD GTT_MOB_BODY GTT_MOB_DROPS GTT_MOB_SPAWN GTT_MOB_DESPAWN GTT_MOB_PARTS

static const gtt_bad_case_t GTT_BAD_MOBS[] = {
    {GTT_MOB_GOOD "colour: grey\n", ":14:", "unknown field 'colour' in a mob"},
    {"texture: mobs/pebble\n", ":1:", "missing field 'name'"},
    {"name: rock\n", ":1:", "is not its directory's"},
    {"name: pebble\ntexture: items/coal\n", ":2:", "is not of the form mobs/<name>"},
    {"name: pebble\ntexture: mobs/nothing\n", ":2:", "texture 'mobs/nothing' does not exist"},
    {GTT_MOB_GOOD "health: 1\n", ":14:", "duplicate key"},
    {"name: pebble\nhealth: 0\n", ":2:", "health '0' must be above 0, up to 1024"},
    {"name: pebble\nspeed: 2\n", ":2:", "speed '2' must be above 0, up to 1"},
    {"name: pebble\nhalf_width: 3\n", ":2:", "half_width '3' must be above 0, up to 2"},
    {"name: pebble\nheight: -1\n", ":2:", "height '-1' must be above 0, up to 4"},
    {"name: pebble\nheight: 0.0000000001\n", ":2:", "with at most 9 decimals"},
    {"name: pebble\ntexture_width: 0\n", ":2:", "texture_width is 0"},
    {"name: pebble\ndrops: { item: coal }\n", ":2:", "drops must be a list"},
    {"name: pebble\ndrops: [coal]\n", ":2:", "a drop must be a mapping"},
    {"name: pebble\ndrops: [{ item: nothing, count: 1 }]\n", ":2:", "drop item 'nothing' does not exist"},
    {"name: pebble\ndrops: [{ count: 1 }]\n", ":2:", "missing field 'item' in a drop"},
    {"name: pebble\ndrops: [{ item: coal, count: 65 }]\n", ":2:", "count is 65"},
    {"name: pebble\ndrops: [{ item: coal, count: 2, max: 1 }]\n", ":2:", "drop max is below its count"},
    {"name: pebble\ndrops: [{ item: coal, count: 0 }]\n", ":2:", "can never give anything"},
    {"name: pebble\ndrops: [{ item: coal, count: 1, chance: 0 }]\n", ":2:", "must be above 0"},
    {"name: pebble\ndrops: [{ item: coal, count: 1, chance: 1.5 }]\n", ":2:", "must be above 0"},
    {"name: pebble\ndrops: [{ item: coal, count: 1, shine: 1 }]\n", ":2:", "unknown field 'shine' in a drop"},
    {"name: pebble\nspawn: 3\n", ":2:", "spawn must be a mapping"},
    {"name: pebble\nspawn: { sparkle: 1 }\n", ":2:", "unknown field 'sparkle' in spawn"},
    {"name: pebble\nspawn: { weight: 1001 }\n", ":2:", "weight is 1001"},
    {"name: pebble\nspawn: { pack_min: 0 }\n", ":2:", "pack_min is 0"},
    {"name: pebble\nspawn: { pack_min: 3, pack_max: 2 }\n", ":2:", "pack_max is below pack_min"},
    {"name: pebble\nspawn: { max_light: 16 }\n", ":2:", "max_light is 16"},
    {"name: pebble\nspawn: { max_block_light: 16 }\n", ":2:", "max_block_light is 16"},
    {"name: pebble\nspawn: { min_distance: 129 }\n", ":2:", "min_distance is 129"},
    {"name: pebble\nspawn: { never_on: stone }\n", ":2:", "never_on must be a list"},
    {"name: pebble\nspawn: { never_on: [nothing] }\n", ":2:", "never_on block 'nothing' does not exist"},
    {"name: pebble\nspawn:\n  weight: 1\n", ":2:", "missing field 'pack_min'"},
    {"name: pebble\ndespawn: 3\n", ":2:", "despawn must be a mapping"},
    {"name: pebble\ndespawn: { rust: 1 }\n", ":2:", "unknown field 'rust' in despawn"},
    {"name: pebble\ndespawn: { near: 32, far: 32 }\n", ":2:", "far must be above near"},
    {"name: pebble\nspawn: { min_distance: 24 }\ndespawn: { near: 8, far: 24 }\n",
     ":3:", "far must be above spawn's min_distance"},
    {"name: pebble\ndespawn: { one_in: 0 }\n", ":2:", "one_in is 0"},
    {"name: pebble\ndespawn: { idle_ticks: -1 }\n", ":2:", "idle_ticks is -1"},
    {"name: pebble\nparts: []\n", ":2:", "parts must be a list of 1 to 16"},
    {"name: pebble\nparts: 3\n", ":2:", "parts must be a list of 1 to 16"},
    {"name: pebble\nparts: [a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q]\n",
     ":2:", "parts must be a list of 1 to 16"},
    {"name: pebble\nparts: [leg]\n", ":2:", "a part must be a mapping"},
    {"name: pebble\nparts: [{ wing: 1 }]\n", ":2:", "unknown field 'wing' in a part"},
    {"name: pebble\nparts: [{ pivot: [0, 0, 0] }]\n", ":2:", "missing field 'size' in a part"},
    {"name: pebble\nparts: [{ size: [1, 2] }]\n", ":2:", "size must be a list of 3 numbers"},
    {"name: pebble\nparts: [{ size: [1, 0, 3] }]\n", ":2:", "size '0' must be above 0"},
    {"name: pebble\nparts: [{ pivot: [65, 0, 0] }]\n", ":2:", "pivot '65' must be of a size up to 64"},
    {"name: pebble\nparts: [{ uv: [1] }]\n", ":2:", "uv must be a list of 2 numbers"},
    {"name: pebble\nparts: [{ uv: [1, 2000] }]\n", ":2:", "uv is 2000"},
    {"name: pebble\nparts: [{ moves: tail }]\n", ":2:", "unknown moves 'tail'"},
    {"name: pebble\nparts: [{ walk_swing: 8 }]\n", ":2:", "walk_swing '8' must be of a size up to 7"},
    {"name: pebble\nparts: [{ fleece: woolly }]\n", ":2:", "true or false"},
    {"- just\n- a list\n", ":1:", "a mob's file must be a mapping"},
    /* Plan 005 phase 4: what an animal needs. */
    {"name: pebble\nmind: angry\n", ":2:", "unknown mind 'angry'"},
    {"name: pebble\nmind: passive\ndespawn: { near: 32 }\n", ":3:", "a passive mob never despawns"},
    {"name: pebble\nspawn: { min_light: 16 }\n", ":2:", "min_light is 16"},
    {"name: pebble\nspawn: { max_light: 7, min_light: 9 }\n", ":2:", "min_light is above max_light"},
    {"name: pebble\nspawn: { on: stone }\n", ":2:", "on must be a list"},
    {"name: pebble\nspawn: { on: [nothing] }\n", ":2:", "on block 'nothing' does not exist"},
    {"name: pebble\nspawn: { biomes: [nowhere] }\n", ":2:", "biome 'nowhere' does not exist"},
    {"name: pebble\nspawn: { min_height: 2000 }\n", ":2:", "min_height is 2000"},
    {"name: pebble\nspawn: { min_height: 10, max_height: 5 }\n", ":2:", "max_height is below min_height"},
    {"name: pebble\nmale: { name: ram, scale: 1.2 }\n", ":2:", "male has no female"},
    {"name: pebble\nfemale: { name: ewe, scale: 1 }\n", ":2:", "female has no male"},
    {"name: pebble\nmale: 3\nfemale: { name: ewe, scale: 1 }\n", ":2:", "male must be a mapping"},
    {"name: pebble\nmale: { scale: 1 }\nfemale: { name: ewe, scale: 1 }\n", ":2:", "missing field 'name' in male"},
    {"name: pebble\nmale: { name: ram, scale: 1 }\nfemale: { name: ewe }\n", ":3:", "missing field 'scale' in female"},
    {"name: pebble\nmale: { name: Ram, scale: 1 }\nfemale: { name: ewe, scale: 1 }\n", ":2:", "is not lower case"},
    {"name: pebble\nmale: { name: ram, scale: 3 }\nfemale: { name: ewe, scale: 1 }\n",
     ":2:", "scale '3' must be above 0, up to 2"},
    {"name: pebble\nmale: { name: ram, scale: 1, horns: 2 }\nfemale: { name: ewe, scale: 1 }\n",
     ":2:", "unknown field 'horns' in male"},
    {"name: pebble\nmale: { name: ram, scale: 1, drops: [{ item: nothing, count: 1 }] }\n",
     ":2:", "drop item 'nothing' does not exist"},
    {"name: pebble\nparts: [{ uv: [0, 0] }]\nmale: { name: ram, scale: 1, parts: [a, b, c, d, e, f, g, h, i, j, k, l, "
     "m, n, o, p] }\n",
     ":3:", "the mob's 1 parts and male's 16 are over 16"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: 3 }\n", ":2:", "yields must be a list"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [milk] }\n", ":2:", "a yield must be a mapping"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [{ count: 1 }] }\n",
     ":2:", "missing field 'item' in a yield"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [{ item: nothing }] }\n",
     ":2:", "yield item 'nothing' does not exist"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [{ item: coal, using: nothing }] }\n",
     ":2:", "yield using 'nothing' does not exist"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [{ item: coal, count: 0 }] }\n", ":2:", "count is 0"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [{ item: coal, count: 2, max: 1 }] }\n",
     ":2:", "yield max is below its count"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [{ item: coal, every: 0 }] }\n", ":2:", "every is 0"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [{ item: coal, shine: 1 }] }\n",
     ":2:", "unknown field 'shine' in a yield"},
    {"name: pebble\nfemale: { name: ewe, scale: 1, yields: [a, b, c, d, e] }\n", ":2:", "over 4 yields"},
};
enum { GTT_BAD_MOB_COUNT = sizeof(GTT_BAD_MOBS) / sizeof(GTT_BAD_MOBS[0]) };

static void gtt_mob_tree(test_context_t *t, const gtt_tree_t *tree) {
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, "mobs/pebble"));
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_textures, "mobs/pebble"));
    gtt_write(t, gtt_path(t, tree->borrowed_textures, "mobs/pebble/index.mctex"), "");
}

TEST(each_bad_mob_is_refused_at_its_line_and_nothing_is_written) {
    const gtt_tree_t tree = gtt_tree_make(t, "bad_mobs");
    gtt_mob_tree(t, &tree);
    char *const file = gtt_path(t, tree.borrowed_config, "mobs/pebble/index.yaml");
    char *const table = gtt_path(t, tree.borrowed_out, "mobs/index.h");
    for (uint32_t i = 0u; i < GTT_BAD_MOB_COUNT; i++) {
        gtt_write(t, file, GTT_BAD_MOBS[i].text);
        test_process_t run;
        gtt_tree_run(t, &tree, &run);
        CHECK_EQ_I64(run.exit_code, 1);
        CHECK_CONTAINS(run.output, GTT_BAD_MOBS[i].position);
        CHECK_CONTAINS(run.output, GTT_BAD_MOBS[i].message);
        CHECK(access(table, F_OK) != 0);
    }
}

TEST(a_good_mob_makes_tables_with_row_0_for_none_and_a_tree_with_none_needs_none) {
    const gtt_tree_t tree = gtt_tree_make(t, "good_mobs");
    char *const header = gtt_path(t, tree.borrowed_out, "mobs/index.h");
    char *const source = gtt_path(t, tree.borrowed_out, "mobs/index.c");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK(access(header, F_OK) != 0);

    gtt_mob_tree(t, &tree);
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "missing");

    gtt_write(t, gtt_path(t, tree.borrowed_config, "mobs/pebble/index.yaml"), GTT_MOB_GOOD);
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    uint64_t length = 0u;
    const char *const declared = gtt_read(t, header, &length);
    CHECK_CONTAINS(declared, "MOB_ID_NONE = 0");
    CHECK_CONTAINS(declared, "MOB_ID_PEBBLE = 1");
    CHECK_CONTAINS(declared, "#define MOB_COUNT 2u");
    CHECK_CONTAINS(declared, "#define MOB_PART_COUNT 2u");
    CHECK_CONTAINS(declared, "#define MOB_PEBBLE_FIRST_PART 1u");
    CHECK_CONTAINS(declared, "#define MOB_PEBBLE_PART_COUNT 1u");
    const char *const defined = gtt_read(t, source, &length);
    CHECK_CONTAINS(defined, ".first_drop = 1, .first_never_on = 1, .first_part = 1");
    CHECK_CONTAINS(defined, ".item = ITEM_ID_COAL, .count_min = 0, .count_max = 2");
    CHECK_CONTAINS(defined, "BLOCK_ID_STONE,");
    CHECK_CONTAINS(defined, ".pivot = {0e0f32, -15e-1f32, 3e0f32}");
    CHECK_CONTAINS(defined, ".flags = MOB_PART_MOVES_ARM");

    /* A directory of them with nothing in it is a mistake too, and it must be told apart from no directory. */
    const gtt_tree_t empty = gtt_tree_make(t, "empty_mobs");
    gtt_mkdirs(t, gtt_path(t, empty.borrowed_config, "mobs"));
    gtt_tree_run(t, &empty, &run);
    CHECK_EQ_I64(run.exit_code, 1);
    CHECK_CONTAINS(run.output, "there must be a mob");
}

/* An animal: passive, by day on stone in land_one, with a ram and a ewe, each with its own rows. */
#define GTT_ANIMAL                                                                                                     \
    GTT_MOB_HEAD GTT_MOB_BODY GTT_MOB_DROPS "mind: passive\n"                                                          \
                                            "spawn: { weight: 5, pack_min: 2, pack_max: 4, max_light: 15, "            \
                                            "max_block_light: 15, min_light: 9, min_distance: 24, on: [stone], "       \
                                            "biomes: [land_one], min_height: 40, max_height: 900 }\n" GTT_MOB_PARTS    \
                                            "male:\n  name: ram\n  scale: 1.25\n  parts:\n"                            \
                                            "    - { size: [1, 1, 4], pivot: [0, 0, 3], offset: [0, 1, 0], uv: [0, "   \
                                            "0], moves: head }\n"                                                      \
                                            "  drops: [{ item: stone, count: 1 }]\n"                                   \
                                            "female:\n  name: ewe\n  scale: 1\n"                                       \
                                            "  yields:\n    - { item: coal, count: 1, max: 3, using: stone, every: "   \
                                            "6000 }\n    - { item: coal, count: 1, every: 600 }\n"

TEST(an_animal_makes_its_sexes_rows_and_its_rules) {
    const gtt_tree_t tree = gtt_tree_make(t, "animal_mobs");
    gtt_biomes_make(t, &tree, GTT_LAND_START GTT_LAND_SURFACE);
    gtt_mob_tree(t, &tree);
    gtt_write(t, gtt_path(t, tree.borrowed_config, "mobs/pebble/index.yaml"), GTT_ANIMAL);
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    REQUIRE(run.exit_code == 0);
    uint64_t length = 0u;
    const char *const declared = gtt_read(t, gtt_path(t, tree.borrowed_out, "mobs/index.h"), &length);
    CHECK_CONTAINS(declared, "#define MOB_PART_COUNT 3u");
    CHECK_CONTAINS(declared, "#define MOB_DROP_COUNT 3u");
    CHECK_CONTAINS(declared, "#define MOB_SEX_ROW_COUNT 3u");
    CHECK_CONTAINS(declared, "#define MOB_YIELD_COUNT 3u");
    CHECK_CONTAINS(declared, "#define MOB_ON_COUNT 2u");
    CHECK_CONTAINS(declared, "#define MOB_BIOME_COUNT 2u");
    CHECK_CONTAINS(declared, "#define MOB_PARTS_MAX 16u");
    CHECK_CONTAINS(declared, "#define MOB_YIELDS_MAX 4u");
    CHECK_CONTAINS(declared, "#define MOB_MIND_PASSIVE 1u");
    CHECK_CONTAINS(declared, "#define MOB_SEX_FEMALE 2u");
    CHECK_CONTAINS(declared, "#define MOB_PEBBLE_PART_COUNT 1u");
    const char *const defined = gtt_read(t, gtt_path(t, tree.borrowed_out, "mobs/index.c"), &length);
    CHECK_CONTAINS(defined, ".despawn_far = 0");
    CHECK_CONTAINS(defined, ".min_light = 9, .mind = MOB_MIND_PASSIVE, .on_count = 1, .first_on = 1, .first_biome = 1, "
                            ".min_height = 40, .max_height = 900, .first_sex = 1, .biome_count = 1");
    CHECK_CONTAINS(defined, "BIOME_ID_LAND_ONE,");
    CHECK_CONTAINS(defined, "{.name = \"ram\", .scale = 125e-2f32, .first_part = 2, .first_drop = 2, .first_yield = 1, "
                            ".part_count = 1, .drop_count = 1, .yield_count = 0, .sex = 1u}");
    CHECK_CONTAINS(defined, "{.name = \"ewe\", .scale = 1e0f32, .first_part = 3, .first_drop = 3, .first_yield = 1, "
                            ".part_count = 0, .drop_count = 0, .yield_count = 2, .sex = 2u}");
    CHECK_CONTAINS(defined,
                   "{.every = 6000, .item = ITEM_ID_COAL, .tool = ITEM_ID_STONE, .count_min = 1, .count_max = 3}");
    CHECK_CONTAINS(defined,
                   "{.every = 600, .item = ITEM_ID_COAL, .tool = ITEM_ID_NONE, .count_min = 1, .count_max = 1}");
    CHECK_CONTAINS(defined, ".item = ITEM_ID_STONE, .count_min = 1, .count_max = 1}");
    /* A hostile mob, as before, has no sexes, every height and the hostile mind. */
    gtt_write(t, gtt_path(t, tree.borrowed_config, "mobs/pebble/index.yaml"), GTT_MOB_GOOD);
    gtt_tree_run(t, &tree, &run);
    REQUIRE(run.exit_code == 0);
    const char *const plain = gtt_read(t, gtt_path(t, tree.borrowed_out, "mobs/index.c"), &length);
    CHECK_CONTAINS(plain, ".min_light = 0, .mind = MOB_MIND_HOSTILE, .on_count = 0");
    CHECK_CONTAINS(plain, ".min_height = -1250, .max_height = 1250, .first_sex = 0, .biome_count = 0");
}

/* Ores: the tree's stone, which drops coal, as an ore, and a second ore of two rows. Each mistake replaces the first file. */
#define GTT_ORE_START "name: seam\nblock: stone\n"
#define GTT_ORE_ROW "order: 20\nz_min: -100\nz_max: 50\nvein: 6\nper_256_chunks: 1024\n"

static void gtt_ores_make(test_context_t *t, const gtt_tree_t *tree, const char *seam) {
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, "ores/seam"));
    gtt_mkdirs(t, gtt_path(t, tree->borrowed_config, "ores/layered"));
    gtt_write(t, gtt_path(t, tree->borrowed_config, "ores/seam/index.yaml"), seam);
    gtt_write(
        t, gtt_path(t, tree->borrowed_config, "ores/layered/index.yaml"),
        "name: layered\nblock: stone\ndeep_block: stone\nveins:\n"
        "  - { row: layered_low, order: 30, z_min: -250, z_max: 0, vein: 4, per_256_chunks: 300, spread: triangle }\n"
        "  - { row: layered_high, order: 10, z_min: 1, z_max: 900, vein: 9, per_256_chunks: 5, "
        "min_column_height: 120 }\n");
}

TEST(a_whole_tree_of_ores_becomes_a_table_in_order) {
    const gtt_tree_t tree = gtt_tree_make(t, "ores_good");
    gtt_ores_make(t, &tree, GTT_ORE_START "drops: coal\n" GTT_ORE_ROW "spread: triangle\n");
    test_process_t run;
    gtt_tree_run(t, &tree, &run);
    CHECK_EQ_I64(run.exit_code, 0);
    CHECK_STR_EQ(run.output, "");
    uint64_t length = 0;
    const char *const source = gtt_read(t, gtt_path(t, tree.borrowed_out, "ores/index.c"), &length);
    const char *const header = gtt_read(t, gtt_path(t, tree.borrowed_out, "ores/index.h"), &length);
    REQUIRE(source != nullptr);
    REQUIRE(header != nullptr);
    CHECK_CONTAINS(header, "#define ORE_ROW_COUNT 3u");
    const char *const high = strstr(source, "\"ore/layered_high\", .per_chunk_256 = 5u, .z_low = 1, .z_high = 900, "
                                            ".min_column_height = 120, .block = BLOCK_ID_STONE, .deep_block = "
                                            "BLOCK_ID_STONE, .size = 9u, .spread = ORE_SPREAD_UNIFORM}");
    const char *const seam = strstr(source, "\"ore/seam\", .per_chunk_256 = 1024u, .z_low = -100, .z_high = 50, "
                                            ".min_column_height = ORE_ANY_COLUMN, .block = BLOCK_ID_STONE, "
                                            ".deep_block = BLOCK_ID_STONE, .size = 6u, .spread = ORE_SPREAD_TRIANGLE}");
    const char *const low = strstr(source, "\"ore/layered_low\", .per_chunk_256 = 300u, .z_low = -250, .z_high = 0, ");
    CHECK(high != nullptr && seam != nullptr && low != nullptr);
    CHECK(high < seam && seam < low); /* by order, not by file */
    CHECK_CONTAINS(source, "/* config/ores/seam, order 20 */");
}

static const gtt_bad_case_t GTT_BAD_ORES[] = {
    {GTT_ORE_START "colour: blue\n" GTT_ORE_ROW, ":3:", "unknown field 'colour' in an ore"},
    {"name: other\nblock: stone\n" GTT_ORE_ROW, ":1:", "is not its directory's"},
    {"name: seam\n" GTT_ORE_ROW, ":1:", "missing field 'block'"},
    {GTT_ORE_START "deep_block: deepslate_ghost\n" GTT_ORE_ROW, ":3:", "block 'deepslate_ghost' does not exist"},
    {GTT_ORE_START "drops: nothing\n" GTT_ORE_ROW, ":3:", "item 'nothing' does not exist"},
    {GTT_ORE_START "drops: stone\n" GTT_ORE_ROW, ":3:", "drops 'stone', and block 'stone' does not drop it"},
    {GTT_ORE_START "z_min: -100\nz_max: 50\nvein: 6\nper_256_chunks: 1024\n", ":1:", "missing field 'order'"},
    {GTT_ORE_START "order: 20\nz_min: 50\nz_max: 50\nvein: 6\nper_256_chunks: 1024\n",
     ":1:", "z_min 50 must be below z_max 50"},
    {GTT_ORE_START "order: 20\nz_min: -1300\nz_max: 50\nvein: 6\nper_256_chunks: 1024\n", ":4:", "z_min is -1300"},
    {GTT_ORE_START "order: 20\nz_min: -100\nz_max: 50\nvein: 0\nper_256_chunks: 1024\n", ":6:", "vein is 0"},
    {GTT_ORE_START "order: 20\nz_min: -100\nz_max: 50\nvein: 256\nper_256_chunks: 1024\n", ":6:", "vein is 256"},
    {GTT_ORE_START GTT_ORE_ROW "spread: lumpy\n", ":8:", "unknown spread 'lumpy'"},
    {GTT_ORE_START GTT_ORE_ROW "min_column_height: 2000\n", ":8:", "min_column_height is 2000"},
    {GTT_ORE_START "order: 30\nz_min: -100\nz_max: 50\nvein: 6\nper_256_chunks: 1024\n",
     ":3:", "order 30 is also row 'layered_low'"},
    {GTT_ORE_START "veins:\n  - { row: layered_low, order: 40, z_min: 0, z_max: 9, vein: 1, per_256_chunks: 1 }\n",
     ":4:", "row 'layered_low' is also in"},
    {GTT_ORE_START "order: 20\nveins:\n  - { row: a, order: 40, z_min: 0, z_max: 9, vein: 1, per_256_chunks: 1 }\n",
     ":3:", "'order' goes in each of veins"},
    {GTT_ORE_START "veins: []\n", ":3:", "veins must be a list of at least one"},
    {GTT_ORE_START "veins:\n  - just_a_name\n", ":4:", "a vein must be a mapping"},
    {GTT_ORE_START "veins:\n  - { order: 40, z_min: 0, z_max: 9, vein: 1, per_256_chunks: 1 }\n",
     ":4:", "missing field 'row'"},
    {GTT_ORE_START "veins:\n  - { row: a, order: 40, z_min: 0, z_max: 9, vein: 1, per_256_chunks: 1, depth: 3 }\n",
     ":4:", "unknown field 'depth' in a vein"},
    {"- a\n- list\n", ":1:", "an ore's file must be a mapping"},
};

TEST(each_bad_ore_is_refused_at_its_line_and_nothing_is_written) {
    const gtt_tree_t tree = gtt_tree_make(t, "ores_bad");
    for (uint32_t i = 0u; i < sizeof(GTT_BAD_ORES) / sizeof(GTT_BAD_ORES[0]); i++) {
        gtt_ores_make(t, &tree, GTT_BAD_ORES[i].text);
        test_process_t run;
        gtt_tree_run(t, &tree, &run);
        CHECK_EQ_I64(run.exit_code, 1);
        CHECK_CONTAINS(run.output, GTT_BAD_ORES[i].position);
        CHECK_CONTAINS(run.output, GTT_BAD_ORES[i].message);
        CHECK_CONTAINS(run.output, "nothing was written");
    }
    struct stat info;
    CHECK(stat(gtt_path(t, tree.borrowed_out, "ores/index.c"), &info) != 0);
}

TEST(a_bad_command_line_says_how) {
    char *const argv[] = {(char *)gtt_arg(t, 0), (char *)"--config", (char *)"x", nullptr};
    test_process_t run;
    test_process_run(t, argv, GTT_TIMEOUT_MS, &run);
    CHECK_EQ_I64(run.exit_code, 2);
    CHECK_CONTAINS(run.output, "usage:");
}

TEST_MAIN()
