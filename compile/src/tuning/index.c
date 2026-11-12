/*
 * compile/config/<path>/index.yaml -> build/<preset>/generated/tuning/<path>/index.{h,hu}, and tuning/hash/index.h with the hash
 * of them all. Runs at build time from cmake/generated.cmake. See README.md.
 *
 * Every file is read and every key checked before anything is written, so a refused run leaves the last good output in
 * place. An output whose bytes would not change is not touched, so a header keeps its time and Ninja rebuilds nothing that
 * includes it.
 */
#include "tuning/index.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define TUNING_SYSTEM_PATH_BYTES 4096u
#define TUNING_FNV_OFFSET 0xcbf29ce484222325ull
#define TUNING_FNV_PRIME 0x100000001b3ull
/* The largest power of ten a float32_t holds: a value past it would be infinity. */
#define TUNING_F32_MAX_ORDER 38

/* The fields a key holds, in the order the README gives them. */
static const char *const tuning_fields[] = {"value", "type", "unit", "min", "max", "why"};
enum : uint32_t { TUNING_FIELD_COUNT = 6u };

static const char *const tuning_type_names[] = {"u32", "i32", "f32", "bool"};
enum : uint32_t { TUNING_TYPE_COUNT = 4u };

/* bool is the unit of a bool and of nothing else. */
static const char *const tuning_units[] = {"blocks", "pixels", "chunks", "voxels", "levels",  "bricks",
                                           "frames", "count",  "steps",  "ratio",  "degrees", "ms",
                                           "bytes",  "kib",    "mib",    "bool"};
enum : uint32_t { TUNING_UNIT_COUNT = 16u };

/* ---- Small text helpers ------------------------------------------------------------------------------------------ */

static bool tuning_is_lower_snake(const uint8_t *text, uint32_t length) {
    if (length == 0u || text[0] < 'a' || text[0] > 'z') {
        return false;
    }
    for (uint32_t i = 0u; i < length; i++) {
        const uint8_t c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            return false;
        }
    }
    return true;
}

static bool tuning_key_is(const yaml_node_t *node, const char *name) {
    return node->key_length == strlen(name) && memcmp(node->borrowed_key, name, node->key_length) == 0;
}

static bool tuning_text_is(const uint8_t *text, uint32_t length, const char *name) {
    return length == strlen(name) && memcmp(text, name, length) == 0;
}

/* Upper-cases and appends, turning / into _. Returns the new length, or capacity if it does not fit. */
static uint32_t tuning_append_upper(char *out, uint32_t at, uint32_t capacity, const uint8_t *text, uint32_t length) {
    if ((uint64_t)at + length + 1u > capacity) {
        return capacity;
    }
    for (uint32_t i = 0u; i < length; i++) {
        uint8_t c = text[i];
        if (c >= 'a' && c <= 'z') {
            c = (uint8_t)(c - 'a' + 'A');
        } else if (c == '/') {
            c = '_';
        }
        out[at + i] = (char)c;
    }
    out[at + length] = '\0';
    return at + length;
}

/* ---- Numbers ----------------------------------------------------------------------------------------------------- */

static uint32_t tuning_digit_count(uint64_t magnitude) {
    uint32_t count = 1u;
    while (magnitude >= 10u) {
        magnitude /= 10u;
        count++;
    }
    return count;
}

/* A decimal written back positionally, digits.digits with at least one digit each side: 160e-1 is 16.0. */
static bool tuning_decimal_text(const yaml_decimal_t *decimal, char *out, uint32_t capacity) {
    char digits[32];
    const uint64_t magnitude =
        decimal->mantissa < 0 ? (uint64_t)0u - (uint64_t)decimal->mantissa : (uint64_t)decimal->mantissa;
    const int32_t digit_length = snprintf(digits, sizeof(digits), "%" PRIu64, magnitude);
    uint32_t at = 0u;
    if (decimal->mantissa < 0) {
        out[at++] = '-';
    }
    const int64_t exponent = decimal->exponent;
    const int64_t whole = (int64_t)digit_length + exponent; /* digits before the point */
    const int64_t needed = (whole > 0 ? whole : 1) + 1 + (exponent < 0 ? -exponent : 1) + 2;
    if (needed >= (int64_t)capacity) {
        return false;
    }
    if (whole <= 0) {
        out[at++] = '0';
    } else {
        for (int64_t i = 0; i < whole; i++) {
            out[at++] = i < digit_length ? digits[i] : '0';
        }
    }
    out[at++] = '.';
    if (exponent >= 0) {
        out[at++] = '0';
    } else {
        for (int64_t i = whole; i < digit_length; i++) {
            out[at++] = i < 0 ? '0' : digits[i];
        }
    }
    out[at] = '\0';
    return true;
}

