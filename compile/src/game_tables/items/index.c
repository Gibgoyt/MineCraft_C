/*
 * config/items/ -> the item tables. See README.md. Like the other parts: read and check each file on its own, then emit. A
 * problem does not stop the reading, so one build shows all of them.
 */
#include "game_tables/items/index.h"

#include "yaml/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GI_MAX_ITEMS 4096u
#define GI_MAX_NAME_BYTES 64u
#define GI_MAX_FILE_BYTES (1ull << 20)
#define GI_STACK_LIMIT 64
#define GI_DURABILITY_LIMIT 65535
#define GI_FOOD_LIMIT 20
#define GI_ARMOR_LIMIT 20
#define GI_DAMAGE_LIMIT 100
/* A finer decimal than this is a typo. It also keeps a value from reaching the compiler as a float that rounds to zero. */
#define GI_MIN_EXPONENT (-6)
#define GI_HEADER_BYTES (1ull << 20)
#define GI_SOURCE_BYTES (1ull << 21)
/* Row 0, which has no file: a zeroed stack is an empty one. */
#define GI_NONE "none"
#define GI_AIR "air"

/* The words a block's file uses for tools, in the order of block_tool_t, which the table's rows are written in. */
enum { GI_TOOL_NONE = 0, GI_TOOL_SHEARS = 5 };
static const char *const GI_TOOLS[] = {"none", "pickaxe", "axe", "shovel", "hoe", "shears", "sword"};
enum { GI_TOOL_COUNT = sizeof(GI_TOOLS) / sizeof(GI_TOOLS[0]) };

/* A tool's tier is a file of config/tiers/ (plan 005 phase 6), and what it harvests is a rung of a block's tier, the words of
 * compile/src/game_tables/blocks/ in the order of block_tier_t. Tier 0 is none, which has no file. */
enum { GI_TIER_NONE = 0, GI_MAX_TIERS = 64 };
static const char *const GI_RUNGS[] = {"none", "wood", "stone", "iron", "diamond"};
enum { GI_RUNG_COUNT = sizeof(GI_RUNGS) / sizeof(GI_RUNGS[0]) };
#define GI_TIER_SPEED_MIN 1000
#define GI_TIER_SPEED_MAX 65535
#define GI_ATTACK_SPEED_LIMIT 20
static const char *const GI_TIER_FIELDS[] = {"name", "harvests", "speed", "axe_attack_speed", "hoe_attack_speed"};
enum { GI_TIER_FIELD_COUNT = sizeof(GI_TIER_FIELDS) / sizeof(GI_TIER_FIELDS[0]) };

enum { GI_SLOT_NONE = 0 };
static const char *const GI_SLOTS[] = {"none", "head", "chest", "legs", "feet"};
enum { GI_SLOT_COUNT = sizeof(GI_SLOTS) / sizeof(GI_SLOTS[0]) };

static const char *const GI_ITEM_FIELDS[] = {"name",       "texture", "stack", "block",      "tool", "tier",
                                             "durability", "damage",  "food",  "saturation", "armor"};
enum { GI_ITEM_FIELD_COUNT = sizeof(GI_ITEM_FIELDS) / sizeof(GI_ITEM_FIELDS[0]) };

static const char *const GI_ARMOR_FIELDS[] = {"slot", "points", "toughness"};
enum { GI_ARMOR_FIELD_COUNT = sizeof(GI_ARMOR_FIELDS) / sizeof(GI_ARMOR_FIELDS[0]) };

/* One item, as read. Everything it points to is in the arena. */
typedef struct gi_item_t {
    const char *borrowed_name;    /* 0: its directory's, so a broken file still has one */
    const char *borrowed_path;    /* 8 */
    const char *borrowed_texture; /* 16 */
    const char *borrowed_block;   /* 24: what it places, or nullptr */
    yaml_decimal_t damage;        /* 32 */
    yaml_decimal_t saturation;    /* 48 */
    uint32_t durability;          /* 64 */
    uint8_t stack;                /* 68 */
    uint8_t tool;                 /* 69 */
    uint8_t tier;                 /* 70 */
    uint8_t food;                 /* 71 */
    uint8_t armor_slot;           /* 72 */
    uint8_t armor_points;         /* 73 */
    uint8_t armor_toughness;      /* 74 */
    uint8_t _pad0[5];             /* 75 */
} gi_item_t;
static_assert(sizeof(gi_item_t) == 80);

