/*
 * config/ores/ -> the ore table. See README.md. Like the biomes part: read and check each file on its own, then the rows
 * against each other, then emit. A problem does not stop the reading, so one build shows all of them.
 */
#include "game_tables/ores/index.h"

#include "yaml/index.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GO_MAX_ORES 255u
#define GO_MAX_ROWS 255u
#define GO_MAX_VEINS_IN_FILE 16u
#define GO_MAX_NAME_BYTES 64u
#define GO_MAX_FILE_BYTES (1ull << 20)
#define GO_Z_LIMIT 1250
#define GO_MAX_ORDER 1000000
/* 4096 veins tried in a chunk: far over anything a world wants, and the tries are a uint32_t. */
#define GO_MAX_PER_256_CHUNKS (4096 * 256)
#define GO_HEADER_BYTES (1ull << 16)
#define GO_SOURCE_BYTES (1ull << 18)

enum { GO_SPREAD_UNIFORM = 0, GO_SPREAD_TRIANGLE = 1 };
static const char *const GO_SPREADS[] = {"uniform", "triangle"};
static const char *const GO_SPREAD_ENUMS[] = {"ORE_SPREAD_UNIFORM", "ORE_SPREAD_TRIANGLE"};
enum { GO_SPREAD_COUNT = sizeof(GO_SPREADS) / sizeof(GO_SPREADS[0]) };

/* What one row of veins says. In a file of one row they are at the top; otherwise each entry of `veins` has them, and `row`. */
static const char *const GO_ROW_FIELDS[] = {
    "order", "z_min", "z_max", "vein", "per_256_chunks", "spread", "min_column_height"};
enum { GO_ROW_FIELD_COUNT = sizeof(GO_ROW_FIELDS) / sizeof(GO_ROW_FIELDS[0]) };
static const char *const GO_FILE_FIELDS[] = {"name",  "block",          "deep_block", "drops",
                                             "veins", "order",          "z_min",      "z_max",
                                             "vein",  "per_256_chunks", "spread",     "min_column_height"};
enum { GO_FILE_FIELD_COUNT = sizeof(GO_FILE_FIELDS) / sizeof(GO_FILE_FIELDS[0]) };
static const char *const GO_VEIN_FIELDS[] = {"row",  "order",          "z_min",  "z_max",
                                             "vein", "per_256_chunks", "spread", "min_column_height"};
enum { GO_VEIN_FIELD_COUNT = sizeof(GO_VEIN_FIELDS) / sizeof(GO_VEIN_FIELDS[0]) };

/* One row of the table, as read. */
typedef struct go_row_t {
    const char *borrowed_row;   /* 0: the row's own name; its stream is "ore/<row>" */
    const char *borrowed_ore;   /* 8: its file's directory */
    const char *borrowed_block; /* 16 */
    const char *borrowed_deep;  /* 24: the same as block for a rock */
    const char *borrowed_path;  /* 32: its file, for reporting */
    int64_t order;              /* 40 */
    uint32_t per_256_chunks;    /* 48 */
    uint32_t line;              /* 52: of its `order`, for reporting */
    uint32_t column;            /* 56 */
    int16_t z_min;              /* 60 */
    int16_t z_max;              /* 62 */
    int16_t min_column_height;  /* 64 */
    uint8_t vein;               /* 66 */
    uint8_t spread;             /* 67 */
    uint8_t _pad0[4];           /* 68 */
} go_row_t;
static_assert(sizeof(go_row_t) == 72);

typedef struct go_context_t {
    tp_arena_t *borrowed_arena;  /* 0 */
    const char *borrowed_blocks; /* 8: <config>/blocks */
    const char *borrowed_items;  /* 16: <config>/items */
    const char *borrowed_path;   /* 24: the file being read, for reporting */
    go_row_t *borrowed_rows;     /* 32: GO_MAX_ROWS of them */
    uint32_t row_count;          /* 40 */
    uint32_t problems;           /* 44 */
} go_context_t;
static_assert(sizeof(go_context_t) == 48);

