/*
 * config/recipes/ -> the recipe tables. See README.md. Like the other parts: read and check each file on its own, then what they say
 * about one another, then emit. A problem does not stop the reading, so one build shows all of them.
 */
#include "game_tables/recipes/index.h"

#include "yaml/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GR_MAX_RECIPES 4096u
#define GR_MAX_NAME_BYTES 64u
#define GR_MAX_FILE_BYTES (1ull << 20)
#define GR_MAX_LIST 256u
#define GR_SIDE 3u
#define GR_CELLS (GR_SIDE * GR_SIDE)
#define GR_STACK_DEFAULT 64
#define GR_SMELT_TICKS_DEFAULT 200
#define GR_TICKS_LIMIT 65535
#define GR_HEADER_BYTES (1ull << 18)
#define GR_SOURCE_BYTES (1ull << 21)
#define GR_GROUP_PREFIX "group_"
#define GR_GROUP_PREFIX_BYTES 6u

typedef enum gr_kind_t : uint8_t {
    GR_KIND_SHAPED = 0,
    GR_KIND_SHAPELESS = 1,
    GR_KIND_SMELT = 2,
    GR_KIND_FUEL = 3,
    GR_KIND_GROUP = 4,
    GR_KIND_COUNT = 5,
} gr_kind_t;
static const char *const GR_KINDS[GR_KIND_COUNT] = {"shaped", "shapeless", "smelt", "fuel", "group"};

/* Where a recipe is made, in the order of the numbers the tables hold. The first is what no line means. The order is the one saves were
 * written in, so a new station goes on the end. */
typedef enum gr_station_t : uint8_t {
    GR_STATION_TABLE = 0,
    GR_STATION_WOODWORKING = 1,
    GR_STATION_STONEWORKING = 2,
    GR_STATION_SMITHING = 3,
    GR_STATION_LEATHERWORKING = 4,
    GR_STATION_HANDS = 5,
    GR_STATION_COUNT = 6,
} gr_station_t;
static const char *const GR_STATIONS[GR_STATION_COUNT] = {"table",    "woodworking",    "stoneworking",
                                                          "smithing", "leatherworking", "hands"};
/* The hands' grid is 2 by 2: what is made there must fit it. */
#define GR_HANDS_SIDE 2u

/* Where a smelt is done: a timed process in a block, the first what no line means. The last burns nothing: a hide tans with time alone. */
typedef enum gr_process_t : uint8_t {
    GR_PROCESS_FURNACE = 0,
    GR_PROCESS_BLAST_FURNACE = 1,
    GR_PROCESS_TANNING_RACK = 2,
    GR_PROCESS_COUNT = 3,
} gr_process_t;
static const char *const GR_PROCESSES[GR_PROCESS_COUNT] = {"furnace", "blast_furnace", "tanning_rack"};
static const uint8_t GR_PROCESS_BURNS[GR_PROCESS_COUNT] = {1u, 1u, 0u};

static const char *const GR_FIELDS_SHAPED[] = {"name", "kind", "pattern", "keys", "gives", "count", "station", "off"};
static const char *const GR_FIELDS_SHAPELESS[] = {"name", "kind", "ingredients", "gives", "count", "station", "off"};
static const char *const GR_FIELDS_SMELT[] = {"name", "kind", "input", "gives", "count", "ticks", "station", "off"};
static const char *const GR_FIELDS_FUEL[] = {"name", "kind", "items", "ticks", "leaves"};
static const char *const GR_FIELDS_GROUP[] = {"name", "kind", "items"};
static const char *const *const GR_FIELDS[GR_KIND_COUNT] = {GR_FIELDS_SHAPED, GR_FIELDS_SHAPELESS, GR_FIELDS_SMELT,
                                                            GR_FIELDS_FUEL, GR_FIELDS_GROUP};
static const uint8_t GR_FIELD_COUNTS[GR_KIND_COUNT] = {8u, 7u, 8u, 5u, 3u};

/* One file, as read. Everything it points to is in the arena. A cell, an input and a list member may each name a group. */
typedef struct gr_recipe_t {
    const char *borrowed_name;            /* 0: its directory's, so a broken file still has one */
    const char *borrowed_path;            /* 8 */
    const char *borrowed_gives;           /* 16: nullptr for a fuel or a group */
    const char *borrowed_leaves;          /* 24: a fuel's: what stays in the slot, or nullptr */
    const char *borrowed_cells[GR_CELLS]; /* 32: by row, nullptr for an empty one; a list's are from the first */
    const char **borrowed_list;           /* 104: a fuel's or a group's items */
    uint32_t list_count;                  /* 112 */
    uint32_t ticks;                       /* 116: a smelt's or a fuel's */
    uint8_t kind;                         /* 120: a gr_kind_t */
    uint8_t count;                        /* 121: what is given */
    uint8_t width;                        /* 122: a shaped one's; 0 for what has no shape */
    uint8_t height;                       /* 123 */
    uint8_t cell_count;                   /* 124: cells that hold something */
    uint8_t good;                         /* 125: it read with no problem, so its names can be looked into */
    uint8_t station;                      /* 126: a gr_station_t, or for a smelt a gr_process_t */
    uint8_t off;                          /* 127: its file turns it off: read and checked, and left out of the tables */
} gr_recipe_t;
static_assert(sizeof(gr_recipe_t) == 128);

typedef struct gr_context_t {
    tp_arena_t *borrowed_arena;   /* 0 */
    const char *borrowed_items;   /* 8: <config>/items */
    const char *borrowed_recipes; /* 16: <config>/recipes */
    const char *borrowed_path;    /* 24: the file being read, for reporting */
    uint32_t problems;            /* 32 */
    uint8_t _pad0[4];             /* 36 */
} gr_context_t;
static_assert(sizeof(gr_context_t) == 40);

/* A list of names a recipe's cell can be: one item, or the items of a group. */
typedef struct gr_names_t {
    const char *const *borrowed_names; /* 0 */
    uint32_t count;                    /* 8 */
    uint8_t _pad0[4];                  /* 12 */
} gr_names_t;
static_assert(sizeof(gr_names_t) == 16);

