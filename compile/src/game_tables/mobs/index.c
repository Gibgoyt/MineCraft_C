/*
 * config/mobs/ -> the mob tables. See README.md. Like the other parts: read and check each file on its own, then emit. A problem
 * does not stop the reading, so one build shows all of them.
 */
#include "game_tables/mobs/index.h"

#include "yaml/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GM_MAX_MOBS 256u
#define GM_MAX_NAME_BYTES 64u
#define GM_MAX_FILE_BYTES (1ull << 20)
#define GM_HEADER_BYTES (1ull << 20)
#define GM_SOURCE_BYTES (1ull << 21)
#define GM_MAX_DROPS 16u
#define GM_MAX_NEVER_ON 16u
#define GM_MAX_ON 16u
#define GM_MAX_BIOMES 32u
#define GM_MAX_YIELDS 4u
/* What a rig holds, a body's own boxes and its sex's together: run/src/entity/animation/ asserts that it agrees. */
#define GM_PARTS_MAX 16u
#define GM_TEXTURE_LIMIT 1024
#define GM_DROP_COUNT_LIMIT 64
#define GM_WEIGHT_LIMIT 1000
#define GM_PACK_LIMIT 16
#define GM_LIGHT_LIMIT 15
#define GM_MIN_DISTANCE_LIMIT 128
#define GM_DESPAWN_DISTANCE_LIMIT 1024
#define GM_DESPAWN_TIME_LIMIT 1000000
#define GM_HEALTH_LIMIT 1024
#define GM_HALF_WIDTH_LIMIT 2
#define GM_HEIGHT_LIMIT 4
#define GM_PART_SIZE_LIMIT 64
#define GM_ANGLE_LIMIT 7
#define GM_SCALE_LIMIT 2
/* Blocks above or below the sea, as config/biomes/ gives the ground's height. */
#define GM_HEIGHT_RANGE 1250
#define GM_EVERY_LIMIT 1000000
/* A finer decimal than this is a typo. It also keeps a value from reaching the compiler as a float that rounds to zero. */
#define GM_MIN_EXPONENT (-9)
/* Row 0, which has no file. */
#define GM_NONE "none"
#define GM_TEXTURE_GROUP "mobs/"

static const char *const GM_MOVES[] = {"none", "head", "arm"};
enum { GM_MOVES_COUNT = sizeof(GM_MOVES) / sizeof(GM_MOVES[0]) };
/* What it does: its index is MOB_MIND_. */
static const char *const GM_MINDS[] = {"hostile", "passive"};
enum { GM_MINDS_COUNT = sizeof(GM_MINDS) / sizeof(GM_MINDS[0]), GM_MIND_HOSTILE = 0, GM_MIND_PASSIVE = 1 };
/* The two parts of an animal: its index plus 1 is MOB_SEX_. */
static const char *const GM_SEXES[] = {"male", "female"};
enum { GM_SEX_COUNT = sizeof(GM_SEXES) / sizeof(GM_SEXES[0]) };

static const char *const GM_MOB_FIELDS[] = {"name",    "texture",    "texture_width", "texture_height", "health",
                                            "speed",   "half_width", "height",        "drops",          "spawn",
                                            "despawn", "parts",      "mind",          "male",           "female"};
enum { GM_MOB_FIELD_COUNT = sizeof(GM_MOB_FIELDS) / sizeof(GM_MOB_FIELDS[0]) };
static const char *const GM_DROP_FIELDS[] = {"item", "count", "max", "chance"};
enum { GM_DROP_FIELD_COUNT = sizeof(GM_DROP_FIELDS) / sizeof(GM_DROP_FIELDS[0]) };
static const char *const GM_SPAWN_FIELDS[] = {"weight",       "pack_min",  "pack_max",  "max_light", "max_block_light",
                                              "min_distance", "never_on",  "min_light", "on",        "biomes",
                                              "min_height",   "max_height"};
enum { GM_SPAWN_FIELD_COUNT = sizeof(GM_SPAWN_FIELDS) / sizeof(GM_SPAWN_FIELDS[0]) };
static const char *const GM_DESPAWN_FIELDS[] = {"near", "far", "idle_ticks", "one_in"};
enum { GM_DESPAWN_FIELD_COUNT = sizeof(GM_DESPAWN_FIELDS) / sizeof(GM_DESPAWN_FIELDS[0]) };
static const char *const GM_PART_FIELDS[] = {"size", "pivot", "offset", "uv", "moves", "rest_pitch", "walk_swing"};
enum { GM_PART_FIELD_COUNT = sizeof(GM_PART_FIELDS) / sizeof(GM_PART_FIELDS[0]) };
static const char *const GM_SEX_FIELDS[] = {"name", "scale", "parts", "drops", "yields"};
enum { GM_SEX_FIELD_COUNT = sizeof(GM_SEX_FIELDS) / sizeof(GM_SEX_FIELDS[0]) };
static const char *const GM_YIELD_FIELDS[] = {"item", "count", "max", "using", "every"};
enum { GM_YIELD_FIELD_COUNT = sizeof(GM_YIELD_FIELDS) / sizeof(GM_YIELD_FIELDS[0]) };

typedef struct gm_drop_t {
    const char *borrowed_item; /* 0 */
    yaml_decimal_t chance;     /* 8 */
    uint32_t count_min;        /* 24 */
    uint32_t count_max;        /* 28 */
} gm_drop_t;
static_assert(sizeof(gm_drop_t) == 32);

typedef struct gm_part_t {
    yaml_decimal_t size[3];    /* 0 */
    yaml_decimal_t pivot[3];   /* 48 */
    yaml_decimal_t offset[3];  /* 96 */
    yaml_decimal_t rest_pitch; /* 144 */
    yaml_decimal_t walk_swing; /* 160 */
    uint32_t u;                /* 176 */
    uint32_t v;                /* 180 */
    uint8_t moves;             /* 184: into GM_MOVES */
    uint8_t _pad0[7];          /* 185 */
} gm_part_t;
static_assert(sizeof(gm_part_t) == 192);

/* What a living animal gives, as read. */
typedef struct gm_yield_t {
    const char *borrowed_item;  /* 0 */
    const char *borrowed_using; /* 8: the item held to take it; nullptr: it gives it by itself */
    uint32_t count_min;         /* 16 */
    uint32_t count_max;         /* 20 */
    uint32_t every;             /* 24: ticks */
    uint8_t _pad0[4];           /* 28 */
} gm_yield_t;
static_assert(sizeof(gm_yield_t) == 32);

/* A male: or a female: part, as read. */
typedef struct gm_sex_t {
    const char *borrowed_name;   /* 0 */
    gm_part_t *borrowed_parts;   /* 8: its boxes besides the mob's own */
    gm_drop_t *borrowed_drops;   /* 16: rolled after the mob's own */
    gm_yield_t *borrowed_yields; /* 24 */
    yaml_decimal_t scale;        /* 32 */
    uint32_t part_count;         /* 48 */
    uint32_t drop_count;         /* 52 */
    uint32_t yield_count;        /* 56 */
    uint8_t present;             /* 60 */
    uint8_t _pad0[3];            /* 61 */
} gm_sex_t;
static_assert(sizeof(gm_sex_t) == 64);

/* One mob, as read. Everything it points to is in the arena. */
typedef struct gm_mob_t {
    const char *borrowed_name;      /* 0: its directory's, so a broken file still has one */
    const char *borrowed_texture;   /* 8 */
    gm_drop_t *borrowed_drops;      /* 16 */
    const char **borrowed_never_on; /* 24 */
    gm_part_t *borrowed_parts;      /* 32 */
    const char **borrowed_on;       /* 40 */
    const char **borrowed_biomes;   /* 48 */
    yaml_decimal_t health;          /* 56 */
    yaml_decimal_t speed;           /* 72 */
    yaml_decimal_t half_width;      /* 88 */
    yaml_decimal_t height;          /* 104 */
    gm_sex_t sexes[GM_SEX_COUNT];   /* 120: male, female */
    uint32_t texture_width;         /* 248 */
    uint32_t texture_height;        /* 252 */
    uint32_t drop_count;            /* 256 */
    uint32_t never_on_count;        /* 260 */
    uint32_t part_count;            /* 264 */
    uint32_t weight;                /* 268 */
    uint32_t pack_min;              /* 272 */
    uint32_t pack_max;              /* 276 */
    uint32_t max_light;             /* 280 */
    uint32_t max_block_light;       /* 284 */
    uint32_t min_distance;          /* 288 */
    uint32_t despawn_near;          /* 292 */
    uint32_t despawn_far;           /* 296: 0 for a passive mob, which never goes */
    uint32_t despawn_idle_ticks;    /* 300 */
    uint32_t despawn_one_in;        /* 304 */
    uint32_t on_count;              /* 308 */
    uint32_t biome_count;           /* 312 */
    uint32_t min_light;             /* 316 */
    uint32_t mind;                  /* 320: into GM_MINDS */
    int32_t min_height;             /* 324 */
    int32_t max_height;             /* 328 */
    uint8_t _pad0[4];               /* 332 */
} gm_mob_t;
static_assert(sizeof(gm_mob_t) == 336);

