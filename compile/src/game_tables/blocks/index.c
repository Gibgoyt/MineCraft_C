/*
 * config/blocks/ -> the block tables. See README.md. It works in three steps: read and check each file on its own, resolve
 * what the files name in one another, and emit. A problem does not stop the reading, so one build shows all of them.
 */
#include "game_tables/blocks/index.h"

#include "types/block_state/index.h"
#include "types/sound_blob/index.h"
#include "yaml/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* What a voxel has room for beside its state: run/include/types/block_state/. */
#define GB_MAX_BLOCKS 4096u
static_assert(GB_MAX_BLOCKS == BLOCK_STATE_ID_LIMIT);
/* As compile/src/game_tables/items/ holds it to. */
#define GB_MAX_ITEMS 4096u
#define GB_MAX_NAME_BYTES 64u
#define GB_MAX_FILE_BYTES (1ull << 20)
#define GB_MAX_DROPS_PER_BLOCK 16u
#define GB_MAX_DROP_COUNT 64
#define GB_MAX_LIGHT 15
#define GB_MAX_HARDNESS 1000
/* A finer decimal than this is a typo. It also keeps a value from reaching the compiler as a float that rounds to zero. */
#define GB_MIN_EXPONENT (-6)
/* More than the real tables need by far: 2 MiB of text is about 8000 blocks. */
#define GB_HEADER_BYTES (1ull << 21)
#define GB_SOURCE_BYTES (1ull << 22)
#define GB_FACE_COUNT 6u
#define GB_AIR "air"

/* The order is the table's: bit 0 is the positive side of an axis, so a ray's step picks a face with no branch. */
typedef enum gb_face_t : uint8_t {
    GB_FACE_WEST = 0,   /* -x */
    GB_FACE_EAST = 1,   /* +x */
    GB_FACE_SOUTH = 2,  /* -y */
    GB_FACE_NORTH = 3,  /* +y: y is north, as everywhere */
    GB_FACE_BOTTOM = 4, /* -z */
    GB_FACE_TOP = 5,    /* +z */
} gb_face_t;

static const char *const GB_FACE_NAMES[GB_FACE_COUNT] = {"west", "east", "south", "north", "bottom", "top"};

enum { GB_SHAPE_NONE = 0, GB_SHAPE_CUBE = 1, GB_SHAPE_FLUID = 8 };
enum { GB_SHAPE_DOOR = 10 };
static const char *const GB_SHAPE_NAMES[] = {"none",  "cube", "slab",  "stairs",     "cross", "crop",
                                             "torch", "thin", "fluid", "torch_wall", "door"};
enum { GB_SHAPE_COUNT = sizeof(GB_SHAPE_NAMES) / sizeof(GB_SHAPE_NAMES[0]) };

/* What a shape means when a file says nothing else. Air is the only shape without a texture. */
typedef struct gb_shape_t {
    uint8_t solid;       /* 0: a body collides with it */
    uint8_t opacity;     /* 1: block light lost passing through, 15 stops it */
    uint8_t replaceable; /* 2: placing a block here overwrites it */
} gb_shape_t;
static_assert(sizeof(gb_shape_t) == 3);

static const gb_shape_t GB_SHAPES[GB_SHAPE_COUNT] = {
    {0, 0, 1},
    {1, 15, 0},
    {1, 0, 0},
    {1, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 1},
    {0, 0, 0},
    /* A door: not solid as a block is, because whether it has a body goes by its state, shut or open: run/src/physics/collision/. */
    {0, 0, 0},
};

/* What a block's state may hold, beside its id: see run/include/types/block_state/. The names are printed into the generated
 * header in this order, as BLOCK_STATES_. */
#define GB_STATES_FACING 1u
#define GB_STATES_HALF 2u
#define GB_STATES_OPEN 4u
static const char *const GB_STATES_NAMES[] = {"FACING", "HALF", "OPEN"};
enum { GB_STATES_COUNT = sizeof(GB_STATES_NAMES) / sizeof(GB_STATES_NAMES[0]) };
static const char *const GB_FACINGS[] = {"none", "horizontal"};
enum { GB_FACING_COUNT = sizeof(GB_FACINGS) / sizeof(GB_FACINGS[0]) };
static const char *const GB_HALVES[] = {"none", "upper_lower"};
enum { GB_HALF_COUNT = sizeof(GB_HALVES) / sizeof(GB_HALVES[0]) };

enum { GB_TOOL_NONE = 0, GB_TOOL_SHEARS = 5 };
static const char *const GB_TOOLS[] = {"none", "pickaxe", "axe", "shovel", "hoe", "shears", "sword"};
enum { GB_TOOL_COUNT = sizeof(GB_TOOLS) / sizeof(GB_TOOLS[0]) };

enum { GB_TIER_NONE = 0 };
static const char *const GB_TIERS[] = {"none", "wood", "stone", "iron", "diamond"};
enum { GB_TIER_COUNT = sizeof(GB_TIERS) / sizeof(GB_TIERS[0]) };

/* Bits of a table row's flags. The names are printed into the generated header in this order. */
#define GB_FLAG_SOLID 1u
#define GB_FLAG_REPLACEABLE 2u
#define GB_FLAG_FALLS 4u
#define GB_FLAG_DECAYS 8u
#define GB_FLAG_UNBREAKABLE 16u
#define GB_FLAG_LOG 32u
#define GB_FLAG_MAP_COVER 64u
static const char *const GB_FLAG_NAMES[] = {"SOLID",       "REPLACEABLE", "FALLS",    "DECAYS",
                                            "UNBREAKABLE", "LOG",         "MAP_COVER"};
enum { GB_FLAG_COUNT = sizeof(GB_FLAG_NAMES) / sizeof(GB_FLAG_NAMES[0]) };

static const char *const GB_BLOCK_FIELDS[] = {
    "name",        "hardness", "tool", "tier",    "drops", "shape",  "light", "opacity", "solid",    "falls",
    "replaceable", "decays",   "log",  "texture", "sound", "facing", "half",  "open",    "map_cover"};
enum { GB_BLOCK_FIELD_COUNT = sizeof(GB_BLOCK_FIELDS) / sizeof(GB_BLOCK_FIELDS[0]) };