[[gnu::format(printf, 3, 4)]]
static void go_report(go_context_t *context, const yaml_node_t *node, const char *format, ...) {
    fprintf(stderr, "%s:%u:%u: ", context->borrowed_path, node->line == 0u ? 1u : node->line,
            node->column == 0u ? 1u : node->column);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    context->problems++;
}

static void go_report_yaml(go_context_t *context, const yaml_error_t *error) {
    fprintf(stderr, "%s:%u:%u: %s\n", context->borrowed_path, error->line, error->column, error->message);
    context->problems++;
}

static bool go_is_one_of(const yaml_node_t *child, const char *const *fields, uint32_t field_count) {
    for (uint32_t i = 0; i < field_count; i++) {
        if (child->key_length == strlen(fields[i]) && memcmp(child->borrowed_key, fields[i], child->key_length) == 0) {
            return true;
        }
    }
    return false;
}

/* Reports every key of map that the list does not hold. */
static void go_reject_unknown(go_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                              const char *what, const char *const *fields, uint32_t field_count) {
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        if (!go_is_one_of(child, fields, field_count)) {
            go_report(context, child, "unknown field '%.*s' in %s", (int32_t)child->key_length,
                      (const char *)child->borrowed_key, what);
        }
    }
}

/* Lower case, digits and underscores, so it can be part of a C string and a directory. */
static bool go_name_is_valid(const char *name) {
    const uint64_t length = strlen(name);
    if (length == 0u || length > GO_MAX_NAME_BYTES || name[0] < 'a' || name[0] > 'z') {
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
static const char *go_read_text(go_context_t *context, const yaml_node_t *node) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        go_report_yaml(context, &error);
        return nullptr;
    }
    return tp_arena_text(context->borrowed_arena, text, length);
}

/* A name that must have its file in directory: a block or an item. nullptr with the problem reported. */
static const char *go_read_reference(go_context_t *context, const yaml_node_t *node, const char *what,
                                     const char *directory) {
    const char *name = go_read_text(context, node);
    if (name == nullptr) {
        return nullptr;
    }
    if (!go_name_is_valid(name)) {
        go_report(context, node, "%s '%s' is not a valid name", what, name);
        return nullptr;
    }
    struct stat info;
    const char *file = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", directory, name);
    if (stat(file, &info) != 0 || !S_ISREG(info.st_mode)) {
        go_report(context, node, "%s '%s' does not exist: no %s", what, name, file);
        return nullptr;
    }
    return name;
}

/* Parses a whole file into document, or reports why not. */
static bool go_parse(go_context_t *context, const char *path, yaml_document_t *document) {
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(context->borrowed_arena, path, GO_MAX_FILE_BYTES, &text, &length);
    if (status != TP_READ_OK) {
        if (status == TP_READ_MISSING) {
            fprintf(stderr, "%s: missing\n", path);
        }
        context->problems++;
        return false;
    }
    yaml_storage_t storage = {
        .nodes = tp_arena_alloc(context->borrowed_arena, (length + 1u) * sizeof(yaml_node_t)),
        .strings = tp_arena_alloc(context->borrowed_arena, length + 1u),
        .node_capacity = (uint32_t)(length + 1u),
        .string_capacity = (uint32_t)length,
    };
    yaml_error_t error = {0};
    if (!yaml_parse(document, text, length, storage, &error)) {
        fprintf(stderr, "%s:%u:%u: %s\n", path, error.line, error.column, error.message);
        context->problems++;
        return false;
    }
    return true;
}

/* Whether the block's own file lists item among its drops. The blocks part has checked that file; a broken one says no. */
static bool go_block_drops(go_context_t *context, const char *block, const char *item) {
    const char *path = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", context->borrowed_blocks, block);
    yaml_document_t document;
    const uint32_t problems = context->problems;
    if (!go_parse(context, path, &document)) {
        context->problems = problems; /* the blocks part reports it */
        return false;
    }
    const yaml_node_t *root = yaml_root(&document);
    const yaml_node_t *drops = root->kind == YAML_KIND_MAP ? yaml_map_get(&document, root, "drops") : nullptr;
    if (drops == nullptr || drops->kind != YAML_KIND_SEQ) {
        return false;
    }
    for (const yaml_node_t *drop = yaml_first_child(&document, drops); drop != nullptr;
         drop = yaml_next_sibling(&document, drop)) {
        const yaml_node_t *name = drop->kind == YAML_KIND_MAP ? yaml_map_get(&document, drop, "item") : nullptr;
        if (name != nullptr && yaml_text_equals(name, item)) {
            return true;
        }
    }
    return false;
}

