/*
 * config/biomes/ -> the biome tables. See README.md. Like the blocks part: read and check each file on its own, then emit. A
 * problem does not stop the reading, so one build shows all of them.
 */
#include "game_tables/biomes/index.h"

#include "yaml/index.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GZ_MAX_BIOMES 255u /* a uint8_t, and room for a "none" */
#define GZ_MAX_NAME_BYTES 64u
#define GZ_MAX_FILE_BYTES (1ull << 20)
#define GZ_MAX_PATCHES 4u
#define GZ_MAX_UNDERGROUND 8u
#define GZ_LAYERS_MAX 16
/* What the rules can add to filler_depth at most: run/src/terrain/surface/ asks the header for this. */
#define GZ_FILLER_WOBBLE 3
#define GZ_MAX_FEATURE_RATE 256
#define GZ_HEIGHT_LIMIT 1250
#define GZ_STEEP_LIMIT 16
/* Numbers are compared in millionths. A finer decimal than that is a typo. */
#define GZ_SCALE 1000000
#define GZ_HEADER_BYTES (1ull << 20)
#define GZ_SOURCE_BYTES (1ull << 21)

enum { GZ_WHERE_UNDERGROUND = 6 };
static const char *const GZ_WHERES[] = {"land", "ocean", "deep_ocean", "river", "beach", "stony_shore", "underground"};
enum { GZ_WHERE_COUNT = sizeof(GZ_WHERES) / sizeof(GZ_WHERES[0]) };

static const char *const GZ_FEATURES[] = {
    "tree_oak",      "tree_birch",  "tree_spruce", "tree_jungle", "tree_acacia", "tree_dark_oak",
    "tree_mangrove", "tree_cherry", "grass",       "fern",        "flower",      "cactus",
    "sugar_cane",    "dead_bush",   "mushroom",    "lily_pad",    "vine",        "melon",
    "pumpkin",       "seagrass",    "kelp",        "snow",        "ice",
};
enum { GZ_FEATURE_COUNT = sizeof(GZ_FEATURES) / sizeof(GZ_FEATURES[0]) };

static const char *const GZ_BIOME_FIELDS[] = {"name",    "where",   "priority", "landform",
                                              "climate", "surface", "cave",     "features"};
enum { GZ_BIOME_FIELD_COUNT = sizeof(GZ_BIOME_FIELDS) / sizeof(GZ_BIOME_FIELDS[0]) };
/* Plan 006 phase 03: the land's shape under landform, and the climate's say under climate. */
static const char *const GZ_LANDFORM_FIELDS[] = {"continentalness", "erosion", "weirdness", "height", "z"};
enum { GZ_LANDFORM_FIELD_COUNT = sizeof(GZ_LANDFORM_FIELDS) / sizeof(GZ_LANDFORM_FIELDS[0]) };
static const char *const GZ_CLIMATE_FIELDS[] = {"families", "variety", "wetness", "t_warm", "t_cold", "rain_mm"};
enum { GZ_CLIMATE_FIELD_COUNT = sizeof(GZ_CLIMATE_FIELDS) / sizeof(GZ_CLIMATE_FIELDS[0]) };
/* What the climate block held before plan 006 phase 03, each refused with where it went. */
static const char *const GZ_MOVED_FIELDS[] = {"temperature", "humidity", "continentalness", "erosion", "weirdness",
                                              "height",      "z"};
enum { GZ_MOVED_FIELD_COUNT = sizeof(GZ_MOVED_FIELDS) / sizeof(GZ_MOVED_FIELDS[0]) };
/* Plan 006 master 2.4's families, in this order: plan 004 6.10's classes but Sea. A uint16_t holds them as bits. */
static const char *const GZ_FAMILIES[] = {"ice",    "tundra", "boreal",  "continental", "temperate", "mediterranean",
                                          "steppe", "desert", "savanna", "monsoon",     "rainforest"};
static const char *const GZ_FAMILY_TITLES[] = {"Ice",       "Tundra",        "Boreal",    "Continental",
                                               "Temperate", "Mediterranean", "Steppe",    "Desert",
                                               "Savanna",   "Monsoon",       "Rainforest"};
enum { GZ_FAMILY_COUNT = sizeof(GZ_FAMILIES) / sizeof(GZ_FAMILIES[0]) };
static_assert(GZ_FAMILY_COUNT <= 16);
/* The limits a biome's climate block may give, as bits of biome_def_t's limits. */
enum { GZ_LIMIT_T_WARM = 1, GZ_LIMIT_T_COLD = 2, GZ_LIMIT_RAIN = 4 };
#define GZ_DEGREES_LIMIT 100
#define GZ_RAIN_LIMIT 20000
static const char *const GZ_SURFACE_FIELDS[] = {"top",         "filler",  "filler_depth", "under",
                                                "under_depth", "wet_top", "wet_depth",    "steep",
                                                "high",        "bands",   "patches"};
enum { GZ_SURFACE_FIELD_COUNT = sizeof(GZ_SURFACE_FIELDS) / sizeof(GZ_SURFACE_FIELDS[0]) };
static const char *const GZ_CAVE_FIELDS[] = {"floor", "ceiling", "patches"};
enum { GZ_CAVE_FIELD_COUNT = sizeof(GZ_CAVE_FIELDS) / sizeof(GZ_CAVE_FIELDS[0]) };
static const char *const GZ_BLOCK_FROM_FIELDS[] = {"block", "from"};
static const char *const GZ_BANDS_FIELDS[] = {"from", "depth"};
static const char *const GZ_PATCH_FIELDS[] = {"block", "from", "to"};

/* The five axes of a biome's box, in the order of biome_def_t: the first two under climate, the rest under landform. */
enum { GZ_AXES = 5, GZ_CLIMATE_AXES = 2 };
static const char *const GZ_AXIS_NAMES[GZ_AXES] = {"variety", "wetness", "continentalness", "erosion", "weirdness"};

typedef struct gz_range_t {
    yaml_decimal_t low;  /* 0 */
    yaml_decimal_t high; /* 16 */
} gz_range_t;
static_assert(sizeof(gz_range_t) == 32);

typedef struct gz_patch_t {
    yaml_decimal_t from;        /* 0 */
    yaml_decimal_t to;          /* 16 */
    const char *borrowed_block; /* 32 */
} gz_patch_t;
static_assert(sizeof(gz_patch_t) == 40);