static const char *const GB_DROP_FIELDS[] = {"item", "count", "max", "chance"};
enum { GB_DROP_FIELD_COUNT = sizeof(GB_DROP_FIELDS) / sizeof(GB_DROP_FIELDS[0]) };

static const char *const GB_TEXTURE_FIELDS[] = {"all", "top", "bottom", "side", "north", "south", "east", "west"};
enum { GB_TEXTURE_FIELD_COUNT = sizeof(GB_TEXTURE_FIELDS) / sizeof(GB_TEXTURE_FIELDS[0]) };

/* One row of a drop list, as read. item is resolved once every block is known. */
typedef struct gb_drop_t {
    const char *borrowed_item; /* 0 */
    yaml_decimal_t chance;     /* 8 */
    uint32_t line;             /* 24: of the item, for an error found later */
    uint32_t column;           /* 28 */
    uint8_t count_min;         /* 32 */
    uint8_t count_max;         /* 33 */
    uint8_t _pad0[6];          /* 34 */
} gb_drop_t;
static_assert(sizeof(gb_drop_t) == 40);

/* One block, as read. Everything it points to is in the arena. */
typedef struct gb_block_t {
    const char *borrowed_name;                 /* 0: its directory's, so a broken file still has one */
    const char *borrowed_path;                 /* 8 */
    const char *borrowed_faces[GB_FACE_COUNT]; /* 16: texture names, nullptr for air */
    gb_drop_t *borrowed_drops;                 /* 64 */
    yaml_decimal_t hardness;                   /* 72 */
    uint32_t drop_count;                       /* 88 */
    uint8_t tool;                              /* 92 */
    uint8_t tier;                              /* 93 */
    uint8_t shape;                             /* 94 */
    uint8_t light;                             /* 95 */
    uint8_t opacity;                           /* 96 */
    uint8_t flags;                             /* 97 */
    uint8_t sound;                             /* 98: a sound_family_t */
    uint8_t states;                            /* 99: GB_STATES_ */
    uint8_t _pad0[4];                          /* 100 */
} gb_block_t;
static_assert(sizeof(gb_block_t) == 104);

/* A sorted set of names, so what the tables hold does not depend on the order of reading. */
typedef struct gb_set_t {
    const char **borrowed_items; /* 0 */
    uint32_t count;              /* 8 */
    uint32_t capacity;           /* 12 */
} gb_set_t;
static_assert(sizeof(gb_set_t) == 16);

typedef struct gb_context_t {
    tp_arena_t *borrowed_arena;    /* 0 */
    const char *borrowed_textures; /* 8 */
    const char *borrowed_items;    /* 16: <config>/items */
    const char *borrowed_path;     /* 24: the file being read, for reporting */
    uint32_t problems;             /* 32 */
    uint8_t _pad0[4];              /* 36 */
} gb_context_t;
static_assert(sizeof(gb_context_t) == 40);

[[gnu::format(printf, 5, 6)]]
static void gb_report_at(gb_context_t *context, const char *path, uint32_t line, uint32_t column, const char *format,
                         ...) {
    fprintf(stderr, "%s:%u:%u: ", path, line, column);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    context->problems++;
}

static void gb_report_yaml(gb_context_t *context, const yaml_error_t *error) {
    gb_report_at(context, context->borrowed_path, error->line, error->column, "%s", error->message);
}

/* The two shapes of a yaml node's text in a message. Neither is NUL-terminated. */
#define GB_KEY_TEXT(node) (int32_t)(node)->key_length, (const char *)(node)->borrowed_key
#define GB_TEXT(node) (int32_t)(node)->text_length, (const char *)(node)->borrowed_text

static bool gb_key_is(const yaml_node_t *node, const char *key) {
    const uint64_t length = strlen(key);
    return node->key_length == length && memcmp(node->borrowed_key, key, length) == 0;
}

/* Reports every key of map that the list does not hold. */
static void gb_reject_unknown(gb_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                              const char *what, const char *const *fields, uint32_t field_count) {
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        bool known = false;
        for (uint32_t i = 0; i < field_count && !known; i++) {
            known = gb_key_is(child, fields[i]);
        }
        if (!known) {
            gb_report_at(context, context->borrowed_path, child->line, child->column, "unknown field '%.*s' in %s",
                         GB_KEY_TEXT(child), what);
        }
    }
}

/* Lower case, digits and underscores, so it can be an enum name in the tables. */
static bool gb_name_is_valid(const char *name) {
    const uint64_t length = strlen(name);
    if (length == 0u || length > GB_MAX_NAME_BYTES || name[0] < 'a' || name[0] > 'z') {
        return false;
    }
    for (uint64_t i = 0; i < length; i++) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

/* A NUL-terminated copy of a scalar, or nullptr with the problem reported. */
static const char *gb_read_text(gb_context_t *context, const yaml_node_t *node) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        gb_report_yaml(context, &error);
        return nullptr;
    }
    return tp_arena_text(context->borrowed_arena, text, length);
}

/* Finds a scalar in a list of allowed words. An absent node leaves out alone. */
static void gb_read_choice(gb_context_t *context, const yaml_node_t *node, const char *what, const char *const *choices,
                           uint32_t choice_count, uint8_t *out) {
    if (node == nullptr) {
        return;
    }
    const char *text = gb_read_text(context, node);
    if (text == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < choice_count; i++) {
        if (strcmp(text, choices[i]) == 0) {
            *out = (uint8_t)i;
            return;
        }
    }
    gb_report_at(context, context->borrowed_path, node->line, node->column, "unknown %s '%s'", what, text);
}

/* An integer from minimum to maximum. An absent node leaves out alone. */
static void gb_read_small(gb_context_t *context, const yaml_node_t *node, const char *what, int64_t minimum,
                          int64_t maximum, uint8_t *out) {
    if (node == nullptr) {
        return;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        gb_report_yaml(context, &error);
    } else if (value < minimum || value > maximum) {
        gb_report_at(context, context->borrowed_path, node->line, node->column,
                     "%s is %" PRId64 ", and must be %" PRId64 " to %" PRId64, what, value, minimum, maximum);
    } else {
        *out = (uint8_t)value;
    }
}