/* One tier, as read. */
typedef struct gi_tier_t {
    const char *borrowed_name;       /* 0 */
    yaml_decimal_t axe_attack_speed; /* 8: swings a second with an axe of it */
    yaml_decimal_t hoe_attack_speed; /* 24 */
    uint32_t speed;                  /* 40: thousandths of a bare hand, on its own tool's blocks */
    uint8_t harvests;                /* 44: a rung of GI_RUNGS */
    uint8_t _pad0[3];                /* 45 */
} gi_tier_t;
static_assert(sizeof(gi_tier_t) == 48);

typedef struct gi_context_t {
    tp_arena_t *borrowed_arena;    /* 0 */
    const char *borrowed_textures; /* 8 */
    const char *borrowed_blocks;   /* 16: <config>/blocks */
    const char *borrowed_path;     /* 24: the file being read, for reporting */
    const char **borrowed_tiers;   /* 32: none, then every file of config/tiers/ by name */
    uint32_t problems;             /* 40 */
    uint32_t tier_count;           /* 44 */
} gi_context_t;
static_assert(sizeof(gi_context_t) == 48);

[[gnu::format(printf, 4, 5)]]
static void gi_report(gi_context_t *context, uint32_t line, uint32_t column, const char *format, ...) {
    fprintf(stderr, "%s:%u:%u: ", context->borrowed_path, line, column);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    context->problems++;
}

static void gi_report_yaml(gi_context_t *context, const yaml_error_t *error) {
    gi_report(context, error->line, error->column, "%s", error->message);
}

static bool gi_key_is(const yaml_node_t *node, const char *key) {
    const uint64_t length = strlen(key);
    return node->key_length == length && memcmp(node->borrowed_key, key, length) == 0;
}

/* Reports every key of map that the list does not hold. A typo must not quietly mean the default. */
static void gi_reject_unknown(gi_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                              const char *what, const char *const *fields, uint32_t field_count) {
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        bool known = false;
        for (uint32_t i = 0; i < field_count && !known; i++) {
            known = gi_key_is(child, fields[i]);
        }
        if (!known) {
            gi_report(context, child->line, child->column, "unknown field '%.*s' in %s", (int32_t)child->key_length,
                      (const char *)child->borrowed_key, what);
        }
    }
}

/* Lower case, digits and underscores, so it can be an enum name in the tables. */
static bool gi_name_is_valid(const char *name) {
    const uint64_t length = strlen(name);
    if (length == 0u || length > GI_MAX_NAME_BYTES || name[0] < 'a' || name[0] > 'z') {
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
static const char *gi_read_text(gi_context_t *context, const yaml_node_t *node) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        gi_report_yaml(context, &error);
        return nullptr;
    }
    return tp_arena_text(context->borrowed_arena, text, length);
}

/* Finds a scalar in a list of allowed words. An absent node leaves out alone. */
static void gi_read_choice(gi_context_t *context, const yaml_node_t *node, const char *what, const char *const *choices,
                           uint32_t choice_count, uint8_t *out) {
    if (node == nullptr) {
        return;
    }
    const char *text = gi_read_text(context, node);
    if (text == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < choice_count; i++) {
        if (strcmp(text, choices[i]) == 0) {
            *out = (uint8_t)i;
            return;
        }
    }
    gi_report(context, node->line, node->column, "unknown %s '%s'", what, text);
}

/* An integer from minimum to maximum. An absent node leaves out alone. */
static void gi_read_number(gi_context_t *context, const yaml_node_t *node, const char *what, int64_t minimum,
                           int64_t maximum, uint32_t *out) {
    if (node == nullptr) {
        return;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        gi_report_yaml(context, &error);
    } else if (value < minimum || value > maximum) {
        gi_report(context, node->line, node->column, "%s is %" PRId64 ", and must be %" PRId64 " to %" PRId64, what,
                  value, minimum, maximum);
    } else {
        *out = (uint32_t)value;
    }
}

static void gi_read_small(gi_context_t *context, const yaml_node_t *node, const char *what, int64_t minimum,
                          int64_t maximum, uint8_t *out) {
    uint32_t value = *out;
    gi_read_number(context, node, what, minimum, maximum, &value);
    *out = (uint8_t)value;
}