/* One biome, as read. A block is nullptr for none. */
typedef struct gz_biome_t {
    const char *borrowed_name;                 /* 0: its directory's, so a broken file still has one */
    const char *borrowed_path;                 /* 8 */
    const char *borrowed_top;                  /* 16 */
    const char *borrowed_filler;               /* 24 */
    const char *borrowed_under;                /* 32 */
    const char *borrowed_wet_top;              /* 40 */
    const char *borrowed_steep;                /* 48 */
    const char *borrowed_high;                 /* 56 */
    const char *borrowed_floor;                /* 64 */
    const char *borrowed_ceiling;              /* 72 */
    gz_range_t axes[GZ_AXES];                  /* 80 */
    gz_range_t height;                         /* 240: of the ground, or an underground biome's z */
    yaml_decimal_t steep_from;                 /* 272 */
    gz_patch_t patches[GZ_MAX_PATCHES];        /* 288 */
    yaml_decimal_t features[GZ_FEATURE_COUNT]; /* 448 */
    int32_t high_from;                         /* 816 */
    int32_t bands_from;                        /* 820 */
    uint32_t patch_count;                      /* 824 */
    uint8_t where;                             /* 828 */
    uint8_t priority;                          /* 829 */
    uint8_t filler_depth;                      /* 830 */
    uint8_t under_depth;                       /* 831 */
    uint8_t wet_depth;                         /* 832 */
    uint8_t bands_depth;                       /* 833 */
    uint8_t limits;                            /* 834: GZ_LIMIT_* */
    uint8_t _pad0[1];                          /* 835 */
    uint16_t families;                         /* 836: bit f for GZ_FAMILIES[f] */
    uint8_t _pad1[2];                          /* 838 */
    gz_range_t t_warm;                         /* 840: degrees C */
    gz_range_t t_cold;                         /* 872 */
    gz_range_t rain;                           /* 904: mm a year */
} gz_biome_t;
static_assert(sizeof(gz_biome_t) == 936);

typedef struct gz_context_t {
    tp_arena_t *borrowed_arena;  /* 0 */
    const char *borrowed_blocks; /* 8: <config>/blocks */
    const char *borrowed_path;   /* 16: the file being read, for reporting */
    uint32_t problems;           /* 24 */
    uint8_t _pad0[4];            /* 28 */
} gz_context_t;
static_assert(sizeof(gz_context_t) == 32);

[[gnu::format(printf, 3, 4)]]
static void gz_report(gz_context_t *context, const yaml_node_t *node, const char *format, ...) {
    fprintf(stderr, "%s:%u:%u: ", context->borrowed_path, node->line == 0u ? 1u : node->line,
            node->column == 0u ? 1u : node->column);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    context->problems++;
}

static void gz_report_yaml(gz_context_t *context, const yaml_error_t *error) {
    fprintf(stderr, "%s:%u:%u: %s\n", context->borrowed_path, error->line, error->column, error->message);
    context->problems++;
}

/* Reports every key of map that the list does not hold. */
static void gz_reject_unknown(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                              const char *what, const char *const *fields, uint32_t field_count) {
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        bool known = false;
        for (uint32_t i = 0; i < field_count && !known; i++) {
            known = child->key_length == strlen(fields[i]) &&
                    memcmp(child->borrowed_key, fields[i], child->key_length) == 0;
        }
        if (!known) {
            gz_report(context, child, "unknown field '%.*s' in %s", (int32_t)child->key_length,
                      (const char *)child->borrowed_key, what);
        }
    }
}

/* Lower case, digits and underscores, so it can be an enum name in the tables. */
static bool gz_name_is_valid(const char *name) {
    const uint64_t length = strlen(name);
    if (length == 0u || length > GZ_MAX_NAME_BYTES || name[0] < 'a' || name[0] > 'z') {
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
static const char *gz_read_text(gz_context_t *context, const yaml_node_t *node) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        gz_report_yaml(context, &error);
        return nullptr;
    }
    return tp_arena_text(context->borrowed_arena, text, length);
}

/* The block a field names, which must have its file. nullptr for an absent field, or with the problem reported. */
static const char *gz_read_block(gz_context_t *context, const yaml_node_t *node) {
    if (node == nullptr) {
        return nullptr;
    }
    const char *name = gz_read_text(context, node);
    if (name == nullptr) {
        return nullptr;
    }
    if (!gz_name_is_valid(name)) {
        gz_report(context, node, "block '%s' is not a valid name", name);
        return nullptr;
    }
    struct stat info;
    const char *file = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", context->borrowed_blocks, name);
    if (stat(file, &info) != 0 || !S_ISREG(info.st_mode)) {
        gz_report(context, node, "block '%s' does not exist: no %s", name, file);
        return nullptr;
    }
    return name;
}

/* A decimal in millionths, or false if it is finer than that or too large to be any number here. */
static bool gz_decimal_scaled(yaml_decimal_t value, int64_t *out) {
    int64_t mantissa = value.mantissa;
    int32_t exponent = value.exponent;
    while (exponent < 0 && mantissa % 10 == 0 && mantissa != 0) {
        mantissa /= 10;
        exponent++;
    }
    if (mantissa == 0) {
        *out = 0;
        return true;
    }
    if (exponent < -6 || exponent > 6) {
        return false;
    }
    const int64_t limit = INT64_C(1000000000000);
    int64_t scaled = mantissa;
    for (int32_t i = exponent + 6; i > 0; i--) {
        if (scaled > limit || scaled < -limit) {
            return false;
        }
        scaled *= 10;
    }
    *out = scaled;
    return true;
}

/* A decimal from minimum to maximum, in whole units. An absent node leaves out alone. */
static void gz_read_decimal(gz_context_t *context, const yaml_node_t *node, const char *what, int64_t minimum,
                            int64_t maximum, yaml_decimal_t *out) {
    if (node == nullptr) {
        return;
    }
    yaml_decimal_t value;
    int64_t scaled = 0;
    yaml_error_t error = {0};
    if (!yaml_get_decimal(node, &value, &error)) {
        gz_report_yaml(context, &error);
    } else if (!gz_decimal_scaled(value, &scaled) || scaled < minimum * GZ_SCALE || scaled > maximum * GZ_SCALE) {
        gz_report(context, node, "%s is '%.*s', and must be %" PRId64 " to %" PRId64 " with at most 6 decimals", what,
                  (int32_t)node->text_length, (const char *)node->borrowed_text, minimum, maximum);
    } else {
        *out = value;
    }
}

/* A whole number from minimum to maximum. An absent node leaves out alone. */
static void gz_read_whole(gz_context_t *context, const yaml_node_t *node, const char *what, int64_t minimum,
                          int64_t maximum, int64_t *out) {
    if (node == nullptr) {
        return;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        gz_report_yaml(context, &error);
    } else if (value < minimum || value > maximum) {
        gz_report(context, node, "%s is %" PRId64 ", and must be %" PRId64 " to %" PRId64, what, value, minimum,
                  maximum);
    } else {
        *out = value;
    }
}

static uint8_t gz_read_small(gz_context_t *context, const yaml_node_t *node, const char *what, int64_t maximum) {
    int64_t value = 0;
    gz_read_whole(context, node, what, 0, maximum, &value);
    return (uint8_t)value;
}