/* Sets or clears a flag if the file says either. An absent node leaves it as the shape's default. */
static void gb_read_flag(gb_context_t *context, const yaml_node_t *node, uint8_t flag, uint8_t *flags) {
    if (node == nullptr) {
        return;
    }
    bool value = false;
    yaml_error_t error = {0};
    if (!yaml_get_bool(node, &value, &error)) {
        gb_report_yaml(context, &error);
    } else if (value) {
        *flags = (uint8_t)(*flags | flag);
    } else {
        *flags = (uint8_t)(*flags & ~flag);
    }
}

/* Whether a non-negative decimal is at most limit, and is no finer than GB_MIN_EXPONENT, by whole numbers only. */
static bool gb_decimal_fits(yaml_decimal_t value, int64_t limit) {
    int64_t mantissa = value.mantissa;
    int32_t exponent = value.exponent;
    if (mantissa < 0) {
        return false;
    }
    if (mantissa == 0) {
        return true;
    }
    while (exponent < 0 && mantissa % 10 == 0) {
        mantissa /= 10;
        exponent++;
    }
    if (exponent < GB_MIN_EXPONENT) {
        return false;
    }
    /* limit and -exponent are small, so neither of these overflows once the mantissa has been compared. */
    for (; exponent > 0; exponent--) {
        if (mantissa > limit) {
            return false;
        }
        mantissa *= 10;
    }
    for (; exponent < 0; exponent++) {
        limit *= 10;
    }
    return mantissa <= limit;
}

static void gb_read_hardness(gb_context_t *context, const yaml_node_t *node, gb_block_t *block) {
    yaml_error_t error = {0};
    if (yaml_text_equals(node, "unbreakable") && node->quoted == 0u) {
        block->flags |= GB_FLAG_UNBREAKABLE;
        block->hardness = (yaml_decimal_t){.mantissa = -1, .exponent = 0};
    } else if (!yaml_get_decimal(node, &block->hardness, &error)) {
        gb_report_yaml(context, &error);
    } else if (!gb_decimal_fits(block->hardness, GB_MAX_HARDNESS)) {
        gb_report_at(context, context->borrowed_path, node->line, node->column,
                     "hardness '%.*s' must be 0 to %d with at most %d decimals, or unbreakable", GB_TEXT(node),
                     GB_MAX_HARDNESS, -GB_MIN_EXPONENT);
    }
}

/* A block's texture must be in textures/blocks/, and its pixels must be there. */
static const char *gb_read_texture(gb_context_t *context, const yaml_node_t *node) {
    const char *name = gb_read_text(context, node);
    if (name == nullptr) {
        return nullptr;
    }
    const char *const group = "blocks/";
    const uint64_t group_length = strlen(group);
    if (strncmp(name, group, group_length) != 0 || !tp_path_is_safe(name) ||
        strchr(name + group_length, '/') != nullptr) {
        gb_report_at(context, context->borrowed_path, node->line, node->column,
                     "texture '%s' is not of the form blocks/<name>", name);
        return nullptr;
    }
    const char *pixels =
        tp_arena_format(context->borrowed_arena, "%s/%s/index.mctex", context->borrowed_textures, name);
    struct stat info;
    if (stat(pixels, &info) != 0 || !S_ISREG(info.st_mode)) {
        gb_report_at(context, context->borrowed_path, node->line, node->column, "texture '%s' does not exist: no %s",
                     name, pixels);
        return nullptr;
    }
    return name;
}

/* all, or else side and top and bottom, where a named face beats side. Every face must end up with a texture. */
static void gb_read_faces(gb_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                          gb_block_t *block) {
    if (map->kind != YAML_KIND_MAP) {
        gb_report_at(context, context->borrowed_path, map->line, map->column,
                     "texture must be a mapping of face to texture");
        return;
    }
    gb_reject_unknown(context, document, map, "texture", GB_TEXTURE_FIELDS, GB_TEXTURE_FIELD_COUNT);
    const yaml_node_t *all = yaml_map_get(document, map, "all");
    if (all != nullptr && map->child_count != 1u) {
        gb_report_at(context, context->borrowed_path, all->line, all->column,
                     "texture 'all' cannot go with other faces");
    }
    const yaml_node_t *side = yaml_map_get(document, map, "side");
    const char *const all_name = all != nullptr ? gb_read_texture(context, all) : nullptr;
    const char *const side_name = side != nullptr ? gb_read_texture(context, side) : nullptr;
    for (uint32_t face = 0; face < GB_FACE_COUNT; face++) {
        const yaml_node_t *named = yaml_map_get(document, map, GB_FACE_NAMES[face]);
        if (named != nullptr) {
            block->borrowed_faces[face] = gb_read_texture(context, named);
        } else if (all != nullptr) {
            block->borrowed_faces[face] = all_name;
        } else if (side != nullptr && face != GB_FACE_TOP && face != GB_FACE_BOTTOM) {
            block->borrowed_faces[face] = side_name;
        } else {
            gb_report_at(context, context->borrowed_path, map->line, map->column, "texture has nothing for the %s face",
                         GB_FACE_NAMES[face]);
        }
    }
}

/* Every block says what it sounds like, so a new one cannot be silent by being forgotten. Only what is never dug says none. */
static void gb_read_sound(gb_context_t *context, const yaml_node_t *root, const yaml_node_t *node, gb_block_t *block) {
    if (node == nullptr) {
        gb_report_at(context, context->borrowed_path, root->line, root->column,
                     "missing field 'sound'; only air and fluids say sound: none");
        return;
    }
    const char *text = gb_read_text(context, node);
    if (text == nullptr) {
        return;
    }
    for (uint32_t family = 0; family < SOUND_FAMILY_COUNT; family++) {
        if (strcmp(text, sound_family_name(family)) == 0) {
            block->sound = (uint8_t)family;
            if (family == SOUND_FAMILY_NONE && block->shape != GB_SHAPE_NONE && block->shape != GB_SHAPE_FLUID) {
                gb_report_at(context, context->borrowed_path, node->line, node->column,
                             "sound none is only for air and fluids, which are never dug");
            }
            return;
        }
    }
    gb_report_at(context, context->borrowed_path, node->line, node->column, "unknown sound '%s'", text);
}

