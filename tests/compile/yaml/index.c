/*
 * ctest compile_yaml: valid documents read back correctly, bad ones fail on the right line, and damaged input
 * can only ever give a valid tree or a positioned error.
 */
#include "harness/index.h"
#include "yaml/index.h"

#include <stdio.h>
#include <string.h>

#define TEST_NODE_CAPACITY 4096u
#define TEST_STRING_CAPACITY 16384u

static yaml_node_t test_nodes[TEST_NODE_CAPACITY];
static uint8_t test_strings[TEST_STRING_CAPACITY];

/* Plan section 5, verbatim. */
static const char TEST_MACHINE_YAML[] =
    "platform: { os: linux, posix: true }\n"
    "toolchain: { c_standard: c23, gcc_min: 16, nvcc_min: \"13.4\", nasm_min: \"3.02\", cmake_min: \"4.4\" }\n"
    "cpu:\n"
    "  model: i9-10900K\n"
    "  arch_flag: skylake\n"
    "  logical_threads: 20\n"
    "  physical_cores: 10\n"
    "  cache_line_bytes: 64\n"
    "  l1d_kib: 32\n"
    "  l2_kib: 256\n"
    "  l3_mib: 20\n"
    "  simd: { sse42: true, avx: true, avx2: true, fma3: true, bmi1: true, bmi2: true, popcnt: true, lzcnt: true, "
    "avx512: false }\n"
    "threads: { main: 1, sim: 1, gpu: 1, workers: 16, reserved_os: 1, pin_physical_first: true }\n"
    "gpu:\n"
    "  enabled: true\n"
    "  api: cuda\n"
    "  model: RTX 2080 Super\n"
    "  architecture: sm_75\n"
    "  vram_mib: 8192\n"
    "  budget: { voxels_mib: 6144, textures_mib: 256, frames_mib: 256 }\n"
    "display: { backend: kms, device: /dev/dri/card1, connector: auto, width: 1920, height: 1080, refresh_hz: 120 }\n"
    "memory: { huge_page_bytes: 2097152, process_budget_mib: 16384 }\n"
    "game: { render_distance_default: 32, render_distance_max: 512, tick_hz: 20 }\n"
    "world: { z_min: -250, z_sea: 0, z_max: 500, chunk_size: 16, section_size: 16 }\n";

/* plans/REPO_STRUCTURE.md section 4. */
static const char TEST_DIRT_YAML[] = "# config/blocks/dirt/index.yaml\n"
                                     "name: dirt\n"
                                     "hardness: 0.5\n"
                                     "tool: shovel\n"
                                     "drops: [{ item: dirt, count: 1 }]\n"
                                     "texture: { all: blocks/dirt }\n";

static yaml_storage_t test_storage(void) {
    return (yaml_storage_t){test_nodes, test_strings, TEST_NODE_CAPACITY, TEST_STRING_CAPACITY};
}

static bool test_parse(yaml_document_t *document, const char *text) {
    yaml_error_t error;
    bool ok = yaml_parse(document, (const uint8_t *)text, strlen(text), test_storage(), &error);
    if (!ok) {
        printf("unexpected error %u:%u: %s\n  in: %s\n", error.line, error.column, error.message, text);
    }
    return ok;
}

static const yaml_node_t *test_at(const yaml_document_t *document, const char *path) {
    return yaml_path_get(document, yaml_root(document), path);
}

static bool test_int_at(const yaml_document_t *document, const char *path, int64_t expected) {
    yaml_error_t error = {0};
    int64_t value = 0;
    return yaml_get_int64(test_at(document, path), &value, &error) && value == expected;
}

static bool test_bool_at(const yaml_document_t *document, const char *path, bool expected) {
    yaml_error_t error = {0};
    bool value = !expected;
    return yaml_get_bool(test_at(document, path), &value, &error) && value == expected;
}

static bool test_text_at(const yaml_document_t *document, const char *path, const char *expected) {
    return yaml_text_equals(test_at(document, path), expected);
}

/* ---- Valid documents -------------------------------------------------------------------------------- */