/* A whole number from minimum to maximum, which must be there. */
static int64_t go_read_whole(go_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                             const char *field, int64_t minimum, int64_t maximum, bool needed, int64_t fallback) {
    const yaml_node_t *node = yaml_map_get(document, map, field);
    if (node == nullptr) {
        if (needed) {
            go_report(context, map, "missing field '%s'", field);
        }
        return fallback;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        go_report_yaml(context, &error);
        return fallback;
    }
    if (value < minimum || value > maximum) {
        go_report(context, node, "%s is %" PRId64 ", and must be %" PRId64 " to %" PRId64, field, value, minimum,
                  maximum);
        return fallback;
    }
    return value;
}

/* The fields of one row, from map: the file itself, or an entry of its veins. */
static void go_read_row(go_context_t *context, const yaml_document_t *document, const yaml_node_t *map, go_row_t *row) {
    const yaml_node_t *order = yaml_map_get(document, map, "order");
    row->line = order != nullptr ? order->line : map->line;
    row->column = order != nullptr ? order->column : map->column;
    row->order = go_read_whole(context, document, map, "order", 0, GO_MAX_ORDER, true, 0);
    row->z_min = (int16_t)go_read_whole(context, document, map, "z_min", -GO_Z_LIMIT, GO_Z_LIMIT, true, 0);
    row->z_max = (int16_t)go_read_whole(context, document, map, "z_max", -GO_Z_LIMIT, GO_Z_LIMIT, true, 1);
    row->vein = (uint8_t)go_read_whole(context, document, map, "vein", 1, UINT8_MAX, true, 1);
    row->per_256_chunks =
        (uint32_t)go_read_whole(context, document, map, "per_256_chunks", 0, GO_MAX_PER_256_CHUNKS, true, 0);
    row->min_column_height =
        (int16_t)go_read_whole(context, document, map, "min_column_height", -GO_Z_LIMIT, GO_Z_LIMIT, false, INT16_MIN);
    if (row->z_min >= row->z_max) {
        go_report(context, map, "z_min %d must be below z_max %d", row->z_min, row->z_max);
    }
    row->spread = GO_SPREAD_UNIFORM;
    const yaml_node_t *spread = yaml_map_get(document, map, "spread");
    if (spread != nullptr) {
        const char *text = go_read_text(context, spread);
        bool known = false;
        for (uint32_t i = 0; text != nullptr && i < GO_SPREAD_COUNT && !known; i++) {
            if (strcmp(text, GO_SPREADS[i]) == 0) {
                row->spread = (uint8_t)i;
                known = true;
            }
        }
        if (text != nullptr && !known) {
            go_report(context, spread, "unknown spread '%s': uniform or triangle", text);
        }
    }
}

/* A new row, or nullptr with the problem reported when the table is full. */
static go_row_t *go_add_row(go_context_t *context, const yaml_node_t *node, const char *ore) {
    if (context->row_count == GO_MAX_ROWS) {
        go_report(context, node, "over %u rows of veins in all", GO_MAX_ROWS);
        return nullptr;
    }
    go_row_t *row = &context->borrowed_rows[context->row_count];
    context->row_count++;
    memset(row, 0, sizeof(*row));
    row->borrowed_path = context->borrowed_path;
    row->borrowed_ore = ore;
    return row;
}