/* [low, high], with low below high. An absent node leaves out alone. */
static void gz_read_range(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *node,
                          const char *what, int64_t limit, gz_range_t *out) {
    if (node == nullptr) {
        return;
    }
    if (node->kind != YAML_KIND_SEQ || node->child_count != 2u) {
        gz_report(context, node, "%s must be [low, high]", what);
        return;
    }
    gz_range_t range = *out;
    const uint32_t before = context->problems;
    gz_read_decimal(context, yaml_seq_at(document, node, 0u), what, -limit, limit, &range.low);
    gz_read_decimal(context, yaml_seq_at(document, node, 1u), what, -limit, limit, &range.high);
    if (context->problems != before) {
        return;
    }
    int64_t low = 0;
    int64_t high = 0;
    (void)gz_decimal_scaled(range.low, &low);
    (void)gz_decimal_scaled(range.high, &high);
    if (low >= high) {
        gz_report(context, node, "%s must be [low, high], with low below high", what);
        return;
    }
    *out = range;
}

/* Refuses each key of map that the climate block held before plan 006 phase 03, saying where it went. */
static void gz_refuse_moved(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                            const char *block) {
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        for (uint32_t i = 0; i < GZ_MOVED_FIELD_COUNT; i++) {
            if (child->key_length != strlen(GZ_MOVED_FIELDS[i]) ||
                memcmp(child->borrowed_key, GZ_MOVED_FIELDS[i], child->key_length) != 0) {
                continue;
            }
            if (i < 2u) {
                gz_report(context, child,
                          "%s has moved: landform/variety. A biome's warmth and rain are its climate families "
                          "(climate: { families: [...] }), and variety and wetness choose within one (plan 006 "
                          "phase 03)",
                          GZ_MOVED_FIELDS[i]);
            } else if (strcmp(block, "climate") == 0) {
                gz_report(context, child, "%s has moved: landform/variety. It goes under landform: (plan 006 phase 03)",
                          GZ_MOVED_FIELDS[i]);
            }
        }
    }
}

/* The keys of map that are neither its own nor one of the moved ones, reported as unknown. */
static void gz_reject_unknown_but_moved(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                                        const char *what, const char *const *fields, uint32_t field_count) {
    for (const yaml_node_t *child = yaml_first_child(document, map); child != nullptr;
         child = yaml_next_sibling(document, child)) {
        bool known = false;
        for (uint32_t i = 0; i < field_count && !known; i++) {
            known = child->key_length == strlen(fields[i]) &&
                    memcmp(child->borrowed_key, fields[i], child->key_length) == 0;
        }
        for (uint32_t i = 0; i < GZ_MOVED_FIELD_COUNT && !known; i++) {
            known = child->key_length == strlen(GZ_MOVED_FIELDS[i]) &&
                    memcmp(child->borrowed_key, GZ_MOVED_FIELDS[i], child->key_length) == 0;
        }
        if (!known) {
            gz_report(context, child, "unknown field '%.*s' in %s", (int32_t)child->key_length,
                      (const char *)child->borrowed_key, what);
        }
    }
    gz_refuse_moved(context, document, map, what);
}

static void gz_read_landform(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                             gz_biome_t *biome) {
    const yaml_node_t *map = yaml_map_get(document, root, "landform");
    if (map == nullptr) {
        return; /* everywhere its where allows */
    }
    if (map->kind != YAML_KIND_MAP) {
        gz_report(context, map, "landform must be a mapping of axis to [low, high]");
        return;
    }
    gz_reject_unknown_but_moved(context, document, map, "landform", GZ_LANDFORM_FIELDS, GZ_LANDFORM_FIELD_COUNT);
    for (uint32_t axis = GZ_CLIMATE_AXES; axis < GZ_AXES; axis++) {
        gz_read_range(context, document, yaml_map_get(document, map, GZ_AXIS_NAMES[axis]), GZ_AXIS_NAMES[axis], 1,
                      &biome->axes[axis]);
    }
    const yaml_node_t *height = yaml_map_get(document, map, "height");
    const yaml_node_t *z = yaml_map_get(document, map, "z");
    const bool underground = biome->where == GZ_WHERE_UNDERGROUND;
    if (underground && height != nullptr) {
        gz_report(context, height, "an underground biome goes by z, not by height");
    } else if (!underground && z != nullptr) {
        gz_report(context, z, "only an underground biome goes by z");
    } else {
        gz_read_range(context, document, height != nullptr ? height : z, height != nullptr ? "height" : "z",
                      GZ_HEIGHT_LIMIT, &biome->height);
    }
}

/* families: a list of GZ_FAMILIES names, or [all]. */
static void gz_read_families(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *list,
                             gz_biome_t *biome) {
    if (list->kind != YAML_KIND_SEQ || list->child_count == 0u) {
        gz_report(context, list,
                  "families must be a list of at least one of: all, ice, tundra, boreal, continental, "
                  "temperate, mediterranean, steppe, desert, savanna, monsoon, rainforest");
        return;
    }
    for (const yaml_node_t *item = yaml_first_child(document, list); item != nullptr;
         item = yaml_next_sibling(document, item)) {
        const char *text = gz_read_text(context, item);
        if (text == nullptr) {
            continue;
        }
        if (strcmp(text, "all") == 0) {
            biome->families |= (uint16_t)((1u << GZ_FAMILY_COUNT) - 1u);
            continue;
        }
        bool known = false;
        for (uint32_t f = 0; f < GZ_FAMILY_COUNT && !known; f++) {
            if (strcmp(text, GZ_FAMILIES[f]) == 0) {
                if ((biome->families & (1u << f)) != 0u) {
                    gz_report(context, item, "family '%s' is named twice", text);
                }
                biome->families |= (uint16_t)(1u << f);
                known = true;
            }
        }
        if (!known) {
            gz_report(context, item, "unknown family '%s'", text);
        }
    }
}

/* A limit: [low, high] within limit, its bit set in limits. */
static void gz_read_limit(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                          const char *what, int64_t limit, uint8_t bit, gz_range_t *out, gz_biome_t *biome) {
    const yaml_node_t *node = yaml_map_get(document, map, what);
    if (node == nullptr) {
        return;
    }
    const uint32_t before = context->problems;
    gz_read_range(context, document, node, what, limit, out);
    if (context->problems == before) {
        biome->limits |= bit;
    }
}

