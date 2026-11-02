/* See README.md. The Root tier: it declares app_t, calls each system's _init in order and _deinit in reverse, and
 * allocates nothing. The CPU check must stay first. */
#include "main/index.h"

#include "heap/index.h"
#include "job/index.h"
#include "log/index.h"
#include "machine/index.h"
#include "main/cpu_check/index.h"
#include "simd/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "types/index.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct main_options_t {
    uint32_t cpu_hidden;  /* features to treat as absent: --pretend-missing-cpu */
    uint8_t report;       /* --report */
    uint8_t pretend_leak; /* --pretend-leak */
    uint8_t _pad0[2];
} main_options_t;
static_assert(sizeof(main_options_t) == 8);

static void main_print_usage(const char *program) {
    fprintf(stderr,
            "usage: %s [--report] [--pretend-missing-cpu <cpu.simd key>] [--pretend-leak]\n"
            "  --report                 print the build's defines and CPU features, then exit\n"
            "  --pretend-missing-cpu    treat this feature as absent, to see the CPU check fail; may repeat\n"
            "  --pretend-leak           skip the shutdown, to see the leak check at exit fail\n",
            program);
}

static bool main_parse_arguments(int32_t argc, char **argv, main_options_t *out) {
    *out = (main_options_t){.cpu_hidden = 0, .report = 0, .pretend_leak = 0, ._pad0 = {0}};
    for (int32_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--report") == 0) {
            out->report = 1;
        } else if (strcmp(argv[i], "--pretend-leak") == 0) {
            out->pretend_leak = 1;
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

/* The CPU check comes first: everything after it assumes these instructions exist. --report only prints, so it starts
 * nothing. The topology is read before main pins itself, since afterwards the answer would be that one CPU. */
static main_exit_t app_init(app_t *app, const main_options_t *options) {
    memset(app, 0, sizeof(*app));
    app->cpu_required = cpu_check_required();
    app->cpu_present = cpu_check_detect() & ~options->cpu_hidden;
    const uint32_t missing = app->cpu_required & ~app->cpu_present;
    if (missing != 0) {
        char message[512];
        cpu_check_message(missing, message, sizeof(message));
        LOG_ERROR("%s", message);
        return MAIN_EXIT_CPU_MISMATCH;
    }
    if (options->report != 0) {
        return MAIN_EXIT_OK;
    }
    if (!simd_init(&app->simd)) {
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    thread_topology_t topology;
    if (!thread_topology_read(&topology) || !job_init(&app->job, &topology)) {
        simd_deinit(&app->simd);
        return MAIN_EXIT_RUNTIME; /* each has said why, and neither left anything behind */
    }
    if (!thread_pin_self(thread_cpu_for(&topology, THREAD_ROLE_MAIN, 0u))) {
        job_deinit(&app->job);
        simd_deinit(&app->simd);
        return MAIN_EXIT_RUNTIME;
    }
    return MAIN_EXIT_OK;
}

static void app_deinit(app_t *app) {
    job_deinit(&app->job);
    simd_deinit(&app->simd);
    memset(app, 0, sizeof(*app));
}

static void main_job_count(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    _Atomic uint64_t *const done = borrowed_argument;
    atomic_fetch_add_explicit(done, 1u, memory_order_relaxed);
}

/* Until game arrives (Stage 08.4) this is all there is to run: one round of real jobs through the real workers, so a
 * runtime that cannot run anything fails here and not in the first stage that needs it. The group's wait carries the
 * ordering, so the load below may be relaxed. */
static bool main_run(app_t *app) {
    const int64_t started_ns = time_now_ns();
    _Atomic uint64_t done = 0;
    job_group_t group;
    job_group_init(&group);
    for (uint32_t i = 0; i < MC_THREADS_WORKERS; i++) {
        job_submit(&app->job, &group, main_job_count, (void *)&done);
    }
    job_group_wait(&app->job, &group);
    job_group_deinit(&group);
    const uint64_t jobs_done = atomic_load_explicit(&done, memory_order_relaxed);
    LOG_INFO("core runtime: %" PRIu32 " workers ran %" PRIu64 " jobs in %.3f ms; no game systems exist yet",
             (uint32_t)MC_THREADS_WORKERS, jobs_done, (libc_double_t)time_ns_to_ms(time_now_ns() - started_ns));
    return jobs_done == MC_THREADS_WORKERS;
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
    main_exit_t status = app_init(&app, &options);
    if (status != MAIN_EXIT_OK) {
        return status;
    }
    if (options.report != 0) {
        main_report(&app);
    } else if (!main_run(&app)) {
        status = MAIN_EXIT_RUNTIME;
    }
    if (options.pretend_leak == 0) {
        app_deinit(&app);
    }
    if (options.report == 0) {
        heap_counters_t counters;
        heap_counters_read(&counters);
        LOG_INFO("heap at exit: %" PRIu64 " reserved, %" PRIu64 " released, %" PRIu64 " bytes live",
                 counters.reserve_calls, counters.release_calls, counters.live_bytes);
    }
    /* Everything is deinitialized by now, so anything still live is a leak: plans/CODING_STANDARD.md section 10. */
    const main_exit_t final_status = heap_exit_check() ? status : MAIN_EXIT_LEAK;
    if (options.pretend_leak != 0) {
        /* The workers are still running and looking at app, on this stack, so main must not return under them. */
        _exit(final_status);
    }
    return final_status;
}