/* -1, 0 or 1 as a is below, equal to or above b, exactly. */
static int32_t tuning_decimal_compare(const yaml_decimal_t *a, const yaml_decimal_t *b) {
    const int32_t sign_a = a->mantissa < 0 ? -1 : (a->mantissa > 0 ? 1 : 0);
    const int32_t sign_b = b->mantissa < 0 ? -1 : (b->mantissa > 0 ? 1 : 0);
    if (sign_a != sign_b || sign_a == 0) {
        return sign_a < sign_b ? -1 : (sign_a > sign_b ? 1 : 0);
    }
    /* Same sign, both non-zero: compare magnitudes, then flip for negatives. */
    uint64_t ma = a->mantissa < 0 ? (uint64_t)0u - (uint64_t)a->mantissa : (uint64_t)a->mantissa;
    uint64_t mb = b->mantissa < 0 ? (uint64_t)0u - (uint64_t)b->mantissa : (uint64_t)b->mantissa;
    int64_t ea = a->exponent, eb = b->exponent;
    while (ma % 10u == 0u) {
        ma /= 10u;
        ea++;
    }
    while (mb % 10u == 0u) {
        mb /= 10u;
        eb++;
    }
    const int64_t order_a = (int64_t)tuning_digit_count(ma) + ea;
    const int64_t order_b = (int64_t)tuning_digit_count(mb) + eb;
    int32_t magnitude_order = 0;
    if (order_a != order_b) {
        magnitude_order = order_a < order_b ? -1 : 1;
    } else {
        /* The same order of magnitude: pad the shorter to as many digits as the longer, at most 19, which a uint64_t holds. */
        uint32_t da = tuning_digit_count(ma), db = tuning_digit_count(mb);
        while (da < db) {
            ma *= 10u;
            da++;
        }
        while (db < da) {
            mb *= 10u;
            db++;
        }
        magnitude_order = ma < mb ? -1 : (ma > mb ? 1 : 0);
    }
    return sign_a < 0 ? -magnitude_order : magnitude_order;
}

static int64_t tuning_decimal_order(const yaml_decimal_t *decimal) {
    const uint64_t magnitude =
        decimal->mantissa < 0 ? (uint64_t)0u - (uint64_t)decimal->mantissa : (uint64_t)decimal->mantissa;
    return magnitude == 0u ? 0 : (int64_t)tuning_digit_count(magnitude) + decimal->exponent;
}

/* One number of a key's type, read and written back as the outputs show it. A wrong type is reported at the node. */
static bool tuning_read_number(const yaml_node_t *node, uint8_t type, yaml_decimal_t *out, char *text,
                               yaml_error_t *error) {
    if (type == TUNING_TYPE_F32) {
        if (!yaml_get_decimal(node, out, error)) {
            return false;
        }
        if (!tuning_decimal_text(out, text, TUNING_NUMBER_BYTES) || tuning_decimal_order(out) > TUNING_F32_MAX_ORDER) {
            return yaml_fail_at_node(node, error, "this f32 is too long or too large");
        }
        /* Written back, it must read as written, so the code gets exactly the literal the file says. */
        if (!tuning_text_is(node->borrowed_text, node->text_length, text)) {
            return yaml_fail_at_node(node, error, "an f32 is written as digits, a point and digits, such as %s", text);
        }
        return true;
    }
    int64_t number = 0;
    if (!yaml_get_int64(node, &number, error)) {
        return false;
    }
    const int64_t low = type == TUNING_TYPE_U32 ? 0 : INT32_MIN;
    const int64_t high = type == TUNING_TYPE_U32 ? (int64_t)UINT32_MAX : INT32_MAX;
    if (number < low || number > high) {
        return yaml_fail_at_node(node, error, "%" PRId64 " is not a %s", number, tuning_type_names[type]);
    }
    *out = (yaml_decimal_t){.mantissa = number, .exponent = 0, ._pad0 = {0}};
    snprintf(text, TUNING_NUMBER_BYTES, "%" PRId64, number);
    return true;
}

/* ---- Checking one key -------------------------------------------------------------------------------------------- */

/* The text of a field, or an error at the key when it is missing. */
static const yaml_node_t *tuning_field(const yaml_document_t *document, const yaml_node_t *key, const char *name,
                                       yaml_error_t *error) {
    const yaml_node_t *node = yaml_map_get(document, key, name);
    if (node == nullptr) {
        yaml_fail_at_node(key, error, "'%.*s' has no %s", (int32_t)key->key_length, (const char *)key->borrowed_key,
                          name);
    }
    return node;
}

