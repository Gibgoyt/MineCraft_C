/*
 * The Owner of the tool: maps its one arena, walks textures/, and runs the three commands. See README.md. Its parts do the
 * work: recipe paints, analyze derives, mcvec writes, materials reads textures/vector.yaml.
 */
#include "texture_vector/index.h"

#include "texture_pack/mctex/index.h"
#include "texture_pack/output/index.h"
#include "texture_pack/support/index.h"
#include "texture_vector/analyze/index.h"
#include "texture_vector/materials/index.h"
#include "texture_vector/mcvec/index.h"
#include "texture_vector/recipe/index.h"
#include "yaml/index.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* Reserved, not used: only touched pages cost anything. */
#define TV_ARENA_BYTES (1ull << 33)
#define TV_YAML_BYTES (1ull << 20)
#define TV_MCTEX_BYTES (MCTEX_MAX_PIXEL_BYTES + sizeof(mctex_header_t))
#define TV_README_BYTES (1ull << 16)

typedef enum tv_command_t : uint8_t {
    TV_COMMAND_RECIPES = 0, /* writes each recipe's pixels and README */
    TV_COMMAND_VECTORS = 1, /* writes each texture's vector data */
    TV_COMMAND_CHECK = 2,   /* writes nothing, and says whether the tree is whole */
} tv_command_t;

typedef struct tv_arguments_t {
    const char *textures;  /* 0 */
    const char *materials; /* 8 */
    const char *out;       /* 16 */
    uint8_t command;       /* 24: a tv_command_t */
    uint8_t _pad0[7];      /* 25 */
} tv_arguments_t;
static_assert(sizeof(tv_arguments_t) == 32);

/* One directory under a group. */
typedef struct tv_texture_t {
    const char *to;    /* 0: group/name, first because the sort compares it */
    const char *group; /* 8 */
    const char *name;  /* 16 */
} tv_texture_t;
static_assert(sizeof(tv_texture_t) == 24);

typedef struct tv_tree_t {
    tv_texture_t *textures; /* 0 */
    uint64_t count;         /* 8 */
} tv_tree_t;
static_assert(sizeof(tv_tree_t) == 16);

/* One texture's index.yaml, parsed. */
typedef struct tv_yaml_t {
    yaml_document_t document; /* 0 */
    const yaml_node_t *root;  /* 16 */
    const yaml_node_t *kind;  /* 24: source.kind */
} tv_yaml_t;
static_assert(sizeof(tv_yaml_t) == 32);

static bool tv_arguments_parse(int32_t argc, char **argv, tv_arguments_t *out) {
    memset(out, 0, sizeof(*out));
    if (argc < 2) {
        return false;
    }
    if (strcmp(argv[1], "recipes") == 0) {
        out->command = TV_COMMAND_RECIPES;
    } else if (strcmp(argv[1], "vectors") == 0) {
        out->command = TV_COMMAND_VECTORS;
    } else if (strcmp(argv[1], "check") == 0) {
        out->command = TV_COMMAND_CHECK;
    } else {
        return false;
    }
    for (int32_t i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return false;
        }
        if (strcmp(argv[i], "--textures") == 0) {
            out->textures = argv[i + 1];
        } else if (strcmp(argv[i], "--materials") == 0) {
            out->materials = argv[i + 1];
        } else if (strcmp(argv[i], "--out") == 0) {
            out->out = argv[i + 1];
        } else {
            return false;
        }
    }
    switch ((tv_command_t)out->command) {
    case TV_COMMAND_RECIPES:
        return out->textures != nullptr && out->materials == nullptr && out->out == nullptr;
    case TV_COMMAND_VECTORS:
        return out->textures != nullptr && out->materials != nullptr && out->out != nullptr;
    case TV_COMMAND_CHECK:
        return out->textures != nullptr && out->materials != nullptr && out->out == nullptr;
    }
    return false;
}