static void gb_read_drop(gb_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                         gb_drop_t *drop) {
    drop->chance = (yaml_decimal_t){.mantissa = 1, .exponent = 0};
    drop->line = map->line;
    drop->column = map->column;
    if (map->kind != YAML_KIND_MAP) {
        gb_report_at(context, context->borrowed_path, map->line, map->column,
                     "a drop must be a mapping with item and count");
        return;
    }
    gb_reject_unknown(context, document, map, "drop", GB_DROP_FIELDS, GB_DROP_FIELD_COUNT);

    const yaml_node_t *item = yaml_map_get(document, map, "item");
    if (item == nullptr) {
        gb_report_at(context, context->borrowed_path, map->line, map->column, "drop has no item");
    } else {
        const char *name = gb_read_text(context, item);
        drop->line = item->line;
        drop->column = item->column;
        if (name != nullptr && !gb_name_is_valid(name)) {
            gb_report_at(context, context->borrowed_path, item->line, item->column,
                         "drop item '%s' is not a valid name", name);
        } else {
            drop->borrowed_item = name;
        }
    }

    const yaml_node_t *count = yaml_map_get(document, map, "count");
    if (count == nullptr) {
        gb_report_at(context, context->borrowed_path, map->line, map->column, "drop has no count");
    }
    gb_read_small(context, count, "drop count", 1, GB_MAX_DROP_COUNT, &drop->count_min);
    drop->count_max = drop->count_min;
    const yaml_node_t *most = yaml_map_get(document, map, "max");
    if (most != nullptr) {
        gb_read_small(context, most, "drop max", 1, GB_MAX_DROP_COUNT, &drop->count_max);
        if (drop->count_max < drop->count_min) {
            gb_report_at(context, context->borrowed_path, most->line, most->column, "drop max is below its count");
        }
    }

    const yaml_node_t *chance = yaml_map_get(document, map, "chance");
    if (chance != nullptr) {
        yaml_error_t error = {0};
        if (!yaml_get_decimal(chance, &drop->chance, &error)) {
            gb_report_yaml(context, &error);
        } else if (drop->chance.mantissa <= 0 || !gb_decimal_fits(drop->chance, 1)) {
            gb_report_at(context, context->borrowed_path, chance->line, chance->column,
                         "drop chance '%.*s' must be above 0, at most 1, and have at most %d decimals", GB_TEXT(chance),
                         -GB_MIN_EXPONENT);
        }
    }
}

static void gb_read_drops(gb_context_t *context, const yaml_document_t *document, const yaml_node_t *list,
                          gb_block_t *block) {
    if (list->kind != YAML_KIND_SEQ) {
        gb_report_at(context, context->borrowed_path, list->line, list->column,
                     "drops must be a list; a block that drops nothing says drops: []");
        return;
    }
    if (list->child_count > GB_MAX_DROPS_PER_BLOCK) {
        gb_report_at(context, context->borrowed_path, list->line, list->column, "drops has %u rows, and the most is %u",
                     list->child_count, GB_MAX_DROPS_PER_BLOCK);
        return;
    }
    block->borrowed_drops = tp_arena_alloc(context->borrowed_arena, (list->child_count + 1ull) * sizeof(gb_drop_t));
    for (const yaml_node_t *row = yaml_first_child(document, list); row != nullptr;
         row = yaml_next_sibling(document, row)) {
        gb_read_drop(context, document, row, &block->borrowed_drops[block->drop_count]);
        block->drop_count++;
    }
}