/* Whether a non-negative decimal is at most limit, and is no finer than GI_MIN_EXPONENT, by whole numbers only. */
static bool gi_decimal_fits(yaml_decimal_t value, int64_t limit) {
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
    if (exponent < GI_MIN_EXPONENT) {
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

static void gi_read_decimal(gi_context_t *context, const yaml_node_t *node, const char *what, int64_t limit,
                            yaml_decimal_t *out) {
    if (node == nullptr) {
        return;
    }
    yaml_error_t error = {0};
    if (!yaml_get_decimal(node, out, &error)) {
        gi_report_yaml(context, &error);
    } else if (!gi_decimal_fits(*out, limit)) {
        gi_report(context, node->line, node->column, "%s '%.*s' must be 0 to %" PRId64 " with at most %d decimals",
                  what, (int32_t)node->text_length, (const char *)node->borrowed_text, limit, -GI_MIN_EXPONENT);
    }
}

static bool gi_file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

/* What the hotbar and the inventory screen draw: from either group, since a block's item looks like the block. */
static void gi_read_texture(gi_context_t *context, const yaml_node_t *node, gi_item_t *item) {
    const char *name = gi_read_text(context, node);
    if (name == nullptr) {
        return;
    }
    const bool in_items = strncmp(name, "items/", 6u) == 0, in_blocks = strncmp(name, "blocks/", 7u) == 0;
    const char *const rest = in_items ? name + 6u : name + 7u;
    if ((!in_items && !in_blocks) || !tp_path_is_safe(name) || strchr(rest, '/') != nullptr) {
        gi_report(context, node->line, node->column, "texture '%s' is not of the form items/<name> or blocks/<name>",
                  name);
        return;
    }
    const char *pixels =
        tp_arena_format(context->borrowed_arena, "%s/%s/index.mctex", context->borrowed_textures, name);
    if (!gi_file_exists(pixels)) {
        gi_report(context, node->line, node->column, "texture '%s' does not exist: no %s", name, pixels);
        return;
    }
    item->borrowed_texture = name;
}

static bool gi_block_exists(gi_context_t *context, const char *name) {
    return gi_file_exists(tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", context->borrowed_blocks, name));
}

static void gi_read_block(gi_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                          gi_item_t *item) {
    const yaml_node_t *node = yaml_map_get(document, root, "block");
    if (node == nullptr) {
        /* Dirt that placed nothing would be a mistake that no other check catches. */
        if (gi_block_exists(context, item->borrowed_name)) {
            gi_report(context, root->line, root->column, "item '%s' is named for a block; say block: %s",
                      item->borrowed_name, item->borrowed_name);
        }
        return;
    }
    const char *name = gi_read_text(context, node);
    if (name == nullptr) {
        return;
    }
    if (strcmp(name, GI_AIR) == 0) {
        gi_report(context, node->line, node->column, "an item cannot place %s", GI_AIR);
    } else if (!gi_name_is_valid(name) || !gi_block_exists(context, name)) {
        gi_report(context, node->line, node->column, "block '%s' does not exist: no %s/%s/index.yaml", name,
                  context->borrowed_blocks, name);
    } else {
        item->borrowed_block = name;
    }
}

static void gi_read_armor(gi_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                          gi_item_t *item) {
    if (map->kind != YAML_KIND_MAP) {
        gi_report(context, map->line, map->column, "armor must be a mapping with slot and points");
        return;
    }
    gi_reject_unknown(context, document, map, "armor", GI_ARMOR_FIELDS, GI_ARMOR_FIELD_COUNT);
    const yaml_node_t *slot = yaml_map_get(document, map, "slot");
    const yaml_node_t *points = yaml_map_get(document, map, "points");
    if (slot == nullptr || points == nullptr) {
        gi_report(context, map->line, map->column, "armor needs both slot and points");
        return;
    }
    gi_read_choice(context, slot, "armor slot", GI_SLOTS, GI_SLOT_COUNT, &item->armor_slot);
    if (item->armor_slot == GI_SLOT_NONE) {
        gi_report(context, slot->line, slot->column, "armor must go on a slot");
    }
    gi_read_small(context, points, "armor points", 1, GI_ARMOR_LIMIT, &item->armor_points);
    const yaml_node_t *toughness = yaml_map_get(document, map, "toughness");
    if (toughness != nullptr) {
        gi_read_small(context, toughness, "armor toughness", 1, GI_ARMOR_LIMIT, &item->armor_toughness);
    }
}

/* What fields say about one another, once they have all been read. */
static void gi_check_together(gi_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                              const gi_item_t *item) {
    const yaml_node_t *const tier = yaml_map_get(document, root, "tier");
    const yaml_node_t *const tool = yaml_map_get(document, root, "tool");
    const yaml_node_t *const durability = yaml_map_get(document, root, "durability");
    const yaml_node_t *const saturation = yaml_map_get(document, root, "saturation");
    const yaml_node_t *const armor = yaml_map_get(document, root, "armor");
    const bool tiered = item->tool != GI_TOOL_NONE && item->tool != GI_TOOL_SHEARS;
    if (item->tier != GI_TIER_NONE && !tiered) {
        gi_report(context, tier->line, tier->column, "tier '%s' needs a tool that has tiers",
                  context->borrowed_tiers[item->tier]);
    }
    if (tiered && item->tier == GI_TIER_NONE) {
        gi_report(context, tool->line, tool->column, "tool '%s' needs a tier", GI_TOOLS[item->tool]);
    }
    if ((item->tool != GI_TOOL_NONE || armor != nullptr) && item->durability == 0u) {
        gi_report(context, root->line, root->column, "a tool or armor needs a durability");
    }
    if (item->tool != GI_TOOL_NONE && armor != nullptr) {
        gi_report(context, armor->line, armor->column, "an item cannot be both a tool and armor");
    }
    /* A stack holds one wear, so what wears cannot share one. */
    if (durability != nullptr && item->stack != 1u) {
        gi_report(context, durability->line, durability->column, "what has a durability must have stack: 1");
    }
    if (durability != nullptr && item->borrowed_block != nullptr) {
        gi_report(context, durability->line, durability->column, "an item that places a block has no durability");
    }
    if (saturation != nullptr && item->food == 0u) {
        gi_report(context, saturation->line, saturation->column, "saturation needs a food");
    }
}

/* One parsed file into one item. Every field absent from the file keeps the default given here. */
static void gi_read_fields(gi_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                           gi_item_t *item) {
    gi_reject_unknown(context, document, root, "an item", GI_ITEM_FIELDS, GI_ITEM_FIELD_COUNT);

    const yaml_node_t *name = yaml_map_get(document, root, "name");
    if (name == nullptr) {
        gi_report(context, root->line, root->column, "missing field 'name'");
    } else {
        const char *text = gi_read_text(context, name);
        if (text != nullptr && strcmp(text, item->borrowed_name) != 0) {
            gi_report(context, name->line, name->column, "name '%s' is not its directory's, '%s'", text,
                      item->borrowed_name);
        }
    }

    const yaml_node_t *texture = yaml_map_get(document, root, "texture");
    if (texture == nullptr) {
        gi_report(context, root->line, root->column, "missing field 'texture'");
    } else {
        gi_read_texture(context, texture, item);
    }

    item->stack = GI_STACK_LIMIT;
    gi_read_small(context, yaml_map_get(document, root, "stack"), "stack", 1, GI_STACK_LIMIT, &item->stack);
    gi_read_block(context, document, root, item);
    gi_read_choice(context, yaml_map_get(document, root, "tool"), "tool", GI_TOOLS, GI_TOOL_COUNT, &item->tool);
    gi_read_choice(context, yaml_map_get(document, root, "tier"), "tier", context->borrowed_tiers, context->tier_count,
                   &item->tier);
    gi_read_number(context, yaml_map_get(document, root, "durability"), "durability", 1, GI_DURABILITY_LIMIT,
                   &item->durability);
    gi_read_decimal(context, yaml_map_get(document, root, "damage"), "damage", GI_DAMAGE_LIMIT, &item->damage);
    gi_read_small(context, yaml_map_get(document, root, "food"), "food", 1, GI_FOOD_LIMIT, &item->food);
    gi_read_decimal(context, yaml_map_get(document, root, "saturation"), "saturation", GI_FOOD_LIMIT,
                    &item->saturation);
    const yaml_node_t *armor = yaml_map_get(document, root, "armor");
    if (armor != nullptr) {
        gi_read_armor(context, document, armor, item);
    }
    gi_check_together(context, document, root, item);
}

/* Reads one item's file. What it finds wrong is counted in the context. */
static void gi_read_item(gi_context_t *context, const char *items, const char *name, gi_item_t *item) {
    memset(item, 0, sizeof(*item));
    item->borrowed_name = name;
    item->borrowed_path = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", items, name);
    context->borrowed_path = item->borrowed_path;
    if (!gi_name_is_valid(name)) {
        gi_report(context, 1, 1, "directory name '%s' is not lower case, digits and underscores", name);
        return;
    }
    if (strcmp(name, GI_NONE) == 0) {
        gi_report(context, 1, 1, "'%s' is the name of the empty row, which has no file", GI_NONE);
        return;
    }
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status =
        tp_read_path(context->borrowed_arena, item->borrowed_path, GI_MAX_FILE_BYTES, &text, &length);
    if (status != TP_READ_OK) {
        if (status == TP_READ_MISSING) {
            fprintf(stderr, "%s: missing\n", item->borrowed_path);
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
        gi_report_yaml(context, &error);
        return;
    }
    const yaml_node_t *root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        gi_report(context, root->line == 0u ? 1u : root->line, root->column == 0u ? 1u : root->column,
                  "an item's file must be a mapping");
        return;
    }
    gi_read_fields(context, &document, root, item);
}

static int32_t gi_compare_text(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Every directory under items, sorted, so what the tables hold does not depend on the order of reading. */
static bool gi_list_items(tp_arena_t *arena, const char *items, const char ***out_names, uint32_t *out_count) {
    const char **names = tp_arena_alloc(arena, (GI_MAX_ITEMS + 1ull) * sizeof(const char *));
    uint32_t count = 0;
    DIR *listing = opendir(items);
    if (listing == nullptr) {
        return tp_fail("%s: cannot be listed", items);
    }
    for (struct dirent *entry = readdir(listing); entry != nullptr; entry = readdir(listing)) {
        struct stat info;
        const char *child = tp_arena_format(arena, "%s/%s", items, entry->d_name);
        if (entry->d_name[0] == '.' || stat(child, &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        if (count == GI_MAX_ITEMS) {
            closedir(listing);
            return tp_fail("%s: holds over %u items", items, GI_MAX_ITEMS);
        }
        names[count] = tp_arena_text(arena, entry->d_name, strlen(entry->d_name));
        count++;
    }
    closedir(listing);
    qsort(names, count, sizeof(const char *), gi_compare_text);
    *out_names = names;
    *out_count = count;
    return true;
}

/* In upper case, for an enum name. Names were checked to be short. */
static const char *gi_upper(tp_arena_t *arena, const char *name) {
    char *text = tp_arena_text(arena, name, strlen(name));
    for (char *c = text; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return text;
}

static void gi_add_enum(tp_arena_t *arena, tp_builder_t *header, const char *type, const char *prefix,
                        const char *const *names, uint32_t count) {
    tp_builder_add(header, "typedef enum %s : uint8_t {\n", type);
    for (uint32_t i = 0; i < count; i++) {
        tp_builder_add(header, "    %s%s = %u,\n", prefix, gi_upper(arena, names[i]), i);
    }
    tp_builder_add(header, "} %s;\n\n", type);
}

static void gi_emit_header(tp_arena_t *arena, tp_builder_t *out, const gi_item_t *items, uint32_t count,
                           const char *const *tier_names, uint32_t tier_count) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/items/. Change the yaml and build; never edit this. */\n"
        "#ifndef TABLES_ITEMS_INDEX_H\n#define TABLES_ITEMS_INDEX_H\n\n"
        "#include \"types/index.h\"\n\n#include <stdint.h>\n\n");
    tp_builder_add(out,
                   "/* Item 0 is none, so a zeroed stack is empty, and the rest sort by name: adding an item moves the "
                   "ids after it. */\n");
    tp_builder_add(out, "typedef enum item_id_t : uint16_t {\n    ITEM_ID_%s = 0,\n", gi_upper(arena, GI_NONE));
    for (uint32_t i = 0; i < count; i++) {
        tp_builder_add(out, "    ITEM_ID_%s = %u,\n", gi_upper(arena, items[i].borrowed_name), i + 1u);
    }
    tp_builder_add(out, "} item_id_t;\n\n");
    tp_builder_add(
        out, "#define ITEM_COUNT %uu\n/* The most any stack of anything holds. */\n#define ITEM_STACK_LIMIT %uu\n",
        count + 1u, (uint32_t)GI_STACK_LIMIT);
    tp_builder_add(out, "/* In item_def_t.block: this item places nothing. */\n#define ITEM_NO_BLOCK 0xFFFFu\n\n");
    tp_builder_add(out, "/* A tool's tier, a file of config/tiers/, by name after none. What it breaks is item_tiers' "
                        "harvest_tier, so gold, "
                        "which breaks what wood does, is no rung of a block's tier. */\n");
    gi_add_enum(arena, out, "item_tier_t", "ITEM_TIER_", tier_names, tier_count);
    tp_builder_add(out,
                   "#define ITEM_TIER_COUNT %uu\n\n"
                   "/* One row per tier, as its file says. Row 0, none, is a bare hand. */\n"
                   "typedef struct item_tier_def_t {\n"
                   "    const char *name;           /* 0 */\n"
                   "    float32_t axe_attack_speed; /* 8: swings a second with an axe of it */\n"
                   "    float32_t hoe_attack_speed; /* 12 */\n"
                   "    uint16_t speed_milli;       /* 16: thousandths of a bare hand, on its own tool's blocks */\n"
                   "    uint8_t harvest_tier;       /* 18: the block_tier_t it breaks */\n"
                   "    uint8_t _pad0[5];           /* 19 */\n"
                   "} item_tier_def_t;\n"
                   "static_assert(sizeof(item_tier_def_t) == 24);\n\n"
                   "extern const item_tier_def_t item_tiers[ITEM_TIER_COUNT];\n\n",
                   tier_count);
    gi_add_enum(arena, out, "item_armor_slot_t", "ITEM_ARMOR_SLOT_", GI_SLOTS, GI_SLOT_COUNT);
    tp_builder_add(out,
                   "/* One row per item. */\n"
                   "typedef struct item_def_t {\n"
                   "    const char *name;      /* 0 */\n"
                   "    const char *texture;   /* 8: \"items/coal\" or \"blocks/dirt\"; nullptr only for none */\n"
                   "    float32_t damage;      /* 16: what a hit with it does, in half hearts; 0 unless it says */\n"
                   "    float32_t saturation;  /* 20: what eating it gives on top of food */\n"
                   "    uint16_t block;        /* 24: the block_id_t it places, or ITEM_NO_BLOCK */\n"
                   "    uint16_t durability;   /* 26: uses it has in it; 0 if it does not wear */\n"
                   "    uint8_t max_stack;     /* 28: 1 to ITEM_STACK_LIMIT; 0 only for none */\n"
                   "    uint8_t tool;          /* 29: a block_tool_t */\n"
                   "    uint8_t tier;          /* 30: an item_tier_t */\n"
                   "    uint8_t harvest_tier;  /* 31: the block_tier_t it counts as */\n"
                   "    uint8_t food;          /* 32: half drumsticks eating it gives; 0 if it is not food */\n"
                   "    uint8_t armor_slot;    /* 33: an item_armor_slot_t */\n"
                   "    uint8_t armor_points;  /* 34: half shirts it gives when worn */\n"
                   "    uint8_t armor_toughness; /* 35: what it adds against hard hits when worn */\n"
                   "    uint8_t _pad0[4];      /* 36 */\n"
                   "} item_def_t;\n"
                   "static_assert(sizeof(item_def_t) == 40);\n\n"
                   "extern const item_def_t item_defs[ITEM_COUNT];\n\n"
                   "#endif\n");
}

static void gi_add_decimal(tp_builder_t *out, yaml_decimal_t value) {
    tp_builder_add(out, "%" PRId64 "e%df32", value.mantissa, value.exponent);
}

static void gi_emit_source(tp_arena_t *arena, tp_builder_t *out, const gi_item_t *items, uint32_t count,
                           const gi_tier_t *tiers, uint32_t tier_count) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/items/. Change the yaml and build; never edit this. */\n"
        "#include \"tables/items/index.h\"\n\n"
        "#include \"tables/blocks/index.h\"\n\n"
        "const item_tier_def_t item_tiers[ITEM_TIER_COUNT] = {\n"
        "    [ITEM_TIER_NONE] = {.name = \"none\", .speed_milli = 1000, .harvest_tier = BLOCK_TIER_NONE},\n");
    for (uint32_t i = 1u; i < tier_count; i++) {
        const gi_tier_t *const tier = &tiers[i];
        tp_builder_add(out, "    [ITEM_TIER_%s] = {.name = \"%s\", .axe_attack_speed = ",
                       gi_upper(arena, tier->borrowed_name), tier->borrowed_name);
        gi_add_decimal(out, tier->axe_attack_speed);
        tp_builder_add(out, ", .hoe_attack_speed = ");
        gi_add_decimal(out, tier->hoe_attack_speed);
        tp_builder_add(out, ", .speed_milli = %u, .harvest_tier = BLOCK_TIER_%s},\n", tier->speed,
                       gi_upper(arena, GI_RUNGS[tier->harvests]));
    }
    tp_builder_add(out, "};\n\nconst item_def_t item_defs[ITEM_COUNT] = {\n");
    tp_builder_add(out, "    [ITEM_ID_NONE] = {.name = \"%s\", .block = ITEM_NO_BLOCK},\n", GI_NONE);
    for (uint32_t i = 0; i < count; i++) {
        const gi_item_t *const item = &items[i];
        tp_builder_add(out, "    [ITEM_ID_%s] = {.name = \"%s\", .texture = \"%s\", .damage = ",
                       gi_upper(arena, item->borrowed_name), item->borrowed_name, item->borrowed_texture);
        gi_add_decimal(out, item->damage);
        tp_builder_add(out, ", .saturation = ");
        gi_add_decimal(out, item->saturation);
        if (item->borrowed_block != nullptr) {
            tp_builder_add(out, ", .block = BLOCK_ID_%s", gi_upper(arena, item->borrowed_block));
        } else {
            tp_builder_add(out, ", .block = ITEM_NO_BLOCK");
        }
        tp_builder_add(out, ", .durability = %u, .max_stack = %u, .tool = BLOCK_TOOL_%s, .tier = ITEM_TIER_%s",
                       item->durability, item->stack, gi_upper(arena, GI_TOOLS[item->tool]),
                       gi_upper(arena, item->tier == GI_TIER_NONE ? "none" : tiers[item->tier].borrowed_name));
        tp_builder_add(out, ", .harvest_tier = BLOCK_TIER_%s, .food = %u, .armor_slot = ITEM_ARMOR_SLOT_%s",
                       gi_upper(arena, GI_RUNGS[tiers[item->tier].harvests]), item->food,
                       gi_upper(arena, GI_SLOTS[item->armor_slot]));
        tp_builder_add(out, ", .armor_points = %u, .armor_toughness = %u},\n", item->armor_points,
                       item->armor_toughness);
    }
    tp_builder_add(out, "};\n");
}

/* One tier's file into one row. What it finds wrong is counted in the context. */
static void gi_read_tier(gi_context_t *context, const char *directory, const char *name, gi_tier_t *tier) {
    memset(tier, 0, sizeof(*tier));
    tier->borrowed_name = name;
    context->borrowed_path = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", directory, name);
    if (!gi_name_is_valid(name) || strcmp(name, GI_NONE) == 0) {
        gi_report(context, 1, 1, "a tier's directory is lower case, digits and underscores, and not '%s'", GI_NONE);
        return;
    }
    uint8_t *text = nullptr;
    uint64_t length = 0;
    if (tp_read_path(context->borrowed_arena, context->borrowed_path, GI_MAX_FILE_BYTES, &text, &length) !=
        TP_READ_OK) {
        gi_report(context, 1, 1, "cannot be read");
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
        gi_report_yaml(context, &error);
        return;
    }
    const yaml_node_t *const root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        gi_report(context, 1, 1, "a tier's file must be a mapping");
        return;
    }
    gi_reject_unknown(context, &document, root, "a tier", GI_TIER_FIELDS, GI_TIER_FIELD_COUNT);
    const yaml_node_t *const own = yaml_map_get(&document, root, "name");
    const char *const said = own != nullptr ? gi_read_text(context, own) : nullptr;
    if (said == nullptr || strcmp(said, name) != 0) {
        gi_report(context, root->line, root->column, "a tier's name is its directory's, '%s'", name);
    }
    const char *const fields[] = {"harvests", "speed", "axe_attack_speed", "hoe_attack_speed"};
    for (uint32_t i = 0u; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (yaml_map_get(&document, root, fields[i]) == nullptr) {
            gi_report(context, root->line, root->column, "missing field '%s'", fields[i]);
        }
    }
    gi_read_choice(context, yaml_map_get(&document, root, "harvests"), "rung", GI_RUNGS, GI_RUNG_COUNT,
                   &tier->harvests);
    gi_read_number(context, yaml_map_get(&document, root, "speed"), "speed", GI_TIER_SPEED_MIN, GI_TIER_SPEED_MAX,
                   &tier->speed);
    gi_read_decimal(context, yaml_map_get(&document, root, "axe_attack_speed"), "axe_attack_speed",
                    GI_ATTACK_SPEED_LIMIT, &tier->axe_attack_speed);
    gi_read_decimal(context, yaml_map_get(&document, root, "hoe_attack_speed"), "hoe_attack_speed",
                    GI_ATTACK_SPEED_LIMIT, &tier->hoe_attack_speed);
}

/* Every tier, none first and then by name. A tree with no config/tiers/ has only none, so a tool in it is refused for want of a tier. */
static bool gi_read_tiers(gi_context_t *context, const char *config, gi_tier_t **out_tiers) {
    tp_arena_t *const arena = context->borrowed_arena;
    const char *const directory = tp_arena_format(arena, "%s/tiers", config);
    const char **names = nullptr;
    uint32_t count = 0u;
    struct stat info;
    if (stat(directory, &info) == 0 && !gi_list_items(arena, directory, &names, &count)) {
        return false;
    }
    if (count + 1u > GI_MAX_TIERS) {
        return tp_fail("%s: holds over %u tiers", directory, GI_MAX_TIERS - 1u);
    }
    gi_tier_t *const tiers = tp_arena_alloc(arena, (count + 1ull) * sizeof(gi_tier_t));
    const char **const words = tp_arena_alloc(arena, (count + 1ull) * sizeof(const char *));
    memset(&tiers[0], 0, sizeof(tiers[0]));
    tiers[0].borrowed_name = GI_NONE;
    tiers[0].speed = GI_TIER_SPEED_MIN;
    words[0] = GI_NONE;
    for (uint32_t i = 0u; i < count; i++) {
        gi_read_tier(context, directory, names[i], &tiers[i + 1u]);
        words[i + 1u] = names[i];
    }
    context->borrowed_tiers = words;
    context->tier_count = count + 1u;
    *out_tiers = tiers;
    return true;
}

bool game_tables_items_build(tp_arena_t *arena, const char *config, const char *textures, const char *out) {
    const char *items_path = tp_arena_format(arena, "%s/items", config);
    const char **names = nullptr;
    uint32_t count = 0;
    if (!gi_list_items(arena, items_path, &names, &count)) {
        return false;
    }
    gi_context_t context = {
        .borrowed_arena = arena,
        .borrowed_textures = textures,
        .borrowed_blocks = tp_arena_format(arena, "%s/blocks", config),
    };
    gi_tier_t *tiers = nullptr;
    if (!gi_read_tiers(&context, config, &tiers)) {
        return false;
    }
    gi_item_t *items = tp_arena_alloc(arena, (count + 1ull) * sizeof(gi_item_t));
    for (uint32_t i = 0; i < count; i++) {
        gi_read_item(&context, items_path, names[i], &items[i]);
    }
    if (context.problems != 0u) {
        return tp_fail("%s: %u problems; nothing was written", items_path, context.problems);
    }
    tp_builder_t header = tp_builder_begin(arena, GI_HEADER_BYTES);
    tp_builder_t source = tp_builder_begin(arena, GI_SOURCE_BYTES);
    gi_emit_header(arena, &header, items, count, context.borrowed_tiers, context.tier_count);
    gi_emit_source(arena, &source, items, count, tiers, context.tier_count);

    const char *directory = tp_arena_format(arena, "%s/items", out);
    return tp_make_directories(arena, directory) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.h", directory), header.borrowed_text,
                               header.used, nullptr) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.c", directory), source.borrowed_text,
                               source.used, nullptr);
}
