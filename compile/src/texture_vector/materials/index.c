/* See README.md. */
#include "texture_vector/materials/index.h"

#include "texture_pack/output/index.h"
#include "yaml/index.h"

#include <string.h>

#define TV_MATERIALS_FILE_BYTES (1ull << 20)

static const char *tv_materials_text(tp_arena_t *arena, const yaml_node_t *node) {
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (!yaml_get_text(node, &text, &length, &error)) {
        return nullptr;
    }
    return tp_arena_text(arena, text, length);
}

/* group/name, with a group the tool knows and a name in [a-z0-9_]+. */
static bool tv_materials_texture_is_valid(const char *texture) {
    const char *slash = strchr(texture, '/');
    if (slash == nullptr || slash[1] == '\0') {
        return false;
    }
    bool known_group = false;
    const uint64_t group_length = (uint64_t)(slash - texture);
    for (uint32_t g = 0; g < tp_output_group_count(); g++) {
        const char *group = tp_output_group_at(g);
        known_group = known_group || (strlen(group) == group_length && strncmp(group, texture, group_length) == 0);
    }
    if (!known_group) {
        return false;
    }
    for (const char *c = slash + 1; *c != '\0'; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) {
            return false;
        }
    }
    return true;
}

static bool tv_materials_fail(const char *path, const yaml_node_t *node, const char *why) {
    return tp_fail("%s:%u:%u: %s", path, node->line, node->column, why);
}

/* A number from 0 to max, into out. An absent key leaves out alone. */
static bool tv_materials_read_byte(const char *path, const yaml_document_t *document, const yaml_node_t *row,
                                   const char *key, int64_t max, uint8_t *out) {
    const yaml_node_t *node = yaml_map_get(document, row, key);
    if (node == nullptr) {
        return true;
    }
    int64_t value = 0;
    yaml_error_t error = {0};
    if (!yaml_get_int64(node, &value, &error)) {
        return tp_fail("%s:%u:%u: %s", path, error.line, error.column, error.message);
    }
    if (value < 0 || value > max) {
        return tv_materials_fail(path, node, "out of range");
    }
    *out = (uint8_t)value;
    return true;
}

static bool tv_materials_read_row(tp_arena_t *arena, const char *path, const yaml_document_t *document,
                                  const yaml_node_t *row, tv_material_row_t *out) {
    if (row->kind != YAML_KIND_MAP) {
        return tv_materials_fail(path, row, "each material must be a mapping");
    }
    for (const yaml_node_t *key = yaml_first_child(document, row); key != nullptr;
         key = yaml_next_sibling(document, key)) {
        const char *const known[] = {"texture", "emissive", "min_luma", "warm", "alpha"};
        bool is_known = false;
        for (uint32_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
            is_known = is_known || (strlen(known[i]) == key->key_length &&
                                    memcmp(known[i], key->borrowed_key, key->key_length) == 0);
        }
        if (!is_known) {
            return tv_materials_fail(path, key, "unknown key");
        }
    }
    const yaml_node_t *texture = yaml_map_get(document, row, "texture");
    if (texture == nullptr) {
        return tv_materials_fail(path, row, "a material needs a texture");
    }
    out->texture = tv_materials_text(arena, texture);
    if (out->texture == nullptr || !tv_materials_texture_is_valid(out->texture)) {
        return tv_materials_fail(path, texture, "texture must be group/name");
    }
    out->line = row->line;
    out->material = (tv_material_t){.alpha_class = TV_ALPHA_FROM_PIXELS};
    if (!tv_materials_read_byte(path, document, row, "emissive", 15, &out->material.emissive_level) ||
        !tv_materials_read_byte(path, document, row, "min_luma", 255, &out->material.min_luma)) {
        return false;
    }
    const yaml_node_t *warm = yaml_map_get(document, row, "warm");
    if (warm != nullptr) {
        bool value = false;
        yaml_error_t error = {0};
        if (!yaml_get_bool(warm, &value, &error)) {
            return tp_fail("%s:%u:%u: %s", path, error.line, error.column, error.message);
        }
        out->material.warm = value ? 1u : 0u;
    }
    const yaml_node_t *alpha = yaml_map_get(document, row, "alpha");
    if (alpha != nullptr) {
        if (yaml_text_equals(alpha, "translucent")) {
            out->material.alpha_class = MCVEC_ALPHA_TRANSLUCENT;
        } else if (yaml_text_equals(alpha, "cutout")) {
            out->material.alpha_class = MCVEC_ALPHA_CUTOUT;
        } else if (yaml_text_equals(alpha, "opaque")) {
            out->material.alpha_class = MCVEC_ALPHA_OPAQUE;
        } else {
            return tv_materials_fail(path, alpha, "alpha must be opaque, cutout or translucent");
        }
    }
    if (out->material.emissive_level == 0 && (out->material.min_luma != 0 || out->material.warm != 0)) {
        return tv_materials_fail(path, row, "min_luma and warm mean nothing without emissive");
    }
    if (out->material.emissive_level == 0 && alpha == nullptr) {
        return tv_materials_fail(path, row, "a material must say emissive or alpha");
    }
    return true;
}

