/* ctest fuzz_yaml. See README.md. */
#include "fuzz/support/index.h"

#include "yaml/index.h"

#include <inttypes.h>
#include <string.h>

enum : uint64_t {
    FY_INPUT_BYTES = 1ull << 17,
    FY_NODE_CAPACITY = FY_INPUT_BYTES + 1u,
};

typedef struct fy_context_t {
    fuzz_buffer_t text;    /* 0 */
    fuzz_buffer_t nodes;   /* 24 */
    fuzz_buffer_t strings; /* 48 */
} fy_context_t;
static_assert(sizeof(fy_context_t) == 72);

#define FY_TOKEN(text) {(const uint8_t *)(text), sizeof(text) - 1u}
static const fuzz_token_t fy_tokens[] = {
    FY_TOKEN(": "),
    FY_TOKEN("- "),
    FY_TOKEN("\n"),
    FY_TOKEN("\n  "),
    FY_TOKEN("\n    "),
    FY_TOKEN("#"),
    FY_TOKEN("{ "),
    FY_TOKEN(" }"),
    FY_TOKEN("[ "),
    FY_TOKEN(" ]"),
    FY_TOKEN(", "),
    FY_TOKEN("\""),
    FY_TOKEN("'"),
    FY_TOKEN("\\n"),
    FY_TOKEN("\\x41"),
    FY_TOKEN("\\u0041"),
    FY_TOKEN("~"),
    FY_TOKEN("null"),
    FY_TOKEN("true"),
    FY_TOKEN("false"),
    FY_TOKEN("0x7F"),
    FY_TOKEN("1e400"),
    FY_TOKEN("-9223372036854775808"),
    FY_TOKEN("9223372036854775808"),
    FY_TOKEN("0.5"),
    FY_TOKEN(".inf"),
    FY_TOKEN(".nan"),
    FY_TOKEN("---"),
    FY_TOKEN("..."),
    FY_TOKEN("&a "),
    FY_TOKEN("*a"),
    FY_TOKEN("!!str "),
    FY_TOKEN("|\n"),
    FY_TOKEN(">\n"),
    FY_TOKEN("\t"),
    FY_TOKEN("\r\n"),
    FY_TOKEN("? "),
};
enum { FY_TOKEN_COUNT = sizeof(fy_tokens) / sizeof(fy_tokens[0]) };

/* Each of these reaches a different part of the grammar, before any file is read. */
static const char *const fy_seed_texts[] = {
    "",
    "a: 1\n",
    "# comment only\n",
    "a:\n  b:\n    c: [1, 2, { d: e }]\n  f: ~\n",
    "- a\n- b: 1\n  c: 2\n- - x\n  - y\n",
    "q: \"esc \\n \\x41 \\u00e9 \\\\ \\\"\"\ns: 'it''s'\n",
    "n: -12\nh: 0x7F\nd: 1.5e-3\nb: true\nz: null\n",
    "{ a: 1, b: [x, y, { c: d }], e: \"f\" }\n",
    "[[[[[[[[1]]]]]]]]\n",
    "k: { }\nl: [ ]\n",
    "- { item: dirt, count: 1 }\n- { item: stone, count: 64 }\n",
};
enum { FY_SEED_COUNT = sizeof(fy_seed_texts) / sizeof(fy_seed_texts[0]) };

static const char *const fy_count_names[] = {"accepted", "refused"};

static void fy_seeds(test_context_t *t, void *context, fuzz_corpus_t *corpus) {
    (void)context;
    for (uint32_t i = 0u; i < FY_SEED_COUNT; i++) {
        fuzz_seed_add(corpus, fy_seed_texts[i], strlen(fy_seed_texts[i]));
    }
    /* Every file the build really reads. ctest runs this from the top of the tree. */
    (void)fuzz_seed_files(t, corpus, "config", ".yaml", nullptr, 0u);
    (void)fuzz_seed_files(t, corpus, "textures", ".yaml", nullptr, 0u);
}