static void gz_read_climate(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                            gz_biome_t *biome) {
    const yaml_node_t *map = yaml_map_get(document, root, "climate");
    const bool underground = biome->where == GZ_WHERE_UNDERGROUND;
    if (map == nullptr) {
        if (!underground) {
            gz_report(context, root,
                      "missing field 'climate' with its families: every biome but an underground one "
                      "names the climate families it belongs to (plan 006 phase 03)");
        }
        return;
    }
    if (map->kind != YAML_KIND_MAP) {
        gz_report(context, map, "climate must be a mapping");
        return;
    }
    gz_reject_unknown_but_moved(context, document, map, "climate", GZ_CLIMATE_FIELDS, GZ_CLIMATE_FIELD_COUNT);
    for (uint32_t axis = 0; axis < GZ_CLIMATE_AXES; axis++) {
        gz_read_range(context, document, yaml_map_get(document, map, GZ_AXIS_NAMES[axis]), GZ_AXIS_NAMES[axis], 1,
                      &biome->axes[axis]);
    }
    const yaml_node_t *families = yaml_map_get(document, map, "families");
    if (underground) {
        if (families != nullptr) {
            gz_report(context, families,
                      "an underground biome has no families: caves do not follow the surface's "
                      "climate");
        }
        if (yaml_map_get(document, map, "t_warm") != nullptr || yaml_map_get(document, map, "t_cold") != nullptr ||
            yaml_map_get(document, map, "rain_mm") != nullptr) {
            gz_report(context, map, "an underground biome has no t_warm, t_cold or rain_mm");
        }
        return;
    }
    if (families == nullptr) {
        gz_report(context, map,
                  "climate has no families: every biome but an underground one names the climate "
                  "families it belongs to (plan 006 phase 03)");
    } else {
        gz_read_families(context, document, families, biome);
    }
    gz_read_limit(context, document, map, "t_warm", GZ_DEGREES_LIMIT, GZ_LIMIT_T_WARM, &biome->t_warm, biome);
    gz_read_limit(context, document, map, "t_cold", GZ_DEGREES_LIMIT, GZ_LIMIT_T_COLD, &biome->t_cold, biome);
    gz_read_limit(context, document, map, "rain_mm", GZ_RAIN_LIMIT, GZ_LIMIT_RAIN, &biome->rain, biome);
    if ((biome->limits & GZ_LIMIT_RAIN) != 0u) {
        int64_t low = 0;
        int64_t high = 0;
        (void)gz_decimal_scaled(biome->rain.low, &low);
        (void)gz_decimal_scaled(biome->rain.high, &high);
        if (low < 0 || low % GZ_SCALE != 0 || high % GZ_SCALE != 0) {
            gz_report(context, yaml_map_get(document, map, "rain_mm"), "rain_mm must be whole mm from 0 up");
        }
    }
}

/* Defaults: every axis whole, any height. */
static void gz_box_defaults(gz_biome_t *biome) {
    const yaml_decimal_t one = {.mantissa = 1};
    const yaml_decimal_t minus_one = {.mantissa = -1};
    const yaml_decimal_t any_high = {.mantissa = GZ_HEIGHT_LIMIT};
    const yaml_decimal_t any_low = {.mantissa = -GZ_HEIGHT_LIMIT};
    for (uint32_t axis = 0; axis < GZ_AXES; axis++) {
        biome->axes[axis] = (gz_range_t){.low = minus_one, .high = one};
    }
    biome->height = (gz_range_t){.low = any_low, .high = any_high};
}

static void gz_read_patches(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *list,
                            gz_biome_t *biome) {
    if (list == nullptr) {
        return;
    }
    if (list->kind != YAML_KIND_SEQ) {
        gz_report(context, list, "patches must be a list");
        return;
    }
    if (list->child_count > GZ_MAX_PATCHES) {
        gz_report(context, list, "patches has %u rows, and the most is %u", list->child_count, GZ_MAX_PATCHES);
        return;
    }
    for (const yaml_node_t *row = yaml_first_child(document, list); row != nullptr;
         row = yaml_next_sibling(document, row)) {
        if (row->kind != YAML_KIND_MAP) {
            gz_report(context, row, "a patch must be a mapping with block, from and to");
            continue;
        }
        gz_reject_unknown(context, document, row, "a patch", GZ_PATCH_FIELDS, 3u);
        const yaml_node_t *block = yaml_map_get(document, row, "block");
        const yaml_node_t *from = yaml_map_get(document, row, "from");
        const yaml_node_t *to = yaml_map_get(document, row, "to");
        if (block == nullptr || from == nullptr || to == nullptr) {
            gz_report(context, row, "a patch must have block, from and to");
            continue;
        }
        gz_patch_t *patch = &biome->patches[biome->patch_count];
        biome->patch_count++;
        patch->borrowed_block = gz_read_block(context, block);
        gz_read_decimal(context, from, "patch from", -1, 1, &patch->from);
        gz_read_decimal(context, to, "patch to", -1, 1, &patch->to);
        int64_t low = 0;
        int64_t high = 0;
        if (gz_decimal_scaled(patch->from, &low) && gz_decimal_scaled(patch->to, &high) && low >= high) {
            gz_report(context, row, "a patch's from must be below its to");
        }
    }
}

/* The fields of surface that stand alone: each a small map. */
static void gz_read_surface_extras(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                                   gz_biome_t *biome) {
    const yaml_node_t *steep = yaml_map_get(document, map, "steep");
    if (steep != nullptr) {
        gz_reject_unknown(context, document, steep, "steep", GZ_BLOCK_FROM_FIELDS, 2u);
        const yaml_node_t *block = yaml_map_get(document, steep, "block");
        const yaml_node_t *from = yaml_map_get(document, steep, "from");
        if (block == nullptr || from == nullptr) {
            gz_report(context, steep, "steep must have block and from");
        } else {
            biome->borrowed_steep = gz_read_block(context, block);
            gz_read_decimal(context, from, "steep from", 0, GZ_STEEP_LIMIT, &biome->steep_from);
            if (biome->steep_from.mantissa <= 0) {
                gz_report(context, from, "steep from must be above 0");
            }
        }
    }
    const yaml_node_t *high = yaml_map_get(document, map, "high");
    if (high != nullptr) {
        gz_reject_unknown(context, document, high, "high", GZ_BLOCK_FROM_FIELDS, 2u);
        const yaml_node_t *block = yaml_map_get(document, high, "block");
        const yaml_node_t *from = yaml_map_get(document, high, "from");
        if (block == nullptr || from == nullptr) {
            gz_report(context, high, "high must have block and from");
        } else {
            biome->borrowed_high = gz_read_block(context, block);
            int64_t value = 0;
            gz_read_whole(context, from, "high from", -GZ_HEIGHT_LIMIT, GZ_HEIGHT_LIMIT, &value);
            biome->high_from = (int32_t)value;
        }
    }
    const yaml_node_t *bands = yaml_map_get(document, map, "bands");
    if (bands != nullptr) {
        gz_reject_unknown(context, document, bands, "bands", GZ_BANDS_FIELDS, 2u);
        const yaml_node_t *from = yaml_map_get(document, bands, "from");
        const yaml_node_t *depth = yaml_map_get(document, bands, "depth");
        if (from == nullptr || depth == nullptr) {
            gz_report(context, bands, "bands must have from and depth");
        } else {
            int64_t value = 0;
            gz_read_whole(context, from, "bands from", -GZ_HEIGHT_LIMIT, GZ_HEIGHT_LIMIT, &value);
            biome->bands_from = (int32_t)value;
            biome->bands_depth = gz_read_small(context, depth, "bands depth", GZ_LAYERS_MAX);
            if (biome->bands_depth == 0u) {
                gz_report(context, depth, "bands depth must be above 0");
            }
        }
    }
}