/* One parsed file into one block. Every field absent from the file keeps the default given here or by its shape. */
static void gb_read_fields(gb_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                           gb_block_t *block) {
    gb_reject_unknown(context, document, root, "a block", GB_BLOCK_FIELDS, GB_BLOCK_FIELD_COUNT);

    const yaml_node_t *name = yaml_map_get(document, root, "name");
    if (name == nullptr) {
        gb_report_at(context, context->borrowed_path, root->line, root->column, "missing field 'name'");
    } else {
        const char *text = gb_read_text(context, name);
        if (text != nullptr && strcmp(text, block->borrowed_name) != 0) {
            gb_report_at(context, context->borrowed_path, name->line, name->column,
                         "name '%s' is not its directory's, '%s'", text, block->borrowed_name);
        }
    }

    const yaml_node_t *hardness = yaml_map_get(document, root, "hardness");
    if (hardness == nullptr) {
        gb_report_at(context, context->borrowed_path, root->line, root->column, "missing field 'hardness'");
    } else {
        gb_read_hardness(context, hardness, block);
    }

    const yaml_node_t *drops = yaml_map_get(document, root, "drops");
    if (drops == nullptr) {
        gb_report_at(context, context->borrowed_path, root->line, root->column,
                     "missing field 'drops'; a block that drops nothing says drops: []");
    } else {
        gb_read_drops(context, document, drops, block);
    }

    /* The shape first, since it gives the rest their defaults. */
    block->shape = GB_SHAPE_CUBE;
    gb_read_choice(context, yaml_map_get(document, root, "shape"), "shape", GB_SHAPE_NAMES, GB_SHAPE_COUNT,
                   &block->shape);
    const gb_shape_t *shape = &GB_SHAPES[block->shape];
    block->opacity = shape->opacity;
    block->flags = (uint8_t)(block->flags | (shape->solid != 0u ? GB_FLAG_SOLID : 0u) |
                             (shape->replaceable != 0u ? GB_FLAG_REPLACEABLE : 0u));

    const yaml_node_t *texture = yaml_map_get(document, root, "texture");
    if (block->shape == GB_SHAPE_NONE) {
        if (texture != nullptr) {
            gb_report_at(context, context->borrowed_path, texture->line, texture->column,
                         "shape none draws nothing, so it has no texture");
        }
    } else if (texture == nullptr) {
        gb_report_at(context, context->borrowed_path, root->line, root->column, "missing field 'texture'");
    } else {
        gb_read_faces(context, document, texture, block);
    }

    gb_read_sound(context, root, yaml_map_get(document, root, "sound"), block);

    const yaml_node_t *tool = yaml_map_get(document, root, "tool");
    gb_read_choice(context, tool, "tool", GB_TOOLS, GB_TOOL_COUNT, &block->tool);
    const yaml_node_t *tier = yaml_map_get(document, root, "tier");
    gb_read_choice(context, tier, "tier", GB_TIERS, GB_TIER_COUNT, &block->tier);
    if (block->tier != GB_TIER_NONE && (block->tool == GB_TOOL_NONE || block->tool == GB_TOOL_SHEARS)) {
        gb_report_at(context, context->borrowed_path, tier->line, tier->column, "tier '%s' needs a tool that has tiers",
                     GB_TIERS[block->tier]);
    }

    gb_read_small(context, yaml_map_get(document, root, "light"), "light", 0, GB_MAX_LIGHT, &block->light);
    gb_read_small(context, yaml_map_get(document, root, "opacity"), "opacity", 0, GB_MAX_LIGHT, &block->opacity);
    gb_read_flag(context, yaml_map_get(document, root, "solid"), GB_FLAG_SOLID, &block->flags);
    gb_read_flag(context, yaml_map_get(document, root, "replaceable"), GB_FLAG_REPLACEABLE, &block->flags);
    gb_read_flag(context, yaml_map_get(document, root, "falls"), GB_FLAG_FALLS, &block->flags);
    gb_read_flag(context, yaml_map_get(document, root, "decays"), GB_FLAG_DECAYS, &block->flags);
    gb_read_flag(context, yaml_map_get(document, root, "log"), GB_FLAG_LOG, &block->flags);
    gb_read_flag(context, yaml_map_get(document, root, "map_cover"), GB_FLAG_MAP_COVER, &block->flags);

    /* What its state holds beside its id. Facing and half are words, so that another kind of either has somewhere to go. */
    uint8_t facing = 0u;
    gb_read_choice(context, yaml_map_get(document, root, "facing"), "facing", GB_FACINGS, GB_FACING_COUNT, &facing);
    uint8_t half = 0u;
    gb_read_choice(context, yaml_map_get(document, root, "half"), "half", GB_HALVES, GB_HALF_COUNT, &half);
    block->states = (uint8_t)((facing != 0u ? GB_STATES_FACING : 0u) | (half != 0u ? GB_STATES_HALF : 0u));
    gb_read_flag(context, yaml_map_get(document, root, "open"), GB_STATES_OPEN, &block->states);
    if (block->shape == GB_SHAPE_NONE && block->states != 0u) {
        gb_report_at(context, context->borrowed_path, root->line, root->column,
                     "shape none draws nothing, so it has no state to show");
    }
    const uint8_t door_states = GB_STATES_FACING | GB_STATES_HALF | GB_STATES_OPEN;
    if (block->shape == GB_SHAPE_DOOR && block->states != door_states) {
        gb_report_at(context, context->borrowed_path, root->line, root->column,
                     "shape door needs facing: horizontal, half: upper_lower and open: true");
    }
}

/* Reads one block's file. What it finds wrong is counted in the context. */
static void gb_read_block(gb_context_t *context, const char *blocks, const char *name, gb_block_t *block) {
    memset(block, 0, sizeof(*block));
    block->borrowed_name = name;
    block->borrowed_path = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", blocks, name);
    context->borrowed_path = block->borrowed_path;
    if (!gb_name_is_valid(name)) {
        gb_report_at(context, block->borrowed_path, 1, 1,
                     "directory name '%s' is not lower case, digits and underscores", name);
        return;
    }
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status =
        tp_read_path(context->borrowed_arena, block->borrowed_path, GB_MAX_FILE_BYTES, &text, &length);
    if (status != TP_READ_OK) {
        if (status == TP_READ_MISSING) {
            fprintf(stderr, "%s: missing\n", block->borrowed_path);
        }
        context->problems++;
        return;
    }
    yaml_storage_t storage = {
        .nodes = tp_arena_alloc(context->borrowed_arena, (length + 1u) * sizeof(yaml_node_t)),
        .strings = tp_arena_alloc(context->borrowed_arena, length + 1u),
        .node_capacity = (uint32_t)(length + 1u),
        .string_capacity = (uint32_t)length,
    };
    yaml_document_t document;
    yaml_error_t error = {0};
    if (!yaml_parse(&document, text, length, storage, &error)) {
        gb_report_yaml(context, &error);
        return;
    }
    const yaml_node_t *root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        gb_report_at(context, block->borrowed_path, root->line == 0u ? 1u : root->line,
                     root->column == 0u ? 1u : root->column, "a block's file must be a mapping");
        return;
    }
    gb_read_fields(context, &document, root, block);
}