/* What every message must be, parse or read. */
static void fy_error_check(const yaml_error_t *error) {
    if (memchr(error->message, '\0', sizeof(error->message)) == nullptr) {
        fuzz_fail("an error message has no end");
    }
}

/* Whether text is inside the source or the strings, as a document says all of it is. */
static bool fy_slice_inside(const uint8_t *slice, uint32_t length, const uint8_t *text, uint64_t text_length,
                            const uint8_t *strings, uint64_t strings_length) {
    if (slice == nullptr) {
        return length == 0u;
    }
    const uintptr_t at = (uintptr_t)slice;
    const bool in_text = at >= (uintptr_t)text && at + length <= (uintptr_t)text + text_length;
    const bool in_strings = at >= (uintptr_t)strings && at + length <= (uintptr_t)strings + strings_length;
    return in_text || in_strings;
}

/* Every node as a consumer would see it, in every typed read. */
static void fy_document_walk(const yaml_document_t *document, const uint8_t *text, uint64_t length,
                             const uint8_t *strings, uint64_t strings_length) {
    if (document->node_count > length + 1u) {
        fuzz_fail("%" PRIu32 " nodes out of %" PRIu64 " bytes", document->node_count, length);
    }
    uint64_t sum = 0u;
    for (uint32_t i = 0u; i < document->node_count; i++) {
        const yaml_node_t *const node = &document->borrowed_nodes[i];
        if (node->kind > YAML_KIND_SEQ) {
            fuzz_fail("node %" PRIu32 " has kind %" PRIu32, i, (uint32_t)node->kind);
        }
        if (!fy_slice_inside(node->borrowed_key, node->key_length, text, length, strings, strings_length) ||
            !fy_slice_inside(node->borrowed_text, node->text_length, text, length, strings, strings_length)) {
            fuzz_fail("node %" PRIu32 " borrows from outside what it was given", i);
        }
        for (uint32_t k = 0u; k < node->key_length; k++) {
            sum += node->borrowed_key[k];
        }
        for (uint32_t k = 0u; k < node->text_length; k++) {
            sum += node->borrowed_text[k];
        }
        if ((node->first_child != YAML_NO_NODE && node->first_child >= document->node_count) ||
            (node->next_sibling != YAML_NO_NODE && node->next_sibling >= document->node_count)) {
            fuzz_fail("node %" PRIu32 " links past the end", i);
        }
        /* Children come after their parent, so any walk ends. */
        if ((node->first_child != YAML_NO_NODE && node->first_child <= i) ||
            (node->next_sibling != YAML_NO_NODE && node->next_sibling <= i)) {
            fuzz_fail("node %" PRIu32 " links backwards", i);
        }
        uint32_t children = 0u;
        for (const yaml_node_t *child = yaml_first_child(document, node); child != nullptr;
             child = yaml_next_sibling(document, child)) {
            children++;
            if (children > document->node_count) {
                fuzz_fail("node %" PRIu32 " has a loop of children", i);
            }
        }
        if (children != node->child_count) {
            fuzz_fail("node %" PRIu32 " says %" PRIu32 " children and has %" PRIu32, i, node->child_count, children);
        }

        yaml_error_t error;
        memset(&error, 0, sizeof(error));
        bool truth = false;
        int64_t whole = 0;
        yaml_decimal_t decimal = {0};
        const uint8_t *read_text = nullptr;
        uint32_t read_length = 0u;
        (void)yaml_get_bool(node, &truth, &error);
        fy_error_check(&error);
        memset(&error, 0, sizeof(error));
        (void)yaml_get_int64(node, &whole, &error);
        fy_error_check(&error);
        memset(&error, 0, sizeof(error));
        (void)yaml_get_decimal(node, &decimal, &error);
        fy_error_check(&error);
        memset(&error, 0, sizeof(error));
        (void)yaml_get_text(node, &read_text, &read_length, &error);
        fy_error_check(&error);
        (void)yaml_seq_at(document, node, children / 2u);
        (void)yaml_map_get(document, node, "item");
        (void)yaml_path_get(document, node, "a.b.c");
        (void)yaml_text_equals(node, "true");
        memset(&error, 0, sizeof(error));
        (void)yaml_path_require(document, node, "cpu.simd.avx2", &error);
        fy_error_check(&error);
        memset(&error, 0, sizeof(error));
        (void)yaml_fail_at_node(node, &error, "unknown key %s", "x");
        fy_error_check(&error);
    }
    /* So that none of those reads can be taken out as having no effect. */
    if (sum == UINT64_MAX) {
        fuzz_fail("impossible");
    }
}