/* Every row that one parsed file says, appended to the context's. */
static void go_read_fields(go_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                           const char *ore) {
    go_reject_unknown(context, document, root, "an ore", GO_FILE_FIELDS, GO_FILE_FIELD_COUNT);
    const yaml_node_t *name = yaml_map_get(document, root, "name");
    if (name == nullptr) {
        go_report(context, root, "missing field 'name'");
    } else {
        const char *text = go_read_text(context, name);
        if (text != nullptr && strcmp(text, ore) != 0) {
            go_report(context, name, "name '%s' is not its directory's, '%s'", text, ore);
        }
    }
    const yaml_node_t *block_node = yaml_map_get(document, root, "block");
    const yaml_node_t *deep_node = yaml_map_get(document, root, "deep_block");
    const char *block = nullptr;
    if (block_node == nullptr) {
        go_report(context, root, "missing field 'block'");
    } else {
        block = go_read_reference(context, block_node, "block", context->borrowed_blocks);
    }
    const char *deep =
        deep_node == nullptr ? block : go_read_reference(context, deep_node, "block", context->borrowed_blocks);
    const yaml_node_t *drops = yaml_map_get(document, root, "drops");
    if (drops != nullptr) {
        const char *item = go_read_reference(context, drops, "item", context->borrowed_items);
        if (item != nullptr && block != nullptr && !go_block_drops(context, block, item)) {
            go_report(context, drops, "drops '%s', and block '%s' does not drop it", item, block);
        }
        if (item != nullptr && deep != nullptr && deep != block && !go_block_drops(context, deep, item)) {
            go_report(context, drops, "drops '%s', and block '%s' does not drop it", item, deep);
        }
    }

    const yaml_node_t *veins = yaml_map_get(document, root, "veins");
    if (veins == nullptr) {
        go_row_t *row = go_add_row(context, root, ore);
        if (row != nullptr) {
            row->borrowed_row = ore;
            row->borrowed_block = block;
            row->borrowed_deep = deep;
            go_read_row(context, document, root, row);
        }
        return;
    }
    for (const yaml_node_t *child = yaml_first_child(document, root); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        if (go_is_one_of(child, GO_ROW_FIELDS, GO_ROW_FIELD_COUNT)) {
            go_report(context, child, "'%.*s' goes in each of veins, in a file that has them",
                      (int32_t)child->key_length, (const char *)child->borrowed_key);
        }
    }
    if (veins->kind != YAML_KIND_SEQ || yaml_first_child(document, veins) == nullptr) {
        go_report(context, veins, "veins must be a list of at least one");
        return;
    }
    uint32_t count = 0;
    for (const yaml_node_t *entry = yaml_first_child(document, veins); entry != nullptr;
         entry = yaml_next_sibling(document, entry)) {
        if (++count > GO_MAX_VEINS_IN_FILE) {
            go_report(context, entry, "over %u veins in one ore", GO_MAX_VEINS_IN_FILE);
            return;
        }
        if (entry->kind != YAML_KIND_MAP) {
            go_report(context, entry, "a vein must be a mapping");
            continue;
        }
        go_reject_unknown(context, document, entry, "a vein", GO_VEIN_FIELDS, GO_VEIN_FIELD_COUNT);
        go_row_t *row = go_add_row(context, entry, ore);
        if (row == nullptr) {
            return;
        }
        row->borrowed_block = block;
        row->borrowed_deep = deep;
        const yaml_node_t *row_name = yaml_map_get(document, entry, "row");
        if (row_name == nullptr) {
            go_report(context, entry, "missing field 'row'");
        } else {
            row->borrowed_row = go_read_text(context, row_name);
            if (row->borrowed_row != nullptr && !go_name_is_valid(row->borrowed_row)) {
                go_report(context, row_name, "row '%s' is not lower case, digits and underscores", row->borrowed_row);
            }
        }
        go_read_row(context, document, entry, row);
    }
}

/* Reads one ore's file. What it finds wrong is counted in the context. */
static void go_read_ore(go_context_t *context, const char *ores, const char *name) {
    const char *path = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", ores, name);
    context->borrowed_path = path;
    if (!go_name_is_valid(name)) {
        fprintf(stderr, "%s:1:1: directory name '%s' is not lower case, digits and underscores\n", path, name);
        context->problems++;
        return;
    }
    yaml_document_t document;
    if (!go_parse(context, path, &document)) {
        return;
    }
    const yaml_node_t *root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        go_report(context, root, "an ore's file must be a mapping");
        return;
    }
    go_read_fields(context, &document, root, name);
}