TEST(machine_yaml) {
    yaml_document_t d;
    CHECK(test_parse(&d, TEST_MACHINE_YAML));
    const yaml_node_t *root = yaml_root(&d);
    CHECK(root->kind == YAML_KIND_MAP);
    CHECK(root->child_count == 9u);

    /* Keys come back in source order. */
    static const char *const keys[] = {"platform", "toolchain", "cpu",  "threads", "gpu",
                                       "display",  "memory",    "game", "world"};
    const yaml_node_t *child = yaml_first_child(&d, root);
    for (uint32_t i = 0u; i < 9u; i++) {
        CHECK(child != nullptr && child->key_length == strlen(keys[i]) &&
              memcmp(child->borrowed_key, keys[i], child->key_length) == 0);
        child = yaml_next_sibling(&d, child);
    }
    CHECK(child == nullptr);

    CHECK(test_text_at(&d, "platform.os", "linux"));
    CHECK(test_bool_at(&d, "platform.posix", true));
    CHECK(test_text_at(&d, "toolchain.c_standard", "c23"));
    CHECK(test_int_at(&d, "toolchain.gcc_min", 16));
    CHECK(test_text_at(&d, "toolchain.nvcc_min", "13.4"));
    CHECK(test_at(&d, "toolchain.nvcc_min")->quoted == 1u);
    CHECK(test_text_at(&d, "toolchain.nasm_min", "3.02"));
    CHECK(test_text_at(&d, "toolchain.cmake_min", "4.4"));
    CHECK(test_text_at(&d, "cpu.model", "i9-10900K"));
    CHECK(test_text_at(&d, "cpu.arch_flag", "skylake"));
    CHECK(test_int_at(&d, "cpu.logical_threads", 20));
    CHECK(test_int_at(&d, "cpu.physical_cores", 10));
    CHECK(test_int_at(&d, "cpu.cache_line_bytes", 64));
    CHECK(test_int_at(&d, "cpu.l1d_kib", 32));
    CHECK(test_int_at(&d, "cpu.l2_kib", 256));
    CHECK(test_int_at(&d, "cpu.l3_mib", 20));
    CHECK(test_at(&d, "cpu.simd")->child_count == 9u);
    CHECK(test_bool_at(&d, "cpu.simd.sse42", true));
    CHECK(test_bool_at(&d, "cpu.simd.avx", true));
    CHECK(test_bool_at(&d, "cpu.simd.avx2", true));
    CHECK(test_bool_at(&d, "cpu.simd.fma3", true));
    CHECK(test_bool_at(&d, "cpu.simd.bmi1", true));
    CHECK(test_bool_at(&d, "cpu.simd.bmi2", true));
    CHECK(test_bool_at(&d, "cpu.simd.popcnt", true));
    CHECK(test_bool_at(&d, "cpu.simd.lzcnt", true));
    CHECK(test_bool_at(&d, "cpu.simd.avx512", false));
    CHECK(test_int_at(&d, "threads.main", 1));
    CHECK(test_int_at(&d, "threads.sim", 1));
    CHECK(test_int_at(&d, "threads.gpu", 1));
    CHECK(test_int_at(&d, "threads.workers", 16));
    CHECK(test_int_at(&d, "threads.reserved_os", 1));
    CHECK(test_bool_at(&d, "threads.pin_physical_first", true));
    CHECK(test_bool_at(&d, "gpu.enabled", true));
    CHECK(test_text_at(&d, "gpu.api", "cuda"));
    CHECK(test_text_at(&d, "gpu.model", "RTX 2080 Super"));
    CHECK(test_text_at(&d, "gpu.architecture", "sm_75"));
    CHECK(test_int_at(&d, "gpu.vram_mib", 8192));
    CHECK(test_int_at(&d, "gpu.budget.voxels_mib", 6144));
    CHECK(test_int_at(&d, "gpu.budget.textures_mib", 256));
    CHECK(test_int_at(&d, "gpu.budget.frames_mib", 256));
    CHECK(test_text_at(&d, "display.backend", "kms"));
    CHECK(test_text_at(&d, "display.device", "/dev/dri/card1"));
    CHECK(test_text_at(&d, "display.connector", "auto"));
    CHECK(test_int_at(&d, "display.width", 1920));
    CHECK(test_int_at(&d, "display.height", 1080));
    CHECK(test_int_at(&d, "display.refresh_hz", 120));
    CHECK(test_int_at(&d, "memory.huge_page_bytes", 2097152));
    CHECK(test_int_at(&d, "memory.process_budget_mib", 16384));
    CHECK(test_int_at(&d, "game.render_distance_default", 32));
    CHECK(test_int_at(&d, "game.render_distance_max", 512));
    CHECK(test_int_at(&d, "game.tick_hz", 20));
    CHECK(test_int_at(&d, "world.z_min", -250));
    CHECK(test_int_at(&d, "world.z_sea", 0));
    CHECK(test_int_at(&d, "world.z_max", 500));
    CHECK(test_int_at(&d, "world.chunk_size", 16));
    CHECK(test_int_at(&d, "world.section_size", 16));

    /* Positions point at the real lines of the file. */
    CHECK(test_at(&d, "cpu")->line == 3u);
    CHECK(test_at(&d, "cpu.l2_kib")->line == 10u);
    CHECK(test_at(&d, "world.z_min")->line == 24u);
    CHECK(test_at(&d, "cpu.model")->column == 10u);
}

TEST(dirt_yaml) {
    yaml_document_t d;
    CHECK(test_parse(&d, TEST_DIRT_YAML));
    CHECK(test_text_at(&d, "name", "dirt"));
    yaml_error_t error = {0};
    yaml_decimal_t hardness = {0};
    CHECK(yaml_get_decimal(test_at(&d, "hardness"), &hardness, &error));
    CHECK(hardness.mantissa == 5 && hardness.exponent == -1);
    const yaml_node_t *drops = test_at(&d, "drops");
    CHECK(drops->kind == YAML_KIND_SEQ && drops->child_count == 1u);
    const yaml_node_t *first = yaml_seq_at(&d, drops, 0u);
    CHECK(yaml_text_equals(yaml_map_get(&d, first, "item"), "dirt"));
    CHECK(yaml_map_get(&d, first, "nothing") == nullptr);
    CHECK(yaml_seq_at(&d, drops, 1u) == nullptr);
    CHECK(test_text_at(&d, "texture.all", "blocks/dirt"));
}