/* Checks one key of a file and fills in its row. Returns false with the first problem in error. */
static bool tuning_check_key(const yaml_document_t *document, const yaml_node_t *key, tuning_key_t *row,
                             yaml_error_t *error) {
    if (!tuning_is_lower_snake(key->borrowed_key, key->key_length)) {
        return yaml_fail_at_node(key, error, "'%.*s' is not lower snake case, such as ao_turns",
                                 (int32_t)key->key_length, (const char *)key->borrowed_key);
    }
    if (key->kind != YAML_KIND_MAP) {
        return yaml_fail_at_node(key, error, "'%.*s' must be a mapping of value, type, unit, min, max and why",
                                 (int32_t)key->key_length, (const char *)key->borrowed_key);
    }
    for (const yaml_node_t *field = yaml_first_child(document, key); field != nullptr;
         field = yaml_next_sibling(document, field)) {
        bool known = false;
        for (uint32_t i = 0u; i < TUNING_FIELD_COUNT; i++) {
            known = known || tuning_key_is(field, tuning_fields[i]);
        }
        if (!known) {
            return yaml_fail_at_node(field, error,
                                     "unknown field '%.*s': a key holds value, type, unit, min, max and why",
                                     (int32_t)field->key_length, (const char *)field->borrowed_key);
        }
    }

    /* The type first, as it says how to read the rest. */
    const yaml_node_t *type_node = tuning_field(document, key, "type", error);
    if (type_node == nullptr) {
        return false;
    }
    row->type = TUNING_TYPE_COUNT;
    for (uint32_t i = 0u; i < TUNING_TYPE_COUNT; i++) {
        if (type_node->kind == YAML_KIND_SCALAR && type_node->quoted == 0u &&
            yaml_text_equals(type_node, tuning_type_names[i])) {
            row->type = (uint8_t)i;
        }
    }
    if (row->type == TUNING_TYPE_COUNT) {
        return yaml_fail_at_node(type_node, error, "type must be u32, i32, f32 or bool");
    }

    const yaml_node_t *unit_node = tuning_field(document, key, "unit", error);
    if (unit_node == nullptr || !yaml_get_text(unit_node, &row->borrowed_unit, &row->unit_length, error)) {
        return false;
    }
    bool unit_known = false;
    for (uint32_t i = 0u; i < TUNING_UNIT_COUNT; i++) {
        unit_known = unit_known || tuning_text_is(row->borrowed_unit, row->unit_length, tuning_units[i]);
    }
    if (!unit_known) {
        return yaml_fail_at_node(unit_node, error, "unknown unit '%.*s': see compile/config/README.md for the list",
                                 (int32_t)row->unit_length, (const char *)row->borrowed_unit);
    }
    const bool unit_is_bool = tuning_text_is(row->borrowed_unit, row->unit_length, "bool");
    if (unit_is_bool != (row->type == TUNING_TYPE_BOOL)) {
        return yaml_fail_at_node(unit_node, error, "the unit bool goes with the type bool, and only with it");
    }

    const yaml_node_t *why = tuning_field(document, key, "why", error);
    const uint8_t *why_text = nullptr;
    uint32_t why_length = 0u;
    if (why == nullptr || !yaml_get_text(why, &why_text, &why_length, error)) {
        return false;
    }
    if (why_length == 0u) {
        return yaml_fail_at_node(why, error, "why may not be empty: say what the number does");
    }

    const yaml_node_t *value = tuning_field(document, key, "value", error);
    if (value == nullptr) {
        return false;
    }
    const yaml_node_t *minimum = yaml_map_get(document, key, "min");
    const yaml_node_t *maximum = yaml_map_get(document, key, "max");
    if (row->type == TUNING_TYPE_BOOL) {
        if (minimum != nullptr || maximum != nullptr) {
            return yaml_fail_at_node(minimum != nullptr ? minimum : maximum, error, "a bool has no min or max");
        }
        bool truth = false;
        if (!yaml_get_bool(value, &truth, error)) {
            return false;
        }
        snprintf(row->value, sizeof(row->value), "%s", truth ? "true" : "false");
        return true;
    }
    if (tuning_field(document, key, "min", error) == nullptr || tuning_field(document, key, "max", error) == nullptr) {
        return false;
    }
    yaml_decimal_t number, low, high;
    if (!tuning_read_number(minimum, row->type, &low, row->minimum, error) ||
        !tuning_read_number(maximum, row->type, &high, row->maximum, error) ||
        !tuning_read_number(value, row->type, &number, row->value, error)) {
        return false;
    }
    if (tuning_decimal_compare(&low, &high) > 0) {
        return yaml_fail_at_node(minimum, error, "min %s is past max %s", row->minimum, row->maximum);
    }
    if (tuning_decimal_compare(&number, &low) < 0 || tuning_decimal_compare(&number, &high) > 0) {
        return yaml_fail_at_node(value, error, "%s is outside %s..%s", row->value, row->minimum, row->maximum);
    }
    return true;
}

static void tuning_report(tuning_t *tuning, const char *config, const char *path, const yaml_error_t *error) {
    fprintf(stderr, "%s/%s%sindex.yaml:%" PRIu32 ":%" PRIu32 ": %s\n", config, path, path[0] != '\0' ? "/" : "",
            error->line, error->column, error->message);
    tuning->error_count++;
}

/* Checks every key of a parsed file, and reports each bad one. Keys go into the table in source order. */
static void tuning_check_file(tuning_t *tuning, const char *config, uint32_t file_index) {
    tuning_file_t *file = &tuning->borrowed_files[file_index];
    yaml_error_t error;
    memset(&error, 0, sizeof(error));
    file->first_key = tuning->key_count;
    file->key_count = 0u;
    const yaml_node_t *root = yaml_root(&file->document);
    if (root->kind != YAML_KIND_MAP) {
        yaml_fail_at_node(root, &error,
                          "a tuning file is a mapping of keys, each with value, type, unit, min, max and why");
        tuning_report(tuning, config, file->path, &error);
        return;
    }
    for (const yaml_node_t *key = yaml_first_child(&file->document, root); key != nullptr;
         key = yaml_next_sibling(&file->document, key)) {
        memset(&error, 0, sizeof(error));
        if (tuning->key_count >= TUNING_MAX_KEYS) {
            yaml_fail_at_node(key, &error, "more than %u keys in compile/config", TUNING_MAX_KEYS);
            tuning_report(tuning, config, file->path, &error);
            return;
        }
        tuning_key_t *row = &tuning->borrowed_keys[tuning->key_count];
        memset(row, 0, sizeof(*row));
        row->borrowed_node = key;
        row->borrowed_name = key->borrowed_key;
        row->name_length = key->key_length;
        row->file = file_index;
        if (!tuning_check_key(&file->document, key, row, &error)) {
            tuning_report(tuning, config, file->path, &error);
            continue;
        }
        uint32_t at = tuning_append_upper(row->define, 0u, TUNING_DEFINE_BYTES, (const uint8_t *)"MC_TUNE_", 8u);
        at = tuning_append_upper(row->define, at, TUNING_DEFINE_BYTES, (const uint8_t *)file->path,
                                 (uint32_t)strlen(file->path));
        at = tuning_append_upper(row->define, at, TUNING_DEFINE_BYTES, (const uint8_t *)"_", 1u);
        at = tuning_append_upper(row->define, at, TUNING_DEFINE_BYTES, key->borrowed_key, key->key_length);
        if (at >= TUNING_DEFINE_BYTES) {
            yaml_fail_at_node(key, &error, "the define this key makes is too long");
            tuning_report(tuning, config, file->path, &error);
            continue;
        }
        file->has_f32 = file->has_f32 != 0u || row->type == TUNING_TYPE_F32 ? 1u : 0u;
        tuning->key_count++;
        file->key_count++;
    }
}

