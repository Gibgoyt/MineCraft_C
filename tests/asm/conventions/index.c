/* ctest asm_conventions. See README.md. Runs the build's own NASM over the kernels, with a machine/index.inc of the test's
 * making, to prove that a kernel refuses a config that turns off what it uses. Arguments come from test.cmake. */
#include "harness/index.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum : uint32_t {
    ARG_NASM = 0u,
    ARG_ASM_INCLUDE = 1u,
    ARG_GENERATED = 2u,
    ARG_TMP = 3u,
    ARG_COUNT = 4u,
    NASM_TIMEOUT_MS = 60000u,
    PATH_BYTES = 1024u,
};

static const char *const every_feature[] = {"SSE42", "AVX",    "AVX2",  "FMA3", "BMI1",
                                            "BMI2",  "POPCNT", "LZCNT", "F16C", "AVX512"};
enum : uint32_t { FEATURE_COUNT = sizeof(every_feature) / sizeof(every_feature[0]) };

/* Writes <tmp>/fake/machine/index.inc: every feature on except the one named. */
static bool write_fake_machine(test_context_t *t, const char *turned_off) {
    char path[PATH_BYTES];
    const char *tmp = t->borrowed_args[ARG_TMP];
    (void)snprintf(path, sizeof(path), "%s/fake", tmp);
    (void)mkdir(tmp, 0755);
    (void)mkdir(path, 0755);
    (void)snprintf(path, sizeof(path), "%s/fake/machine", tmp);
    (void)mkdir(path, 0755);
    (void)snprintf(path, sizeof(path), "%s/fake/machine/index.inc", tmp);
    FILE *file = fopen(path, "w");
    if (file == nullptr) {
        return false;
    }
    for (uint32_t i = 0; i < FEATURE_COUNT; i++) {
        (void)fprintf(file, "%%define MC_CPU_HAS_%s %d\n", every_feature[i],
                      strcmp(every_feature[i], turned_off) == 0 ? 0 : 1);
    }
    return fclose(file) == 0;
}

/* Runs NASM on asm/src/<kernel>/index.asm, as the build does, with machine/index.inc coming from config_dir. */
static void run_nasm(test_context_t *t, const char *kernel, const char *config_dir, test_process_t *out) {
    char source[PATH_BYTES];
    char object[PATH_BYTES];
    char include_asm[PATH_BYTES];
    char include_config[PATH_BYTES];
    (void)snprintf(source, sizeof(source), "asm/src/%s/index.asm", kernel);
    (void)snprintf(object, sizeof(object), "%s/%s.o", t->borrowed_args[ARG_TMP], kernel);
    (void)snprintf(include_asm, sizeof(include_asm), "-I%s/", t->borrowed_args[ARG_ASM_INCLUDE]);
    (void)snprintf(include_config, sizeof(include_config), "-I%s/", config_dir);
    static char format_flag[] = "-felf64";
    static char warnings_flag[] = "-w+all";
    static char error_flag[] = "-Werror";
    static char output_flag[] = "-o";
    char *const argv[] = {t->borrowed_args[ARG_NASM],
                          format_flag,
                          warnings_flag,
                          error_flag,
                          include_asm,
                          include_config,
                          source,
                          output_flag,
                          object,
                          nullptr};
    test_process_run(t, argv, NASM_TIMEOUT_MS, out);
}

static void check_refused(test_context_t *t, const char *kernel, const char *feature) {
    char fake[PATH_BYTES];
    (void)snprintf(fake, sizeof(fake), "%s/fake", t->borrowed_args[ARG_TMP]);
    if (!CHECK(write_fake_machine(t, feature))) {
        return;
    }
    test_process_t result;
    run_nasm(t, kernel, fake, &result);
    CHECK(result.started != 0u);
    CHECK(result.exit_code != 0);
    CHECK_CONTAINS(result.output, feature);
    CHECK_CONTAINS(result.output, "config/machine.yaml");
}

static void check_accepted(test_context_t *t, const char *kernel, const char *feature) {
    char fake[PATH_BYTES];
    (void)snprintf(fake, sizeof(fake), "%s/fake", t->borrowed_args[ARG_TMP]);
    if (!CHECK(write_fake_machine(t, feature))) {
        return;
    }
    test_process_t result;
    run_nasm(t, kernel, fake, &result);
    CHECK_EQ_I64(result.exit_code, 0);
}

TEST(kernels_assemble_clean_under_the_real_config) {
    REQUIRE((uint32_t)t->arg_count == ARG_COUNT);
    const char *const kernels[] = {"copy", "blend", "bitmask"};
    for (uint32_t i = 0; i < 3u; i++) {
        test_process_t result;
        run_nasm(t, kernels[i], t->borrowed_args[ARG_GENERATED], &result);
        CHECK(result.started != 0u);
        CHECK_EQ_I64(result.exit_code, 0);
        CHECK_EQ_U64(result.output_length, 0u);
    }
}

TEST(a_kernel_refuses_a_config_without_what_it_uses) {
    REQUIRE((uint32_t)t->arg_count == ARG_COUNT);
    check_refused(t, "copy", "AVX");
    check_refused(t, "blend", "AVX2");
    check_refused(t, "bitmask", "AVX2");
    check_refused(t, "bitmask", "POPCNT");
    check_refused(t, "bitmask", "BMI2");
}

TEST(a_kernel_does_not_care_about_what_it_does_not_use) {
    REQUIRE((uint32_t)t->arg_count == ARG_COUNT);
    check_accepted(t, "copy", "AVX2");
    check_accepted(t, "blend", "POPCNT");
    check_accepted(t, "bitmask", "F16C");
}

TEST_MAIN()