TEST(block_shapes) {
    yaml_document_t d;

    /* Empty and comment-only documents have a null root. */
    CHECK(test_parse(&d, ""));
    CHECK(yaml_root(&d)->kind == YAML_KIND_NULL);
    CHECK(test_parse(&d, "# just a comment\n\n   \n\t\n  # another\n"));
    CHECK(yaml_root(&d)->kind == YAML_KIND_NULL);

    /* A scalar or a flow collection can be the whole document. */
    CHECK(test_parse(&d, "hello there # comment\n"));
    CHECK(yaml_text_equals(yaml_root(&d), "hello there"));
    CHECK(test_parse(&d, "[1, 2]"));
    CHECK(yaml_root(&d)->child_count == 2u);

    /* No final line feed, and CRLF line ends. */
    CHECK(test_parse(&d, "a: 1\r\nb:\r\n  c: 2\r\n# x\r\nd: [3,\r\n  4]"));
    CHECK(test_int_at(&d, "a", 1));
    CHECK(test_int_at(&d, "b.c", 2));
    CHECK(test_at(&d, "b.c")->line == 3u);
    CHECK(test_at(&d, "d")->child_count == 2u);

    /* Nesting, dedenting, and a null when the next line is not deeper. */
    CHECK(test_parse(&d, "a:\n  b:\n    c: 1\n  d:\ne: 2\nf:\n"));
    CHECK(test_int_at(&d, "a.b.c", 1));
    CHECK(test_at(&d, "a.d")->kind == YAML_KIND_NULL);
    CHECK(test_at(&d, "f")->kind == YAML_KIND_NULL);
    CHECK(test_int_at(&d, "e", 2));

    /* A scalar value on the next line. */
    CHECK(test_parse(&d, "a:\n  text here\nb: 1\n"));
    CHECK(test_text_at(&d, "a", "text here"));

    /* A flow value on the next line. */
    CHECK(test_parse(&d, "a:\n  [1, 2]\nb: 1\n"));
    CHECK(test_at(&d, "a")->child_count == 2u);

    /* Sequences: of scalars, of mappings with more keys below, nested on one line, and empty items. */
    CHECK(test_parse(&d, "- a\n- b: 1\n  c: 2\n- - x\n  - y\n-\n- {z: 3}\n"));
    const yaml_node_t *root = yaml_root(&d);
    CHECK(root->kind == YAML_KIND_SEQ && root->child_count == 5u);
    CHECK(yaml_text_equals(yaml_seq_at(&d, root, 0u), "a"));
    CHECK(yaml_seq_at(&d, root, 1u)->kind == YAML_KIND_MAP && yaml_seq_at(&d, root, 1u)->child_count == 2u);
    CHECK(yaml_text_equals(yaml_seq_at(&d, yaml_seq_at(&d, root, 2u), 1u), "y"));
    CHECK(yaml_seq_at(&d, root, 3u)->kind == YAML_KIND_NULL);
    CHECK(yaml_seq_at(&d, root, 4u)->kind == YAML_KIND_MAP);

    /* A sequence at the same column as its parent key, ended by the next key. */
    CHECK(test_parse(&d, "a:\n- 1\n- x: 2\n  y:\n  - 3\n  z: 4\nb: 5\n"));
    CHECK(test_at(&d, "a")->child_count == 2u);
    CHECK(yaml_map_get(&d, yaml_seq_at(&d, test_at(&d, "a"), 1u), "y")->kind == YAML_KIND_SEQ);
    CHECK(test_int_at(&d, "b", 5));

    /* A sequence indented under its key, and a mapping with its dash on a line of its own. */
    CHECK(test_parse(&d, "a:\n  - 1\n  -\n    k: 2\n    l: 3\nb: 4\n"));
    CHECK(yaml_seq_at(&d, test_at(&d, "a"), 1u)->child_count == 2u);

    /* Document markers, and comments between lines at odd columns. */
    CHECK(test_parse(&d, "# lead\n---\na: 1\n      # deep comment\nb: 2\n...\n# trail\n\n"));
    CHECK(test_int_at(&d, "b", 2));
    CHECK(test_at(&d, "b")->line == 5u);

    /* A byte order mark, not counted in columns. */
    CHECK(test_parse(&d, "\xEF\xBB\xBF"
                         "a: 1\n"));
    CHECK(test_at(&d, "a")->column == 4u);
    CHECK(test_int_at(&d, "a", 1));

    /* Space before the colon, tabs after separators, and a root that starts indented. */
    CHECK(test_parse(&d, "  a : 1\n  b:\t2\t# c\n"));
    CHECK(test_int_at(&d, "a", 1));
    CHECK(test_int_at(&d, "b", 2));

    /* A tab after a dash is harmless when no value follows on that line. */
    CHECK(test_parse(&d, "-\t# c\n- x\n"));
    CHECK(yaml_root(&d)->child_count == 2u);
    CHECK(yaml_seq_at(&d, yaml_root(&d), 0u)->kind == YAML_KIND_NULL);
}

TEST(plain_scalars) {
    yaml_document_t d;
    CHECK(test_parse(&d, "url: http://x.y/z?a=b\n"
                         "time: 12:30\n"
                         "hash: a#b\n"
                         "dash: -5\n"
                         "word: -x\n"
                         "colon: :x\n"
                         "brackets: a[1]{2},3\n"
                         "hmm: ?x\n"
                         "utf8: caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x8E\xAE\n"
                         "yes_word: yes\n"
                         "cap: True\n"
                         "tilde: ~\n"
                         "nothing: null\n"
                         "---x: 1\n"));
    CHECK(test_text_at(&d, "url", "http://x.y/z?a=b"));
    CHECK(test_text_at(&d, "time", "12:30"));
    CHECK(test_text_at(&d, "hash", "a#b"));
    CHECK(test_int_at(&d, "dash", -5));
    CHECK(test_text_at(&d, "word", "-x"));
    CHECK(test_text_at(&d, "colon", ":x"));
    CHECK(test_text_at(&d, "brackets", "a[1]{2},3"));
    CHECK(test_text_at(&d, "hmm", "?x"));
    CHECK(test_text_at(&d, "utf8", "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x8E\xAE"));
    CHECK(test_text_at(&d, "yes_word", "yes"));
    CHECK(test_text_at(&d, "cap", "True"));
    CHECK(test_at(&d, "tilde")->kind == YAML_KIND_NULL);
    CHECK(test_at(&d, "nothing")->kind == YAML_KIND_NULL);
    CHECK(test_int_at(&d, "---x", 1));
}