/* Two keys whose defines are the same, from different files: render/a_b's c and render/a's b_c. Reported at the later. */
static void tuning_check_duplicates(tuning_t *tuning, const char *config) {
    for (uint32_t i = 1u; i < tuning->key_count; i++) {
        for (uint32_t j = 0u; j < i; j++) {
            if (strcmp(tuning->borrowed_keys[i].define, tuning->borrowed_keys[j].define) == 0) {
                yaml_error_t error;
                memset(&error, 0, sizeof(error));
                const tuning_file_t *other = &tuning->borrowed_files[tuning->borrowed_keys[j].file];
                yaml_fail_at_node(tuning->borrowed_keys[i].borrowed_node, &error,
                                  "%s is made by %s/index.yaml line %" PRIu32 " too", tuning->borrowed_keys[i].define,
                                  other->path, tuning->borrowed_keys[j].borrowed_node->line);
                tuning_report(tuning, config, tuning->borrowed_files[tuning->borrowed_keys[i].file].path, &error);
                break;
            }
        }
    }
}

/* ---- Finding and reading the files ------------------------------------------------------------------------------- */

static bool tuning_read_file(tuning_t *tuning, const char *system_path, const char *relative) {
    if (tuning->file_count >= TUNING_MAX_FILES) {
        fprintf(stderr, "tuning: more than %u files under --config\n", TUNING_MAX_FILES);
        return false;
    }
    const uint64_t relative_length = strlen(relative);
    if (relative_length >= TUNING_PATH_BYTES) {
        fprintf(stderr, "tuning: %s: the path is too long\n", system_path);
        return false;
    }
    const int32_t descriptor = open(system_path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        fprintf(stderr, "tuning: cannot open %s: %s\n", system_path, strerror(errno));
        return false;
    }
    struct stat status;
    bool ok = fstat(descriptor, &status) == 0 && S_ISREG(status.st_mode);
    const uint64_t size = ok ? (uint64_t)status.st_size : 0u;
    if (!ok || tuning->text_used + size > TUNING_MAX_INPUT_BYTES) {
        fprintf(stderr, "tuning: %s must be a regular file, and all of compile/config at most %u bytes\n", system_path,
                TUNING_MAX_INPUT_BYTES);
        close(descriptor);
        return false;
    }
    tuning_file_t *file = &tuning->borrowed_files[tuning->file_count];
    memset(file, 0, sizeof(*file));
    memcpy(file->path, relative, relative_length + 1u);
    file->borrowed_text = tuning->borrowed_text + tuning->text_used;
    while (file->text_length < size) {
        const int64_t got = read(descriptor, file->borrowed_text + file->text_length, size - file->text_length);
        if (got < 0) {
            fprintf(stderr, "tuning: cannot read %s: %s\n", system_path, strerror(errno));
            close(descriptor);
            return false;
        }
        if (got == 0) {
            break; /* It shrank while being read. The parser judges what is there. */
        }
        file->text_length += (uint64_t)got;
    }
    close(descriptor);
    /* Text length + 1 nodes and text length string bytes can never run out: see yaml_storage_t. */
    file->storage = (yaml_storage_t){
        .nodes = tuning->borrowed_nodes + tuning->nodes_used,
        .strings = tuning->borrowed_strings + tuning->text_used,
        .node_capacity = (uint32_t)file->text_length + 1u,
        .string_capacity = (uint32_t)file->text_length,
    };
    tuning->nodes_used += file->text_length + 1u;
    tuning->text_used += file->text_length;
    tuning->file_count++;
    return true;
}

/*
 * Finds every index.yaml below config, without recursion: a stack of open directories, one per level. Directories are
 * read in whatever order the file system gives, and the files sorted after, so the output never depends on it.
 */