static void gz_read_surface(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                            gz_biome_t *biome) {
    if (map->kind != YAML_KIND_MAP) {
        gz_report(context, map, "surface must be a mapping");
        return;
    }
    gz_reject_unknown(context, document, map, "surface", GZ_SURFACE_FIELDS, GZ_SURFACE_FIELD_COUNT);
    const yaml_node_t *top = yaml_map_get(document, map, "top");
    const yaml_node_t *filler = yaml_map_get(document, map, "filler");
    if (top == nullptr || filler == nullptr) {
        gz_report(context, map, "surface must have top and filler");
    }
    biome->borrowed_top = gz_read_block(context, top);
    biome->borrowed_filler = gz_read_block(context, filler);
    biome->borrowed_under = gz_read_block(context, yaml_map_get(document, map, "under"));
    biome->borrowed_wet_top = gz_read_block(context, yaml_map_get(document, map, "wet_top"));
    biome->filler_depth = 3u;
    const yaml_node_t *filler_depth = yaml_map_get(document, map, "filler_depth");
    if (filler_depth != nullptr) {
        biome->filler_depth = gz_read_small(context, filler_depth, "filler_depth", GZ_LAYERS_MAX);
    }
    biome->under_depth =
        gz_read_small(context, yaml_map_get(document, map, "under_depth"), "under_depth", GZ_LAYERS_MAX);
    biome->wet_depth = gz_read_small(context, yaml_map_get(document, map, "wet_depth"), "wet_depth", GZ_LAYERS_MAX);
    if ((biome->wet_depth != 0u) != (yaml_map_get(document, map, "wet_top") != nullptr)) {
        gz_report(context, map, "wet_top and wet_depth go together");
    }
    if ((biome->under_depth != 0u) != (yaml_map_get(document, map, "under") != nullptr)) {
        gz_report(context, map, "under and under_depth go together");
    }
    if (1 + biome->filler_depth + GZ_FILLER_WOBBLE + biome->under_depth > GZ_LAYERS_MAX) {
        gz_report(context, map, "top, filler_depth, up to %d more and under_depth come to over %d layers",
                  GZ_FILLER_WOBBLE, GZ_LAYERS_MAX);
    }
    gz_read_surface_extras(context, document, map, biome);
    gz_read_patches(context, document, yaml_map_get(document, map, "patches"), biome);
}

static void gz_read_cave(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                         gz_biome_t *biome) {
    if (map->kind != YAML_KIND_MAP) {
        gz_report(context, map, "cave must be a mapping");
        return;
    }
    gz_reject_unknown(context, document, map, "cave", GZ_CAVE_FIELDS, GZ_CAVE_FIELD_COUNT);
    const yaml_node_t *floor = yaml_map_get(document, map, "floor");
    const yaml_node_t *ceiling = yaml_map_get(document, map, "ceiling");
    if (floor == nullptr || ceiling == nullptr) {
        gz_report(context, map, "cave must have floor and ceiling");
    }
    biome->borrowed_floor = gz_read_block(context, floor);
    biome->borrowed_ceiling = gz_read_block(context, ceiling);
    gz_read_patches(context, document, yaml_map_get(document, map, "patches"), biome);
}

static void gz_read_features(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *map,
                             gz_biome_t *biome) {
    if (map == nullptr) {
        return;
    }
    if (map->kind != YAML_KIND_MAP) {
        gz_report(context, map, "features must be a mapping of feature to tries per chunk");
        return;
    }
    gz_reject_unknown(context, document, map, "features", GZ_FEATURES, GZ_FEATURE_COUNT);
    for (uint32_t i = 0; i < GZ_FEATURE_COUNT; i++) {
        gz_read_decimal(context, yaml_map_get(document, map, GZ_FEATURES[i]), GZ_FEATURES[i], 0, GZ_MAX_FEATURE_RATE,
                        &biome->features[i]);
    }
}

/* One parsed file into one biome. */
static void gz_read_fields(gz_context_t *context, const yaml_document_t *document, const yaml_node_t *root,
                           gz_biome_t *biome) {
    gz_reject_unknown(context, document, root, "a biome", GZ_BIOME_FIELDS, GZ_BIOME_FIELD_COUNT);

    const yaml_node_t *name = yaml_map_get(document, root, "name");
    if (name == nullptr) {
        gz_report(context, root, "missing field 'name'");
    } else {
        const char *text = gz_read_text(context, name);
        if (text != nullptr && strcmp(text, biome->borrowed_name) != 0) {
            gz_report(context, name, "name '%s' is not its directory's, '%s'", text, biome->borrowed_name);
        }
    }

    const yaml_node_t *where = yaml_map_get(document, root, "where");
    bool where_known = false;
    if (where == nullptr) {
        gz_report(context, root, "missing field 'where'");
    } else {
        const char *text = gz_read_text(context, where);
        for (uint32_t i = 0; text != nullptr && i < GZ_WHERE_COUNT && !where_known; i++) {
            if (strcmp(text, GZ_WHERES[i]) == 0) {
                biome->where = (uint8_t)i;
                where_known = true;
            }
        }
        if (text != nullptr && !where_known) {
            gz_report(context, where, "unknown where '%s'", text);
        }
    }
    if (!where_known) {
        return; /* what else it must have depends on this */
    }

    biome->priority = gz_read_small(context, yaml_map_get(document, root, "priority"), "priority", UINT8_MAX);
    gz_box_defaults(biome);
    gz_read_landform(context, document, root, biome);
    gz_read_climate(context, document, root, biome);

    const yaml_node_t *surface = yaml_map_get(document, root, "surface");
    const yaml_node_t *cave = yaml_map_get(document, root, "cave");
    if (biome->where == GZ_WHERE_UNDERGROUND) {
        if (surface != nullptr) {
            gz_report(context, surface, "an underground biome has cave, not surface");
        }
        if (cave == nullptr) {
            gz_report(context, root, "missing field 'cave'");
        } else {
            gz_read_cave(context, document, cave, biome);
        }
    } else {
        if (cave != nullptr) {
            gz_report(context, cave, "only an underground biome has cave");
        }
        if (surface == nullptr) {
            gz_report(context, root, "missing field 'surface'");
        } else {
            gz_read_surface(context, document, surface, biome);
        }
    }
    gz_read_features(context, document, yaml_map_get(document, root, "features"), biome);
}

/* Reads one biome's file. What it finds wrong is counted in the context. */
static void gz_read_biome(gz_context_t *context, const char *biomes, const char *name, gz_biome_t *biome) {
    memset(biome, 0, sizeof(*biome));
    biome->borrowed_name = name;
    biome->borrowed_path = tp_arena_format(context->borrowed_arena, "%s/%s/index.yaml", biomes, name);
    context->borrowed_path = biome->borrowed_path;
    if (!gz_name_is_valid(name)) {
        fprintf(stderr, "%s:1:1: directory name '%s' is not lower case, digits and underscores\n", biome->borrowed_path,
                name);
        context->problems++;
        return;
    }
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status =
        tp_read_path(context->borrowed_arena, biome->borrowed_path, GZ_MAX_FILE_BYTES, &text, &length);
    if (status != TP_READ_OK) {
        if (status == TP_READ_MISSING) {
            fprintf(stderr, "%s: missing\n", biome->borrowed_path);
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
        gz_report_yaml(context, &error);
        return;
    }
    const yaml_node_t *root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        gz_report(context, root, "a biome's file must be a mapping");
        return;
    }
    gz_read_fields(context, &document, root, biome);
}