TEST(quoted_scalars) {
    yaml_document_t d;
    CHECK(test_parse(&d, "a: 'it''s'\n"
                         "b: ''''\n"
                         "c: 'no \\n escape'\n"
                         "d: \"tab\\there\\n\"\n"
                         "e: \"\\\\ \\\" \\/ \\a\\b\\e\\f\\r\\v\\ \"\n"
                         "f: \"\\x41\\xE9\\u20AC\\U0001F3AE\"\n"
                         "g: \"\"\n"
                         "h: 'null'\n"
                         "i: \"true\"\n"
                         "'quoted key': 1\n"
                         "\"esc\\x41ped\": 2\n"
                         "\"\": 3\n"
                         "j: '# not a comment' # a comment\n"
                         "k: \"a: b, [c]\"\n"));
    CHECK(test_text_at(&d, "a", "it's"));
    CHECK(test_text_at(&d, "b", "'"));
    CHECK(test_text_at(&d, "c", "no \\n escape"));
    CHECK(test_text_at(&d, "d", "tab\there\n"));
    CHECK(test_text_at(&d, "e", "\\ \" / \x07\x08\x1B\x0C\x0D\x0B "));
    CHECK(test_text_at(&d, "f", "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x8E\xAE"));
    CHECK(test_text_at(&d, "g", ""));
    CHECK(test_at(&d, "h")->kind == YAML_KIND_SCALAR);
    CHECK(test_text_at(&d, "h", "null"));
    CHECK(test_int_at(&d, "quoted key", 1));
    CHECK(test_int_at(&d, "escAped", 2));
    CHECK(yaml_map_get(&d, yaml_root(&d), "") != nullptr);
    CHECK(test_text_at(&d, "j", "# not a comment"));
    CHECK(test_text_at(&d, "k", "a: b, [c]"));

    /* A quoted scalar is text, so it does not pass as a typed value. */
    yaml_error_t error = {0};
    bool truth = false;
    CHECK(!yaml_get_bool(test_at(&d, "i"), &truth, &error));
    CHECK(error.line == 9u);
}

TEST(flow) {
    yaml_document_t d;
    CHECK(test_parse(&d, "a: {}\nb: []\nc: {x: , y: ~, z: 1,}\nd: [1, [2, [3]], {e: [4]},]\n"
                         "f: {\"q\":1, 'r': 2}\n"
                         "g: {u: a b , v: http://c, w: d:e}\n"
                         "h: [\n  1, # one\n\n  2\n  ]\n"
                         "i: { j: {\n    k: 1 },\n  l: 2 }\n"
                         "m: [a, 'b,c', \"d]\"]\n"
                         "n: {o:\n  1}\n"));
    CHECK(test_at(&d, "a")->kind == YAML_KIND_MAP && test_at(&d, "a")->child_count == 0u);
    CHECK(test_at(&d, "b")->kind == YAML_KIND_SEQ && test_at(&d, "b")->child_count == 0u);
    CHECK(test_at(&d, "c")->child_count == 3u);
    CHECK(test_at(&d, "c.x")->kind == YAML_KIND_NULL);
    CHECK(test_at(&d, "c.y")->kind == YAML_KIND_NULL);
    CHECK(test_int_at(&d, "c.z", 1));
    const yaml_node_t *seq = test_at(&d, "d");
    CHECK(seq->child_count == 3u);
    CHECK(yaml_seq_at(&d, yaml_seq_at(&d, yaml_seq_at(&d, seq, 1u), 1u), 0u)->text_length == 1u);
    CHECK(yaml_map_get(&d, yaml_seq_at(&d, seq, 2u), "e")->child_count == 1u);
    CHECK(test_int_at(&d, "f.q", 1));
    CHECK(test_int_at(&d, "f.r", 2));
    CHECK(test_text_at(&d, "g.u", "a b"));
    CHECK(test_text_at(&d, "g.v", "http://c"));
    CHECK(test_text_at(&d, "g.w", "d:e"));
    CHECK(test_at(&d, "h")->child_count == 2u);
    CHECK(yaml_seq_at(&d, test_at(&d, "h"), 1u)->line == 10u);
    CHECK(test_int_at(&d, "i.j.k", 1));
    CHECK(test_int_at(&d, "i.l", 2));
    CHECK(yaml_text_equals(yaml_seq_at(&d, test_at(&d, "m"), 1u), "b,c"));
    CHECK(yaml_text_equals(yaml_seq_at(&d, test_at(&d, "m"), 2u), "d]"));
    CHECK(test_int_at(&d, "n.o", 1));

    /* The root's lines have no parent block, so column 0 is fine. */
    CHECK(test_parse(&d, "{\na: 1,\nb: [\n2\n]\n}\n"));
    CHECK(test_int_at(&d, "a", 1));
}

/* ---- Typed reads ------------------------------------------------------------------------------------ */

static bool test_int_of(const char *text, int64_t expected) {
    yaml_document_t d;
    yaml_error_t error = {0};
    int64_t value = 0;
    return test_parse(&d, text) && yaml_get_int64(yaml_root(&d), &value, &error) && value == expected;
}

static bool test_int_refused(const char *text) {
    yaml_document_t d;
    yaml_error_t error = {0};
    int64_t value = 0;
    return test_parse(&d, text) && !yaml_get_int64(yaml_root(&d), &value, &error) && error.line == 1u;
}

static bool test_decimal_of(const char *text, int64_t mantissa, int32_t exponent) {
    yaml_document_t d;
    yaml_error_t error = {0};
    yaml_decimal_t value = {0};
    return test_parse(&d, text) && yaml_get_decimal(yaml_root(&d), &value, &error) && value.mantissa == mantissa &&
           value.exponent == exponent;
}

static bool test_decimal_refused(const char *text) {
    yaml_document_t d;
    yaml_error_t error = {0};
    yaml_decimal_t value = {0};
    return test_parse(&d, text) && !yaml_get_decimal(yaml_root(&d), &value, &error) && error.line == 1u;
}