static bool tuning_find_files(tuning_t *tuning, const char *config) {
    char path[TUNING_SYSTEM_PATH_BYTES];
    const uint64_t config_length = strlen(config);
    if (config_length + 2u >= sizeof(path)) {
        fprintf(stderr, "tuning: --config is too long\n");
        return false;
    }
    memcpy(path, config, config_length + 1u);
    DIR *open_directories[TUNING_MAX_DEPTH + 1u] = {nullptr};
    uint64_t lengths[TUNING_MAX_DEPTH + 1u] = {0u};
    open_directories[0] = opendir(path);
    if (open_directories[0] == nullptr) {
        fprintf(stderr, "tuning: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    lengths[0] = config_length;
    uint32_t depth = 1u;
    bool ok = true;
    while (depth > 0u && ok) {
        const struct dirent *item = readdir(open_directories[depth - 1u]);
        if (item == nullptr) {
            closedir(open_directories[depth - 1u]);
            open_directories[depth - 1u] = nullptr;
            depth--;
            continue;
        }
        if (item->d_name[0] == '.') {
            continue; /* ., .., and anything hidden such as an editor's swap file */
        }
        const uint64_t base = lengths[depth - 1u];
        const uint64_t name_length = strlen(item->d_name);
        if (base + 1u + name_length + 1u > sizeof(path)) {
            fprintf(stderr, "tuning: a path under %s is too long\n", config);
            ok = false;
            break;
        }
        path[base] = '/';
        memcpy(path + base + 1u, item->d_name, name_length + 1u);
        struct stat status;
        if (stat(path, &status) != 0) {
            fprintf(stderr, "tuning: cannot stat %s: %s\n", path, strerror(errno));
            ok = false;
        } else if (S_ISDIR(status.st_mode)) {
            if (depth > TUNING_MAX_DEPTH) {
                fprintf(stderr, "tuning: %s is more than %u directories deep\n", path, TUNING_MAX_DEPTH);
                ok = false;
            } else {
                open_directories[depth] = opendir(path);
                if (open_directories[depth] == nullptr) {
                    fprintf(stderr, "tuning: cannot open %s: %s\n", path, strerror(errno));
                    ok = false;
                } else {
                    lengths[depth] = base + 1u + name_length;
                    depth++;
                }
            }
        } else if (strcmp(item->d_name, "index.yaml") == 0) {
            if (depth == 1u) {
                fprintf(stderr, "%s:1:1: an index.yaml goes in a directory below %s, such as render/shade/\n", path,
                        config);
                tuning->error_count++;
            } else {
                /* The directory below config is the file's path: render/shade. */
                char relative[TUNING_PATH_BYTES];
                const uint64_t relative_length = base - config_length - 1u;
                if (relative_length >= sizeof(relative)) {
                    fprintf(stderr, "tuning: %s: the path is too long\n", path);
                    ok = false;
                } else {
                    memcpy(relative, path + config_length + 1u, relative_length);
                    relative[relative_length] = '\0';
                    ok = tuning_read_file(tuning, path, relative);
                }
            }
        }
    }
    for (uint32_t i = 0u; i < depth; i++) {
        if (open_directories[i] != nullptr) {
            closedir(open_directories[i]);
        }
    }
    return ok;
}

/* Insertion sort by path: a few dozen files at most. */
static void tuning_sort_files(tuning_t *tuning) {
    for (uint32_t i = 1u; i < tuning->file_count; i++) {
        tuning_file_t moving = tuning->borrowed_files[i];
        uint32_t j = i;
        while (j > 0u && strcmp(tuning->borrowed_files[j - 1u].path, moving.path) > 0) {
            tuning->borrowed_files[j] = tuning->borrowed_files[j - 1u];
            j--;
        }
        tuning->borrowed_files[j] = moving;
    }
}

static int32_t tuning_name_compare(const tuning_key_t *a, const tuning_key_t *b) {
    const uint32_t shorter = a->name_length < b->name_length ? a->name_length : b->name_length;
    const int32_t order = memcmp(a->borrowed_name, b->borrowed_name, shorter);
    if (order != 0) {
        return order;
    }
    return a->name_length < b->name_length ? -1 : (a->name_length > b->name_length ? 1 : 0);
}

/* Each file's keys in name order, so the outputs and the hash do not depend on the order they are written in. */
static void tuning_sort_keys(tuning_t *tuning) {
    for (uint32_t f = 0u; f < tuning->file_count; f++) {
        tuning_key_t *keys = tuning->borrowed_keys + tuning->borrowed_files[f].first_key;
        for (uint32_t i = 1u; i < tuning->borrowed_files[f].key_count; i++) {
            tuning_key_t moving = keys[i];
            uint32_t j = i;
            while (j > 0u && tuning_name_compare(&keys[j - 1u], &moving) > 0) {
                keys[j] = keys[j - 1u];
                j--;
            }
            keys[j] = moving;
        }
    }
}

/* ---- Writing ----------------------------------------------------------------------------------------------------- */

static void tuning_output_bytes(tuning_output_t *output, const void *bytes, uint64_t length) {
    if (output->length + length > TUNING_OUTPUT_BYTES) {
        output->overflowed = 1u;
        return;
    }
    memcpy(output->borrowed_bytes + output->length, bytes, length);
    output->length += length;
}

static void tuning_output_text(tuning_output_t *output, const char *text) {
    tuning_output_bytes(output, text, strlen(text));
}

/* A value as C and CUDA both read it: 16u, (-2), F32(3.0), 1. F32 is run/include/types/index.h's, 3.0f32 to GCC and 3.0F to
 * nvcc, so the two headers of a file hold the same defines and either may be included beside the other. */
static void tuning_output_value(tuning_output_t *output, const tuning_key_t *key) {
    const bool negative = key->value[0] == '-';
    if (key->type == TUNING_TYPE_BOOL) {
        tuning_output_text(output, strcmp(key->value, "true") == 0 ? "1" : "0");
        return;
    }
    if (negative) {
        tuning_output_text(output, "(");
    }
    if (key->type == TUNING_TYPE_F32) {
        tuning_output_text(output, "F32(");
    }
    tuning_output_text(output, key->value);
    if (key->type == TUNING_TYPE_U32) {
        tuning_output_text(output, "u");
    } else if (key->type == TUNING_TYPE_F32) {
        tuning_output_text(output, ")");
    }
    if (negative) {
        tuning_output_text(output, ")");
    }
}

/* The header of one file. The .h is for C and for the gpu_bridge headers nvcc reads too, and the .hu for CUDA-only code; they
 * differ only in their guard. */
static void tuning_build_header(const tuning_t *tuning, const tuning_file_t *file, bool cuda, tuning_output_t *output) {
    char guard[TUNING_DEFINE_BYTES];
    uint32_t at = tuning_append_upper(guard, 0u, sizeof(guard), (const uint8_t *)"MC_TUNE_", 8u);
    at = tuning_append_upper(guard, at, sizeof(guard), (const uint8_t *)file->path, (uint32_t)strlen(file->path));
    tuning_append_upper(guard, at, sizeof(guard), (const uint8_t *)(cuda ? "_INDEX_HU" : "_INDEX_H"), cuda ? 9u : 8u);
    tuning_output_text(output, "/* Generated by compile/src/tuning from compile/config/");
    tuning_output_text(output, file->path);
    tuning_output_text(output, "/index.yaml. Do not edit: change the yaml. */\n#ifndef ");
    tuning_output_text(output, guard);
    tuning_output_text(output, "\n#define ");
    tuning_output_text(output, guard);
    tuning_output_text(output, "\n\n");
    if (file->has_f32 != 0u) {
        tuning_output_text(output, "#include \"types/index.h\"\n\n");
    }
    for (uint32_t i = 0u; i < file->key_count; i++) {
        const tuning_key_t *key = &tuning->borrowed_keys[file->first_key + i];
        tuning_output_text(output, "#define ");
        tuning_output_text(output, key->define);
        tuning_output_text(output, " ");
        tuning_output_value(output, key);
        tuning_output_text(output, " /* ");
        tuning_output_bytes(output, key->borrowed_unit, key->unit_length);
        if (key->type != TUNING_TYPE_BOOL) {
            tuning_output_text(output, ", ");
            tuning_output_text(output, key->minimum);
            tuning_output_text(output, "..");
            tuning_output_text(output, key->maximum);
        }
        tuning_output_text(output, " */\n");
    }
    tuning_output_text(output, "\n#endif\n");
}

/* One line of the hash and of MC_TUNE_TEXT: "render/shade ao_turns 4". */
static uint32_t tuning_key_line(const tuning_t *tuning, const tuning_key_t *key, char *out, uint32_t capacity) {
    const int32_t length = snprintf(out, capacity, "%s %.*s %s", tuning->borrowed_files[key->file].path,
                                    (int32_t)key->name_length, (const char *)key->borrowed_name, key->value);
    return length < 0 ? 0u : ((uint32_t)length < capacity ? (uint32_t)length : capacity - 1u);
}

static uint64_t tuning_hash(const tuning_t *tuning) {
    uint64_t hash = TUNING_FNV_OFFSET;
    char line[TUNING_PATH_BYTES + TUNING_DEFINE_BYTES + TUNING_NUMBER_BYTES];
    for (uint32_t i = 0u; i < tuning->key_count; i++) {
        const uint32_t length = tuning_key_line(tuning, &tuning->borrowed_keys[i], line, sizeof(line));
        line[length] = '\n';
        for (uint32_t j = 0u; j <= length; j++) {
            hash ^= (uint8_t)line[j];
            hash *= TUNING_FNV_PRIME;
        }
    }
    return hash;
}

/* generated/tuning/hash/index.h: the hash of every value, and every value as text, for --report and the bench's history. */
static void tuning_build_summary(const tuning_t *tuning, tuning_output_t *output) {
    char text[TUNING_PATH_BYTES + TUNING_DEFINE_BYTES + TUNING_NUMBER_BYTES];
    tuning_output_text(output,
                       "/* Generated by compile/src/tuning from every index.yaml in compile/config/. Do not edit. */\n"
                       "#ifndef MC_TUNE_HASH_INDEX_H\n#define MC_TUNE_HASH_INDEX_H\n\n");
    snprintf(text, sizeof(text), "#define MC_TUNE_HASH 0x%016" PRIx64 "ull\n", tuning_hash(tuning));
    tuning_output_text(output, text);
    snprintf(text, sizeof(text), "#define MC_TUNE_KEY_COUNT %" PRIu32 "u\n", tuning->key_count);
    tuning_output_text(output, text);
    tuning_output_text(output, "/* One line per key, \"path key value\", in the order the hash reads them. */\n"
                               "#define MC_TUNE_TEXT \\\n");
    for (uint32_t i = 0u; i < tuning->key_count; i++) {
        tuning_key_line(tuning, &tuning->borrowed_keys[i], text, sizeof(text));
        tuning_output_text(output, "    \"");
        tuning_output_text(output, text);
        tuning_output_text(output, "\\n\" \\\n");
    }
    tuning_output_text(output, "    \"\"\n\n#endif\n");
}

static bool tuning_write_all(int32_t descriptor, const uint8_t *bytes, uint64_t length) {
    uint64_t done = 0u;
    while (done < length) {
        const int64_t wrote = write(descriptor, bytes + done, length - done);
        if (wrote <= 0) {
            return false;
        }
        done += (uint64_t)wrote;
    }
    return true;
}

/* True if the file exists and already holds exactly these bytes. Any failure to read just means "write it". */
static bool tuning_file_holds(const char *path, const tuning_output_t *output, uint8_t *scratch) {
    const int32_t descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return false;
    }
    uint64_t total = 0u;
    bool ok = true;
    while (total <= TUNING_OUTPUT_BYTES) {
        const int64_t got = read(descriptor, scratch + total, TUNING_OUTPUT_BYTES + 1u - total);
        if (got < 0) {
            ok = false;
        }
        if (got <= 0) {
            break;
        }
        total += (uint64_t)got;
    }
    close(descriptor);
    return ok && total == output->length && memcmp(scratch, output->borrowed_bytes, output->length) == 0;
}

/* Through a temporary and a rename, so a compiler reading it never sees half a header. always is for the stamp, which
 * must be newer than its inputs after every run. */
static bool tuning_write_output(const char *path, const tuning_output_t *output, uint8_t *scratch, bool always) {
    char temporary[TUNING_SYSTEM_PATH_BYTES + 32u];
    if (output->overflowed != 0u) {
        fprintf(stderr, "tuning: %s does not fit in %u bytes\n", path, TUNING_OUTPUT_BYTES);
        return false;
    }
    if (!always && tuning_file_holds(path, output, scratch)) {
        return true;
    }
    snprintf(temporary, sizeof(temporary), "%s.tmp.%" PRId64, path, (int64_t)getpid());
    const int32_t descriptor = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        fprintf(stderr, "tuning: cannot create %s: %s\n", temporary, strerror(errno));
        return false;
    }
    const bool wrote = tuning_write_all(descriptor, output->borrowed_bytes, output->length);
    const bool closed = close(descriptor) == 0;
    if (!wrote || !closed || rename(temporary, path) != 0) {
        fprintf(stderr, "tuning: cannot write %s: %s\n", path, strerror(errno));
        unlink(temporary);
        return false;
    }
    return true;
}