static int32_t gz_compare_text(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Every directory under biomes, sorted. */
static bool gz_list_biomes(tp_arena_t *arena, DIR *listing, const char *biomes, const char ***out_names,
                           uint32_t *out_count) {
    const char **names = tp_arena_alloc(arena, (GZ_MAX_BIOMES + 1ull) * sizeof(const char *));
    uint32_t count = 0;
    for (struct dirent *item = readdir(listing); item != nullptr; item = readdir(listing)) {
        struct stat info;
        const char *child = tp_arena_format(arena, "%s/%s", biomes, item->d_name);
        if (item->d_name[0] == '.' || stat(child, &info) != 0 || !S_ISDIR(info.st_mode)) {
            continue;
        }
        if (count == GZ_MAX_BIOMES) {
            return tp_fail("%s: holds over %u biomes", biomes, GZ_MAX_BIOMES);
        }
        names[count] = tp_arena_text(arena, item->d_name, strlen(item->d_name));
        count++;
    }
    qsort(names, count, sizeof(const char *), gz_compare_text);
    *out_names = names;
    *out_count = count;
    return true;
}

/* In upper case, for an enum name. Names were checked to be short. */
static const char *gz_upper(tp_arena_t *arena, const char *name) {
    char *text = tp_arena_text(arena, name, strlen(name));
    for (char *c = text; *c != '\0'; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 'a' + 'A');
        }
    }
    return text;
}

/* A decimal in the form that holds what the file says, with no rounding in this tool. */
static void gz_add_decimal(tp_builder_t *out, yaml_decimal_t value) {
    tp_builder_add(out, "%" PRId64 "e%df32", value.mantissa, value.exponent);
}

static void gz_add_range(tp_builder_t *out, const char *name, gz_range_t range) {
    tp_builder_add(out, ", .%s = {", name);
    gz_add_decimal(out, range.low);
    tp_builder_add(out, ", ");
    gz_add_decimal(out, range.high);
    tp_builder_add(out, "}");
}

static void gz_add_block(tp_arena_t *arena, tp_builder_t *out, const char *field, const char *block) {
    if (block != nullptr) {
        tp_builder_add(out, ", .%s = BLOCK_ID_%s", field, gz_upper(arena, block));
    }
}

static void gz_emit_header(tp_arena_t *arena, tp_builder_t *out, const gz_biome_t *biomes, uint32_t count,
                           uint32_t patch_count, uint32_t underground_count) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/biomes/. Change the yaml and build; never edit this. */\n"
        "#ifndef TABLES_BIOMES_INDEX_H\n#define TABLES_BIOMES_INDEX_H\n\n"
        "#include \"types/index.h\"\n\n#include <stdint.h>\n\n");
    tp_builder_add(out, "/* Sorted by name, so adding a biome moves the ids after it. */\n");
    tp_builder_add(out, "typedef enum biome_id_t : uint8_t {\n");
    for (uint32_t b = 0; b < count; b++) {
        tp_builder_add(out, "    BIOME_ID_%s = %u,\n", gz_upper(arena, biomes[b].borrowed_name), b);
    }
    tp_builder_add(out, "} biome_id_t;\n\n");
    tp_builder_add(out, "#define BIOME_COUNT %uu\n#define BIOME_PATCH_COUNT %uu\n#define BIOME_UNDERGROUND_COUNT %uu\n",
                   count, patch_count, underground_count);
    tp_builder_add(out, "#define BIOME_FEATURE_COUNT %uu\n", (uint32_t)GZ_FEATURE_COUNT);
    tp_builder_add(out, "/* The deepest any surface rule reaches, counting the top as one. */\n");
    tp_builder_add(out, "#define BIOME_LAYERS_MAX %uu\n", (uint32_t)GZ_LAYERS_MAX);
    tp_builder_add(out, "/* The most that a column's own noise adds to filler_depth. */\n");
    tp_builder_add(out, "#define BIOME_FILLER_WOBBLE %uu\n\n", (uint32_t)GZ_FILLER_WOBBLE);

    tp_builder_add(out, "typedef enum biome_where_t : uint8_t {\n");
    for (uint32_t i = 0; i < GZ_WHERE_COUNT; i++) {
        tp_builder_add(out, "    BIOME_WHERE_%s = %u,\n", gz_upper(arena, GZ_WHERES[i]), i);
    }
    tp_builder_add(out, "    BIOME_WHERE_COUNT = %u,\n} biome_where_t;\n\n", (uint32_t)GZ_WHERE_COUNT);
    tp_builder_add(out, "/* Plan 006 master 2.4: the climate families, plan 004 6.10's classes worked out at a "
                        "column's own height. A biome's\n"
                        " * families are bits of biome_def_t's families. */\n");
    tp_builder_add(out, "typedef enum biome_family_t : uint8_t {\n");
    for (uint32_t i = 0; i < GZ_FAMILY_COUNT; i++) {
        tp_builder_add(out, "    BIOME_FAMILY_%s = %u,\n", gz_upper(arena, GZ_FAMILIES[i]), i);
    }
    tp_builder_add(out, "    BIOME_FAMILY_COUNT = %u,\n} biome_family_t;\n\n", (uint32_t)GZ_FAMILY_COUNT);
    tp_builder_add(out,
                   "/* Which of a biome's climate limits it gives: bits of biome_def_t's limits. */\n"
                   "enum : uint8_t {\n    BIOME_LIMIT_T_WARM = %u,\n    BIOME_LIMIT_T_COLD = %u,\n"
                   "    BIOME_LIMIT_RAIN = %u,\n};\n\n",
                   (uint32_t)GZ_LIMIT_T_WARM, (uint32_t)GZ_LIMIT_T_COLD, (uint32_t)GZ_LIMIT_RAIN);
    tp_builder_add(out, "typedef enum biome_feature_t : uint8_t {\n");
    for (uint32_t i = 0; i < GZ_FEATURE_COUNT; i++) {
        tp_builder_add(out, "    BIOME_FEATURE_%s = %u,\n", gz_upper(arena, GZ_FEATURES[i]), i);
    }
    tp_builder_add(out, "} biome_feature_t;\n\n");
    tp_builder_add(
        out,
        "typedef struct biome_range_t {\n"
        "    float32_t low;  /* 0 */\n"
        "    float32_t high; /* 4 */\n"
        "} biome_range_t;\n"
        "static_assert(sizeof(biome_range_t) == 8);\n\n"
        "/* In place of the top, where the column's patch noise is from `from` up to, and not including, `to`. */\n"
        "typedef struct biome_patch_t {\n"
        "    float32_t from;   /* 0 */\n"
        "    float32_t to;     /* 4 */\n"
        "    uint16_t block;   /* 8 */\n"
        "    uint8_t _pad0[2]; /* 10 */\n"
        "} biome_patch_t;\n"
        "static_assert(sizeof(biome_patch_t) == 12);\n\n"
        "/* One row per biome. A block of air is none. See config/biomes/README.md for what each field does. */\n"
        "typedef struct biome_def_t {\n"
        "    const char *name;              /* 0 */\n"
        "    biome_range_t variety;         /* 8: climate's, inside a family */\n"
        "    biome_range_t wetness;         /* 16: climate's, inside a family */\n"
        "    biome_range_t continentalness; /* 24 */\n"
        "    biome_range_t erosion;         /* 32 */\n"
        "    biome_range_t weirdness;       /* 40 */\n"
        "    biome_range_t height;          /* 48: of the ground, or an underground biome's z */\n"
        "    float32_t steep_from;          /* 56: 0 for never */\n"
        "    uint16_t top;                  /* 60 */\n"
        "    uint16_t filler;               /* 62 */\n"
        "    uint16_t under;                /* 64 */\n"
        "    uint16_t wet_top;              /* 66 */\n"
        "    uint16_t steep;                /* 68 */\n"
        "    uint16_t high;                 /* 70 */\n"
        "    uint16_t floor;                /* 72: underground */\n"
        "    uint16_t ceiling;              /* 74: underground */\n"
        "    int16_t high_from;             /* 76 */\n"
        "    int16_t bands_from;            /* 78 */\n"
        "    uint16_t first_patch;          /* 80: into biome_patches */\n"
        "    uint8_t patch_count;           /* 82 */\n"
        "    uint8_t filler_depth;          /* 83 */\n"
        "    uint8_t under_depth;           /* 84 */\n"
        "    uint8_t wet_depth;             /* 85 */\n"
        "    uint8_t bands_depth;           /* 86: 0 for no bands */\n"
        "    uint8_t where;                 /* 87: a biome_where_t */\n"
        "    uint8_t priority;              /* 88 */\n"
        "    uint8_t limits;                /* 89: BIOME_LIMIT_* this biome gives */\n"
        "    uint16_t families;             /* 90: bit f for biome_family_t f; 0 for underground */\n"
        "    int32_t t_warm_low_cc;         /* 92: the warmest month at the column, centi-degrees C */\n"
        "    int32_t t_warm_high_cc;        /* 96 */\n"
        "    int32_t t_cold_low_cc;         /* 100: the coldest month */\n"
        "    int32_t t_cold_high_cc;        /* 104 */\n"
        "    int32_t rain_low_mm;           /* 108: the yearly rain, mm; its middle is plan 006 phase 04's */\n"
        "    int32_t rain_high_mm;          /* 112 */\n"
        "    uint8_t _pad0[4];              /* 116 */\n"
        "} biome_def_t;\n"
        "static_assert(sizeof(biome_def_t) == 120);\n\n"
        "extern const biome_def_t biome_defs[BIOME_COUNT];\n"
        "/* Each family's name, as F3 and the benches print it. */\n"
        "extern const char *const biome_family_names[BIOME_FAMILY_COUNT];\n"
        "extern const biome_patch_t biome_patches[BIOME_PATCH_COUNT];\n"
        "/* How many of each feature a 16 by 16 chunk tries. */\n"
        "extern const float32_t biome_features[BIOME_COUNT][BIOME_FEATURE_COUNT];\n"
        "/* Highest priority first, then by name. The first that holds a voxel has it. */\n"
        "extern const uint8_t biome_underground[BIOME_UNDERGROUND_COUNT];\n\n"
        "#endif\n");
}