static int32_t tv_compare_text(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

/* Every texture directory, in an order that does not depend on the file system. */
static bool tv_tree_load(tp_arena_t *arena, const char *textures, tv_tree_t *out) {
    uint64_t capacity = 0;
    out->count = 0;
    out->textures = nullptr;
    for (uint32_t pass = 0; pass < 2u; pass++) {
        uint64_t found = 0;
        for (uint32_t g = 0; g < tp_output_group_count(); g++) {
            const char *group = tp_output_group_at(g);
            const char *path = tp_arena_format(arena, "%s/%s", textures, group);
            DIR *listing = opendir(path);
            if (listing == nullptr) {
                return tp_fail("%s: cannot be listed", path);
            }
            const uint64_t first = found;
            struct dirent *item = readdir(listing);
            while (item != nullptr) {
                struct stat info;
                const char *child = tp_arena_format(arena, "%s/%s", path, item->d_name);
                if (item->d_name[0] != '.' && stat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
                    if (pass == 1u) {
                        out->textures[found].group = group;
                        out->textures[found].name = tp_arena_text(arena, item->d_name, strlen(item->d_name));
                        out->textures[found].to = tp_arena_format(arena, "%s/%s", group, item->d_name);
                    }
                    found++;
                }
                item = readdir(listing);
            }
            closedir(listing);
            if (pass == 1u) {
                qsort(out->textures + first, found - first, sizeof(tv_texture_t), tv_compare_text);
            }
        }
        if (pass == 0u) {
            capacity = found;
            out->textures = tp_arena_alloc(arena, (capacity + 1u) * sizeof(tv_texture_t));
        } else {
            out->count = found;
        }
    }
    return true;
}

static bool tv_yaml_load(tp_arena_t *arena, const char *textures, const tv_texture_t *texture, tv_yaml_t *out) {
    const char *path = tp_arena_format(arena, "%s/%s/index.yaml", textures, texture->to);
    uint8_t *text = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(arena, path, TV_YAML_BYTES, &text, &length);
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
    yaml_error_t error = {0};
    if (!yaml_parse(&out->document, text, length, storage, &error)) {
        return tp_fail("%s:%u:%u: %s", path, error.line, error.column, error.message);
    }
    out->root = yaml_root(&out->document);
    out->kind = yaml_path_get(&out->document, out->root, "source.kind");
    if (out->kind == nullptr) {
        return tp_fail("%s: says nothing of source.kind, so nothing says where this texture came from", path);
    }
    return true;
}

static bool tv_yaml_size(const tv_yaml_t *yaml, const char *path, uint32_t *out_width, uint32_t *out_height,
                         uint32_t *out_frames) {
    const char *const keys[] = {"size.width", "size.height", "size.frames"};
    uint32_t *const outputs[] = {out_width, out_height, out_frames};
    for (uint32_t i = 0; i < 3u; i++) {
        int64_t value = 0;
        yaml_error_t error = {0};
        const yaml_node_t *node = yaml_path_require(&yaml->document, yaml->root, keys[i], &error);
        if (node == nullptr || !yaml_get_int64(node, &value, &error)) {
            return tp_fail("%s:%u:%u: %s", path, error.line, error.column, error.message);
        }
        if (value < 1 || value > (int64_t)MCTEX_MAX_DIMENSION) {
            return tp_fail("%s:%u:%u: %s is out of range", path, node->line, node->column, keys[i]);
        }
        *outputs[i] = (uint32_t)value;
    }
    uint64_t bytes = 0;
    if (!mctex_pixel_bytes(*out_width, *out_height, *out_frames, &bytes)) {
        return tp_fail("%s: this size is over the limits of .mctex", path);
    }
    return true;
}

static const char *tv_readme_text(tp_arena_t *arena, const tv_texture_t *texture, const yaml_document_t *document,
                                  const yaml_node_t *source, const char *yaml_path) {
    const yaml_node_t *about = yaml_map_get(document, source, "about");
    const uint8_t *text = nullptr;
    uint32_t length = 0;
    yaml_error_t error = {0};
    if (about == nullptr || !yaml_get_text(about, &text, &length, &error) || length == 0) {
        tp_fail("%s: a recipe needs source.about, one sentence for its README", yaml_path);
        return nullptr;
    }
    const char *sentence = tp_arena_text(arena, text, length);
    if (!tp_text_is_plain(sentence)) {
        tp_fail("%s: source.about must be printable ASCII", yaml_path);
        return nullptr;
    }
    tp_builder_t readme = tp_builder_begin(arena, TV_README_BYTES);
    tp_builder_add(&readme, "# textures/%s\n\n%s\n\n", texture->to, sentence);
    tp_builder_add(&readme, "Our own: painted by the recipe in `index.yaml`, from nothing but that recipe. It uses no "
                            "pack's pixels, and owes no credit.\n\n");
    tp_builder_add(&readme, "Written by `compile/src/texture_vector`, which `textures/README.md` describes.\n");
    return readme.borrowed_text;
}

/* True if the file already holds exactly these bytes. Says what is wrong if not. */
static bool tv_file_matches(tp_arena_t *arena, const char *path, const void *bytes, uint64_t length, uint64_t limit) {
    uint8_t *file = nullptr;
    uint64_t file_length = 0;
    const tp_read_status_t status = tp_read_path(arena, path, limit, &file, &file_length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: missing; run texture_vector recipes", path);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    if (file_length != length || memcmp(file, bytes, length) != 0) {
        return tp_fail("%s: is not what its recipe makes; run texture_vector recipes", path);
    }
    return true;
}

/* Paints one recipe. Writing, or only comparing with what is there. */
static bool tv_recipe_one(tp_arena_t *arena, const char *textures, const tv_texture_t *texture, const tv_yaml_t *yaml,
                          bool writing, uint32_t *out_written) {
    const char *directory = tp_arena_format(arena, "%s/%s", textures, texture->to);
    const char *yaml_path = tp_arena_format(arena, "%s/index.yaml", directory);
    const yaml_node_t *name = yaml_map_get(&yaml->document, yaml->root, "name");
    const yaml_node_t *group = yaml_map_get(&yaml->document, yaml->root, "group");
    if (name == nullptr || group == nullptr || !yaml_text_equals(name, texture->name) ||
        !yaml_text_equals(group, texture->group)) {
        return tp_fail("%s: its name and group must be those of its directory", yaml_path);
    }
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frames = 0;
    if (!tv_yaml_size(yaml, yaml_path, &width, &height, &frames)) {
        return false;
    }
    uint64_t pixel_bytes = 0;
    if (!mctex_pixel_bytes(width, height, frames, &pixel_bytes)) {
        return tp_fail("%s: this size is over the limits of .mctex", yaml_path);
    }
    uint8_t *pixels = tp_arena_alloc(arena, pixel_bytes);
    const yaml_node_t *source = yaml_map_get(&yaml->document, yaml->root, "source");
    yaml_error_t error = {0};
    if (!tv_recipe_run(arena, &yaml->document, source, width, height, frames, pixels, &error)) {
        return tp_fail("%s:%u:%u: %s", yaml_path, error.line, error.column, error.message);
    }
    const uint64_t file_bytes = sizeof(mctex_header_t) + pixel_bytes;
    uint8_t *file = tp_arena_alloc(arena, file_bytes);
    const mctex_status_t encoded = mctex_encode(file, file_bytes, width, height, frames, pixels, pixel_bytes);
    if (encoded != MCTEX_OK) {
        return tp_fail("%s: %s", texture->to, mctex_status_text(encoded));
    }
    const char *readme = tv_readme_text(arena, texture, &yaml->document, source, yaml_path);
    if (readme == nullptr) {
        return false;
    }
    const char *mctex_path = tp_arena_format(arena, "%s/index.mctex", directory);
    const char *readme_path = tp_arena_format(arena, "%s/README.md", directory);
    if (!writing) {
        return tv_file_matches(arena, mctex_path, file, file_bytes, TV_MCTEX_BYTES) &&
               tv_file_matches(arena, readme_path, readme, strlen(readme), TV_README_BYTES);
    }
    bool wrote = false;
    if (!tp_write_if_changed(arena, mctex_path, file, file_bytes, &wrote)) {
        return false;
    }
    *out_written += wrote ? 1u : 0u;
    if (!tp_write_if_changed(arena, readme_path, readme, strlen(readme), &wrote)) {
        return false;
    }
    *out_written += wrote ? 1u : 0u;
    return true;
}

static bool tv_command_recipes(tp_arena_t *arena, const char *textures, const tv_tree_t *tree, bool writing,
                               uint32_t *out_recipes, uint32_t *out_packs) {
    bool good = true;
    uint32_t written = 0;
    uint32_t recipes = 0;
    uint32_t packs = 0;
    for (uint64_t i = 0; i < tree->count; i++) {
        const uint64_t mark = arena->used;
        tv_yaml_t yaml;
        if (!tv_yaml_load(arena, textures, &tree->textures[i], &yaml)) {
            good = false;
        } else if (yaml_text_equals(yaml.kind, "procedural")) {
            recipes++;
            good = tv_recipe_one(arena, textures, &tree->textures[i], &yaml, writing, &written) && good;
        } else if (yaml_text_equals(yaml.kind, "pack")) {
            packs++;
        } else {
            good =
                tp_fail("%s/%s/index.yaml: source.kind is neither pack nor procedural", textures, tree->textures[i].to);
        }
        arena->used = mark;
    }
    *out_recipes = recipes;
    *out_packs = packs;
    if (writing && good) {
        printf("texture_vector: %u recipes painted, %u files changed\n", recipes, written);
    }
    return good;
}

static bool tv_load_pixels(tp_arena_t *arena, const char *textures, const char *to, tv_image_t *image,
                           uint64_t *out_hash) {
    const char *path = tp_arena_format(arena, "%s/%s/index.mctex", textures, to);
    uint8_t *file = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_path(arena, path, TV_MCTEX_BYTES, &file, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: missing", path);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    mctex_view_t view;
    const mctex_status_t parsed = mctex_parse(file, length, &view);
    if (parsed != MCTEX_OK) {
        return tp_fail("%s: %s", path, mctex_status_text(parsed));
    }
    mctex_header_t header;
    memcpy(&header, file, sizeof(header));
    *out_hash = header.pixel_hash;
    *image = (tv_image_t){.borrowed_pixels = view.borrowed_pixels,
                          .pixel_bytes = view.pixel_bytes,
                          .width = view.width,
                          .height = view.height,
                          .frames = view.frame_count};
    return true;
}

static bool tv_vector_one(tp_arena_t *arena, const tv_arguments_t *arguments, const tv_materials_t *materials,
                          const tv_texture_t *texture, uint32_t *out_alpha_classes, uint32_t *out_emissive,
                          uint32_t *out_written) {
    tv_image_t image;
    uint64_t source_hash = 0;
    if (!tv_load_pixels(arena, arguments->textures, texture->to, &image, &source_hash)) {
        return false;
    }
    tv_vector_t vector;
    tv_analyze(arena, &image, tv_materials_find(materials, texture->to), &vector);
    mcvec_header_t header = {.width = image.width,
                             .height = image.height,
                             .frame_count = image.frames,
                             .palette_count = vector.palette.count,
                             .source_hash = source_hash,
                             .alpha_class = vector.alpha_class,
                             .emissive_class = vector.emissive_class,
                             .emissive_level = vector.emissive_level,
                             .palette_exact = vector.palette.exact,
                             .luma_min = vector.luma_min,
                             .luma_max = vector.luma_max,
                             .luma_mean = vector.luma_mean,
                             .visible_pixels = vector.visible_pixels,
                             .emissive_pixels = vector.emissive_pixels};
    memcpy(header.mean_rgba, vector.mean_rgba, 4u);
    uint64_t bytes = 0;
    if (!mcvec_file_bytes(image.width, image.height, image.frames, vector.palette.count, &bytes)) {
        return tp_fail("%s: over the limits of .mcvec", texture->to);
    }
    uint8_t *file = tp_arena_alloc(arena, bytes);
    const mcvec_status_t encoded = mcvec_encode(file, bytes, &header, vector.palette.rgba, vector.indices,
                                                vector.height, vector.normal, vector.emissive);
    if (encoded != MCVEC_OK) {
        return tp_fail("%s: %s", texture->to, mcvec_status_text(encoded));
    }
    const char *directory = tp_arena_format(arena, "%s/%s", arguments->out, texture->to);
    bool wrote = false;
    if (!tp_make_directories(arena, directory) ||
        !tp_write_if_changed(arena, tp_arena_format(arena, "%s/index.mcvec", directory), file, bytes, &wrote)) {
        return false;
    }
    out_alpha_classes[vector.alpha_class]++;
    *out_emissive += vector.emissive_class != MCVEC_EMISSIVE_NONE ? 1u : 0u;
    *out_written += wrote ? 1u : 0u;
    return true;
}

static bool tv_command_vectors(tp_arena_t *arena, const tv_arguments_t *arguments, const tv_tree_t *tree,
                               const tv_materials_t *materials) {
    bool good = true;
    uint32_t alpha_classes[MCVEC_ALPHA_CLASS_COUNT] = {0, 0, 0};
    uint32_t emissive = 0;
    uint32_t written = 0;
    for (uint64_t i = 0; i < tree->count; i++) {
        const uint64_t mark = arena->used;
        good =
            tv_vector_one(arena, arguments, materials, &tree->textures[i], alpha_classes, &emissive, &written) && good;
        arena->used = mark;
    }
    if (good) {
        printf("texture_vector: vector data for %" PRIu64
               " textures, %u opaque, %u cutout, %u translucent, %u emissive; %u "
               "files changed\n",
               tree->count, alpha_classes[MCVEC_ALPHA_OPAQUE], alpha_classes[MCVEC_ALPHA_CUTOUT],
               alpha_classes[MCVEC_ALPHA_TRANSLUCENT], emissive, written);
    }
    return good;
}

static bool tv_tree_has(const tv_tree_t *tree, const char *to) {
    for (uint64_t i = 0; i < tree->count; i++) {
        if (strcmp(tree->textures[i].to, to) == 0) {
            return true;
        }
    }
    return false;
}

/* Recipes still make their pixels, each texture is whole, and everything the game needs and vector.yaml names is there. */
static bool tv_command_check(tp_arena_t *arena, const tv_arguments_t *arguments, const tv_tree_t *tree,
                             const tv_materials_t *materials) {
    uint32_t recipes = 0;
    uint32_t packs = 0;
    bool good = tv_command_recipes(arena, arguments->textures, tree, false, &recipes, &packs);
    for (uint64_t i = 0; i < tree->count; i++) {
        const uint64_t mark = arena->used;
        tv_image_t image;
        uint64_t hash = 0;
        good = tv_load_pixels(arena, arguments->textures, tree->textures[i].to, &image, &hash) && good;
        arena->used = mark;
    }
    for (uint32_t i = 0; i < materials->need_count; i++) {
        if (!tv_tree_has(tree, materials->needs[i])) {
            good = tp_fail("%s: the game needs %s, and neither the pack nor a recipe gives it", materials->path,
                           materials->needs[i]);
        }
    }
    for (uint32_t i = 0; i < materials->row_count; i++) {
        if (!tv_tree_has(tree, materials->rows[i].texture)) {
            good = tp_fail("%s:%u: no such texture: %s", materials->path, materials->rows[i].line,
                           materials->rows[i].texture);
        }
    }
    if (good) {
        printf("texture_vector: checked %s: %u from the pack, %u from recipes, %u needed and all there\n",
               arguments->textures, packs, recipes, materials->need_count);
    }
    return good;
}

int32_t main(int32_t argc, char **argv) {
    tv_arguments_t arguments;
    if (!tv_arguments_parse(argc, argv, &arguments)) {
        fprintf(stderr,
                "usage: texture_vector recipes --textures <directory>\n"
                "       texture_vector vectors --textures <directory> --materials <vector.yaml> --out <directory>\n"
                "       texture_vector check --textures <directory> --materials <vector.yaml>\n");
        return TEXTURE_VECTOR_EXIT_USAGE;
    }
    void *owned_region =
        mmap(nullptr, TV_ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (owned_region == MAP_FAILED) {
        perror("texture_vector: mmap");
        return TEXTURE_VECTOR_EXIT_USAGE;
    }
    tp_arena_t arena = {.borrowed_base = owned_region, .capacity = TV_ARENA_BYTES, .used = 0};

    tv_tree_t tree;
    tv_materials_t materials;
    uint32_t recipes = 0;
    uint32_t packs = 0;
    bool good = tv_tree_load(&arena, arguments.textures, &tree);
    if (good && arguments.command != TV_COMMAND_RECIPES) {
        good = tv_materials_load(&arena, arguments.materials, &materials);
    }
    if (good) {
        switch ((tv_command_t)arguments.command) {
        case TV_COMMAND_RECIPES:
            good = tv_command_recipes(&arena, arguments.textures, &tree, true, &recipes, &packs);
            break;
        case TV_COMMAND_VECTORS:
            good = tv_command_vectors(&arena, &arguments, &tree, &materials);
            break;
        case TV_COMMAND_CHECK:
            good = tv_command_check(&arena, &arguments, &tree, &materials);
            break;
        }
    }
    munmap(owned_region, TV_ARENA_BYTES);
    return good ? TEXTURE_VECTOR_EXIT_OK : TEXTURE_VECTOR_EXIT_REFUSED;
}
