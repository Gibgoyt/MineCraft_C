/* See README.md. The Root tier: it declares app_t, calls each system's _init in order and _deinit in reverse, and
 * allocates nothing. No system with an _init exists yet, so app_init holds only the CPU check, which must stay first. */
#include "main/index.h"

#include "heap/index.h"
#include "log/index.h"
#include "machine/index.h"
#include "main/cpu_check/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct main_options_t {
    uint32_t cpu_hidden; /* features to treat as absent: --pretend-missing-cpu */
    uint8_t report;      /* --report */
    uint8_t _pad0[3];
} main_options_t;
static_assert(sizeof(main_options_t) == 8);

static void main_print_usage(const char *program) {
    fprintf(stderr,
            "usage: %s [--report] [--pretend-missing-cpu <cpu.simd key>]\n"
            "  --report                 print the build's defines and CPU features, then exit\n"
            "  --pretend-missing-cpu    treat this feature as absent, to see the CPU check fail; may repeat\n",
            program);
}

static bool main_parse_arguments(int32_t argc, char **argv, main_options_t *out) {
    *out = (main_options_t){.cpu_hidden = 0, .report = 0, ._pad0 = {0}};
    for (int32_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--report") == 0) {
            out->report = 1;
        } else if (strcmp(argv[i], "--pretend-missing-cpu") == 0 && i + 1 < argc) {
            i++;
            uint32_t feature = 0;
            if (!cpu_check_feature_from_name(argv[i], &feature)) {
                fprintf(stderr, "%s: '%s' is not a key under cpu.simd in config/machine.yaml\n", argv[0], argv[i]);
                return false;
            }
            out->cpu_hidden |= feature;
        } else {
            fprintf(stderr, "%s: unknown argument '%s'\n", argv[0], argv[i]);
            return false;
        }
    }
    return true;
}

/* The CPU check comes first: everything after it assumes these instructions exist. */
static bool app_init(app_t *app, const main_options_t *options) {
    app->cpu_required = cpu_check_required();
    app->cpu_present = cpu_check_detect() & ~options->cpu_hidden;
    const uint32_t missing = app->cpu_required & ~app->cpu_present;
    if (missing != 0) {
        char message[512];
        cpu_check_message(missing, message, sizeof(message));
        LOG_ERROR("%s", message);
        return false;
    }
    return true;
}

static void app_deinit(app_t *app) {
    *app = (app_t){.cpu_required = 0, .cpu_present = 0};
}

/* Stage 02.3 asks the binary to report its defines, to show a yaml change reaching compiled code. This path must never
 * touch a display or an input device: ctest runs it. */
static void main_report(const app_t *app) {
    char names[128];
    printf("MC_PLATFORM_POSIX=%" PRId32 "\n", (int32_t)MC_PLATFORM_POSIX);
    printf("MC_THREADS_WORKERS=%" PRId32 "\n", (int32_t)MC_THREADS_WORKERS);
    printf("MC_GPU_ENABLED=%" PRId32 "\n", (int32_t)MC_GPU_ENABLED);
    printf("MC_CPU_ARCH_FLAG=%s\n", MC_CPU_ARCH_FLAG);
    cpu_check_names(app->cpu_required, names, sizeof(names));
    printf("cpu required: %s\n", names);
    cpu_check_names(app->cpu_present, names, sizeof(names));
    printf("cpu present: %s\n", names);
    cpu_check_names(app->cpu_required & ~app->cpu_present, names, sizeof(names));
    printf("cpu missing: %s\n", names);
}

int32_t main(int32_t argc, char **argv) {
    main_options_t options;
    if (!main_parse_arguments(argc, argv, &options)) {
        main_print_usage(argv[0]);
        return MAIN_EXIT_USAGE;
    }

    app_t app;
    if (!app_init(&app, &options)) {
        return MAIN_EXIT_CPU_MISMATCH;
    }
    if (options.report != 0) {
        main_report(&app);
    } else {
        LOG_INFO("core runtime started and stopped; no game systems exist yet");
    }
    app_deinit(&app);
    /* Everything is deinitialized by now, so anything still live is a leak: plans/CODING_STANDARD.md section 10. */
    return heap_exit_check() ? MAIN_EXIT_OK : MAIN_EXIT_LEAK;
}