/* A decimal in its whole units times scale, which the reading made sure is whole for rain and fits for degrees. */
static int64_t gz_units(yaml_decimal_t value, int64_t per_unit) {
    int64_t scaled = 0;
    (void)gz_decimal_scaled(value, &scaled);
    return scaled / (GZ_SCALE / per_unit);
}

/* The limits a biome gives, in centi-degrees and mm. */
static void gz_add_limits(tp_builder_t *out, const gz_biome_t *biome) {
    if ((biome->limits & GZ_LIMIT_T_WARM) != 0u) {
        tp_builder_add(out, ", .t_warm_low_cc = %" PRId64 ", .t_warm_high_cc = %" PRId64,
                       gz_units(biome->t_warm.low, 100), gz_units(biome->t_warm.high, 100));
    }
    if ((biome->limits & GZ_LIMIT_T_COLD) != 0u) {
        tp_builder_add(out, ", .t_cold_low_cc = %" PRId64 ", .t_cold_high_cc = %" PRId64,
                       gz_units(biome->t_cold.low, 100), gz_units(biome->t_cold.high, 100));
    }
    if ((biome->limits & GZ_LIMIT_RAIN) != 0u) {
        tp_builder_add(out, ", .rain_low_mm = %" PRId64 ", .rain_high_mm = %" PRId64, gz_units(biome->rain.low, 1),
                       gz_units(biome->rain.high, 1));
    }
}