/* Makes each missing directory of path, which must hold room for one more byte. */
static bool tuning_make_directories(char *path) {
    const uint64_t length = strlen(path);
    for (uint64_t i = 1u; i <= length; i++) {
        if (path[i] == '/' || path[i] == '\0') {
            const char kept = path[i];
            path[i] = '\0';
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "tuning: cannot create %s: %s\n", path, strerror(errno));
                path[i] = kept;
                return false;
            }
            path[i] = kept;
        }
    }
    return true;
}

static tuning_output_t tuning_fresh_output(const tuning_t *tuning) {
    return (tuning_output_t){.borrowed_bytes = tuning->borrowed_output, .length = 0u, .overflowed = 0u, ._pad0 = {0}};
}

static bool tuning_write_everything(const tuning_t *tuning, const char *out, const char *stamp) {
    char path[TUNING_SYSTEM_PATH_BYTES];
    for (uint32_t f = 0u; f < tuning->file_count; f++) {
        const tuning_file_t *file = &tuning->borrowed_files[f];
        const int32_t length = snprintf(path, sizeof(path) - 16u, "%s/%s", out, file->path);
        if (length < 0 || (uint64_t)length >= sizeof(path) - 16u) {
            fprintf(stderr, "tuning: --out is too long\n");
            return false;
        }
        if (!tuning_make_directories(path)) {
            return false;
        }
        for (uint32_t pass = 0u; pass < 2u; pass++) {
            const bool cuda = pass == 1u;
            snprintf(path + length, sizeof(path) - (uint64_t)length, "/%s", cuda ? "index.hu" : "index.h");
            tuning_output_t output = tuning_fresh_output(tuning);
            tuning_build_header(tuning, file, cuda, &output);
            if (!tuning_write_output(path, &output, tuning->borrowed_scratch, false)) {
                return false;
            }
        }
    }
    const int32_t length = snprintf(path, sizeof(path) - 16u, "%s/%s", out, TUNING_SUMMARY_DIRECTORY);
    if (length < 0 || (uint64_t)length >= sizeof(path) - 16u) {
        fprintf(stderr, "tuning: --out is too long\n");
        return false;
    }
    if (!tuning_make_directories(path)) {
        return false;
    }
    snprintf(path + length, sizeof(path) - (uint64_t)length, "/index.h");
    tuning_output_t output = tuning_fresh_output(tuning);
    tuning_build_summary(tuning, &output);
    if (!tuning_write_output(path, &output, tuning->borrowed_scratch, false)) {
        return false;
    }
    if (stamp != nullptr) {
        char text[64];
        snprintf(text, sizeof(text), "%016" PRIx64 "\n", tuning_hash(tuning));
        output = tuning_fresh_output(tuning);
        tuning_output_text(&output, text);
        return tuning_write_output(stamp, &output, tuning->borrowed_scratch, true);
    }
    return true;
}