TEST(typed_reads) {
    CHECK(test_int_of("0", 0));
    CHECK(test_int_of("-0", 0));
    CHECK(test_int_of("+7", 7));
    CHECK(test_int_of("010", 10));
    CHECK(test_int_of("0x10", 16));
    CHECK(test_int_of("0xfF", 255));
    CHECK(test_int_of("-0x10", -16));
    CHECK(test_int_of("9223372036854775807", INT64_MAX));
    CHECK(test_int_of("-9223372036854775808", INT64_MIN));
    CHECK(test_int_of("0x7FFFFFFFFFFFFFFF", INT64_MAX));
    CHECK(test_int_refused("9223372036854775808"));
    CHECK(test_int_refused("-9223372036854775809"));
    CHECK(test_int_refused("99999999999999999999999"));
    CHECK(test_int_refused("0x8000000000000000"));
    CHECK(test_int_refused("1.5"));
    CHECK(test_int_refused("-"));
    CHECK(test_int_refused("+"));
    CHECK(test_int_refused("0x"));
    CHECK(test_int_refused("1a"));
    CHECK(test_int_refused("1_000"));
    CHECK(test_int_refused("\"5\""));
    CHECK(test_int_refused("~"));
    CHECK(test_int_refused("[1]"));
    CHECK(test_int_refused("{a: 1}"));

    CHECK(test_decimal_of("0.5", 5, -1));
    CHECK(test_decimal_of("-0.25", -25, -2));
    CHECK(test_decimal_of("3", 3, 0));
    CHECK(test_decimal_of("1.50", 150, -2));
    CHECK(test_decimal_of(".5", 5, -1));
    CHECK(test_decimal_of("5.", 5, 0));
    CHECK(test_decimal_of("1e3", 1, 3));
    CHECK(test_decimal_of("1.5E-3", 15, -4));
    CHECK(test_decimal_of("+2e+2", 2, 2));
    CHECK(test_decimal_of("-9223372036854775808", INT64_MIN, 0));
    CHECK(test_decimal_refused("."));
    CHECK(test_decimal_refused("-."));
    CHECK(test_decimal_refused("e5"));
    CHECK(test_decimal_refused("1e"));
    CHECK(test_decimal_refused("1e+"));
    CHECK(test_decimal_refused("1.2.3"));
    CHECK(test_decimal_refused(".inf"));
    CHECK(test_decimal_refused(".nan"));
    CHECK(test_decimal_refused("1e99999999"));
    CHECK(test_decimal_refused("9223372036854775808"));
    CHECK(test_decimal_refused("abc"));
    CHECK(test_decimal_refused("'1.5'"));

    yaml_document_t d;
    yaml_error_t error = {0};
    bool truth = false;
    CHECK(test_parse(&d, "a: yes\nb: True\nc: 1\nd: false\n"));
    CHECK(!yaml_get_bool(test_at(&d, "a"), &truth, &error));
    CHECK(error.line == 1u && strstr(error.message, "key 'a'") != nullptr && strstr(error.message, "'yes'") != nullptr);
    error = (yaml_error_t){0};
    CHECK(!yaml_get_bool(test_at(&d, "b"), &truth, &error));
    CHECK(error.line == 2u);
    error = (yaml_error_t){0};
    CHECK(!yaml_get_bool(test_at(&d, "c"), &truth, &error));
    CHECK(error.line == 3u);
    truth = true;
    CHECK(test_bool_at(&d, "d", false));

    /* Text, and what is not text. */
    const uint8_t *text = nullptr;
    uint32_t length = 0u;
    error = (yaml_error_t){0};
    CHECK(yaml_get_text(test_at(&d, "c"), &text, &length, &error) && length == 1u && text[0] == '1');
    CHECK(test_parse(&d, "m: {}\nn: ~\n"));
    CHECK(!yaml_get_text(test_at(&d, "m"), &text, &length, &error));
    CHECK(strstr(error.message, "a mapping") != nullptr);
    error = (yaml_error_t){0};
    CHECK(!yaml_get_text(test_at(&d, "n"), &text, &length, &error));
    CHECK(error.line == 2u && strstr(error.message, "null") != nullptr);

    /* A missing value, passed straight from a failed lookup. */
    error = (yaml_error_t){0};
    int64_t number = 0;
    CHECK(!yaml_get_int64(nullptr, &number, &error));
    CHECK(error.line == 1u);

    /* Lookups that do not fit. */
    CHECK(test_parse(&d, "a: {b: 1}\ns: [1]\n"));
    CHECK(test_at(&d, "a.b.c") == nullptr);
    CHECK(test_at(&d, "a.") == nullptr);
    CHECK(test_at(&d, ".a") == nullptr);
    CHECK(test_at(&d, "s.0") == nullptr);
    CHECK(yaml_map_get(&d, test_at(&d, "s"), "x") == nullptr);
    CHECK(yaml_seq_at(&d, test_at(&d, "a"), 0u) == nullptr);
    CHECK(yaml_text_equals(test_at(&d, "a"), "") == false);

    error = (yaml_error_t){0};
    CHECK(yaml_path_require(&d, test_at(&d, "a"), "b", &error) != nullptr);
    CHECK(error.line == 0u);
    CHECK(yaml_path_require(&d, test_at(&d, "a"), "b.nope", &error) == nullptr);
    CHECK(error.line == 1u && strstr(error.message, "b.nope") != nullptr);

    /* A tool's own complaint gets the node's position. */
    error = (yaml_error_t){0};
    CHECK(!yaml_fail_at_node(test_at(&d, "s"), &error, "unknown key %s", "s"));
    CHECK(error.line == 2u && error.column == 1u && strcmp(error.message, "unknown key s") == 0);
}

/* ---- Bad input -------------------------------------------------------------------------------------- */

typedef struct test_bad_t {
    const char *text;
    const char *message_part;
    uint32_t line;
    uint32_t column;
} test_bad_t;
static_assert(sizeof(test_bad_t) == 24);