static void gz_emit_source(tp_arena_t *arena, tp_builder_t *out, const gz_biome_t *biomes, uint32_t count,
                           const uint8_t *underground, uint32_t underground_count) {
    tp_builder_add(
        out,
        "/* Generated by compile/src/game_tables from config/biomes/. Change the yaml and build; never edit this. */\n"
        "#include \"tables/biomes/index.h\"\n\n#include \"tables/blocks/index.h\"\n\n"
        "const biome_patch_t biome_patches[BIOME_PATCH_COUNT] = {\n");
    for (uint32_t b = 0; b < count; b++) {
        for (uint32_t p = 0; p < biomes[b].patch_count; p++) {
            const gz_patch_t *patch = &biomes[b].patches[p];
            tp_builder_add(out, "    {.from = ");
            gz_add_decimal(out, patch->from);
            tp_builder_add(out, ", .to = ");
            gz_add_decimal(out, patch->to);
            tp_builder_add(out, ", .block = BLOCK_ID_%s}, /* %s */\n", gz_upper(arena, patch->borrowed_block),
                           biomes[b].borrowed_name);
        }
    }
    tp_builder_add(out, "};\n\nconst char *const biome_family_names[BIOME_FAMILY_COUNT] = {\n");
    for (uint32_t f = 0; f < GZ_FAMILY_COUNT; f++) {
        tp_builder_add(out, "    [BIOME_FAMILY_%s] = \"%s\",\n", gz_upper(arena, GZ_FAMILIES[f]), GZ_FAMILY_TITLES[f]);
    }
    tp_builder_add(out, "};\n\nconst biome_def_t biome_defs[BIOME_COUNT] = {\n");
    uint32_t first_patch = 0;
    for (uint32_t b = 0; b < count; b++) {
        const gz_biome_t *biome = &biomes[b];
        tp_builder_add(out, "    [BIOME_ID_%s] = {.name = \"%s\"", gz_upper(arena, biome->borrowed_name),
                       biome->borrowed_name);
        for (uint32_t axis = 0; axis < GZ_AXES; axis++) {
            gz_add_range(out, GZ_AXIS_NAMES[axis], biome->axes[axis]);
        }
        gz_add_range(out, "height", biome->height);
        tp_builder_add(out, ", .steep_from = ");
        gz_add_decimal(out, biome->steep_from);
        gz_add_block(arena, out, "top", biome->borrowed_top);
        gz_add_block(arena, out, "filler", biome->borrowed_filler);
        gz_add_block(arena, out, "under", biome->borrowed_under);
        gz_add_block(arena, out, "wet_top", biome->borrowed_wet_top);
        gz_add_block(arena, out, "steep", biome->borrowed_steep);
        gz_add_block(arena, out, "high", biome->borrowed_high);
        gz_add_block(arena, out, "floor", biome->borrowed_floor);
        gz_add_block(arena, out, "ceiling", biome->borrowed_ceiling);
        tp_builder_add(out,
                       ", .high_from = %d, .bands_from = %d, .first_patch = %u, .patch_count = %u, .filler_depth = %u, "
                       ".under_depth = %u, .wet_depth = %u, .bands_depth = %u, .where = BIOME_WHERE_%s, "
                       ".priority = %u, .limits = %u, .families = 0x%03xu",
                       biome->high_from, biome->bands_from, first_patch, biome->patch_count, biome->filler_depth,
                       biome->under_depth, biome->wet_depth, biome->bands_depth,
                       gz_upper(arena, GZ_WHERES[biome->where]), biome->priority, biome->limits, biome->families);
        gz_add_limits(out, biome);
        tp_builder_add(out, "},\n");
        first_patch += biome->patch_count;
    }
    tp_builder_add(out, "};\n\nconst float32_t biome_features[BIOME_COUNT][BIOME_FEATURE_COUNT] = {\n");
    for (uint32_t b = 0; b < count; b++) {
        tp_builder_add(out, "    [BIOME_ID_%s] = {", gz_upper(arena, biomes[b].borrowed_name));
        bool first = true;
        for (uint32_t i = 0; i < GZ_FEATURE_COUNT; i++) {
            if (biomes[b].features[i].mantissa != 0) {
                tp_builder_add(out, "%s[BIOME_FEATURE_%s] = ", first ? "" : ", ", gz_upper(arena, GZ_FEATURES[i]));
                gz_add_decimal(out, biomes[b].features[i]);
                first = false;
            }
        }
        tp_builder_add(out, "},\n");
    }
    tp_builder_add(out, "};\n\nconst uint8_t biome_underground[BIOME_UNDERGROUND_COUNT] = {\n");
    for (uint32_t i = 0; i < underground_count; i++) {
        tp_builder_add(out, "    BIOME_ID_%s,\n", gz_upper(arena, biomes[underground[i]].borrowed_name));
    }
    tp_builder_add(out, "};\n");
}

/* On at most GZ_MAX_UNDERGROUND rows that begin sorted by name: highest priority first, otherwise keeping that order. */
static void gz_sort_underground(const gz_biome_t *biomes, uint8_t *rows, uint32_t count) {
    for (uint32_t i = 1u; i < count; i++) {
        const uint8_t row = rows[i];
        uint32_t at = i;
        while (at > 0u && biomes[rows[at - 1u]].priority < biomes[row].priority) {
            rows[at] = rows[at - 1u];
            at--;
        }
        rows[at] = row;
    }
}

bool game_tables_biomes_build(tp_arena_t *arena, const char *config, const char *out) {
    const char *biomes_path = tp_arena_format(arena, "%s/biomes", config);
    DIR *listing = opendir(biomes_path);
    if (listing == nullptr) {
        if (errno == ENOENT) {
            return true; /* a small config, for a test of something else */
        }
        return tp_fail("%s: cannot be listed", biomes_path);
    }
    const char **names = nullptr;
    uint32_t count = 0;
    const bool listed = gz_list_biomes(arena, listing, biomes_path, &names, &count);
    closedir(listing);
    if (!listed) {
        return false;
    }

    gz_context_t context = {
        .borrowed_arena = arena,
        .borrowed_blocks = tp_arena_format(arena, "%s/blocks", config),
    };
    gz_biome_t *biomes = tp_arena_alloc(arena, (count + 1ull) * sizeof(gz_biome_t));
    uint32_t filled[GZ_WHERE_COUNT] = {0};
    uint32_t patch_count = 0;
    uint8_t underground[GZ_MAX_UNDERGROUND] = {0};
    for (uint32_t b = 0; b < count; b++) {
        gz_read_biome(&context, biomes_path, names[b], &biomes[b]);
        patch_count += biomes[b].patch_count;
        if (biomes[b].where == GZ_WHERE_UNDERGROUND && filled[GZ_WHERE_UNDERGROUND] < GZ_MAX_UNDERGROUND) {
            underground[filled[GZ_WHERE_UNDERGROUND]] = (uint8_t)b;
        }
        filled[biomes[b].where]++;
    }
    if (context.problems != 0u) {
        return tp_fail("%s: %u problems; nothing was written", biomes_path, context.problems);
    }
    for (uint32_t where = 0; where < GZ_WHERE_COUNT; where++) {
        if (filled[where] == 0u) {
            return tp_fail("%s: no biome has where: %s, so that kind of place would have none; nothing was written",
                           biomes_path, GZ_WHERES[where]);
        }
    }
    /* Plan 006 phase 03: a family chooses only among its own, so each family needs a biome for every kind of surface place. */
    for (uint32_t family = 0; family < GZ_FAMILY_COUNT; family++) {
        for (uint32_t where = 0; where < GZ_WHERE_UNDERGROUND; where++) {
            bool held = false;
            for (uint32_t b = 0; b < count && !held; b++) {
                held = biomes[b].where == where && (biomes[b].families & (1u << family)) != 0u;
            }
            if (!held) {
                return tp_fail("%s: family %s has no biome with where: %s, so such a place in that climate would have "
                               "none; nothing was written",
                               biomes_path, GZ_FAMILIES[family], GZ_WHERES[where]);
            }
        }
    }
    if (filled[GZ_WHERE_UNDERGROUND] > GZ_MAX_UNDERGROUND) {
        return tp_fail("%s: %u underground biomes, and the most is %u; nothing was written", biomes_path,
                       filled[GZ_WHERE_UNDERGROUND], GZ_MAX_UNDERGROUND);
    }
    /* A table of no rows would not compile. */
    if (patch_count == 0u) {
        return tp_fail("%s: no biome has a patch; nothing was written", biomes_path);
    }
    gz_sort_underground(biomes, underground, filled[GZ_WHERE_UNDERGROUND]);

    tp_builder_t header = tp_builder_begin(arena, GZ_HEADER_BYTES);
    tp_builder_t source = tp_builder_begin(arena, GZ_SOURCE_BYTES);
    gz_emit_header(arena, &header, biomes, count, patch_count, filled[GZ_WHERE_UNDERGROUND]);
    gz_emit_source(arena, &source, biomes, count, underground, filled[GZ_WHERE_UNDERGROUND]);

    const char *directory = tp_arena_format(arena, "%s/biomes", out);
    return tp_make_directories(arena, directory) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.h", directory), header.borrowed_text,
                               header.used, nullptr) &&
           tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.c", directory), source.borrowed_text,
                               source.used, nullptr);
}