/* ---- The region -------------------------------------------------------------------------------------------------- */

static bool tuning_init(tuning_t *tuning) {
    memset(tuning, 0, sizeof(*tuning));
    const uint64_t files_bytes = (uint64_t)TUNING_MAX_FILES * sizeof(tuning_file_t);
    const uint64_t keys_bytes = (uint64_t)TUNING_MAX_KEYS * sizeof(tuning_key_t);
    const uint64_t node_bytes = ((uint64_t)TUNING_MAX_INPUT_BYTES + TUNING_MAX_FILES) * sizeof(yaml_node_t);
    const uint64_t text_bytes = TUNING_MAX_INPUT_BYTES;
    const uint64_t region_bytes =
        node_bytes + files_bytes + keys_bytes + 2u * text_bytes + 2u * (uint64_t)TUNING_OUTPUT_BYTES + 64u;
    /* Reserved, not committed: only what the files need is ever touched. */
    void *region =
        mmap(nullptr, region_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (region == MAP_FAILED) {
        fprintf(stderr, "tuning: cannot map %" PRIu64 " bytes: %s\n", region_bytes, strerror(errno));
        return false;
    }
    /* The structs first, as they alone need alignment, and a mapping starts page-aligned. */
    tuning->owned_region = region;
    tuning->region_bytes = region_bytes;
    tuning->borrowed_nodes = region;
    tuning->borrowed_files = (tuning_file_t *)(tuning->owned_region + node_bytes);
    tuning->borrowed_keys = (tuning_key_t *)(tuning->owned_region + node_bytes + files_bytes);
    tuning->borrowed_text = tuning->owned_region + node_bytes + files_bytes + keys_bytes;
    tuning->borrowed_strings = tuning->borrowed_text + text_bytes;
    tuning->borrowed_output = tuning->borrowed_strings + text_bytes;
    tuning->borrowed_scratch = tuning->borrowed_output + TUNING_OUTPUT_BYTES;
    return true;
}

static void tuning_deinit(tuning_t *tuning) {
    if (tuning->owned_region != nullptr) {
        munmap(tuning->owned_region, tuning->region_bytes);
        tuning->owned_region = nullptr;
    }
}

/* Reads and checks everything, and only then writes. Returns the exit status. */
static int32_t tuning_run(tuning_t *tuning, const char *config, const char *out, const char *stamp) {
    if (!tuning_find_files(tuning, config)) {
        return TUNING_EXIT_CANNOT_RUN;
    }
    tuning_sort_files(tuning);
    for (uint32_t f = 0u; f < tuning->file_count; f++) {
        tuning_file_t *file = &tuning->borrowed_files[f];
        yaml_error_t error;
        /* Each directory name becomes part of a define, so it follows the rule a key does. */
        const char *segment = file->path;
        bool path_ok = true;
        while (path_ok && *segment != '\0') {
            const char *end = strchr(segment, '/');
            const uint32_t length = (uint32_t)(end != nullptr ? (uint64_t)(end - segment) : strlen(segment));
            path_ok = tuning_is_lower_snake((const uint8_t *)segment, length);
            segment += length + (end != nullptr ? 1u : 0u);
        }
        if (!path_ok) {
            fprintf(stderr,
                    "%s/%s/index.yaml:1:1: each directory of the path is lower snake case, such as render/shade\n",
                    config, file->path);
            tuning->error_count++;
            continue;
        }
        const uint64_t reserved_length = strlen(TUNING_SUMMARY_DIRECTORY);
        if (strncmp(file->path, TUNING_SUMMARY_DIRECTORY, reserved_length) == 0 &&
            (file->path[reserved_length] == '\0' || file->path[reserved_length] == '/')) {
            fprintf(stderr, "%s/%s/index.yaml:1:1: %s/ is where the hash of them all is written; use another name\n",
                    config, file->path, TUNING_SUMMARY_DIRECTORY);
            tuning->error_count++;
            continue;
        }
        if (!yaml_parse(&file->document, file->borrowed_text, file->text_length, file->storage, &error)) {
            tuning_report(tuning, config, file->path, &error);
            continue;
        }
        file->parsed = 1u;
        tuning_check_file(tuning, config, f);
    }
    tuning_check_duplicates(tuning, config);
    if (tuning->error_count != 0u) {
        fprintf(stderr, "tuning: %" PRIu32 " problem%s; nothing was written\n", tuning->error_count,
                tuning->error_count == 1u ? "" : "s");
        return TUNING_EXIT_REFUSED;
    }
    tuning_sort_keys(tuning);
    return tuning_write_everything(tuning, out, stamp) ? TUNING_EXIT_OK : TUNING_EXIT_CANNOT_RUN;
}

int32_t main(int32_t argc, char **argv) {
    const char *config = nullptr;
    const char *out = nullptr;
    const char *stamp = nullptr;
    bool usage_ok = true;
    for (int32_t i = 1; i < argc; i++) {
        const bool has_value = i + 1 < argc;
        if (strcmp(argv[i], "--config") == 0 && has_value) {
            config = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && has_value) {
            out = argv[++i];
        } else if (strcmp(argv[i], "--stamp") == 0 && has_value) {
            stamp = argv[++i];
        } else {
            usage_ok = false;
        }
    }
    if (!usage_ok || config == nullptr || out == nullptr) {
        fprintf(stderr, "usage: tuning --config <compile/config> --out <generated>/tuning [--stamp <file>]\n");
        return TUNING_EXIT_CANNOT_RUN;
    }
    tuning_t tuning;
    int32_t status = TUNING_EXIT_CANNOT_RUN;
    if (tuning_init(&tuning)) {
        status = tuning_run(&tuning, config, out, stamp);
    }
    tuning_deinit(&tuning);
    return status;
}