static int32_t gb_compare_text(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Every directory under blocks, sorted, with air first: an all-zero section has to be empty. */
static bool gb_list_blocks(tp_arena_t *arena, const char *blocks, const char ***out_names, uint32_t *out_count) {
    const char **names = tp_arena_alloc(arena, GB_MAX_BLOCKS * sizeof(const char *));
    uint32_t count = 0;
    DIR *listing = opendir(blocks);
    if (listing == nullptr) {
        return tp_fail("%s: cannot be listed", blocks);
    }
    bool has_air = false;
    for (struct dirent *item = readdir(listing); item != nullptr; item = readdir(listing)) {
        struct stat info;
        const char *child = tp_arena_format(arena, "%s/%s", blocks, item->d_name);
        if (item->d_name[0] == '.' || stat(child, &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        if (count == GB_MAX_BLOCKS) {
            closedir(listing);
            return tp_fail("%s: holds over %u blocks", blocks, GB_MAX_BLOCKS);
        }
        if (strcmp(item->d_name, GB_AIR) == 0) {
            has_air = true;
        } else {
            names[count] = tp_arena_text(arena, item->d_name, strlen(item->d_name));
            count++;
        }
    }
    closedir(listing);
    if (!has_air) {
        return tp_fail("%s: has no %s, which must be block 0", blocks, GB_AIR);
    }
    qsort(names, count, sizeof(const char *), gb_compare_text);
    memmove(names + 1, names, count * sizeof(const char *));
    names[0] = GB_AIR;
    *out_names = names;
    *out_count = count + 1u;
    return true;
}

static gb_set_t gb_set_begin(tp_arena_t *arena, uint32_t capacity) {
    return (gb_set_t){.borrowed_items = tp_arena_alloc(arena, (capacity + 1ull) * sizeof(const char *)),
                      .capacity = capacity};
}

/* Only ever called below capacity. Linear, and over a few hundred names. */
static void gb_set_add(gb_set_t *set, const char *name) {
    for (uint32_t i = 0; i < set->count; i++) {
        if (strcmp(set->borrowed_items[i], name) == 0) {
            return;
        }
    }
    set->borrowed_items[set->count] = name;
    set->count++;
}

static void gb_set_sort(gb_set_t *set) {
    qsort(set->borrowed_items, set->count, sizeof(const char *), gb_compare_text);
}

static uint32_t gb_set_index(const gb_set_t *set, const char *name) {
    const char **found = bsearch(&name, set->borrowed_items, set->count, sizeof(const char *), gb_compare_text);
    return (uint32_t)(found - set->borrowed_items);
}

/* The block of this name, or UINT32_MAX. names is sorted but for air, so a straight look is simplest. */
static uint32_t gb_block_find(const gb_block_t *blocks, uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(blocks[i].borrowed_name, name) == 0) {
            return i;
        }
    }
    return UINT32_MAX;
}

/* A drop names an item, which is a file: the items part checks what is in it. */
static bool gb_item_exists(gb_context_t *context, const char *name) {
    struct stat info;
    const char *defined = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", context->borrowed_items, name);
    return stat(defined, &info) == 0 && S_ISREG(info.st_mode);
}

static void gb_resolve_drops(gb_context_t *context, const gb_block_t *blocks, uint32_t block_count, gb_set_t *items) {
    for (uint32_t b = 0; b < block_count; b++) {
        for (uint32_t d = 0; d < blocks[b].drop_count; d++) {
            const gb_drop_t *drop = &blocks[b].borrowed_drops[d];
            if (drop->borrowed_item == nullptr) {
                continue; /* already reported */
            }
            const uint32_t block = gb_block_find(blocks, block_count, drop->borrowed_item);
            if (block == 0u) {
                gb_report_at(context, blocks[b].borrowed_path, drop->line, drop->column, "%s cannot be a drop", GB_AIR);
            } else if (!gb_item_exists(context, drop->borrowed_item)) {
                gb_report_at(context, blocks[b].borrowed_path, drop->line, drop->column,
                             "drop item '%s' does not exist: no %s/%s/index.yaml", drop->borrowed_item,
                             context->borrowed_items, drop->borrowed_item);
            } else {
                gb_set_add(items, drop->borrowed_item);
            }
        }
    }
}

/* Every item can lie on the ground, not only what a block drops: a broken chest throws out whatever it held. So there is a row for each
 * file in items, and since both sort by name, row i is item i + 1. */
static void gb_add_all_items(gb_context_t *context, gb_set_t *items) {
    DIR *listing = opendir(context->borrowed_items);
    if (listing == nullptr) {
        return; /* a drop naming anything has said so already */
    }
    for (struct dirent *entry = readdir(listing); entry != nullptr; entry = readdir(listing)) {
        if (items->count < items->capacity && entry->d_name[0] != '.' && gb_item_exists(context, entry->d_name)) {
            gb_set_add(items, tp_arena_text(context->borrowed_arena, entry->d_name, strlen(entry->d_name)));
        }
    }
    closedir(listing);
}

/* In upper case, for an enum name. Names were checked to be short. */
static const char *gb_upper(tp_arena_t *arena, const char *name) {
    char *text = tp_arena_text(arena, name, strlen(name));
    for (char *c = text; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return text;
}

/* A decimal in the form that holds what the file says, with no rounding in this tool: 5e-1f32. */
static void gb_add_decimal(tp_builder_t *builder, yaml_decimal_t value) {
    tp_builder_add(builder, "%" PRId64 "e%df32", value.mantissa, value.exponent);
}

static void gb_add_enum(tp_arena_t *arena, tp_builder_t *header, const char *type, const char *prefix,
                        const char *const *names, uint32_t count) {
    tp_builder_add(header, "typedef enum %s : uint8_t {\n", type);
    for (uint32_t i = 0; i < count; i++) {
        tp_builder_add(header, "    %s%s = %u,\n", prefix, gb_upper(arena, names[i]), i);
    }
    tp_builder_add(header, "} %s;\n\n", type);
}

static void gb_emit_header(tp_arena_t *arena, tp_builder_t *out, const gb_block_t *blocks, uint32_t block_count,
                           const gb_set_t *textures, const gb_set_t *items, uint32_t drop_count) {
    uint32_t no_block_count = 0u;
    for (uint32_t i = 0; i < items->count; i++) {
        no_block_count += gb_block_find(blocks, block_count, items->borrowed_items[i]) == UINT32_MAX ? 1u : 0u;
    }
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/blocks/. Change the yaml and build; never edit this. */\n"
        "#ifndef TABLES_BLOCKS_INDEX_H\n#define TABLES_BLOCKS_INDEX_H\n\n"
        "#include \"types/block_state/index.h\"\n#include \"types/index.h\"\n#include \"types/sound_blob/index.h\"\n\n"
        "#include <stdint.h>\n\n");
    tp_builder_add(out, "/* Block 0 is air, and the rest sort by name, so adding a block moves the ids after it. */\n");
    tp_builder_add(out, "typedef enum block_id_t : uint16_t {\n");
    for (uint32_t b = 0; b < block_count; b++) {
        tp_builder_add(out, "    BLOCK_ID_%s = %u,\n", gb_upper(arena, blocks[b].borrowed_name), b);
    }
    tp_builder_add(out, "} block_id_t;\n\n");
    tp_builder_add(out,
                   "#define BLOCK_COUNT %uu\n#define BLOCK_TEXTURE_COUNT %uu\n#define BLOCK_DROP_COUNT %uu\n"
                   "#define BLOCK_DROP_ITEM_COUNT %uu\n#define BLOCK_DROP_ITEM_NO_BLOCK_COUNT %uu\n",
                   block_count, textures->count, drop_count, items->count, no_block_count);
    tp_builder_add(out, "/* A voxel holds an id in its low bits, with its state above. */\n"
                        "static_assert(BLOCK_COUNT <= BLOCK_STATE_ID_LIMIT);\n");
    tp_builder_add(out, "/* In block_drop_item_t: this item is no block. */\n#define BLOCK_NO_ID 0xFFFFu\n");
    tp_builder_add(out, "/* In block_def_t.faces: air draws nothing. */\n#define BLOCK_NO_TEXTURE 0xFFFFu\n\n");

    gb_add_enum(arena, out, "block_shape_t", "BLOCK_SHAPE_", GB_SHAPE_NAMES, GB_SHAPE_COUNT);
    gb_add_enum(arena, out, "block_tool_t", "BLOCK_TOOL_", GB_TOOLS, GB_TOOL_COUNT);
    tp_builder_add(out, "/* The lowest tool that gets the drops. Anything less breaks the block for nothing. */\n");
    gb_add_enum(arena, out, "block_tier_t", "BLOCK_TIER_", GB_TIERS, GB_TIER_COUNT);
    tp_builder_add(out, "/* x is east, y is north, z is up. Bit 0 is the positive side of an axis. */\n");
    gb_add_enum(arena, out, "block_face_t", "BLOCK_FACE_", GB_FACE_NAMES, GB_FACE_COUNT);
    for (uint32_t i = 0; i < GB_FLAG_COUNT; i++) {
        tp_builder_add(out, "#define BLOCK_FLAG_%s %uu\n", GB_FLAG_NAMES[i], 1u << i);
    }
    tp_builder_add(
        out, "/* What a block's state may hold beside its id, in block_def_t.states: see types/block_state/. */\n");
    for (uint32_t i = 0; i < GB_STATES_COUNT; i++) {
        tp_builder_add(out, "#define BLOCK_STATES_%s %uu\n", GB_STATES_NAMES[i], 1u << i);
    }
    tp_builder_add(
        out,
        "\n/* One row per block. Hardness is what mining time scales with; it is -1 with BLOCK_FLAG_UNBREAKABLE. */\n"
        "typedef struct block_def_t {\n"
        "    const char *name;   /* 0 */\n"
        "    float32_t hardness; /* 8 */\n"
        "    uint16_t faces[%u];  /* 12: into block_texture_names, by block_face_t */\n"
        "    uint16_t first_drop; /* 24: into block_drops */\n"
        "    uint8_t drop_count; /* 26 */\n"
        "    uint8_t tool;       /* 27: a block_tool_t */\n"
        "    uint8_t tier;       /* 28: a block_tier_t */\n"
        "    uint8_t shape;      /* 29: a block_shape_t */\n"
        "    uint8_t light;      /* 30: block light given off, 0 to 15 */\n"
        "    uint8_t opacity;    /* 31: block light lost passing through, 15 stops it */\n"
        "    uint8_t flags;      /* 32: BLOCK_FLAG_ */\n"
        "    uint8_t sound;      /* 33: a sound_family_t, from types/sound_blob/ */\n"
        "    uint8_t states;     /* 34: BLOCK_STATES_: what its state holds beside its id */\n"
        "    uint8_t _pad0[5];   /* 35 */\n"
        "} block_def_t;\n"
        "static_assert(sizeof(block_def_t) == 40);\n\n"
        "/* Each row is rolled on its own: chance, then a count from count_min to count_max. */\n"
        "typedef struct block_drop_t {\n"
        "    float32_t chance;  /* 0: above 0, at most 1 */\n"
        "    uint16_t item;     /* 4: into block_drop_items */\n"
        "    uint8_t count_min; /* 6 */\n"
        "    uint8_t count_max; /* 7 */\n"
        "} block_drop_t;\n"
        "static_assert(sizeof(block_drop_t) == 8);\n\n"
        "/* One row per item, sorted as item ids are: row i is the item_id_t i + 1. */\n"
        "typedef struct block_drop_item_t {\n"
        "    const char *name; /* 0 */\n"
        "    uint16_t block;   /* 8: the block of that name, or BLOCK_NO_ID */\n"
        "    uint16_t item;    /* 10: the item_id_t of that name, from tables/items/ */\n"
        "    uint8_t _pad0[4]; /* 12 */\n"
        "} block_drop_item_t;\n"
        "static_assert(sizeof(block_drop_item_t) == 16);\n\n"
        "extern const block_def_t block_defs[BLOCK_COUNT];\n"
        "extern const block_drop_t block_drops[BLOCK_DROP_COUNT];\n"
        "extern const block_drop_item_t block_drop_items[BLOCK_DROP_ITEM_COUNT];\n"
        "/* \"blocks/dirt\", sorted: what run/src/texture/registry/ finds a texture by. */\n"
        "extern const char *const block_texture_names[BLOCK_TEXTURE_COUNT];\n\n"
        "#endif\n",
        GB_FACE_COUNT);
}

static void gb_add_flags(tp_builder_t *out, uint8_t flags) {
    if (flags == 0u) {
        tp_builder_add(out, "0u");
        return;
    }
    bool first = true;
    for (uint32_t i = 0; i < GB_FLAG_COUNT; i++) {
        if ((flags & (1u << i)) != 0u) {
            tp_builder_add(out, "%sBLOCK_FLAG_%s", first ? "" : " | ", GB_FLAG_NAMES[i]);
            first = false;
        }
    }
}

static void gb_add_states(tp_builder_t *out, uint8_t states) {
    if (states == 0u) {
        tp_builder_add(out, "0u");
        return;
    }
    bool first = true;
    for (uint32_t i = 0; i < GB_STATES_COUNT; i++) {
        if ((states & (1u << i)) != 0u) {
            tp_builder_add(out, "%sBLOCK_STATES_%s", first ? "" : " | ", GB_STATES_NAMES[i]);
            first = false;
        }
    }
}

static void gb_emit_source(tp_arena_t *arena, tp_builder_t *out, const gb_block_t *blocks, uint32_t block_count,
                           const gb_set_t *textures, const gb_set_t *items) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/blocks/. Change the yaml and build; never edit this. */\n"
        "#include \"tables/blocks/index.h\"\n\n"
        "#include \"tables/items/index.h\"\n\n"
        "const char *const block_texture_names[BLOCK_TEXTURE_COUNT] = {\n");
    for (uint32_t i = 0; i < textures->count; i++) {
        tp_builder_add(out, "    \"%s\",\n", textures->borrowed_items[i]);
    }
    tp_builder_add(out, "};\n\nconst block_drop_item_t block_drop_items[BLOCK_DROP_ITEM_COUNT] = {\n");
    for (uint32_t i = 0; i < items->count; i++) {
        const uint32_t block = gb_block_find(blocks, block_count, items->borrowed_items[i]);
        const char *const item = gb_upper(arena, items->borrowed_items[i]);
        if (block == UINT32_MAX) {
            tp_builder_add(out, "    {.name = \"%s\", .block = BLOCK_NO_ID, .item = ITEM_ID_%s},\n",
                           items->borrowed_items[i], item);
        } else {
            tp_builder_add(out, "    {.name = \"%s\", .block = BLOCK_ID_%s, .item = ITEM_ID_%s},\n",
                           items->borrowed_items[i], item, item);
        }
    }
    tp_builder_add(out, "};\n\nconst block_drop_t block_drops[BLOCK_DROP_COUNT] = {\n");
    for (uint32_t b = 0; b < block_count; b++) {
        for (uint32_t d = 0; d < blocks[b].drop_count; d++) {
            const gb_drop_t *drop = &blocks[b].borrowed_drops[d];
            tp_builder_add(out, "    {.chance = ");
            gb_add_decimal(out, drop->chance);
            tp_builder_add(out, ", .item = %u, .count_min = %u, .count_max = %u}, /* %s: %s */\n",
                           gb_set_index(items, drop->borrowed_item), drop->count_min, drop->count_max,
                           blocks[b].borrowed_name, drop->borrowed_item);
        }
    }
    tp_builder_add(out, "};\n\nconst block_def_t block_defs[BLOCK_COUNT] = {\n");
    uint32_t first_drop = 0;
    for (uint32_t b = 0; b < block_count; b++) {
        const gb_block_t *block = &blocks[b];
        tp_builder_add(out, "    [BLOCK_ID_%s] = {.name = \"%s\", .hardness = ", gb_upper(arena, block->borrowed_name),
                       block->borrowed_name);
        gb_add_decimal(out, block->hardness);
        tp_builder_add(out, ", .faces = {");
        for (uint32_t face = 0; face < GB_FACE_COUNT; face++) {
            if (block->borrowed_faces[face] == nullptr) {
                tp_builder_add(out, "%sBLOCK_NO_TEXTURE", face == 0u ? "" : ", ");
            } else {
                tp_builder_add(out, "%s%u", face == 0u ? "" : ", ",
                               gb_set_index(textures, block->borrowed_faces[face]));
            }
        }
        tp_builder_add(out, "}, .first_drop = %u, .drop_count = %u, .tool = BLOCK_TOOL_%s, .tier = BLOCK_TIER_%s, ",
                       first_drop, block->drop_count, gb_upper(arena, GB_TOOLS[block->tool]),
                       gb_upper(arena, GB_TIERS[block->tier]));
        tp_builder_add(out, ".shape = BLOCK_SHAPE_%s, .light = %u, .opacity = %u, .flags = ",
                       gb_upper(arena, GB_SHAPE_NAMES[block->shape]), block->light, block->opacity);
        gb_add_flags(out, block->flags);
        tp_builder_add(out, ", .sound = SOUND_FAMILY_%s, .states = ", gb_upper(arena, sound_family_name(block->sound)));
        gb_add_states(out, block->states);
        tp_builder_add(out, "},\n");
        first_drop += block->drop_count;
    }
    tp_builder_add(out, "};\n");
}