typedef struct gm_context_t {
    tp_arena_t *borrowed_arena;    /* 0 */
    const char *borrowed_textures; /* 8 */
    const char *borrowed_items;    /* 16: <config>/items */
    const char *borrowed_blocks;   /* 24: <config>/blocks */
    const char *borrowed_biomes;   /* 32: <config>/biomes */
    const char *borrowed_path;     /* 40: the file being read, for reporting */
    uint32_t problems;             /* 48 */
    uint8_t _pad0[4];              /* 52 */
} gm_context_t;
static_assert(sizeof(gm_context_t) == 56);

[[gnu::format(printf, 4, 5)]]
static void gm_report(gm_context_t *context, uint32_t line, uint32_t column, const char *format, ...) {
    fprintf(stderr, "%s:%u:%u: ", context->borrowed_path, line, column);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    context->problems++;
}

static void gm_report_yaml(gm_context_t *context, const yaml_error_t *error) {
    gm_report(context, error->line, error->column, "%s", error->message);
}

static bool gm_key_is(const yaml_node_t *node, const char *key) {
    const uint64_t length = strlen(key);
    return node->key_length == length && memcmp(node->borrowed_key, key, length) == 0;
}

/* Reports every key of map that the list does not hold. A typo must not quietly mean the default. */
static void gm_reject_unknown(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                              const char *what, const char *const *fields, uint32_t field_count) {
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        bool known = false;
        for (uint32_t i = 0; i < field_count && !known; i++) {
            known = gm_key_is(child, fields[i]);
        }
        if (!known) {
            gm_report(context, child->line, child->column, "unknown field '%.*s' in %s", (int32_t)child->key_length,
                      (const char *)child->borrowed_key, what);
        }
    }
}

/* Lower case, digits and underscores, so it can be an enum name in the tables. */
static bool gm_name_is_valid(const char *name) {
    const uint64_t length = strlen(name);
    if (length == 0u || length > GM_MAX_NAME_BYTES || name[0] < 'a' || name[0] > 'z') {
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
static const char *gm_read_text(gm_context_t *context, const yaml_node_t *node) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        gm_report_yaml(context, &error);
        return nullptr;
    }
    return tp_arena_text(context->borrowed_arena, text, length);
}

/* An integer from minimum to maximum, which must be there unless optional. Absent or wrong leaves out alone. */
static void gm_read_number(gm_context_t *context, const yaml_node_t *parent, const yaml_node_t *node, const char *what,
                           int64_t minimum, int64_t maximum, bool optional, uint32_t *out) {
    if (node == nullptr) {
        if (!optional) {
            gm_report(context, parent->line, parent->column, "missing field '%s'", what);
        }
        return;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        gm_report_yaml(context, &error);
    } else if (value < minimum || value > maximum) {
        gm_report(context, node->line, node->column, "%s is %" PRId64 ", and must be %" PRId64 " to %" PRId64, what,
                  value, minimum, maximum);
    } else {
        *out = (uint32_t)value;
    }
}

/* The same for a number that may be below 0, and is never needed. */
static void gm_read_signed(gm_context_t *context, const yaml_node_t *node, const char *what, int32_t limit,
                           int32_t *out) {
    if (node == nullptr) {
        return;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        gm_report_yaml(context, &error);
    } else if (value < -(int64_t)limit || value > (int64_t)limit) {
        gm_report(context, node->line, node->column, "%s is %" PRId64 ", and must be %d to %d", what, value, -limit,
                  limit);
    } else {
        *out = (int32_t)value;
    }
}

/* One of a list of words, by its index. Absent leaves out alone. */
static void gm_read_word(gm_context_t *context, const yaml_node_t *node, const char *what, const char *const *words,
                         uint32_t word_count, uint32_t *out) {
    if (node == nullptr) {
        return;
    }
    const char *const text = gm_read_text(context, node);
    for (uint32_t i = 0u; text != nullptr && i < word_count; i++) {
        if (strcmp(text, words[i]) == 0) {
            *out = i;
            return;
        }
    }
    if (text != nullptr) {
        gm_report(context, node->line, node->column, "unknown %s '%s'", what, text);
    }
}