static const test_bad_t TEST_BAD_INPUTS[] = {
    /* Features outside the subset. */
    {"a: 1\nb: &x 2\n", "anchors", 2u, 4u},
    {"a: 1\nb: *x\n", "aliases", 2u, 4u},
    {"a: !!str 1\n", "tags", 1u, 4u},
    {"a: |\n  text\n", "block scalars", 1u, 4u},
    {"a: >\n  text\n", "block scalars", 1u, 4u},
    {"? a\n: b\n", "complex keys", 1u, 1u},
    {"%YAML 1.2\n---\na: 1\n", "directives", 1u, 1u},
    {"a: @x\n", "cannot start", 1u, 4u},
    {"a: `x\n", "cannot start", 1u, 4u},
    {"a: %x\n", "cannot start", 1u, 4u},
    {"a: 1\n---\nb: 2\n", "one document", 2u, 1u},
    {"---\n---\n", "one document", 2u, 1u},
    {"--- a: 1\n", "unexpected content", 1u, 5u},
    {"a: 1\n...\nb: 2\n", "end marker", 3u, 1u},
    {"... x\n", "unexpected content", 1u, 5u},
    {"a: b\n  c\n", "indentation", 2u, 3u},

    /* Indentation. */
    {"a:\n\tb: 1\n", "tabs", 2u, 1u},
    {"a:\n  \tb: 1\n", "tabs", 2u, 3u},
    {"-\ta: 1\n  b: 2\n", "a tab may not separate", 1u, 3u},
    {"- \tx\n", "a tab may not separate", 1u, 4u},
    {"a:\n    b: 1\n  c: 2\n", "indentation", 3u, 3u},
    {"a: 1\n  b: 2\n", "indentation", 2u, 3u},
    {"  a: 1\nb: 2\n", "root", 2u, 1u},
    {"- a\n  - b\n", "indentation", 2u, 3u},
    {"a:\n  - b\n - c\n", "indentation", 3u, 2u},
    {"x\ny\n", "root", 2u, 1u},
    {"[1]\n[2]\n", "root", 2u, 1u},

    /* Mixed-up block structure. */
    {"a: 1\n- b\n", "sequence item", 2u, 1u},
    {"- a\nb: 1\n", "sequence item", 2u, 1u},
    {"a: 1\nb\n", "key: value", 2u, 1u},
    {"a: 1\n{b: 2}\n", "cannot be a key", 2u, 1u},
    {"a: b: c\n", "same line", 1u, 4u},
    {"a: - b\n", "sequence item", 1u, 4u},
    {"- a: b: c\n", "same line", 1u, 6u},
    {": a\n", "missing key", 1u, 1u},
    {"a:b\nc: 1\n", "root", 2u, 1u},
    {"a: 1\na: 2\n", "duplicate key 'a', first used on line 1", 2u, 1u},
    {"m:\n  a: 1\n  b: 2\n  'a': 3\n", "duplicate key", 4u, 3u},
    {"\"a\": 1\na: 2\n", "duplicate key", 2u, 1u},
    {"a: 1 2 : 3\n", "same line", 1u, 4u},
    {"a: 'x' y\n", "unexpected content", 1u, 8u},
    {"a: 'x'#y\n", "unexpected content", 1u, 7u},
    {"a: [1]x\n", "unexpected content", 1u, 7u},
    {"a: {b: 1}}\n", "unexpected content", 1u, 10u},
    {"'a':1\n", "unexpected content", 1u, 4u},

    /* Quoted scalars. */
    {"a: 'x\n", "unterminated", 1u, 4u},
    {"a: 1\nb: \"x\ny\"\n", "unterminated", 2u, 4u},
    {"a: 'x", "unterminated", 1u, 4u},
    {"a: \"x\\\n", "unterminated", 1u, 4u},
    {"a: \"x\\", "unterminated", 1u, 4u},
    {"a: \"\\q\"\n", "unknown escape", 1u, 5u},
    {"a: \"\\x4\"\n", "hex digits", 1u, 5u},
    {"a: \"\\u12\"\n", "hex digits", 1u, 5u},
    {"a: \"\\xZZ\"\n", "hex digits", 1u, 5u},
    {"a: \"\\uD800\"\n", "Unicode", 1u, 5u},
    {"a: \"\\U00110000\"\n", "Unicode", 1u, 5u},
    {"a: \"\\x00\"\n", "NUL", 1u, 5u},

    /* Flow collections. */
    {"a: [1, 2\n", "unterminated", 1u, 4u},
    {"a: {b: 1\n", "unterminated", 1u, 4u},
    {"a: {b:", "unterminated", 1u, 4u},
    {"x: 1\ny: [\n  [1,\n  2]\n", "unterminated", 2u, 4u},
    {"a: [1}\n", "expected ',' or ']'", 1u, 6u},
    {"a: {b: 1]\n", "expected ',' or '}'", 1u, 9u},
    {"a: [}\n", "unexpected '}'", 1u, 5u},
    {"a: {]\n", "unexpected ']'", 1u, 5u},
    {"a: [1,,2]\n", "empty entry", 1u, 7u},
    {"a: [,]\n", "empty entry", 1u, 5u},
    {"a: {b}\n", "key: value", 1u, 5u},
    {"a: {b, c: 1}\n", "key: value", 1u, 5u},
    {"a: {b:1}\n", "key: value", 1u, 5u},
    {"a: {[b]: 1}\n", "cannot be a key", 1u, 5u},
    {"a: {b: 1, b: 2}\n", "duplicate key", 1u, 11u},
    {"a: [b: 1]\n", "expected ','", 1u, 6u},
    {"a: {b: c: d}\n", "expected ','", 1u, 9u},
    {"a: {: 1}\n", "missing key", 1u, 5u},
    {"a: [- 1]\n", "sequence item", 1u, 5u},
    {"a: [&x 1]\n", "anchors", 1u, 5u},
    {"a: [#c\n]\n", "comment needs whitespace", 1u, 5u},
    {"a:\n  [1,\n  2]\nb: [\n3]\n", "indented deeper", 5u, 1u},
    {"a: [\n]\n", "indented deeper", 2u, 1u},
    {"[\n---\n]\n", "document marker", 2u, 1u},
    {"[\n...\n]\n", "document marker", 2u, 1u},
    {"a: 'x' : 1\n", "same line", 1u, 4u},

    /* Bytes that are not text. */
    {"a: 1\nb: \x01\n", "control character", 2u, 4u},
    {"a: 1\n\x7F\n", "control character", 2u, 1u},
    {"a\rb: 1\n", "carriage return", 1u, 2u},
    {"a: 1\n\n\xFF\n", "UTF-8", 3u, 1u},
    {"a: \xC0\x80\n", "UTF-8", 1u, 4u},
    {"a: \xC3\n", "UTF-8", 1u, 4u},
    {"a: \xE0\x80\x80\n", "UTF-8", 1u, 4u},
    {"a: \xED\xA0\x80\n", "UTF-8", 1u, 4u},
    {"a: \xF4\x90\x80\x80\n", "UTF-8", 1u, 4u},
    {"a: \xF0\x9F\x8E", "UTF-8", 1u, 4u},
};