bool game_tables_blocks_build(tp_arena_t *arena, const char *config, const char *textures, const char *out) {
    const char *blocks_path = tp_arena_format(arena, "%s/blocks", config);
    const char **names = nullptr;
    uint32_t block_count = 0;
    if (!gb_list_blocks(arena, blocks_path, &names, &block_count)) {
        return false;
    }

    gb_context_t context = {
        .borrowed_arena = arena,
        .borrowed_textures = textures,
        .borrowed_items = tp_arena_format(arena, "%s/items", config),
    };
    gb_block_t *blocks = tp_arena_alloc(arena, block_count * sizeof(gb_block_t));
    gb_set_t texture_set = gb_set_begin(arena, block_count * GB_FACE_COUNT);
    uint32_t drop_count = 0;
    for (uint32_t b = 0; b < block_count; b++) {
        gb_read_block(&context, blocks_path, names[b], &blocks[b]);
        drop_count += blocks[b].drop_count;
        for (uint32_t face = 0; face < GB_FACE_COUNT; face++) {
            if (blocks[b].borrowed_faces[face] != nullptr) {
                gb_set_add(&texture_set, blocks[b].borrowed_faces[face]);
            }
        }
    }

    gb_set_t item_set = gb_set_begin(arena, drop_count + GB_MAX_ITEMS);
    gb_resolve_drops(&context, blocks, block_count, &item_set);
    gb_add_all_items(&context, &item_set);
    if (context.problems != 0u) {
        return tp_fail("%s: %u problems; nothing was written", blocks_path, context.problems);
    }
    /* A table of no rows would not compile, and a world where nothing drops or nothing draws is a mistake anyway. */
    if (drop_count == 0u || texture_set.count == 0u) {
        return tp_fail("%s: no block drops anything, or none has a texture", blocks_path);
    }
    if (drop_count > UINT16_MAX || texture_set.count > UINT16_MAX) {
        return tp_fail("%s: over %u drops or textures", blocks_path, UINT16_MAX);
    }
    gb_set_sort(&texture_set);
    gb_set_sort(&item_set);

    tp_builder_t header = tp_builder_begin(arena, GB_HEADER_BYTES);
    tp_builder_t source = tp_builder_begin(arena, GB_SOURCE_BYTES);
    gb_emit_header(arena, &header, blocks, block_count, &texture_set, &item_set, drop_count);
    gb_emit_source(arena, &source, blocks, block_count, &texture_set, &item_set);

    const char *directory = tp_arena_format(arena, "%s/blocks", out);
    return tp_make_directories(arena, directory) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.h", directory), header.borrowed_text,
                               header.used, nullptr) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.c", directory), source.borrowed_text,
                               source.used, nullptr);
}