/* Whether a decimal's size is at most limit, and it is no finer than GM_MIN_EXPONENT, by whole numbers only. */
static bool gm_decimal_fits(yaml_decimal_t value, int64_t limit) {
    int64_t mantissa = value.mantissa < 0 ? -value.mantissa : value.mantissa;
    int32_t exponent = value.exponent;
    if (mantissa == 0) {
        return true;
    }
    while (exponent < 0 && mantissa % 10 == 0) {
        mantissa /= 10;
        exponent++;
    }
    if (exponent < GM_MIN_EXPONENT) {
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

typedef enum gm_sign_t : uint8_t {
    GM_SIGN_POSITIVE = 0, /* above 0 */
    GM_SIGN_ANY = 1,
} gm_sign_t;

/* A decimal of this sign, of at most limit in size. Absent or wrong leaves out alone. */
static void gm_read_decimal(gm_context_t *context, const yaml_node_t *node, const char *what, gm_sign_t sign,
                            int64_t limit, yaml_decimal_t *out) {
    if (node == nullptr) {
        return;
    }
    yaml_decimal_t value = {0};
    yaml_error_t error = {0};
    if (!yaml_get_decimal(node, &value, &error)) {
        gm_report_yaml(context, &error);
    } else if (!gm_decimal_fits(value, limit) || (sign == GM_SIGN_POSITIVE && value.mantissa <= 0)) {
        gm_report(context, node->line, node->column, "%s '%.*s' must be %s %" PRId64 " with at most %d decimals", what,
                  (int32_t)node->text_length, (const char *)node->borrowed_text,
                  sign == GM_SIGN_POSITIVE ? "above 0, up to" : "of a size up to", limit, -GM_MIN_EXPONENT);
    } else {
        *out = value;
    }
}

static void gm_read_required_decimal(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                                     const char *field, int64_t limit, yaml_decimal_t *out) {
    const yaml_node_t *const node = yaml_map_get(document, root, field);
    if (node == nullptr) {
        gm_report(context, root->line, root->column, "missing field '%s'", field);
        return;
    }
    gm_read_decimal(context, node, field, GM_SIGN_POSITIVE, limit, out);
}

/* Exactly so many decimals in a list. */
static void gm_read_vector(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *part,
                           const char *field, gm_sign_t sign, int64_t limit, yaml_decimal_t *out) {
    const yaml_node_t *const node = yaml_map_get(document, part, field);
    if (node == nullptr) {
        gm_report(context, part->line, part->column, "missing field '%s' in a part", field);
        return;
    }
    if (node->kind != YAML_KIND_SEQ || node->child_count != 3u) {
        gm_report(context, node->line, node->column, "%s must be a list of 3 numbers", field);
        return;
    }
    for (uint32_t i = 0u; i < 3u; i++) {
        gm_read_decimal(context, yaml_seq_at(document, node, i), field, sign, limit, &out[i]);
    }
}

static bool gm_file_exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static void gm_read_texture(gm_context_t *context, const yaml_node_t *node, gm_mob_t *mob) {
    const char *const name = gm_read_text(context, node);
    if (name == nullptr) {
        return;
    }
    const uint64_t group = strlen(GM_TEXTURE_GROUP);
    if (strncmp(name, GM_TEXTURE_GROUP, group) != 0 || !tp_path_is_safe(name) || strchr(name + group, '/') != nullptr) {
        gm_report(context, node->line, node->column, "texture '%s' is not of the form mobs/<name>", name);
        return;
    }
    const char *const pixels =
        tp_arena_format(context->borrowed_arena, "%s/%s/index.mctex", context->borrowed_textures, name);
    if (!gm_file_exists(pixels)) {
        gm_report(context, node->line, node->column, "texture '%s' does not exist: no %s", name, pixels);
        return;
    }
    mob->borrowed_texture = name;
}

/* A name that has a directory holding an index.yaml, under base. */
static const char *gm_read_reference(gm_context_t *context, const yaml_node_t *node, const char *what,
                                     const char *base) {
    const char *const name = gm_read_text(context, node);
    if (name == nullptr) {
        return nullptr;
    }
    if (!gm_name_is_valid(name) ||
        !gm_file_exists(tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", base, name))) {
        gm_report(context, node->line, node->column, "%s '%s' does not exist: no %s/%s/index.yaml", what, name, base,
                  name);
        return nullptr;
    }
    return name;
}

static void gm_read_drop(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *row,
                         gm_drop_t *drop) {
    if (row->kind != YAML_KIND_MAP) {
        gm_report(context, row->line, row->column, "a drop must be a mapping");
        return;
    }
    gm_reject_unknown(context, document, row, "a drop", GM_DROP_FIELDS, GM_DROP_FIELD_COUNT);
    const yaml_node_t *const item = yaml_map_get(document, row, "item");
    if (item == nullptr) {
        gm_report(context, row->line, row->column, "missing field 'item' in a drop");
    } else {
        drop->borrowed_item = gm_read_reference(context, item, "drop item", context->borrowed_items);
    }
    gm_read_number(context, row, yaml_map_get(document, row, "count"), "count", 0, GM_DROP_COUNT_LIMIT, false,
                   &drop->count_min);
    drop->count_max = drop->count_min;
    const yaml_node_t *const most = yaml_map_get(document, row, "max");
    gm_read_number(context, row, most, "max", 0, GM_DROP_COUNT_LIMIT, true, &drop->count_max);
    if (drop->count_max < drop->count_min) {
        gm_report(context, most->line, most->column, "drop max is below its count");
    }
    drop->chance = (yaml_decimal_t){.mantissa = 1, .exponent = 0};
    gm_read_decimal(context, yaml_map_get(document, row, "chance"), "drop chance", GM_SIGN_POSITIVE, 1, &drop->chance);
    if (drop->count_max == 0u) {
        gm_report(context, row->line, row->column, "a drop that can never give anything is a mistake");
    }
}

/* A list of drops, which a caller has found. */
static void gm_read_drop_list(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *drops,
                              gm_drop_t **out_drops, uint32_t *out_count) {
    if (drops->kind != YAML_KIND_SEQ) {
        gm_report(context, drops->line, drops->column, "drops must be a list");
        return;
    }
    if (drops->child_count > GM_MAX_DROPS) {
        gm_report(context, drops->line, drops->column, "over %u drops", GM_MAX_DROPS);
        return;
    }
    *out_drops = tp_arena_alloc(context->borrowed_arena, (drops->child_count + 1ull) * sizeof(gm_drop_t));
    *out_count = drops->child_count;
    uint32_t i = 0u;
    for (const yaml_node_t *row = yaml_first_child(document, drops); row != nullptr;
         row = yaml_next_sibling(document, row)) {
        gm_read_drop(context, document, row, &(*out_drops)[i]);
        i++;
    }
}

static void gm_read_drops(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                          gm_mob_t *mob) {
    const yaml_node_t *const drops = yaml_map_get(document, root, "drops");
    if (drops == nullptr) {
        gm_report(context, root->line, root->column, "missing field 'drops'");
        return;
    }
    gm_read_drop_list(context, document, drops, &mob->borrowed_drops, &mob->drop_count);
}

/* A list of names, each with a directory holding an index.yaml under base, such as never_on's blocks. Absent is none. */
static void gm_read_names(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *list,
                          const char *field, const char *what, const char *label, const char *base, uint32_t most,
                          const char ***out_names, uint32_t *out_count) {
    if (list == nullptr) {
        return;
    }
    if (list->kind != YAML_KIND_SEQ) {
        gm_report(context, list->line, list->column, "%s must be a list", field);
        return;
    }
    if (list->child_count > most) {
        gm_report(context, list->line, list->column, "over %u %ss in %s", most, what, field);
        return;
    }
    *out_names = tp_arena_alloc(context->borrowed_arena, (list->child_count + 1ull) * sizeof(const char *));
    *out_count = list->child_count;
    uint32_t i = 0u;
    for (const yaml_node_t *row = yaml_first_child(document, list); row != nullptr;
         row = yaml_next_sibling(document, row)) {
        (*out_names)[i] = gm_read_reference(context, row, label, base);
        i++;
    }
}

static void gm_read_spawn(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                          gm_mob_t *mob) {
    const yaml_node_t *const spawn = yaml_map_get(document, root, "spawn");
    if (spawn == nullptr) {
        gm_report(context, root->line, root->column, "missing field 'spawn'");
        return;
    }
    if (spawn->kind != YAML_KIND_MAP) {
        gm_report(context, spawn->line, spawn->column, "spawn must be a mapping");
        return;
    }
    gm_reject_unknown(context, document, spawn, "spawn", GM_SPAWN_FIELDS, GM_SPAWN_FIELD_COUNT);
    gm_read_number(context, spawn, yaml_map_get(document, spawn, "weight"), "weight", 0, GM_WEIGHT_LIMIT, false,
                   &mob->weight);
    gm_read_number(context, spawn, yaml_map_get(document, spawn, "pack_min"), "pack_min", 1, GM_PACK_LIMIT, false,
                   &mob->pack_min);
    const yaml_node_t *const pack_max = yaml_map_get(document, spawn, "pack_max");
    gm_read_number(context, spawn, pack_max, "pack_max", 1, GM_PACK_LIMIT, false, &mob->pack_max);
    if (pack_max != nullptr && mob->pack_max < mob->pack_min) {
        gm_report(context, pack_max->line, pack_max->column, "pack_max is below pack_min");
    }
    gm_read_number(context, spawn, yaml_map_get(document, spawn, "max_light"), "max_light", 0, GM_LIGHT_LIMIT, false,
                   &mob->max_light);
    gm_read_number(context, spawn, yaml_map_get(document, spawn, "max_block_light"), "max_block_light", 0,
                   GM_LIGHT_LIMIT, false, &mob->max_block_light);
    gm_read_number(context, spawn, yaml_map_get(document, spawn, "min_distance"), "min_distance", 0,
                   GM_MIN_DISTANCE_LIMIT, false, &mob->min_distance);
    gm_read_names(context, document, yaml_map_get(document, spawn, "never_on"), "never_on", "block", "never_on block",
                  context->borrowed_blocks, GM_MAX_NEVER_ON, &mob->borrowed_never_on, &mob->never_on_count);
    const yaml_node_t *const min_light = yaml_map_get(document, spawn, "min_light");
    gm_read_number(context, spawn, min_light, "min_light", 0, GM_LIGHT_LIMIT, true, &mob->min_light);
    if (min_light != nullptr && mob->min_light > mob->max_light) {
        gm_report(context, min_light->line, min_light->column, "min_light is above max_light, so it never comes");
    }
    gm_read_names(context, document, yaml_map_get(document, spawn, "on"), "on", "block", "on block",
                  context->borrowed_blocks, GM_MAX_ON, &mob->borrowed_on, &mob->on_count);
    gm_read_names(context, document, yaml_map_get(document, spawn, "biomes"), "biomes", "biome", "biome",
                  context->borrowed_biomes, GM_MAX_BIOMES, &mob->borrowed_biomes, &mob->biome_count);
    mob->min_height = -GM_HEIGHT_RANGE;
    mob->max_height = GM_HEIGHT_RANGE;
    gm_read_signed(context, yaml_map_get(document, spawn, "min_height"), "min_height", GM_HEIGHT_RANGE,
                   &mob->min_height);
    const yaml_node_t *const max_height = yaml_map_get(document, spawn, "max_height");
    gm_read_signed(context, max_height, "max_height", GM_HEIGHT_RANGE, &mob->max_height);
    if (max_height != nullptr && mob->max_height < mob->min_height) {
        gm_report(context, max_height->line, max_height->column, "max_height is below min_height");
    }
}

static void gm_read_despawn(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                            gm_mob_t *mob) {
    const yaml_node_t *const despawn = yaml_map_get(document, root, "despawn");
    if (mob->mind == GM_MIND_PASSIVE) {
        /* An animal is kept, as the published game keeps its animals: the passive cap is what keeps them few. */
        if (despawn != nullptr) {
            gm_report(context, despawn->line, despawn->column, "a passive mob never despawns, so it has no despawn");
        }
        return;
    }
    if (despawn == nullptr) {
        gm_report(context, root->line, root->column, "missing field 'despawn'");
        return;
    }
    if (despawn->kind != YAML_KIND_MAP) {
        gm_report(context, despawn->line, despawn->column, "despawn must be a mapping");
        return;
    }
    gm_reject_unknown(context, document, despawn, "despawn", GM_DESPAWN_FIELDS, GM_DESPAWN_FIELD_COUNT);
    gm_read_number(context, despawn, yaml_map_get(document, despawn, "near"), "near", 1, GM_DESPAWN_DISTANCE_LIMIT,
                   false, &mob->despawn_near);
    const yaml_node_t *const far = yaml_map_get(document, despawn, "far");
    gm_read_number(context, despawn, far, "far", 1, GM_DESPAWN_DISTANCE_LIMIT, false, &mob->despawn_far);
    if (far != nullptr && mob->despawn_far <= mob->despawn_near) {
        gm_report(context, far->line, far->column, "despawn far must be above near");
    }
    if (far != nullptr && mob->despawn_far <= mob->min_distance) {
        gm_report(context, far->line, far->column, "despawn far must be above spawn's min_distance");
    }
    gm_read_number(context, despawn, yaml_map_get(document, despawn, "idle_ticks"), "idle_ticks", 0,
                   GM_DESPAWN_TIME_LIMIT, false, &mob->despawn_idle_ticks);
    gm_read_number(context, despawn, yaml_map_get(document, despawn, "one_in"), "one_in", 1, GM_DESPAWN_TIME_LIMIT,
                   false, &mob->despawn_one_in);
}

static void gm_read_part(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *row,
                         gm_part_t *part) {
    if (row->kind != YAML_KIND_MAP) {
        gm_report(context, row->line, row->column, "a part must be a mapping");
        return;
    }
    gm_reject_unknown(context, document, row, "a part", GM_PART_FIELDS, GM_PART_FIELD_COUNT);
    gm_read_vector(context, document, row, "size", GM_SIGN_POSITIVE, GM_PART_SIZE_LIMIT, part->size);
    gm_read_vector(context, document, row, "pivot", GM_SIGN_ANY, GM_PART_SIZE_LIMIT, part->pivot);
    gm_read_vector(context, document, row, "offset", GM_SIGN_ANY, GM_PART_SIZE_LIMIT, part->offset);
    gm_read_decimal(context, yaml_map_get(document, row, "rest_pitch"), "rest_pitch", GM_SIGN_ANY, GM_ANGLE_LIMIT,
                    &part->rest_pitch);
    gm_read_decimal(context, yaml_map_get(document, row, "walk_swing"), "walk_swing", GM_SIGN_ANY, GM_ANGLE_LIMIT,
                    &part->walk_swing);
    const yaml_node_t *const uv = yaml_map_get(document, row, "uv");
    if (uv == nullptr) {
        gm_report(context, row->line, row->column, "missing field 'uv' in a part");
    } else if (uv->kind != YAML_KIND_SEQ || uv->child_count != 2u) {
        gm_report(context, uv->line, uv->column, "uv must be a list of 2 numbers");
    } else {
        gm_read_number(context, uv, yaml_seq_at(document, uv, 0u), "uv", 0, GM_TEXTURE_LIMIT, false, &part->u);
        gm_read_number(context, uv, yaml_seq_at(document, uv, 1u), "uv", 0, GM_TEXTURE_LIMIT, false, &part->v);
    }
    const yaml_node_t *const moves = yaml_map_get(document, row, "moves");
    if (moves != nullptr) {
        const char *const text = gm_read_text(context, moves);
        bool known = false;
        for (uint32_t i = 0u; text != nullptr && i < GM_MOVES_COUNT; i++) {
            if (strcmp(text, GM_MOVES[i]) == 0) {
                part->moves = (uint8_t)i;
                known = true;
            }
        }
        if (text != nullptr && !known) {
            gm_report(context, moves->line, moves->column, "unknown moves '%s'", text);
        }
    }
}

/* A list of parts, which a caller has found: from least to GM_PARTS_MAX of them. */
static void gm_read_part_list(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *parts,
                              uint32_t least, gm_part_t **out_parts, uint32_t *out_count) {
    if (parts->kind != YAML_KIND_SEQ || parts->child_count < least || parts->child_count > GM_PARTS_MAX) {
        gm_report(context, parts->line, parts->column, "parts must be a list of %u to %u", least, GM_PARTS_MAX);
        return;
    }
    *out_parts = tp_arena_alloc(context->borrowed_arena, (parts->child_count + 1ull) * sizeof(gm_part_t));
    *out_count = parts->child_count;
    uint32_t i = 0u;
    for (const yaml_node_t *row = yaml_first_child(document, parts); row != nullptr;
         row = yaml_next_sibling(document, row)) {
        gm_read_part(context, document, row, &(*out_parts)[i]);
        i++;
    }
}

static void gm_read_parts(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                          gm_mob_t *mob) {
    const yaml_node_t *const parts = yaml_map_get(document, root, "parts");
    if (parts == nullptr) {
        gm_report(context, root->line, root->column, "missing field 'parts'");
        return;
    }
    gm_read_part_list(context, document, parts, 1u, &mob->borrowed_parts, &mob->part_count);
}

/* One thing a living animal gives: by itself, such as an egg, or to an item held, such as wool to shears. */
static void gm_read_yield(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *row,
                          gm_yield_t *yield) {
    if (row->kind != YAML_KIND_MAP) {
        gm_report(context, row->line, row->column, "a yield must be a mapping");
        return;
    }
    gm_reject_unknown(context, document, row, "a yield", GM_YIELD_FIELDS, GM_YIELD_FIELD_COUNT);
    const yaml_node_t *const item = yaml_map_get(document, row, "item");
    if (item == nullptr) {
        gm_report(context, row->line, row->column, "missing field 'item' in a yield");
    } else {
        yield->borrowed_item = gm_read_reference(context, item, "yield item", context->borrowed_items);
    }
    gm_read_number(context, row, yaml_map_get(document, row, "count"), "count", 1, GM_DROP_COUNT_LIMIT, false,
                   &yield->count_min);
    yield->count_max = yield->count_min;
    const yaml_node_t *const most = yaml_map_get(document, row, "max");
    gm_read_number(context, row, most, "max", 1, GM_DROP_COUNT_LIMIT, true, &yield->count_max);
    if (most != nullptr && yield->count_max < yield->count_min) {
        gm_report(context, most->line, most->column, "yield max is below its count");
    }
    const yaml_node_t *const tool = yaml_map_get(document, row, "using");
    if (tool != nullptr) {
        yield->borrowed_using = gm_read_reference(context, tool, "yield using", context->borrowed_items);
    }
    gm_read_number(context, row, yaml_map_get(document, row, "every"), "every", 1, GM_EVERY_LIMIT, false,
                   &yield->every);
}

static void gm_read_yields(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *yields,
                           gm_sex_t *sex) {
    if (yields->kind != YAML_KIND_SEQ) {
        gm_report(context, yields->line, yields->column, "yields must be a list");
        return;
    }
    if (yields->child_count > GM_MAX_YIELDS) {
        gm_report(context, yields->line, yields->column, "over %u yields", GM_MAX_YIELDS);
        return;
    }
    sex->borrowed_yields = tp_arena_alloc(context->borrowed_arena, (yields->child_count + 1ull) * sizeof(gm_yield_t));
    sex->yield_count = yields->child_count;
    uint32_t i = 0u;
    for (const yaml_node_t *row = yaml_first_child(document, yields); row != nullptr;
         row = yaml_next_sibling(document, row)) {
        gm_read_yield(context, document, row, &sex->borrowed_yields[i]);
        i++;
    }
}

/* A male: or a female: part. Its boxes go with the mob's own, so the two together must fit a rig. */
static void gm_read_sex(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *node,
                        const char *which, const gm_mob_t *mob, gm_sex_t *sex) {
    if (node->kind != YAML_KIND_MAP) {
        gm_report(context, node->line, node->column, "%s must be a mapping", which);
        return;
    }
    sex->present = 1u;
    gm_reject_unknown(context, document, node, which, GM_SEX_FIELDS, GM_SEX_FIELD_COUNT);
    const yaml_node_t *const name = yaml_map_get(document, node, "name");
    if (name == nullptr) {
        gm_report(context, node->line, node->column, "missing field 'name' in %s", which);
    } else {
        const char *const text = gm_read_text(context, name);
        if (text != nullptr && !gm_name_is_valid(text)) {
            gm_report(context, name->line, name->column, "%s name '%s' is not lower case, digits and underscores",
                      which, text);
        } else {
            sex->borrowed_name = text;
        }
    }
    const yaml_node_t *const scale = yaml_map_get(document, node, "scale");
    if (scale == nullptr) {
        gm_report(context, node->line, node->column, "missing field 'scale' in %s", which);
    } else {
        gm_read_decimal(context, scale, "scale", GM_SIGN_POSITIVE, GM_SCALE_LIMIT, &sex->scale);
    }
    const yaml_node_t *const parts = yaml_map_get(document, node, "parts");
    if (parts != nullptr) {
        gm_read_part_list(context, document, parts, 0u, &sex->borrowed_parts, &sex->part_count);
        if (mob->part_count + sex->part_count > GM_PARTS_MAX) {
            gm_report(context, parts->line, parts->column, "the mob's %u parts and %s's %u are over %u",
                      mob->part_count, which, sex->part_count, GM_PARTS_MAX);
        }
    }
    const yaml_node_t *const drops = yaml_map_get(document, node, "drops");
    if (drops != nullptr) {
        gm_read_drop_list(context, document, drops, &sex->borrowed_drops, &sex->drop_count);
    }
    const yaml_node_t *const yields = yaml_map_get(document, node, "yields");
    if (yields != nullptr) {
        gm_read_yields(context, document, yields, sex);
    }
}

/* Both or neither: an animal is male or female, half and half. */
static void gm_read_sexes(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                          gm_mob_t *mob) {
    const yaml_node_t *nodes[GM_SEX_COUNT];
    for (uint32_t s = 0u; s < GM_SEX_COUNT; s++) {
        nodes[s] = yaml_map_get(document, root, GM_SEXES[s]);
        if (nodes[s] != nullptr) {
            gm_read_sex(context, document, nodes[s], GM_SEXES[s], mob, &mob->sexes[s]);
        }
    }
    for (uint32_t s = 0u; s < GM_SEX_COUNT; s++) {
        const yaml_node_t *const other = nodes[GM_SEX_COUNT - 1u - s];
        if (nodes[s] == nullptr && other != nullptr) {
            gm_report(context, other->line, other->column, "%s has no %s: an animal has both or neither",
                      GM_SEXES[GM_SEX_COUNT - 1u - s], GM_SEXES[s]);
        }
    }
}

/* One parsed file into one mob. */
static void gm_read_fields(gm_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                           gm_mob_t *mob) {
    gm_reject_unknown(context, document, root, "a mob", GM_MOB_FIELDS, GM_MOB_FIELD_COUNT);

    const yaml_node_t *const name = yaml_map_get(document, root, "name");
    if (name == nullptr) {
        gm_report(context, root->line, root->column, "missing field 'name'");
    } else {
        const char *const text = gm_read_text(context, name);
        if (text != nullptr && strcmp(text, mob->borrowed_name) != 0) {
            gm_report(context, name->line, name->column, "name '%s' is not its directory's, '%s'", text,
                      mob->borrowed_name);
        }
    }
    const yaml_node_t *const texture = yaml_map_get(document, root, "texture");
    if (texture == nullptr) {
        gm_report(context, root->line, root->column, "missing field 'texture'");
    } else {
        gm_read_texture(context, texture, mob);
    }
    gm_read_number(context, root, yaml_map_get(document, root, "texture_width"), "texture_width", 1, GM_TEXTURE_LIMIT,
                   false, &mob->texture_width);
    gm_read_number(context, root, yaml_map_get(document, root, "texture_height"), "texture_height", 1, GM_TEXTURE_LIMIT,
                   false, &mob->texture_height);
    gm_read_required_decimal(context, document, root, "health", GM_HEALTH_LIMIT, &mob->health);
    gm_read_required_decimal(context, document, root, "speed", 1, &mob->speed);
    gm_read_required_decimal(context, document, root, "half_width", GM_HALF_WIDTH_LIMIT, &mob->half_width);
    gm_read_required_decimal(context, document, root, "height", GM_HEIGHT_LIMIT, &mob->height);
    gm_read_word(context, yaml_map_get(document, root, "mind"), "mind", GM_MINDS, GM_MINDS_COUNT, &mob->mind);
    gm_read_drops(context, document, root, mob);
    gm_read_spawn(context, document, root, mob);
    gm_read_despawn(context, document, root, mob);
    gm_read_parts(context, document, root, mob);
    gm_read_sexes(context, document, root, mob);
}

/* Reads one mob's file. What it finds wrong is counted in the context. */
static void gm_read_mob(gm_context_t *context, const char *mobs, const char *name, gm_mob_t *mob) {
    memset(mob, 0, sizeof(*mob));
    mob->borrowed_name = name;
    context->borrowed_path = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", mobs, name);
    if (!gm_name_is_valid(name)) {
        gm_report(context, 1, 1, "directory name '%s' is not lower case, digits and underscores", name);
        return;
    }
    if (strcmp(name, GM_NONE) == 0) {
        gm_report(context, 1, 1, "'%s' is the name of the empty row, which has no file", GM_NONE);
        return;
    }
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status =
        tp_read_path(context->borrowed_arena, context->borrowed_path, GM_MAX_FILE_BYTES, &text, &length);
    if (status != TP_READ_OK) {
        if (status == TP_READ_MISSING) {
            fprintf(stderr, "%s: missing\n", context->borrowed_path);
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
        gm_report_yaml(context, &error);
        return;
    }
    const yaml_node_t *const root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        gm_report(context, root->line == 0u ? 1u : root->line, root->column == 0u ? 1u : root->column,
                  "a mob's file must be a mapping");
        return;
    }
    gm_read_fields(context, &document, root, mob);
}

static int32_t gm_compare_text(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Every directory under mobs, sorted, so what the tables hold does not depend on the order of reading. */
static bool gm_list_mobs(tp_arena_t *arena, const char *mobs, const char ***out_names, uint32_t *out_count) {
    const char **names = tp_arena_alloc(arena, (GM_MAX_MOBS + 1ull) * sizeof(const char *));
    uint32_t count = 0;
    DIR *listing = opendir(mobs);
    if (listing == nullptr) {
        return tp_fail("%s: cannot be listed", mobs);
    }
    for (struct dirent *entry = readdir(listing); entry != nullptr; entry = readdir(listing)) {
        struct stat info;
        const char *const child = tp_arena_format(arena, "%s/%s", mobs, entry->d_name);
        if (entry->d_name[0] == '.' || stat(child, &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        if (count == GM_MAX_MOBS) {
            closedir(listing);
            return tp_fail("%s: holds over %u mobs", mobs, GM_MAX_MOBS);
        }
        names[count] = tp_arena_text(arena, entry->d_name, strlen(entry->d_name));
        count++;
    }
    closedir(listing);
    qsort(names, count, sizeof(const char *), gm_compare_text);
    *out_names = names;
    *out_count = count;
    return true;
}

/* In upper case, for an enum name. Names were checked to be short. */
static const char *gm_upper(tp_arena_t *arena, const char *name) {
    char *const text = tp_arena_text(arena, name, strlen(name));
    for (char *c = text; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return text;
}

/* How many rows each array has, with its row 0 that belongs to none. */
typedef struct gm_totals_t {
    uint32_t drops;    /* 0 */
    uint32_t never_on; /* 4 */
    uint32_t parts;    /* 8 */
    uint32_t on;       /* 12 */
    uint32_t biomes;   /* 16 */
    uint32_t sexes;    /* 20 */
    uint32_t yields;   /* 24 */
} gm_totals_t;
static_assert(sizeof(gm_totals_t) == 28);

static const gm_totals_t GM_TOTALS_START = {
    .drops = 1u, .never_on = 1u, .parts = 1u, .on = 1u, .biomes = 1u, .sexes = 1u, .yields = 1u};

/* What one mob adds to each array. */
static void gm_totals_add(gm_totals_t *totals, const gm_mob_t *mob) {
    totals->drops += mob->drop_count;
    totals->never_on += mob->never_on_count;
    totals->parts += mob->part_count;
    totals->on += mob->on_count;
    totals->biomes += mob->biome_count;
    for (uint32_t s = 0u; s < GM_SEX_COUNT; s++) {
        const gm_sex_t *const sex = &mob->sexes[s];
        totals->drops += sex->drop_count;
        totals->parts += sex->part_count;
        totals->yields += sex->yield_count;
        totals->sexes += sex->present;
    }
}

static gm_totals_t gm_totals_of(const gm_mob_t *mobs, uint32_t count) {
    gm_totals_t totals = GM_TOTALS_START;
    for (uint32_t i = 0u; i < count; i++) {
        gm_totals_add(&totals, &mobs[i]);
    }
    return totals;
}

static void gm_emit_types(tp_builder_t *out) {
    tp_builder_add(
        out,
        "\n/* One box of a body, and entity_animation_part_t is this: see run/src/entity/animation/. */\n"
        "typedef struct mob_part_t {\n"
        "    float32_t size[3];    /* 0: pixels */\n"
        "    float32_t pivot[3];   /* 12: pixels from the middle of the soles of the feet: what it turns about */\n"
        "    float32_t offset[3];  /* 24: pixels: the box's middle from its pivot, at rest */\n"
        "    float32_t rest_pitch; /* 36: radians about its left axis: more points what hangs from it forward and up "
        "*/\n"
        "    float32_t walk_swing; /* 40: radians at a full stride, at the phase's start. The other sign swings the "
        "other way */\n"
        "    uint16_t texture_u;   /* 44: the corner of its box-mapped unwrap in the rig's texture */\n"
        "    uint16_t texture_v;   /* 46 */\n"
        "    uint8_t flags;        /* 48: MOB_PART_MOVES_ */\n"
        "    uint8_t _pad0[3];     /* 49 */\n"
        "} mob_part_t;\n"
        "static_assert(sizeof(mob_part_t) == 52);\n\n");
    tp_builder_add(out, "/* Each row is rolled on its own: chance, then a count from count_min to count_max. */\n"
                        "typedef struct mob_drop_t {\n"
                        "    float32_t chance;  /* 0: above 0, at most 1 */\n"
                        "    uint16_t item;     /* 4: an item_id_t */\n"
                        "    uint8_t count_min; /* 6 */\n"
                        "    uint8_t count_max; /* 7 */\n"
                        "} mob_drop_t;\n"
                        "static_assert(sizeof(mob_drop_t) == 8);\n\n");
    tp_builder_add(
        out, "/* What a living animal gives: count_min to count_max of item, to tool held, or by itself if "
             "tool is ITEM_ID_NONE, and then not again for every ticks. */\n"
             "typedef struct mob_yield_t {\n"
             "    uint32_t every;     /* 0: ticks */\n"
             "    uint16_t item;      /* 4: an item_id_t */\n"
             "    uint16_t tool;      /* 6: an item_id_t: what is held to take it; ITEM_ID_NONE for by itself */\n"
             "    uint8_t count_min;  /* 8 */\n"
             "    uint8_t count_max;  /* 9 */\n"
             "    uint8_t _pad0[2];   /* 10 */\n"
             "} mob_yield_t;\n"
             "static_assert(sizeof(mob_yield_t) == 12);\n\n");
    tp_builder_add(out,
                   "/* A male or a female of an animal: its name, its size, its boxes besides the mob's own, what it "
                   "drops besides the mob's own, and what it gives alive. */\n"
                   "typedef struct mob_sex_t {\n"
                   "    const char *name;     /* 0: \"bull\" */\n"
                   "    float32_t scale;      /* 8: of the mob's box and every part, its own too */\n"
                   "    uint16_t first_part;  /* 12: into mob_parts */\n"
                   "    uint16_t first_drop;  /* 14: into mob_drops */\n"
                   "    uint16_t first_yield; /* 16: into mob_yields */\n"
                   "    uint8_t part_count;   /* 18 */\n"
                   "    uint8_t drop_count;   /* 19 */\n"
                   "    uint8_t yield_count;  /* 20 */\n"
                   "    uint8_t sex;          /* 21: MOB_SEX_ */\n"
                   "    uint8_t _pad0[2];     /* 22 */\n"
                   "} mob_sex_t;\n"
                   "static_assert(sizeof(mob_sex_t) == 24);\n\n");
    tp_builder_add(
        out,
        "/* One row per mob. */\n"
        "typedef struct mob_def_t {\n"
        "    const char *name;            /* 0 */\n"
        "    const char *texture;         /* 8: \"mobs/zombie\"; nullptr only for none */\n"
        "    float32_t health;            /* 16: half hearts */\n"
        "    float32_t speed;             /* 20: blocks per tick on plain ground */\n"
        "    float32_t half_width;        /* 24: blocks */\n"
        "    float32_t height;            /* 28: blocks */\n"
        "    uint32_t despawn_idle_ticks; /* 32: past despawn_near, how long before it may go */\n"
        "    uint32_t despawn_one_in;     /* 36: after that, its chance each tick is one in this */\n"
        "    uint16_t weight;             /* 40: how often it is picked among the mobs; 0 never */\n"
        "    uint16_t min_distance;       /* 42: never nearer the player than this, in blocks */\n"
        "    uint16_t despawn_near;       /* 44: within this of the player, its idle count starts over */\n"
        "    uint16_t despawn_far;        /* 46: past this, it goes at once; 0 for a passive mob, which never "
        "goes */\n"
        "    uint16_t texture_width;      /* 48 */\n"
        "    uint16_t texture_height;     /* 50 */\n"
        "    uint16_t first_drop;         /* 52: into mob_drops */\n"
        "    uint16_t first_never_on;     /* 54: into mob_never_on */\n"
        "    uint16_t first_part;         /* 56: into mob_parts */\n"
        "    uint8_t drop_count;          /* 58 */\n"
        "    uint8_t never_on_count;      /* 59 */\n"
        "    uint8_t part_count;          /* 60 */\n"
        "    uint8_t pack_min;            /* 61 */\n"
        "    uint8_t pack_max;            /* 62 */\n"
        "    uint8_t max_light;           /* 63: 0 to 15: a place passes at or under a roll from 0 to this */\n"
        "    uint8_t max_block_light;     /* 64: 0 to 15: the most block light it can stand */\n"
        "    uint8_t min_light;           /* 65: 0 to 15: the least light it comes to */\n"
        "    uint8_t mind;                /* 66: MOB_MIND_ */\n"
        "    uint8_t on_count;            /* 67 */\n"
        "    uint16_t first_on;           /* 68: into mob_on: the only blocks it comes onto, if any */\n"
        "    uint16_t first_biome;        /* 70: into mob_biomes: the only biomes it comes to, if any */\n"
        "    int16_t min_height;          /* 72: blocks above the sea it comes at, from the floor it stands on */\n"
        "    int16_t max_height;          /* 74 */\n"
        "    uint16_t first_sex;          /* 76: into mob_sexes, the male, with the female after it; 0 for "
        "none */\n"
        "    uint8_t biome_count;         /* 78 */\n"
        "    uint8_t _pad0[1];            /* 79 */\n"
        "} mob_def_t;\n"
        "static_assert(sizeof(mob_def_t) == 80);\n\n"
        "extern const mob_def_t mob_defs[MOB_COUNT];\n"
        "extern const mob_drop_t mob_drops[MOB_DROP_COUNT];\n"
        "extern const uint16_t mob_never_on[MOB_NEVER_ON_COUNT];\n"
        "extern const mob_part_t mob_parts[MOB_PART_COUNT];\n"
        "extern const uint16_t mob_on[MOB_ON_COUNT];\n"
        "extern const uint8_t mob_biomes[MOB_BIOME_COUNT];\n"
        "extern const mob_sex_t mob_sexes[MOB_SEX_ROW_COUNT];\n"
        "extern const mob_yield_t mob_yields[MOB_YIELD_COUNT];\n\n"
        "#endif\n");
}

static void gm_emit_header(tp_arena_t *arena, tp_builder_t *out, const gm_mob_t *mobs, uint32_t count) {
    const gm_totals_t totals = gm_totals_of(mobs, count);
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/mobs/. Change the yaml and build; never edit this. */\n"
        "#ifndef TABLES_MOBS_INDEX_H\n#define TABLES_MOBS_INDEX_H\n\n"
        "#include \"types/index.h\"\n\n#include <stdint.h>\n\n");
    tp_builder_add(out,
                   "/* Mob 0 is none, and the rest sort by name: adding a mob moves the ids after it. A body's kind in "
                   "entity_store_t is one of these. */\n");
    tp_builder_add(out, "typedef enum mob_id_t : uint16_t {\n    MOB_ID_%s = 0,\n", gm_upper(arena, GM_NONE));
    for (uint32_t i = 0u; i < count; i++) {
        tp_builder_add(out, "    MOB_ID_%s = %u,\n", gm_upper(arena, mobs[i].borrowed_name), i + 1u);
    }
    tp_builder_add(out, "} mob_id_t;\n\n");
    tp_builder_add(out,
                   "#define MOB_COUNT %uu\n#define MOB_DROP_COUNT %uu\n#define MOB_NEVER_ON_COUNT %uu\n"
                   "#define MOB_PART_COUNT %uu\n#define MOB_ON_COUNT %uu\n#define MOB_BIOME_COUNT %uu\n"
                   "#define MOB_SEX_ROW_COUNT %uu\n#define MOB_YIELD_COUNT %uu\n"
                   "/* The most boxes in a body, its sex's with its own. */\n#define MOB_PARTS_MAX %uu\n"
                   "/* The most yields a male or a female has. */\n#define MOB_YIELDS_MAX %uu\n\n",
                   count + 1u, totals.drops, totals.never_on, totals.parts, totals.on, totals.biomes, totals.sexes,
                   totals.yields, GM_PARTS_MAX, GM_MAX_YIELDS);
    tp_builder_add(out, "/* In mob_def_t.mind: what it does, and by which rules it comes and goes. */\n");
    for (uint32_t i = 0u; i < GM_MINDS_COUNT; i++) {
        tp_builder_add(out, "#define MOB_MIND_%s %uu\n", gm_upper(arena, GM_MINDS[i]), i);
    }
    tp_builder_add(out, "\n/* In mob_sex_t.sex, and in entity_store_t's sex: none for a mob with no male: and female:. "
                        "*/\n#define MOB_SEX_NONE 0u\n");
    for (uint32_t i = 0u; i < GM_SEX_COUNT; i++) {
        tp_builder_add(out, "#define MOB_SEX_%s %uu\n", gm_upper(arena, GM_SEXES[i]), i + 1u);
    }
    tp_builder_add(out, "\n/* In mob_part_t.flags: what moves it besides walking. */\n");
    for (uint32_t i = 1u; i < GM_MOVES_COUNT; i++) {
        tp_builder_add(out, "#define MOB_PART_MOVES_%s %uu\n", gm_upper(arena, GM_MOVES[i]), 1u << (i - 1u));
    }
    tp_builder_add(out, "\n/* Where each mob's own body is, as constants, so that a rig can be a constant. */\n");
    gm_totals_t first = GM_TOTALS_START;
    for (uint32_t i = 0u; i < count; i++) {
        const char *const upper = gm_upper(arena, mobs[i].borrowed_name);
        tp_builder_add(out,
                       "#define MOB_%s_FIRST_PART %uu\n#define MOB_%s_PART_COUNT %uu\n"
                       "#define MOB_%s_TEXTURE_WIDTH %uu\n#define MOB_%s_TEXTURE_HEIGHT %uu\n",
                       upper, first.parts, upper, mobs[i].part_count, upper, mobs[i].texture_width, upper,
                       mobs[i].texture_height);
        gm_totals_add(&first, &mobs[i]);
    }
    gm_emit_types(out);
}

static void gm_add_decimal(tp_builder_t *out, yaml_decimal_t value) {
    tp_builder_add(out, "%" PRId64 "e%df32", value.mantissa, value.exponent);
}

static void gm_add_vector(tp_builder_t *out, const char *field, const yaml_decimal_t *values) {
    tp_builder_add(out, ".%s = {", field);
    for (uint32_t i = 0u; i < 3u; i++) {
        tp_builder_add(out, i == 0u ? "" : ", ");
        gm_add_decimal(out, values[i]);
    }
    tp_builder_add(out, "}, ");
}

static void gm_add_drops(tp_arena_t *arena, tp_builder_t *out, const gm_drop_t *drops, uint32_t count) {
    for (uint32_t d = 0u; d < count; d++) {
        tp_builder_add(out, "    {.chance = ");
        gm_add_decimal(out, drops[d].chance);
        tp_builder_add(out, ", .item = ITEM_ID_%s, .count_min = %u, .count_max = %u},\n",
                       gm_upper(arena, drops[d].borrowed_item), drops[d].count_min, drops[d].count_max);
    }
}

static void gm_add_parts(tp_arena_t *arena, tp_builder_t *out, const gm_part_t *parts, uint32_t count) {
    for (uint32_t p = 0u; p < count; p++) {
        const gm_part_t *const part = &parts[p];
        tp_builder_add(out, "    {");
        gm_add_vector(out, "size", part->size);
        gm_add_vector(out, "pivot", part->pivot);
        gm_add_vector(out, "offset", part->offset);
        tp_builder_add(out, ".rest_pitch = ");
        gm_add_decimal(out, part->rest_pitch);
        tp_builder_add(out, ", .walk_swing = ");
        gm_add_decimal(out, part->walk_swing);
        tp_builder_add(out, ", .texture_u = %u, .texture_v = %u, .flags = ", part->u, part->v);
        if (part->moves == 0u) {
            tp_builder_add(out, "0u},\n");
        } else {
            tp_builder_add(out, "MOB_PART_MOVES_%s},\n", gm_upper(arena, GM_MOVES[part->moves]));
        }
    }
}

static void gm_add_names(tp_arena_t *arena, tp_builder_t *out, const char *prefix, const char *const *names,
                         uint32_t count) {
    for (uint32_t i = 0u; i < count; i++) {
        tp_builder_add(out, "    %s%s,\n", prefix, gm_upper(arena, names[i]));
    }
}

/* Every array but the defs. A mob's rows of each are together: its own first, then its male's, then its female's. */
static void gm_emit_rows(tp_arena_t *arena, tp_builder_t *out, const gm_mob_t *mobs, uint32_t count) {
    tp_builder_add(out, "const mob_drop_t mob_drops[MOB_DROP_COUNT] = {\n    {.count_max = 0u},\n");
    for (uint32_t i = 0u; i < count; i++) {
        gm_add_drops(arena, out, mobs[i].borrowed_drops, mobs[i].drop_count);
        for (uint32_t s = 0u; s < GM_SEX_COUNT; s++) {
            gm_add_drops(arena, out, mobs[i].sexes[s].borrowed_drops, mobs[i].sexes[s].drop_count);
        }
    }
    tp_builder_add(out, "};\n\nconst uint16_t mob_never_on[MOB_NEVER_ON_COUNT] = {\n    BLOCK_ID_AIR,\n");
    for (uint32_t i = 0u; i < count; i++) {
        gm_add_names(arena, out, "BLOCK_ID_", mobs[i].borrowed_never_on, mobs[i].never_on_count);
    }
    tp_builder_add(out, "};\n\nconst uint16_t mob_on[MOB_ON_COUNT] = {\n    BLOCK_ID_AIR,\n");
    for (uint32_t i = 0u; i < count; i++) {
        gm_add_names(arena, out, "BLOCK_ID_", mobs[i].borrowed_on, mobs[i].on_count);
    }
    tp_builder_add(out, "};\n\nconst uint8_t mob_biomes[MOB_BIOME_COUNT] = {\n    0u,\n");
    for (uint32_t i = 0u; i < count; i++) {
        gm_add_names(arena, out, "BIOME_ID_", mobs[i].borrowed_biomes, mobs[i].biome_count);
    }
    tp_builder_add(out, "};\n\nconst mob_part_t mob_parts[MOB_PART_COUNT] = {\n    {.flags = 0u},\n");
    for (uint32_t i = 0u; i < count; i++) {
        gm_add_parts(arena, out, mobs[i].borrowed_parts, mobs[i].part_count);
        for (uint32_t s = 0u; s < GM_SEX_COUNT; s++) {
            gm_add_parts(arena, out, mobs[i].sexes[s].borrowed_parts, mobs[i].sexes[s].part_count);
        }
    }
    tp_builder_add(out, "};\n\nconst mob_yield_t mob_yields[MOB_YIELD_COUNT] = {\n    {.every = 0u},\n");
    for (uint32_t i = 0u; i < count; i++) {
        for (uint32_t s = 0u; s < GM_SEX_COUNT; s++) {
            const gm_sex_t *const sex = &mobs[i].sexes[s];
            for (uint32_t y = 0u; y < sex->yield_count; y++) {
                const gm_yield_t *const yield = &sex->borrowed_yields[y];
                tp_builder_add(out,
                               "    {.every = %u, .item = ITEM_ID_%s, .tool = ITEM_ID_%s, .count_min = %u, "
                               ".count_max = %u},\n",
                               yield->every, gm_upper(arena, yield->borrowed_item),
                               gm_upper(arena, yield->borrowed_using != nullptr ? yield->borrowed_using : GM_NONE),
                               yield->count_min, yield->count_max);
            }
        }
    }
    tp_builder_add(out, "};\n");
}

/* The sexes' rows, each pointing at where its parts, drops and yields went in gm_emit_rows. */
static void gm_emit_sexes(tp_builder_t *out, const gm_mob_t *mobs, uint32_t count) {
    tp_builder_add(out, "\nconst mob_sex_t mob_sexes[MOB_SEX_ROW_COUNT] = {\n    {.name = \"%s\"},\n", GM_NONE);
    gm_totals_t first = GM_TOTALS_START;
    for (uint32_t i = 0u; i < count; i++) {
        const gm_mob_t *const mob = &mobs[i];
        uint32_t part = first.parts + mob->part_count;
        uint32_t drop = first.drops + mob->drop_count;
        uint32_t yield = first.yields;
        for (uint32_t s = 0u; s < GM_SEX_COUNT; s++) {
            const gm_sex_t *const sex = &mob->sexes[s];
            if (sex->present != 0u) {
                tp_builder_add(out, "    {.name = \"%s\", .scale = ", sex->borrowed_name);
                gm_add_decimal(out, sex->scale);
                tp_builder_add(out,
                               ", .first_part = %u, .first_drop = %u, .first_yield = %u, .part_count = %u, "
                               ".drop_count = %u, .yield_count = %u, .sex = %uu},\n",
                               part, drop, yield, sex->part_count, sex->drop_count, sex->yield_count, s + 1u);
            }
            part += sex->part_count;
            drop += sex->drop_count;
            yield += sex->yield_count;
        }
        gm_totals_add(&first, mob);
    }
    tp_builder_add(out, "};\n");
}

static void gm_emit_def(tp_arena_t *arena, tp_builder_t *out, const gm_mob_t *mob, const gm_totals_t *first) {
    tp_builder_add(out, "    [MOB_ID_%s] = {.name = \"%s\", .texture = \"%s\", .health = ",
                   gm_upper(arena, mob->borrowed_name), mob->borrowed_name, mob->borrowed_texture);
    gm_add_decimal(out, mob->health);
    tp_builder_add(out, ", .speed = ");
    gm_add_decimal(out, mob->speed);
    tp_builder_add(out, ", .half_width = ");
    gm_add_decimal(out, mob->half_width);
    tp_builder_add(out, ", .height = ");
    gm_add_decimal(out, mob->height);
    tp_builder_add(out,
                   ", .despawn_idle_ticks = %u, .despawn_one_in = %u, .weight = %u, .min_distance = %u, "
                   ".despawn_near = %u, .despawn_far = %u, .texture_width = %u, .texture_height = %u",
                   mob->despawn_idle_ticks, mob->despawn_one_in, mob->weight, mob->min_distance, mob->despawn_near,
                   mob->despawn_far, mob->texture_width, mob->texture_height);
    tp_builder_add(out,
                   ", .first_drop = %u, .first_never_on = %u, .first_part = %u, .drop_count = %u, "
                   ".never_on_count = %u, .part_count = %u",
                   first->drops, first->never_on, first->parts, mob->drop_count, mob->never_on_count, mob->part_count);
    tp_builder_add(out, ", .pack_min = %u, .pack_max = %u, .max_light = %u, .max_block_light = %u", mob->pack_min,
                   mob->pack_max, mob->max_light, mob->max_block_light);
    tp_builder_add(out,
                   ", .min_light = %u, .mind = MOB_MIND_%s, .on_count = %u, .first_on = %u, .first_biome = %u, "
                   ".min_height = %d, .max_height = %d, .first_sex = %u, .biome_count = %u},\n",
                   mob->min_light, gm_upper(arena, GM_MINDS[mob->mind]), mob->on_count, first->on, first->biomes,
                   mob->min_height, mob->max_height, mob->sexes[0].present != 0u ? first->sexes : 0u, mob->biome_count);
}

static void gm_emit_source(tp_arena_t *arena, tp_builder_t *out, const gm_mob_t *mobs, uint32_t count) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/mobs/. Change the yaml and build; never edit this. */\n"
        "#include \"tables/mobs/index.h\"\n\n"
        "#include \"tables/biomes/index.h\"\n#include \"tables/blocks/index.h\"\n#include \"tables/items/index.h\"\n\n"
        "const mob_def_t mob_defs[MOB_COUNT] = {\n");
    tp_builder_add(out, "    [MOB_ID_NONE] = {.name = \"%s\"},\n", GM_NONE);
    gm_totals_t first = GM_TOTALS_START;
    for (uint32_t i = 0u; i < count; i++) {
        gm_emit_def(arena, out, &mobs[i], &first);
        gm_totals_add(&first, &mobs[i]);
    }
    tp_builder_add(out, "};\n\n");
    gm_emit_rows(arena, out, mobs, count);
    gm_emit_sexes(out, mobs, count);
}

bool game_tables_mobs_build(tp_arena_t *arena, const char *config, const char *textures, const char *out) {
    const char *const mobs_path = tp_arena_format(arena, "%s/mobs", config);
    struct stat info;
    if (stat(mobs_path, &info) != 0) {
        return true; /* a tree with no mobs has no tables of them, and the real one's absence fails the build by its outputs */
    }
    const char **names = nullptr;
    uint32_t count = 0;
    if (!gm_list_mobs(arena, mobs_path, &names, &count)) {
        return false;
    }
    if (count == 0u) {
        return tp_fail("%s: there must be a mob", mobs_path);
    }
    gm_context_t context = {
        .borrowed_arena = arena,
        .borrowed_textures = textures,
        .borrowed_items = tp_arena_format(arena, "%s/items", config),
        .borrowed_blocks = tp_arena_format(arena, "%s/blocks", config),
        .borrowed_biomes = tp_arena_format(arena, "%s/biomes", config),
    };
    gm_mob_t *const mobs = tp_arena_alloc(arena, (count + 1ull) * sizeof(gm_mob_t));
    for (uint32_t i = 0u; i < count; i++) {
        gm_read_mob(&context, mobs_path, names[i], &mobs[i]);
    }
    if (context.problems != 0u) {
        return tp_fail("%s: %u problems; nothing was written", mobs_path, context.problems);
    }
    tp_builder_t header = tp_builder_begin(arena, GM_HEADER_BYTES);
    tp_builder_t source = tp_builder_begin(arena, GM_SOURCE_BYTES);
    gm_emit_header(arena, &header, mobs, count);
    gm_emit_source(arena, &source, mobs, count);

    const char *const directory = tp_arena_format(arena, "%s/mobs", out);
    return tp_make_directories(arena, directory) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.h", directory), header.borrowed_text,
                               header.used, nullptr) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.c", directory), source.borrowed_text,
                               source.used, nullptr);
}