TEST(bad_inputs) {
    for (uint64_t i = 0u; i < sizeof(TEST_BAD_INPUTS) / sizeof(TEST_BAD_INPUTS[0]); i++) {
        const test_bad_t *bad = &TEST_BAD_INPUTS[i];
        yaml_document_t d;
        yaml_error_t error;
        bool ok = yaml_parse(&d, (const uint8_t *)bad->text, strlen(bad->text), test_storage(), &error);
        bool as_expected = !ok && error.line == bad->line && error.column == bad->column &&
                           strstr(error.message, bad->message_part) != nullptr;
        if (!CHECK(as_expected)) {
            printf("FAIL bad input %u: expected %u:%u '%s', got ok=%d %u:%u '%s'\n  in: %s\n", (uint32_t)i, bad->line,
                   bad->column, bad->message_part, ok, error.line, error.column, error.message, bad->text);
        }
        if (!ok) {
            CHECK(yaml_root(&d) == nullptr);
            CHECK(d.node_count == 0u);
        }
    }

    /* A NUL in the text is an error rather than the end of it. */
    yaml_document_t d;
    yaml_error_t error;
    CHECK(!yaml_parse(&d, (const uint8_t *)"a: 1\n\0b: 2\n", 11u, test_storage(), &error));
    CHECK(error.line == 2u && strstr(error.message, "control character") != nullptr);

    /* Text beyond the size limit, refused before a byte of it is read. */
    CHECK(!yaml_parse(&d, (const uint8_t *)"", (uint64_t)YAML_MAX_TEXT_BYTES + 1u, test_storage(), &error));
    CHECK(error.line == 1u && strstr(error.message, "larger") != nullptr);
}

/* ---- Limits ----------------------------------------------------------------------------------------- */

TEST(depth) {
    static uint8_t text[2048];
    yaml_document_t d;
    yaml_error_t error;

    /* Flow: exactly at the limit, then one past it. */
    uint64_t length = 0u;
    for (uint32_t i = 0u; i < YAML_MAX_DEPTH; i++) {
        text[length++] = '[';
    }
    for (uint32_t i = 0u; i < YAML_MAX_DEPTH; i++) {
        text[length++] = ']';
    }
    CHECK(yaml_parse(&d, text, length, test_storage(), &error));
    length = 0u;
    for (uint32_t i = 0u; i < YAML_MAX_DEPTH + 1u; i++) {
        text[length++] = '[';
    }
    CHECK(!yaml_parse(&d, text, length, test_storage(), &error));
    CHECK(strstr(error.message, "deeper") != nullptr && error.line == 1u);

    /* Block: one `- ` per level on one line, then the last line deeper still. */
    length = 0u;
    for (uint32_t i = 0u; i < YAML_MAX_DEPTH; i++) {
        text[length++] = '-';
        text[length++] = ' ';
    }
    text[length++] = 'x';
    CHECK(yaml_parse(&d, text, length, test_storage(), &error));
    length = 0u;
    for (uint32_t i = 0u; i < YAML_MAX_DEPTH + 1u; i++) {
        text[length++] = '-';
        text[length++] = ' ';
    }
    text[length++] = 'x';
    CHECK(!yaml_parse(&d, text, length, test_storage(), &error));
    CHECK(strstr(error.message, "deeper") != nullptr);

    /* Far deeper than the limit, as a stack overflow probe for any recursion. */
    static uint8_t brackets[1048576];
    memset(brackets, '[', sizeof(brackets));
    CHECK(!yaml_parse(&d, brackets, sizeof(brackets), test_storage(), &error));
}