static void fy_execute(void *context, const uint8_t *input, uint64_t length) {
    fy_context_t *const fy = context;
    if (length > FY_INPUT_BYTES) {
        return;
    }
    const uint8_t *const text = fuzz_buffer_hold(&fy->text, input, length);
    /* Exactly what the header says can never run out. */
    yaml_node_t *const nodes = (yaml_node_t *)fuzz_buffer_fit(&fy->nodes, (length + 1u) * sizeof(yaml_node_t));
    uint8_t *const strings = fuzz_buffer_fit(&fy->strings, length);
    const yaml_storage_t storage = {.nodes = nodes,
                                    .strings = strings,
                                    .node_capacity = (uint32_t)length + 1u,
                                    .string_capacity = (uint32_t)length};
    yaml_document_t document;
    yaml_error_t error;
    const bool parsed = yaml_parse(&document, text, length, storage, &error);
    fy_error_check(&error);
    fuzz_count(parsed ? 0u : 1u);
    if (!parsed) {
        if (error.line == 0u) {
            fuzz_fail("a refused document names no line");
        }
        if (yaml_root(&document) != nullptr) {
            fuzz_fail("a refused document still has a root");
        }
        return;
    }
    if (error.line != 0u) {
        fuzz_fail("an accepted document has an error, at line %" PRIu32, error.line);
    }
    const uint32_t count = document.node_count;
    fy_document_walk(&document, text, length, strings, length);

    /* With much less room it may fail, but only by saying so, and it stays inside what it has. */
    const uint32_t less = count / 2u;
    yaml_node_t *const fewer = (yaml_node_t *)fuzz_buffer_fit(&fy->nodes, (uint64_t)less * sizeof(yaml_node_t));
    uint8_t *const shorter = fuzz_buffer_fit(&fy->strings, length / 2u);
    const yaml_storage_t small = {
        .nodes = fewer, .strings = shorter, .node_capacity = less, .string_capacity = (uint32_t)(length / 2u)};
    yaml_document_t small_document;
    yaml_error_t small_error;
    const bool small_parsed = yaml_parse(&small_document, text, length, small, &small_error);
    fy_error_check(&small_error);
    if (!small_parsed && small_error.line == 0u) {
        fuzz_fail("a document with no room names no line");
    }
}

TEST(yaml) {
    fy_context_t *const fy = test_alloc(t, sizeof(*fy));
    fy->text = fuzz_buffer_make(t, FY_INPUT_BYTES);
    fy->nodes = fuzz_buffer_make(t, FY_NODE_CAPACITY * sizeof(yaml_node_t));
    fy->strings = fuzz_buffer_make(t, FY_INPUT_BYTES);
    const fuzz_target_t target = {.name = "yaml",
                                  .borrowed_context = fy,
                                  .borrowed_tokens = fy_tokens,
                                  .input_capacity = FY_INPUT_BYTES,
                                  .seeds = fy_seeds,
                                  .fix = nullptr,
                                  .execute = fy_execute,
                                  .borrowed_count_names = fy_count_names,
                                  .token_count = FY_TOKEN_COUNT,
                                  .count_name_count = 2u,
                                  .kind_count = 0u,
                                  ._pad0 = {0}};
    fuzz_run(t, &target);
}

TEST_MAIN()