[[gnu::format(printf, 4, 5)]]
static void gr_report(gr_context_t *context, uint32_t line, uint32_t column, const char *format, ...) {
    fprintf(stderr, "%s:%u:%u: ", context->borrowed_path, line, column);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    context->problems++;
}

static void gr_report_yaml(gr_context_t *context, const yaml_error_t *error) {
    gr_report(context, error->line, error->column, "%s", error->message);
}

static bool gr_key_is(const yaml_node_t *node, const char *key) {
    const uint64_t length = strlen(key);
    return node->key_length == length && memcmp(node->borrowed_key, key, length) == 0;
}

static bool gr_name_is_valid(const char *name) {
    const uint64_t length = strlen(name);
    if (length == 0u || length > GR_MAX_NAME_BYTES || name[0] < 'a' || name[0] > 'z') {
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

static bool gr_is_group_name(const char *name) {
    return strncmp(name, GR_GROUP_PREFIX, GR_GROUP_PREFIX_BYTES) == 0;
}

static bool gr_file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool gr_item_exists(gr_context_t *context, const char *name) {
    return gr_name_is_valid(name) &&
           gr_file_exists(tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", context->borrowed_items, name));
}

static bool gr_group_exists(gr_context_t *context, const char *name) {
    return gr_name_is_valid(name) && gr_is_group_name(name) &&
           gr_file_exists(
               tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", context->borrowed_recipes, name));
}

/* A NUL-terminated copy of a scalar, or nullptr with the problem reported. */
static const char *gr_read_text(gr_context_t *context, const yaml_node_t *node) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        gr_report_yaml(context, &error);
        return nullptr;
    }
    return tp_arena_text(context->borrowed_arena, text, length);
}

/* A name that must be an item, or nullptr with the problem reported. */
static const char *gr_read_item(gr_context_t *context, const yaml_node_t *node) {
    const char *const name = gr_read_text(context, node);
    if (name == nullptr) {
        return nullptr;
    }
    if (!gr_item_exists(context, name)) {
        gr_report(context, node->line, node->column, "item '%s' does not exist: no %s/%s/index.yaml", name,
                  context->borrowed_items, name);
        return nullptr;
    }
    return name;
}

/* A name that may be an item or a group, or nullptr with the problem reported. */
static const char *gr_read_choice(gr_context_t *context, const yaml_node_t *node) {
    const char *const name = gr_read_text(context, node);
    if (name == nullptr) {
        return nullptr;
    }
    if (!gr_item_exists(context, name) && !gr_group_exists(context, name)) {
        gr_report(context, node->line, node->column,
                  "'%s' is no item and no group: no %s/%s/index.yaml and no %s/%s/index.yaml", name,
                  context->borrowed_items, name, context->borrowed_recipes, name);
        return nullptr;
    }
    return name;
}

/* An integer from minimum to maximum. An absent node leaves out alone. */
static void gr_read_number(gr_context_t *context, const yaml_node_t *node, const char *what, int64_t minimum,
                           int64_t maximum, uint32_t *out) {
    if (node == nullptr) {
        return;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        gr_report_yaml(context, &error);
    } else if (value < minimum || value > maximum) {
        gr_report(context, node->line, node->column, "%s is %" PRId64 ", and must be %" PRId64 " to %" PRId64, what,
                  value, minimum, maximum);
    } else {
        *out = (uint32_t)value;
    }
}

/* Parses a whole file into the arena. False if it could not be: the caller reports with error, or sees error.line 0 for no file. */
static bool gr_parse_path(tp_arena_t *arena, const char *path, yaml_document_t *document, yaml_error_t *error) {
    uint8_t *text = nullptr;
    uint64_t length = 0;
    if (tp_read_path(arena, path, GR_MAX_FILE_BYTES, &text, &length) != TP_READ_OK) {
        return false;
    }
    yaml_storage_t storage = {
        .nodes = tp_arena_alloc(arena, (length + 1u) * sizeof(yaml_node_t)),
        .strings = tp_arena_alloc(arena, length + 1u),
        .node_capacity = (uint32_t)(length + 1u),
        .string_capacity = (uint32_t)length,
    };
    return yaml_parse(document, text, length, storage, error);
}

/* The most of an item a slot holds, as its own file says, since a recipe must not give more than that at once. */
static uint32_t gr_item_stack(gr_context_t *context, const char *name) {
    uint32_t stack = GR_STACK_DEFAULT;
    yaml_document_t document;
    yaml_error_t error = {0};
    if (!gr_parse_path(context->borrowed_arena,
                       tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", context->borrowed_items, name),
                       &document, &error) ||
        yaml_root(&document)->kind != YAML_KIND_MAP) {
        return stack; /* the items part has said what is wrong with it */
    }
    const yaml_node_t *const node = yaml_map_get(&document, yaml_root(&document), "stack");
    int64_t value = 0;
    if (node != nullptr && yaml_get_int64(node, &value, &error) && value > 0) {
        stack = (uint32_t)value;
    }
    return stack;
}

static void gr_read_gives(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                          gr_recipe_t *recipe) {
    const yaml_node_t *const gives = yaml_map_get(document, root, "gives");
    if (gives == nullptr) {
        gr_report(context, root->line, root->column, "missing field 'gives'");
        return;
    }
    recipe->borrowed_gives = gr_read_item(context, gives);
    uint32_t count = 1u;
    const yaml_node_t *const count_node = yaml_map_get(document, root, "count");
    gr_read_number(context, count_node, "count", 1, GR_STACK_DEFAULT, &count);
    recipe->count = (uint8_t)count;
    if (recipe->borrowed_gives != nullptr && count_node != nullptr &&
        count > gr_item_stack(context, recipe->borrowed_gives)) {
        gr_report(context, count_node->line, count_node->column, "count %u is more than one slot of %s holds", count,
                  recipe->borrowed_gives);
    }
}

/* The pattern's rows, into width, height and which character each cell is. A blank line or column at an edge has no meaning, since a
 * grid is matched by what it holds, and so is refused rather than quietly dropped. */
static bool gr_read_pattern(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                            gr_recipe_t *recipe, char cells[GR_CELLS]) {
    const yaml_node_t *const pattern = yaml_map_get(document, root, "pattern");
    if (pattern == nullptr || pattern->kind != YAML_KIND_SEQ) {
        gr_report(context, root->line, root->column, "a shaped recipe needs a pattern, a list of rows");
        return false;
    }
    if (pattern->child_count == 0u || pattern->child_count > GR_SIDE) {
        gr_report(context, pattern->line, pattern->column, "a pattern has 1 to %u rows", GR_SIDE);
        return false;
    }
    uint32_t height = 0u;
    uint64_t width = 0u;
    bool row_used[GR_SIDE] = {false, false, false};
    bool column_used[GR_SIDE] = {false, false, false};
    for (const yaml_node_t *row = yaml_first_child(document, pattern); row != nullptr;
         row = yaml_next_sibling(document, row)) {
        const char *const text = gr_read_text(context, row);
        if (text == nullptr) {
            return false;
        }
        const uint64_t length = strlen(text);
        if (length == 0u || length > GR_SIDE) {
            gr_report(context, row->line, row->column, "a row has 1 to %u cells", GR_SIDE);
            return false;
        }
        if (height != 0u && length != width) {
            gr_report(context, row->line, row->column, "this row is not as long as the first");
            return false;
        }
        width = length;
        for (uint64_t x = 0u; x < length; x++) {
            cells[(uint64_t)height * GR_SIDE + x] = text[x];
            if (text[x] != ' ') {
                row_used[height] = true;
                column_used[x] = true;
            }
        }
        height++;
    }
    if (!row_used[0] || !row_used[height - 1u] || !column_used[0] || !column_used[width - 1u]) {
        gr_report(context, pattern->line, pattern->column,
                  "a pattern must not have an empty row or column at its edge");
        return false;
    }
    recipe->width = (uint8_t)width;
    recipe->height = (uint8_t)height;
    return true;
}

static void gr_read_shaped(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                           gr_recipe_t *recipe) {
    char cells[GR_CELLS];
    memset(cells, ' ', sizeof(cells));
    if (!gr_read_pattern(context, document, root, recipe, cells)) {
        return;
    }
    const yaml_node_t *const keys = yaml_map_get(document, root, "keys");
    if (keys == nullptr || keys->kind != YAML_KIND_MAP) {
        gr_report(context, root->line, root->column,
                  "a shaped recipe needs keys, a mapping from each letter to what it is");
        return;
    }
    for (const yaml_node_t *key = yaml_first_child(document, keys); key != nullptr;
         key = yaml_next_sibling(document, key)) {
        if (key->key_length != 1u || key->borrowed_key[0] == ' ') {
            gr_report(context, key->line, key->column, "a key is one letter, and not a space");
            continue;
        }
        const char *const choice = gr_read_choice(context, key);
        bool used = false;
        for (uint32_t y = 0u; y < recipe->height; y++) {
            for (uint32_t x = 0u; x < recipe->width; x++) {
                if (cells[y * GR_SIDE + x] == (char)key->borrowed_key[0]) {
                    used = true;
                    recipe->borrowed_cells[y * recipe->width + x] = choice;
                    recipe->cell_count = (uint8_t)(recipe->cell_count + 1u);
                    cells[y * GR_SIDE + x] = '\0';
                }
            }
        }
        if (!used) {
            gr_report(context, key->line, key->column, "key '%c' is not in the pattern", (char)key->borrowed_key[0]);
        }
    }
    for (uint32_t y = 0u; y < recipe->height; y++) {
        for (uint32_t x = 0u; x < recipe->width; x++) {
            const char left = cells[y * GR_SIDE + x];
            if (left != ' ' && left != '\0') {
                gr_report(context, keys->line, keys->column, "the pattern's '%c' is not in keys", left);
                cells[y * GR_SIDE + x] = ' ';
            }
        }
    }
}

static void gr_read_shapeless(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                              gr_recipe_t *recipe) {
    const yaml_node_t *const list = yaml_map_get(document, root, "ingredients");
    if (list == nullptr || list->kind != YAML_KIND_SEQ || list->child_count == 0u || list->child_count > GR_CELLS) {
        gr_report(context, root->line, root->column, "a shapeless recipe needs ingredients, a list of 1 to %u",
                  GR_CELLS);
        return;
    }
    for (const yaml_node_t *child = yaml_first_child(document, list); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        recipe->borrowed_cells[recipe->cell_count] = gr_read_choice(context, child);
        recipe->cell_count = (uint8_t)(recipe->cell_count + 1u);
    }
}

static void gr_read_smelt(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                          gr_recipe_t *recipe) {
    const yaml_node_t *const input = yaml_map_get(document, root, "input");
    if (input == nullptr) {
        gr_report(context, root->line, root->column, "missing field 'input'");
    } else {
        recipe->borrowed_cells[0] = gr_read_choice(context, input);
        recipe->cell_count = 1u;
    }
    recipe->ticks = GR_SMELT_TICKS_DEFAULT;
    gr_read_number(context, yaml_map_get(document, root, "ticks"), "ticks", 1, GR_TICKS_LIMIT, &recipe->ticks);
}

/* A list of names, each an item, or for a fuel maybe a group. */
static void gr_read_list(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                         gr_recipe_t *recipe, bool groups_too) {
    const yaml_node_t *const list = yaml_map_get(document, root, "items");
    if (list == nullptr || list->kind != YAML_KIND_SEQ || list->child_count == 0u || list->child_count > GR_MAX_LIST) {
        gr_report(context, root->line, root->column, "items must be a list of 1 to %u", GR_MAX_LIST);
        return;
    }
    recipe->borrowed_list = tp_arena_alloc(context->borrowed_arena, (uint64_t)list->child_count * sizeof(const char *));
    for (const yaml_node_t *child = yaml_first_child(document, list); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        const char *const name = groups_too ? gr_read_choice(context, child) : gr_read_item(context, child);
        if (name != nullptr) {
            recipe->borrowed_list[recipe->list_count] = name;
            recipe->list_count++;
        }
    }
}

static void gr_read_fuel(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                         gr_recipe_t *recipe) {
    gr_read_list(context, document, root, recipe, true);
    const yaml_node_t *const ticks = yaml_map_get(document, root, "ticks");
    if (ticks == nullptr) {
        gr_report(context, root->line, root->column, "a fuel needs ticks");
    }
    gr_read_number(context, ticks, "ticks", 1, GR_TICKS_LIMIT, &recipe->ticks);
    const yaml_node_t *const leaves = yaml_map_get(document, root, "leaves");
    if (leaves != nullptr) {
        recipe->borrowed_leaves = gr_read_item(context, leaves);
    }
}

/* Where it is made: the table, or for a smelt the furnace, if the file does not say. */
static void gr_read_station(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                            gr_recipe_t *recipe) {
    const bool smelt = recipe->kind == GR_KIND_SMELT;
    const char *const *const words = smelt ? GR_PROCESSES : GR_STATIONS;
    const uint32_t word_count = smelt ? (uint32_t)GR_PROCESS_COUNT : (uint32_t)GR_STATION_COUNT;
    recipe->station = smelt ? (uint8_t)GR_PROCESS_FURNACE : (uint8_t)GR_STATION_TABLE;
    const yaml_node_t *const node = yaml_map_get(document, root, "station");
    if (node == nullptr) {
        return;
    }
    const char *const word = gr_read_text(context, node);
    if (word == nullptr) {
        return;
    }
    for (uint32_t i = 0u; i < word_count; i++) {
        if (strcmp(word, words[i]) == 0) {
            recipe->station = (uint8_t)i;
            if (!smelt && recipe->station == GR_STATION_HANDS &&
                (recipe->width > GR_HANDS_SIDE || recipe->height > GR_HANDS_SIDE ||
                 recipe->cell_count > GR_HANDS_SIDE * GR_HANDS_SIDE)) {
                gr_report(context, node->line, node->column, "a recipe made in the hands fits their grid of %u by %u",
                          GR_HANDS_SIDE, GR_HANDS_SIDE);
            }
            return;
        }
    }
    gr_report(context, node->line, node->column, "unknown %s '%s': see config/recipes/README.md",
              smelt ? "smelting station" : "station", word);
}

/* A file may turn its recipe off, saying why: it is read and checked as ever, so it still names what is real, and the tables leave it out. */
static void gr_read_off(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                        gr_recipe_t *recipe) {
    const yaml_node_t *const node = yaml_map_get(document, root, "off");
    if (node == nullptr) {
        return;
    }
    const char *const why = gr_read_text(context, node);
    if (why != nullptr && why[0] == '\0') {
        gr_report(context, node->line, node->column, "off says why it is off");
    }
    recipe->off = 1u;
}

/* One parsed file into one recipe. */
static void gr_read_fields(gr_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                           gr_recipe_t *recipe) {
    const yaml_node_t *const name = yaml_map_get(document, root, "name");
    if (name == nullptr) {
        gr_report(context, root->line, root->column, "missing field 'name'");
    } else {
        const char *const text = gr_read_text(context, name);
        if (text != nullptr && strcmp(text, recipe->borrowed_name) != 0) {
            gr_report(context, name->line, name->column, "name '%s' is not its directory's, '%s'", text,
                      recipe->borrowed_name);
        }
    }
    const yaml_node_t *const kind = yaml_map_get(document, root, "kind");
    if (kind == nullptr) {
        gr_report(context, root->line, root->column, "missing field 'kind'");
        return;
    }
    const char *const word = gr_read_text(context, kind);
    if (word == nullptr) {
        return;
    }
    recipe->kind = GR_KIND_COUNT;
    for (uint32_t i = 0u; i < GR_KIND_COUNT; i++) {
        if (strcmp(word, GR_KINDS[i]) == 0) {
            recipe->kind = (uint8_t)i;
        }
    }
    if (recipe->kind == GR_KIND_COUNT) {
        gr_report(context, kind->line, kind->column, "unknown kind '%s'", word);
        return;
    }
    if ((recipe->kind == GR_KIND_GROUP) != gr_is_group_name(recipe->borrowed_name)) {
        gr_report(context, kind->line, kind->column, "a group, and only a group, has a name that starts with %s",
                  GR_GROUP_PREFIX);
    }
    for (const yaml_node_t *child = yaml_first_child(document, root); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        bool known = false;
        for (uint32_t i = 0u; i < GR_FIELD_COUNTS[recipe->kind] && !known; i++) {
            known = gr_key_is(child, GR_FIELDS[recipe->kind][i]);
        }
        if (!known) {
            gr_report(context, child->line, child->column, "unknown field '%.*s' in a %s recipe",
                      (int32_t)child->key_length, (const char *)child->borrowed_key, word);
        }
    }
    switch (recipe->kind) {
    case GR_KIND_SHAPED:
        gr_read_shaped(context, document, root, recipe);
        gr_read_gives(context, document, root, recipe);
        gr_read_station(context, document, root, recipe);
        gr_read_off(context, document, root, recipe);
        break;
    case GR_KIND_SHAPELESS:
        gr_read_shapeless(context, document, root, recipe);
        gr_read_gives(context, document, root, recipe);
        gr_read_station(context, document, root, recipe);
        gr_read_off(context, document, root, recipe);
        break;
    case GR_KIND_SMELT:
        gr_read_smelt(context, document, root, recipe);
        gr_read_gives(context, document, root, recipe);
        gr_read_station(context, document, root, recipe);
        gr_read_off(context, document, root, recipe);
        break;
    case GR_KIND_FUEL:
        gr_read_fuel(context, document, root, recipe);
        break;
    default:
        gr_read_list(context, document, root, recipe, false);
        break;
    }
}

/* Reads one recipe's file. What it finds wrong is counted in the context. */
static void gr_read_recipe(gr_context_t *context, const char *name, gr_recipe_t *recipe) {
    memset(recipe, 0, sizeof(*recipe));
    recipe->borrowed_name = name;
    recipe->kind = GR_KIND_COUNT;
    recipe->borrowed_path =
        tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", context->borrowed_recipes, name);
    context->borrowed_path = recipe->borrowed_path;
    const uint32_t problems_before = context->problems;
    if (!gr_name_is_valid(name)) {
        gr_report(context, 1, 1, "directory name '%s' is not lower case, digits and underscores", name);
        return;
    }
    yaml_document_t document;
    yaml_error_t error = {0};
    if (!gr_parse_path(context->borrowed_arena, recipe->borrowed_path, &document, &error)) {
        if (error.line != 0u) {
            gr_report_yaml(context, &error);
        } else {
            gr_report(context, 1, 1, "cannot be read");
        }
        return;
    }
    const yaml_node_t *const root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        gr_report(context, root->line == 0u ? 1u : root->line, root->column == 0u ? 1u : root->column,
                  "a recipe's file must be a mapping");
        return;
    }
    gr_read_fields(context, &document, root, recipe);
    recipe->good = context->problems == problems_before ? 1u : 0u;
}

static int32_t gr_compare_text(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Every directory under recipes, sorted, so what the tables hold does not depend on the order of reading. */
static bool gr_list_recipes(tp_arena_t *arena, const char *recipes, const char ***out_names, uint32_t *out_count) {
    const char **names = tp_arena_alloc(arena, (GR_MAX_RECIPES + 1ull) * sizeof(const char *));
    uint32_t count = 0;
    DIR *listing = opendir(recipes);
    if (listing == nullptr) {
        return tp_fail("%s: cannot be listed", recipes);
    }
    for (struct dirent *entry = readdir(listing); entry != nullptr; entry = readdir(listing)) {
        struct stat info;
        const char *child = tp_arena_format(arena, "%s/%s", recipes, entry->d_name);
        if (entry->d_name[0] == '.' || stat(child, &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        if (count == GR_MAX_RECIPES) {
            closedir(listing);
            return tp_fail("%s: holds over %u recipes", recipes, GR_MAX_RECIPES);
        }
        names[count] = tp_arena_text(arena, entry->d_name, strlen(entry->d_name));
        count++;
    }
    closedir(listing);
    qsort(names, count, sizeof(const char *), gr_compare_text);
    *out_names = names;
    *out_count = count;
    return true;
}

/* ---- What the files say about one another --------------------------------------------------------------------------- */

/* The items a name stands for. Names were checked to exist, and a group holds only items. */
static gr_names_t gr_names_of(const gr_recipe_t *recipes, uint32_t count, const char *const *name) {
    if (gr_is_group_name(*name)) {
        for (uint32_t i = 0u; i < count; i++) {
            if (strcmp(recipes[i].borrowed_name, *name) == 0 && recipes[i].kind == GR_KIND_GROUP) {
                return (gr_names_t){.borrowed_names = recipes[i].borrowed_list, .count = recipes[i].list_count};
            }
        }
        return (gr_names_t){0};
    }
    return (gr_names_t){.borrowed_names = name, .count = 1u};
}

static bool gr_names_overlap(const gr_recipe_t *recipes, uint32_t count, const char *const *left,
                             const char *const *right) {
    const gr_names_t a = gr_names_of(recipes, count, left), b = gr_names_of(recipes, count, right);
    for (uint32_t i = 0u; i < a.count; i++) {
        for (uint32_t j = 0u; j < b.count; j++) {
            if (strcmp(a.borrowed_names[i], b.borrowed_names[j]) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* Whether the cells of a shaped one line up with another's of the same size, one way or the other way round. */
static bool gr_shaped_clash(const gr_recipe_t *recipes, uint32_t count, const gr_recipe_t *a, const gr_recipe_t *b,
                            bool mirrored) {
    for (uint32_t y = 0u; y < a->height; y++) {
        for (uint32_t x = 0u; x < a->width; x++) {
            const uint32_t bx = mirrored ? (uint32_t)a->width - 1u - x : x;
            const char *const *const left = &a->borrowed_cells[y * a->width + x];
            const char *const *const right = &b->borrowed_cells[y * a->width + bx];
            if ((*left == nullptr) != (*right == nullptr)) {
                return false;
            }
            if (*left != nullptr && !gr_names_overlap(recipes, count, left, right)) {
                return false;
            }
        }
    }
    return true;
}

/* Whether what fills a's cells could all go, one for one, in b's. Every subset of b's cells, in order of a's cells. */
static bool gr_any_order_clash(const gr_recipe_t *recipes, uint32_t count, const gr_recipe_t *a, const gr_recipe_t *b) {
    const char *const *a_cells[GR_CELLS];
    const char *const *b_cells[GR_CELLS];
    uint32_t a_count = 0u, b_count = 0u;
    for (uint32_t i = 0u; i < GR_CELLS; i++) {
        if (a->borrowed_cells[i] != nullptr) {
            a_cells[a_count] = &a->borrowed_cells[i];
            a_count++;
        }
        if (b->borrowed_cells[i] != nullptr) {
            b_cells[b_count] = &b->borrowed_cells[i];
            b_count++;
        }
    }
    if (a_count != b_count) {
        return false;
    }
    bool reachable[1u << GR_CELLS];
    memset(reachable, 0, sizeof(reachable));
    reachable[0] = true;
    const uint32_t all = (1u << b_count) - 1u;
    for (uint32_t mask = 0u; mask < all; mask++) {
        if (!reachable[mask]) {
            continue;
        }
        const uint32_t next = (uint32_t)stdc_count_ones(mask);
        for (uint32_t j = 0u; j < b_count; j++) {
            if ((mask & (1u << j)) == 0u && gr_names_overlap(recipes, count, a_cells[next], b_cells[j])) {
                reachable[mask | (1u << j)] = true;
            }
        }
    }
    return reachable[all];
}

/* Two that a grid could match both of would make what comes out depend on which is first. A grid has one station, so two of different
 * stations never meet. */
static void gr_check_clashes(gr_context_t *context, const gr_recipe_t *recipes, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        const gr_recipe_t *const a = &recipes[i];
        if ((a->kind != GR_KIND_SHAPED && a->kind != GR_KIND_SHAPELESS) || a->good == 0u || a->off != 0u) {
            continue;
        }
        for (uint32_t j = i + 1u; j < count; j++) {
            const gr_recipe_t *const b = &recipes[j];
            if ((b->kind != GR_KIND_SHAPED && b->kind != GR_KIND_SHAPELESS) || b->good == 0u || b->off != 0u ||
                b->station != a->station) {
                continue;
            }
            const bool both_shaped = a->kind == GR_KIND_SHAPED && b->kind == GR_KIND_SHAPED;
            const bool clash = both_shaped ? a->width == b->width && a->height == b->height &&
                                                 (gr_shaped_clash(recipes, count, a, b, false) ||
                                                  gr_shaped_clash(recipes, count, a, b, true))
                                           : gr_any_order_clash(recipes, count, a, b);
            if (clash) {
                context->borrowed_path = b->borrowed_path;
                gr_report(context, 1, 1, "one grid can match this and %s", a->borrowed_name);
            }
        }
    }
}

/* An item burns one way and smelts into one thing. Says so for what is named twice, by the second file. */
static void gr_check_once(gr_context_t *context, const gr_recipe_t *recipes, uint32_t count, uint8_t kind,
                          const char *what) {
    for (uint32_t i = 0u; i < count; i++) {
        const gr_recipe_t *const b = &recipes[i];
        if (b->kind != kind || b->good == 0u || b->off != 0u) {
            continue;
        }
        for (uint32_t i_list = 0u; i_list < (kind == GR_KIND_FUEL ? b->list_count : 1u); i_list++) {
            const char *const *const b_name = kind == GR_KIND_FUEL ? &b->borrowed_list[i_list] : &b->borrowed_cells[0];
            for (uint32_t j = 0u; j <= i; j++) {
                const gr_recipe_t *const a = &recipes[j];
                if (a->kind != kind || a->good == 0u || a->off != 0u) {
                    continue;
                }
                const uint32_t a_lists = kind == GR_KIND_FUEL ? (j == i ? i_list : a->list_count) : (j == i ? 0u : 1u);
                for (uint32_t a_list = 0u; a_list < a_lists; a_list++) {
                    const char *const *const a_name =
                        kind == GR_KIND_FUEL ? &a->borrowed_list[a_list] : &a->borrowed_cells[0];
                    if (gr_names_overlap(recipes, count, a_name, b_name)) {
                        context->borrowed_path = b->borrowed_path;
                        gr_report(context, 1, 1, "'%s' %s here and in %s", *b_name, what, a->borrowed_name);
                    }
                }
            }
        }
    }
}

/* ---- Emitting -------------------------------------------------------------------------------------------------------- */

/* In upper case, for an enum name. Names were checked to be short. */
static const char *gr_upper(tp_arena_t *arena, const char *name) {
    char *text = tp_arena_text(arena, name, strlen(name));
    for (char *c = text; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return text;
}

static void gr_emit_header(tp_arena_t *arena, tp_builder_t *out, uint32_t crafts, uint32_t choices,
                           uint32_t choice_items, uint32_t smelts) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/recipes/. Change the yaml and build; never edit this. */\n"
        "#ifndef TABLES_RECIPES_INDEX_H\n#define TABLES_RECIPES_INDEX_H\n\n"
        "#include \"tables/items/index.h\"\n\n#include <stdint.h>\n\n");
    tp_builder_add(
        out,
        "#define RECIPE_COUNT %uu\n#define RECIPE_CHOICE_COUNT %uu\n#define RECIPE_CHOICE_ITEM_COUNT %uu\n"
        "#define RECIPE_SMELT_COUNT %uu\n#define RECIPE_CELLS %uu\n"
        "/* In recipe_def_t.cells: nothing goes here. A cell is otherwise a row of recipe_choices, plus one. */\n"
        "#define RECIPE_CELL_EMPTY 0u\n"
        "/* In recipe_smelt_rows: this item does not smelt. It is otherwise a row of recipe_smelts, plus one. */\n"
        "#define RECIPE_NO_SMELT 0u\n\n",
        crafts, choices, choice_items, smelts, GR_CELLS);
    tp_builder_add(
        out,
        "typedef enum recipe_kind_t : uint8_t {\n    RECIPE_KIND_SHAPED = %u,\n    RECIPE_KIND_SHAPELESS = %u,\n"
        "} recipe_kind_t;\n\n",
        GR_KIND_SHAPED, GR_KIND_SHAPELESS);
    tp_builder_add(out, "/* Where a recipe is made. A grid makes only its own station's. */\n"
                        "typedef enum recipe_station_t : uint8_t {\n");
    for (uint32_t i = 0u; i < GR_STATION_COUNT; i++) {
        tp_builder_add(out, "    RECIPE_STATION_%s = %u,\n", gr_upper(arena, GR_STATIONS[i]), i);
    }
    tp_builder_add(out, "    RECIPE_STATION_COUNT = %u,\n} recipe_station_t;\n\n", (uint32_t)GR_STATION_COUNT);
    tp_builder_add(out, "/* Where a smelt is done: the block whose slots it goes through. */\n"
                        "typedef enum recipe_process_t : uint8_t {\n");
    for (uint32_t i = 0u; i < GR_PROCESS_COUNT; i++) {
        tp_builder_add(out, "    RECIPE_PROCESS_%s = %u,\n", gr_upper(arena, GR_PROCESSES[i]), i);
    }
    tp_builder_add(out, "    RECIPE_PROCESS_COUNT = %u,\n} recipe_process_t;\n\n", (uint32_t)GR_PROCESS_COUNT);
    tp_builder_add(out, "/* One way to fill a cell: any of these items. */\n"
                        "typedef struct recipe_choice_t {\n"
                        "    uint16_t first; /* 0: into recipe_choice_items */\n"
                        "    uint16_t count; /* 2 */\n"
                        "} recipe_choice_t;\n"
                        "static_assert(sizeof(recipe_choice_t) == 4);\n\n"
                        "/* One crafting recipe. Shaped: width by height cells, by row. Shapeless: the first "
                        "cell_count, in any order. */\n"
                        "typedef struct recipe_def_t {\n"
                        "    const char *name;            /* 0 */\n"
                        "    uint16_t cells[RECIPE_CELLS]; /* 8 */\n"
                        "    uint16_t gives;              /* 26: an item_id_t */\n"
                        "    uint8_t count;               /* 28 */\n"
                        "    uint8_t kind;                /* 29: a recipe_kind_t */\n"
                        "    uint8_t width;               /* 30: 0 if shapeless */\n"
                        "    uint8_t height;              /* 31: 0 if shapeless */\n"
                        "    uint8_t cell_count;          /* 32: cells that are not empty */\n"
                        "    uint8_t station;             /* 33: a recipe_station_t */\n"
                        "    uint8_t _pad0[6];            /* 34 */\n"
                        "} recipe_def_t;\n"
                        "static_assert(sizeof(recipe_def_t) == 40);\n\n"
                        "/* What a furnace makes of one input. */\n"
                        "typedef struct recipe_smelt_t {\n"
                        "    const char *name; /* 0 */\n"
                        "    uint16_t gives;   /* 8: an item_id_t */\n"
                        "    uint16_t ticks;   /* 10: how long one takes */\n"
                        "    uint8_t count;    /* 12 */\n"
                        "    uint8_t process;  /* 13: a recipe_process_t: only that block does it */\n"
                        "    uint8_t _pad0[2]; /* 14 */\n"
                        "} recipe_smelt_t;\n"
                        "static_assert(sizeof(recipe_smelt_t) == 16);\n\n"
                        "/* How an item burns. All zero if it does not. */\n"
                        "typedef struct recipe_fuel_t {\n"
                        "    uint16_t ticks;  /* 0: how long one burns */\n"
                        "    uint16_t leaves; /* 2: the item_id_t left in the slot; ITEM_ID_NONE for nothing */\n"
                        "} recipe_fuel_t;\n"
                        "static_assert(sizeof(recipe_fuel_t) == 4);\n\n"
                        "extern const recipe_def_t recipe_defs[RECIPE_COUNT];\n"
                        "extern const recipe_choice_t recipe_choices[RECIPE_CHOICE_COUNT];\n"
                        "extern const uint16_t recipe_choice_items[RECIPE_CHOICE_ITEM_COUNT];\n"
                        "extern const recipe_smelt_t recipe_smelts[RECIPE_SMELT_COUNT];\n"
                        "/* Both by item_id_t. */\n"
                        "extern const uint16_t recipe_smelt_rows[ITEM_COUNT];\n"
                        "extern const recipe_fuel_t recipe_fuels[ITEM_COUNT];\n"
                        "/* By recipe_process_t: 1 if it burns fuel, 0 if it goes by time alone. */\n"
                        "extern const uint8_t recipe_process_burns[RECIPE_PROCESS_COUNT];\n\n"
                        "#endif\n");
}

static uint32_t gr_choice_index(const char *const *choices, uint32_t count, const char *name) {
    const char *const *const found = bsearch(&name, choices, count, sizeof(const char *), gr_compare_text);
    return (uint32_t)(found - choices);
}

static void gr_emit_crafts(tp_arena_t *arena, tp_builder_t *out, const gr_recipe_t *recipes, uint32_t count,
                           const char *const *choices, uint32_t choice_count) {
    tp_builder_add(out, "const recipe_def_t recipe_defs[RECIPE_COUNT] = {\n");
    for (uint32_t i = 0u; i < count; i++) {
        const gr_recipe_t *const recipe = &recipes[i];
        if ((recipe->kind != GR_KIND_SHAPED && recipe->kind != GR_KIND_SHAPELESS) || recipe->off != 0u) {
            continue;
        }
        tp_builder_add(out, "    {.name = \"%s\", .cells = {", recipe->borrowed_name);
        for (uint32_t c = 0u; c < GR_CELLS; c++) {
            const uint32_t cell = recipe->borrowed_cells[c] == nullptr
                                      ? 0u
                                      : gr_choice_index(choices, choice_count, recipe->borrowed_cells[c]) + 1u;
            tp_builder_add(out, "%s%u", c == 0u ? "" : ", ", cell);
        }
        tp_builder_add(out,
                       "}, .gives = ITEM_ID_%s, .count = %u, .kind = %u, .width = %u, .height = %u, .cell_count = %u, "
                       ".station = %u},\n",
                       gr_upper(arena, recipe->borrowed_gives), recipe->count, recipe->kind, recipe->width,
                       recipe->height, recipe->cell_count, recipe->station);
    }
    tp_builder_add(out, "};\n\n");
}

static void gr_emit_choices(tp_arena_t *arena, tp_builder_t *out, const gr_recipe_t *recipes, uint32_t count,
                            const char *const *choices, uint32_t choice_count) {
    tp_builder_add(out, "const recipe_choice_t recipe_choices[RECIPE_CHOICE_COUNT] = {\n");
    uint32_t first = 0u;
    for (uint32_t i = 0u; i < choice_count; i++) {
        const gr_names_t names = gr_names_of(recipes, count, &choices[i]);
        tp_builder_add(out, "    {.first = %u, .count = %u}, /* %s */\n", first, names.count, choices[i]);
        first += names.count;
    }
    tp_builder_add(out, "};\n\nconst uint16_t recipe_choice_items[RECIPE_CHOICE_ITEM_COUNT] = {\n");
    for (uint32_t i = 0u; i < choice_count; i++) {
        const gr_names_t names = gr_names_of(recipes, count, &choices[i]);
        for (uint32_t n = 0u; n < names.count; n++) {
            tp_builder_add(out, "    ITEM_ID_%s,\n", gr_upper(arena, names.borrowed_names[n]));
        }
    }
    tp_builder_add(out, "};\n\n");
}

static void gr_emit_smelts(tp_arena_t *arena, tp_builder_t *out, const gr_recipe_t *recipes, uint32_t count) {
    tp_builder_add(out, "const recipe_smelt_t recipe_smelts[RECIPE_SMELT_COUNT] = {\n");
    for (uint32_t i = 0u; i < count; i++) {
        const gr_recipe_t *const recipe = &recipes[i];
        if (recipe->kind == GR_KIND_SMELT && recipe->off == 0u) {
            tp_builder_add(out, "    {.name = \"%s\", .gives = ITEM_ID_%s, .ticks = %u, .count = %u, .process = %u},\n",
                           recipe->borrowed_name, gr_upper(arena, recipe->borrowed_gives), recipe->ticks, recipe->count,
                           recipe->station);
        }
    }
    tp_builder_add(out, "};\n\nconst uint16_t recipe_smelt_rows[ITEM_COUNT] = {\n");
    uint32_t row = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        if (recipes[i].kind == GR_KIND_SMELT && recipes[i].off == 0u) {
            row++;
            const gr_names_t names = gr_names_of(recipes, count, &recipes[i].borrowed_cells[0]);
            for (uint32_t n = 0u; n < names.count; n++) {
                tp_builder_add(out, "    [ITEM_ID_%s] = %u,\n", gr_upper(arena, names.borrowed_names[n]), row);
            }
        }
    }
    tp_builder_add(out, "};\n\n");
}

static void gr_emit_fuels(tp_arena_t *arena, tp_builder_t *out, const gr_recipe_t *recipes, uint32_t count) {
    tp_builder_add(out, "const recipe_fuel_t recipe_fuels[ITEM_COUNT] = {\n");
    for (uint32_t i = 0u; i < count; i++) {
        const gr_recipe_t *const recipe = &recipes[i];
        if (recipe->kind != GR_KIND_FUEL) {
            continue;
        }
        for (uint32_t l = 0u; l < recipe->list_count; l++) {
            const gr_names_t names = gr_names_of(recipes, count, &recipe->borrowed_list[l]);
            for (uint32_t n = 0u; n < names.count; n++) {
                tp_builder_add(out, "    [ITEM_ID_%s] = {.ticks = %u, .leaves = ITEM_ID_%s},\n",
                               gr_upper(arena, names.borrowed_names[n]), recipe->ticks,
                               recipe->borrowed_leaves != nullptr ? gr_upper(arena, recipe->borrowed_leaves) : "NONE");
            }
        }
    }
    tp_builder_add(out, "};\n\nconst uint8_t recipe_process_burns[RECIPE_PROCESS_COUNT] = {");
    for (uint32_t i = 0u; i < GR_PROCESS_COUNT; i++) {
        tp_builder_add(out, "%s%u", i == 0u ? "" : ", ", GR_PROCESS_BURNS[i]);
    }
    tp_builder_add(out, "};\n");
}

/* The names cells use, sorted and each once. */
static uint32_t gr_collect_choices(tp_arena_t *arena, const gr_recipe_t *recipes, uint32_t count, const char ***out) {
    const char **names = tp_arena_alloc(arena, ((uint64_t)count * GR_CELLS + 1u) * sizeof(const char *));
    uint32_t total = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        if ((recipes[i].kind != GR_KIND_SHAPED && recipes[i].kind != GR_KIND_SHAPELESS) || recipes[i].off != 0u) {
            continue;
        }
        for (uint32_t c = 0u; c < GR_CELLS; c++) {
            if (recipes[i].borrowed_cells[c] != nullptr) {
                names[total] = recipes[i].borrowed_cells[c];
                total++;
            }
        }
    }
    qsort(names, total, sizeof(const char *), gr_compare_text);
    uint32_t kept = 0u;
    for (uint32_t i = 0u; i < total; i++) {
        if (kept == 0u || strcmp(names[kept - 1u], names[i]) != 0) {
            names[kept] = names[i];
            kept++;
        }
    }
    *out = names;
    return kept;
}

bool game_tables_recipes_build(tp_arena_t *arena, const char *config, const char *out) {
    const char *const recipes_path = tp_arena_format(arena, "%s/recipes", config);
    struct stat info;
    if (stat(recipes_path, &info) != 0) {
        return true; /* a tree with no recipes has no tables of them, and the real one's absence fails the build by its outputs */
    }
    const char **names = nullptr;
    uint32_t count = 0;
    if (!gr_list_recipes(arena, recipes_path, &names, &count)) {
        return false;
    }
    gr_context_t context = {
        .borrowed_arena = arena,
        .borrowed_items = tp_arena_format(arena, "%s/items", config),
        .borrowed_recipes = recipes_path,
    };
    gr_recipe_t *const recipes = tp_arena_alloc(arena, (count + 1ull) * sizeof(gr_recipe_t));
    uint32_t crafts = 0u, smelts = 0u;
    for (uint32_t i = 0; i < count; i++) {
        gr_read_recipe(&context, names[i], &recipes[i]);
        const bool on = recipes[i].off == 0u;
        crafts += on && (recipes[i].kind == GR_KIND_SHAPED || recipes[i].kind == GR_KIND_SHAPELESS) ? 1u : 0u;
        smelts += on && recipes[i].kind == GR_KIND_SMELT ? 1u : 0u;
    }
    gr_check_clashes(&context, recipes, count);
    gr_check_once(&context, recipes, count, GR_KIND_SMELT, "smelts");
    gr_check_once(&context, recipes, count, GR_KIND_FUEL, "burns");
    if (context.problems != 0u) {
        return tp_fail("%s: %u problems; nothing was written", recipes_path, context.problems);
    }
    /* A table of no rows would not compile, and a game that makes nothing is a mistake anyway. */
    if (crafts == 0u || smelts == 0u) {
        return tp_fail("%s: there must be a crafting recipe and a smelting one", recipes_path);
    }

    const char **choices = nullptr;
    const uint32_t choice_count = gr_collect_choices(arena, recipes, count, &choices);
    uint32_t choice_items = 0u;
    for (uint32_t i = 0u; i < choice_count; i++) {
        choice_items += gr_names_of(recipes, count, &choices[i]).count;
    }
    tp_builder_t header = tp_builder_begin(arena, GR_HEADER_BYTES);
    tp_builder_t source = tp_builder_begin(arena, GR_SOURCE_BYTES);
    gr_emit_header(arena, &header, crafts, choice_count, choice_items, smelts);
    tp_builder_add(
        &source, "/* Generated by compile/src/game_tables from config/recipes/. Change the yaml and build; never edit "
                 "this. */\n#include \"tables/recipes/index.h\"\n\n");
    gr_emit_crafts(arena, &source, recipes, count, choices, choice_count);
    gr_emit_choices(arena, &source, recipes, count, choices, choice_count);
    gr_emit_smelts(arena, &source, recipes, count);
    gr_emit_fuels(arena, &source, recipes, count);

    const char *const directory = tp_arena_format(arena, "%s/recipes", out);
    return tp_make_directories(arena, directory) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.h", directory), header.borrowed_text,
                               header.used, nullptr) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.c", directory), source.borrowed_text,
                               source.used, nullptr);
}