static bool tv_materials_read_needs(tp_arena_t *arena, const char *path, const yaml_document_t *document,
                                    const yaml_node_t *needs, tv_materials_t *out) {
    if (needs->kind != YAML_KIND_MAP) {
        return tv_materials_fail(path, needs, "needs must be a mapping of group to names");
    }
    uint32_t capacity = 0;
    for (const yaml_node_t *group = yaml_first_child(document, needs); group != nullptr;
         group = yaml_next_sibling(document, group)) {
        capacity += group->child_count;
    }
    out->needs = tp_arena_alloc(arena, (uint64_t)capacity * sizeof(const char *) + 1u);
    for (const yaml_node_t *group = yaml_first_child(document, needs); group != nullptr;
         group = yaml_next_sibling(document, group)) {
        if (group->kind != YAML_KIND_SEQ) {
            return tv_materials_fail(path, group, "each group must be a sequence of names");
        }
        const char *group_name = tp_arena_text(arena, group->borrowed_key, group->key_length);
        for (const yaml_node_t *item = yaml_first_child(document, group); item != nullptr;
             item = yaml_next_sibling(document, item)) {
            const char *name = tv_materials_text(arena, item);
            const char *texture = name != nullptr ? tp_arena_format(arena, "%s/%s", group_name, name) : "";
            if (!tv_materials_texture_is_valid(texture)) {
                return tv_materials_fail(path, item, "not a group/name");
            }
            for (uint32_t i = 0; i < out->need_count; i++) {
                if (strcmp(out->needs[i], texture) == 0) {
                    return tv_materials_fail(path, item, "named twice");
                }
            }
            out->needs[out->need_count++] = texture;
        }
    }
    return true;
}

bool tv_materials_load(tp_arena_t *arena, const char *path, tv_materials_t *out) {
    memset(out, 0, sizeof(*out));
    out->path = path;
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(arena, path, TV_MATERIALS_FILE_BYTES, &text, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: missing", path);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    const yaml_storage_t storage = {.nodes = tp_arena_alloc(arena, sizeof(yaml_node_t) * (length + 1u)),
                                    .strings = tp_arena_alloc(arena, length + 1u),
                                    .node_capacity = (uint32_t)(length + 1u),
                                    .string_capacity = (uint32_t)length};
    yaml_document_t document;
    yaml_error_t error = {0};
    if (!yaml_parse(&document, text, length, storage, &error)) {
        return tp_fail("%s:%u:%u: %s", path, error.line, error.column, error.message);
    }
    const yaml_node_t *root = yaml_root(&document);
    if (root->kind != YAML_KIND_MAP) {
        return tv_materials_fail(path, root, "the top must be a mapping of materials and needs");
    }
    for (const yaml_node_t *key = yaml_first_child(&document, root); key != nullptr;
         key = yaml_next_sibling(&document, key)) {
        const bool is_materials = key->key_length == 9u && memcmp(key->borrowed_key, "materials", 9u) == 0;
        const bool is_needs = key->key_length == 5u && memcmp(key->borrowed_key, "needs", 5u) == 0;
        if (!is_materials && !is_needs) {
            return tv_materials_fail(path, key, "unknown key");
        }
    }
    const yaml_node_t *materials = yaml_map_get(&document, root, "materials");
    const yaml_node_t *needs = yaml_map_get(&document, root, "needs");
    if (materials == nullptr || materials->kind != YAML_KIND_SEQ || needs == nullptr) {
        return tv_materials_fail(path, root, "needs both a materials sequence and needs");
    }
    out->rows = tp_arena_alloc(arena, (uint64_t)materials->child_count * sizeof(tv_material_row_t) + 1u);
    for (const yaml_node_t *row = yaml_first_child(&document, materials); row != nullptr;
         row = yaml_next_sibling(&document, row)) {
        tv_material_row_t *written = &out->rows[out->row_count];
        if (!tv_materials_read_row(arena, path, &document, row, written)) {
            return false;
        }
        for (uint32_t i = 0; i < out->row_count; i++) {
            if (strcmp(out->rows[i].texture, written->texture) == 0) {
                return tv_materials_fail(path, row, "this texture already has a material");
            }
        }
        out->row_count++;
    }
    return tv_materials_read_needs(arena, path, &document, needs, out);
}

tv_material_t tv_materials_find(const tv_materials_t *materials, const char *texture) {
    for (uint32_t i = 0; i < materials->row_count; i++) {
        if (strcmp(materials->rows[i].texture, texture) == 0) {
            return materials->rows[i].material;
        }
    }
    return (tv_material_t){.alpha_class = TV_ALPHA_FROM_PIXELS};
}