static int32_t go_compare_text(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Every directory under ores, sorted. */
static bool go_list_ores(tp_arena_t *arena, DIR *listing, const char *ores, const char ***out_names,
                         uint32_t *out_count) {
    const char **names = tp_arena_alloc(arena, (GO_MAX_ORES + 1ull) * sizeof(const char *));
    uint32_t count = 0;
    for (struct dirent *item = readdir(listing); item != nullptr; item = readdir(listing)) {
        struct stat info;
        const char *child = tp_arena_format(arena, "%s/%s", ores, item->d_name);
        if (item->d_name[0] == '.' || stat(child, &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        if (count == GO_MAX_ORES) {
            return tp_fail("%s: holds over %u ores", ores, GO_MAX_ORES);
        }
        names[count] = tp_arena_text(arena, item->d_name, strlen(item->d_name));
        count++;
    }
    qsort(names, count, sizeof(const char *), go_compare_text);
    *out_names = names;
    *out_count = count;
    return true;
}

static int32_t go_compare_order(const void *left, const void *right) {
    const int64_t a = ((const go_row_t *)left)->order;
    const int64_t b = ((const go_row_t *)right)->order;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* Two rows with one name would have one stream, and two with one order no order between them. */
static void go_check_rows(go_context_t *context) {
    const go_row_t *rows = context->borrowed_rows;
    for (uint32_t a = 0; a < context->row_count; a++) {
        for (uint32_t b = 0; b < a; b++) {
            if (rows[a].borrowed_row != nullptr && rows[b].borrowed_row != nullptr &&
                strcmp(rows[a].borrowed_row, rows[b].borrowed_row) == 0) {
                fprintf(stderr, "%s:%u:%u: row '%s' is also in %s\n", rows[a].borrowed_path, rows[a].line,
                        rows[a].column, rows[a].borrowed_row, rows[b].borrowed_path);
                context->problems++;
            }
            if (rows[a].order == rows[b].order) {
                fprintf(stderr, "%s:%u:%u: order %" PRId64 " is also row '%s' in %s\n", rows[a].borrowed_path,
                        rows[a].line, rows[a].column, rows[a].order,
                        rows[b].borrowed_row != nullptr ? rows[b].borrowed_row : "?", rows[b].borrowed_path);
                context->problems++;
            }
        }
    }
}

/* In upper case, for an enum name. Names were checked to be short. */
static const char *go_upper(tp_arena_t *arena, const char *name) {
    char *text = tp_arena_text(arena, name, strlen(name));
    for (char *c = text; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return text;
}

static void go_emit_header(tp_builder_t *out, uint32_t count) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/ores/. Change the yaml and build; never edit this. */\n"
        "#ifndef TABLES_ORES_INDEX_H\n#define TABLES_ORES_INDEX_H\n\n"
        "#include <stdint.h>\n\n"
        "/* Rows in the table, in the order each row's `order` gives, which is the order they are laid down. */\n"
        "#define ORE_ROW_COUNT %uu\n"
        "/* A row's min_column_height when it can go under any column. */\n"
        "#define ORE_ANY_COLUMN INT16_MIN\n\n"
        "/* How a row picks a vein's z. */\n"
        "typedef enum ore_spread_t : uint8_t {\n"
        "    ORE_SPREAD_UNIFORM = 0,  /* the same at every z from z_low to z_high */\n"
        "    ORE_SPREAD_TRIANGLE = 1, /* most at the middle, none past the ends */\n"
        "} ore_spread_t;\n\n"
        "/* One kind of vein at one range of depths. See config/ores/README.md for what each field does. */\n"
        "typedef struct ore_row_def_t {\n"
        "    const char *borrowed_name; /* 0: \"ore/<row>\", a string literal; the row's stream comes from it */\n"
        "    uint32_t per_chunk_256;    /* 8: veins tried in a chunk, in 256ths: the yaml's per_256_chunks */\n"
        "    int16_t z_low;             /* 12: both ends included */\n"
        "    int16_t z_high;            /* 14 */\n"
        "    int16_t min_column_height; /* 16: only in a chunk whose middle column's height is at least this */\n"
        "    uint16_t block;            /* 18: what it makes in stone and the like */\n"
        "    uint16_t deep_block;       /* 20: what it makes in deepslate and tuff; the same as block for a rock */\n"
        "    uint8_t size;              /* 22: voxels a vein aims for: the yaml's vein */\n"
        "    uint8_t spread;            /* 23: ore_spread_t */\n"
        "} ore_row_def_t;\n"
        "static_assert(sizeof(ore_row_def_t) == 24);\n\n"
        "extern const ore_row_def_t ore_rows[ORE_ROW_COUNT];\n\n"
        "#endif\n",
        count);
}

static void go_emit_source(tp_arena_t *arena, tp_builder_t *out, const go_row_t *rows, uint32_t count) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/ores/. Change the yaml and build; never edit this. */\n"
        "#include \"tables/ores/index.h\"\n\n#include \"tables/blocks/index.h\"\n\n"
        "const ore_row_def_t ore_rows[ORE_ROW_COUNT] = {\n");
    for (uint32_t r = 0; r < count; r++) {
        const go_row_t *row = &rows[r];
        tp_builder_add(out, "    /* config/ores/%s, order %" PRId64 " */\n", row->borrowed_ore, row->order);
        tp_builder_add(out,
                       "    {.borrowed_name = \"ore/%s\", .per_chunk_256 = %uu, .z_low = %d, .z_high = %d, "
                       ".min_column_height = ",
                       row->borrowed_row, row->per_256_chunks, row->z_min, row->z_max);
        if (row->min_column_height == INT16_MIN) {
            tp_builder_add(out, "ORE_ANY_COLUMN");
        } else {
            tp_builder_add(out, "%d", row->min_column_height);
        }
        tp_builder_add(out, ", .block = BLOCK_ID_%s, .deep_block = BLOCK_ID_%s, .size = %uu, .spread = %s},\n",
                       go_upper(arena, row->borrowed_block), go_upper(arena, row->borrowed_deep), row->vein,
                       GO_SPREAD_ENUMS[row->spread]);
    }
    tp_builder_add(out, "};\n");
}

bool game_tables_ores_build(tp_arena_t *arena, const char *config, const char *out) {
    const char *ores_path = tp_arena_format(arena, "%s/ores", config);
    DIR *listing = opendir(ores_path);
    if (listing == nullptr) {
        if (errno == ENOENT) {
            return true; /* a small config, for a test of something else */
        }
        return tp_fail("%s: cannot be listed", ores_path);
    }
    const char **names = nullptr;
    uint32_t count = 0;
    const bool listed = go_list_ores(arena, listing, ores_path, &names, &count);
    closedir(listing);
    if (!listed) {
        return false;
    }

    go_context_t context = {
        .borrowed_arena = arena,
        .borrowed_blocks = tp_arena_format(arena, "%s/blocks", config),
        .borrowed_items = tp_arena_format(arena, "%s/items", config),
        .borrowed_rows = tp_arena_alloc(arena, (GO_MAX_ROWS + 1ull) * sizeof(go_row_t)),
    };
    for (uint32_t o = 0; o < count; o++) {
        go_read_ore(&context, ores_path, names[o]);
    }
    go_check_rows(&context);
    if (context.problems != 0u) {
        return tp_fail("%s: %u problems; nothing was written", ores_path, context.problems);
    }
    /* A table of no rows would not compile. */
    if (context.row_count == 0u) {
        return tp_fail("%s: no ore has a row of veins; nothing was written", ores_path);
    }
    /* The orders are all different, so the sort's lack of stability cannot show. */
    qsort(context.borrowed_rows, context.row_count, sizeof(go_row_t), go_compare_order);

    tp_builder_t header = tp_builder_begin(arena, GO_HEADER_BYTES);
    tp_builder_t source = tp_builder_begin(arena, GO_SOURCE_BYTES);
    go_emit_header(&header, context.row_count);
    go_emit_source(arena, &source, context.borrowed_rows, context.row_count);

    const char *directory = tp_arena_format(arena, "%s/ores", out);
    return tp_make_directories(arena, directory) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.h", directory), header.borrowed_text,
                               header.used, nullptr) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.c", directory), source.borrowed_text,
                               source.used, nullptr);
}