TEST(storage_limits) {
    yaml_document_t d;
    yaml_error_t error;
    const uint8_t *text = (const uint8_t *)TEST_MACHINE_YAML;
    uint64_t length = strlen(TEST_MACHINE_YAML);

    /* Find the exact need, then show one less fails on a real line, and one less byte of strings too. */
    CHECK(yaml_parse(&d, text, length, test_storage(), &error));
    uint32_t needed = d.node_count;
    yaml_storage_t storage = {test_nodes, nullptr, needed, 0u};
    CHECK(yaml_parse(&d, text, length, storage, &error));
    storage.node_capacity = needed - 1u;
    CHECK(!yaml_parse(&d, text, length, storage, &error));
    CHECK(error.line == 24u && strstr(error.message, "node storage") != nullptr);
    storage.node_capacity = 0u;
    CHECK(!yaml_parse(&d, text, length, storage, &error));
    CHECK(error.line == 1u && strstr(error.message, "node storage") != nullptr);

    const uint8_t *escaped = (const uint8_t *)"a: 'x''y'\nb: \"p\\tq\"\n";
    uint64_t escaped_length = strlen((const char *)escaped);
    storage = (yaml_storage_t){test_nodes, test_strings, TEST_NODE_CAPACITY, 6u};
    CHECK(!yaml_parse(&d, escaped, escaped_length, storage, &error));
    CHECK(error.line == 2u && strstr(error.message, "string storage") != nullptr);
    storage.string_capacity = 7u;
    CHECK(yaml_parse(&d, escaped, escaped_length, storage, &error));
    storage.string_capacity = 2u;
    CHECK(!yaml_parse(&d, escaped, escaped_length, storage, &error));
    CHECK(error.line == 1u);

    /* Text that needs no decoding needs no string storage at all. */
    storage = (yaml_storage_t){test_nodes, nullptr, TEST_NODE_CAPACITY, 0u};
    CHECK(yaml_parse(&d, (const uint8_t *)"a: 'b'\nc: \"d\"\n", 14u, storage, &error));

    /* The promise in the header: capacity of length + 1 nodes is always enough, even at the densest. */
    static const char *const dense[] = {"[[[[]]]]", "-\n-\n-\n", "[a,b,c]", "- - - -", "a:\nb:\nc:\n", ""};
    for (uint32_t i = 0u; i < sizeof(dense) / sizeof(dense[0]); i++) {
        uint32_t dense_length = (uint32_t)strlen(dense[i]);
        storage = (yaml_storage_t){test_nodes, nullptr, dense_length + 1u, 0u};
        CHECK(yaml_parse(&d, (const uint8_t *)dense[i], dense_length, storage, &error));
    }
}

/* ---- Damaged input ---------------------------------------------------------------------------------- */

/* Whatever came back must be a real tree, reached by walking it with no recursion and no out-of-range index. */
static void test_check_result(test_context_t *t, bool ok, const yaml_document_t *d, const yaml_error_t *error,
                              uint64_t length) {
    if (!ok) {
        CHECK(error->line >= 1u && error->line <= length + 1u);
        CHECK(error->column >= 1u && error->column <= length + 1u);
        CHECK(error->message[0] != '\0');
        CHECK(memchr(error->message, '\0', sizeof(error->message)) != nullptr);
        return;
    }
    CHECK(d->node_count >= 1u && d->node_count <= length + 1u);
    uint32_t children = 0u;
    for (uint32_t i = 0u; i < d->node_count; i++) {
        const yaml_node_t *node = &d->borrowed_nodes[i];
        uint32_t counted = 0u;
        for (const yaml_node_t *child = yaml_first_child(d, node); child != nullptr;
             child = yaml_next_sibling(d, child)) {
            CHECK(child > node);
            CHECK(child < d->borrowed_nodes + d->node_count);
            counted++;
        }
        CHECK(counted == node->child_count);
        CHECK(node->kind <= YAML_KIND_SEQ);
        children += counted;
        /* Touch all text, so the sanitizer sees any slice running off its buffer. */
        volatile uint8_t sink = 0u;
        for (uint32_t b = 0u; b < node->text_length; b++) {
            sink = node->borrowed_text[b];
        }
        for (uint32_t b = 0u; b < node->key_length; b++) {
            sink = node->borrowed_key[b];
        }
        (void)sink;
    }
    CHECK(children == d->node_count - 1u); /* every node but the root has exactly one parent */
}

/* Parses from a buffer ending exactly at the text's end, so that the sanitizer catches any read past it. */
static void test_damaged_one(test_context_t *t, const uint8_t *source, uint64_t length) {
    static uint8_t tail_buffer[8192];
    /* Copied up against the end, because the sanitizer puts a redzone right after a static array. */
    uint8_t *copy = tail_buffer + sizeof(tail_buffer) - length;
    memcpy(copy, source, length);
    yaml_document_t d;
    yaml_error_t error;
    bool ok = yaml_parse(&d, copy, length, test_storage(), &error);
    test_check_result(t, ok, &d, &error, length);
}

static void test_damaged_text(test_context_t *t, const char *text) {
    uint64_t length = strlen(text);
    static uint8_t buffer[8192];
    if (length >= sizeof(buffer)) {
        CHECK(!"the text must fit the test's buffers");
        return;
    }

    /* Every truncation. */
    for (uint64_t cut = 0u; cut <= length; cut++) {
        test_damaged_one(t, (const uint8_t *)text, cut);
    }
    /* Every position, overwritten with each byte likely to confuse the structure. */
    static const uint8_t mutations[] = {'\n', '\r', '\t', ' ',  ':',   '-',   '#',   ',',   '{',
                                        '}',  '[',  ']',  '\'', '"',   '\\',  '&',   '*',   '!',
                                        '|',  '?',  '~',  0u,   0x7Fu, 0xC3u, 0xE2u, 0xF0u, 0xFFu};
    for (uint64_t at = 0u; at < length; at++) {
        for (uint64_t m = 0u; m < sizeof(mutations); m++) {
            memcpy(buffer, text, length);
            buffer[at] = mutations[m];
            test_damaged_one(t, buffer, length);
        }
    }
    /* Every position, deleted. */
    for (uint64_t at = 0u; at < length; at++) {
        memcpy(buffer, text, at);
        memcpy(buffer + at, text + at + 1u, length - at - 1u);
        test_damaged_one(t, buffer, length - 1u);
    }
}

TEST(damaged) {
    test_damaged_text(t, TEST_MACHINE_YAML);
    test_damaged_text(t, TEST_DIRT_YAML);
    test_damaged_text(t, "a:\n- 'x''y'\n- \"p\\tq\\u20AC\"\n- b: {c: [1, {d: ~}]}\n  e:\n  - - f\n    - g\n...\n");
}

TEST_MAIN()
