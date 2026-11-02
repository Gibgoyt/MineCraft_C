/* See README.md. The Root tier: it declares app_t, calls each system's _init in order and _deinit in reverse, and
 * allocates nothing. The CPU check must stay first. */
#include "main/index.h"

#include "display/headless/demo/index.h"
#include "display/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "input/index.h"
#include "job/index.h"
#include "log/index.h"
#include "machine/index.h"
#include "main/cpu_check/index.h"
#include "math/index.h"
#include "render/index.h"
#include "simd/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "types/index.h"
#include "world/index.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct main_options_t {
    const char *card;           /* --card, or nullptr for config/machine.yaml */
    const char *connector;      /* --connector, or nullptr for config/machine.yaml */
    const char *script;         /* --script */
    const char *screenshot_dir; /* --screenshot-dir */
    uint32_t width;             /* --width, or 0 for config/machine.yaml */
    uint32_t height;            /* --height, or 0 for config/machine.yaml */
    uint32_t ticks;             /* --ticks, or 0 for as long as the script */
    uint32_t cpu_hidden;        /* features to treat as absent: --pretend-missing-cpu */
    uint8_t report;             /* --report */
    uint8_t pretend_leak;       /* --pretend-leak */
    uint8_t test_pattern;       /* --test-pattern */
    uint8_t headless;           /* --headless */
    uint32_t render_distance;   /* --render-distance, or 0 for config/machine.yaml */
    uint8_t crash_test;         /* display_crash_test_t: --crash-test */
    uint8_t test_world;         /* --test-world */
    uint8_t features_off;       /* --no-shapes, --no-alpha, --no-sky and on: SHADE_FEATURE_* */
    uint8_t has_time_of_day;    /* --time-of-day */
    float32_t time_of_day;      /* --time-of-day */
} main_options_t;
static_assert(sizeof(main_options_t) == 64);

static void main_print_usage(const char *program) {
    fprintf(
        stderr,
        "usage: %s [--report] [--pretend-missing-cpu <cpu.simd key>] [--pretend-leak]\n"
        "       %s --test-pattern [--card <path>] [--connector <name>] [--crash-test[=<how>]]\n"
        "       %s --headless [--script <path>] [--screenshot-dir <dir>] [--ticks <n>] [--width <w>] [--height <h>]\n"
        "       %s --test-world [--render-distance <chunks>] [--time-of-day <0 to 1>] [--no-shapes] [--no-alpha]\n"
        "           [--no-sky] [--no-day-cycle] [--no-fog] [--no-shadows] [--no-stored-light] [--headless ...]\n"
        "  --report                 print the build's defines and CPU features, then exit\n"
        "  --pretend-missing-cpu    treat this feature as absent, to see the CPU check fail; may repeat\n"
        "  --pretend-leak           skip the shutdown, to see the leak check at exit fail\n"
        "  --test-pattern           show a test pattern through KMS; only from a text console, never inside X.\n"
        "                           The mouse moves a square, a held key or button turns it red, Esc ends the run\n"
        "  --card, --connector      with --test-pattern: use these and not display: in config/machine.yaml\n"
        "  --crash-test[=<how>]     with --test-pattern: die on purpose after 240 frames, to see the console come\n"
        "                           back. <how> is segv (the default), abort, kill, or hang for the watchdog\n"
        "  While the display is taken, Ctrl+Alt+Backspace is the emergency exit\n"
        "  --headless               no display: replay a script of input and write PNG screenshots; never needs a\n"
        "                           console. See run/src/display/headless/README.md\n"
        "  --no-shapes, --no-alpha  with --test-world: draw every block a whole cube, or stop rays at see-through "
        "texels\n"
        "  --no-sky                 with --test-world: a plain painted sky and the sun straight up, with no shadows\n"
        "  --no-day-cycle           with --test-world: the time of day stays where it starts\n"
        "  --no-fog, --no-shadows, --no-stored-light\n"
        "                           with --test-world: no fog, nothing in the sun's way, every voxel under open sky\n"
        "  --time-of-day <0 to 1>   with --test-world: the time to start at: 0 midnight, 0.25 sunrise, 0.5 noon\n"
        "  --test-world             fly through a generated world: through KMS from a text console, never inside X,\n"
        "                           where WASD, Space and Shift move, the mouse looks, the wheel sets the speed, Ctrl\n"
        "                           goes faster and Esc ends the run. With --headless, the script does all of that\n",
        program, program, program, program);
}

/* A whole number from 1 up, for a flag. */
static bool main_parse_count(const char *flag, const char *text, uint32_t *out) {
    uint64_t value = 0;
    for (const char *c = text; *c != '\0'; c++) {
        if (*c < '0' || *c > '9') {
            value = 0;
            break;
        }
        value = value * 10u + (uint64_t)(*c - '0');
        if (value > INT32_MAX) {
            value = 0;
            break;
        }
    }
    if (value == 0u) {
        fprintf(stderr, "%s wants a whole number above 0, up to %" PRId32 ", not '%s'\n", flag, (int32_t)INT32_MAX,
                text);
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool main_parse_arguments(int32_t argc, char **argv, main_options_t *out) {
    memset(out, 0, sizeof(*out));
    for (int32_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--report") == 0) {
            out->report = 1;
        } else if (strcmp(argv[i], "--pretend-leak") == 0) {
            out->pretend_leak = 1;
        } else if (strcmp(argv[i], "--test-pattern") == 0) {
            out->test_pattern = 1;
        } else if (strncmp(argv[i], "--crash-test", 12u) == 0 && (argv[i][12] == '\0' || argv[i][12] == '=')) {
            const char *const how_name = argv[i][12] == '=' ? argv[i] + 13 : "";
            const display_crash_test_t how = display_vt_guard_crash_test_from_name(how_name);
            if (how == DISPLAY_CRASH_TEST_NONE) {
                fprintf(stderr, "%s: --crash-test takes segv, abort, kill or hang, not '%s'\n", argv[0], how_name);
                return false;
            }
            out->crash_test = (uint8_t)how;
        } else if (strcmp(argv[i], "--headless") == 0) {
            out->headless = 1;
        } else if (strcmp(argv[i], "--test-world") == 0) {
            out->test_world = 1;
        } else if (strcmp(argv[i], "--no-shapes") == 0) {
            out->features_off |= (uint8_t)SHADE_FEATURE_SHAPES;
        } else if (strcmp(argv[i], "--no-alpha") == 0) {
            out->features_off |= (uint8_t)SHADE_FEATURE_ALPHA;
        } else if (strcmp(argv[i], "--no-sky") == 0) {
            out->features_off |= (uint8_t)SHADE_FEATURE_SKY;
        } else if (strcmp(argv[i], "--no-day-cycle") == 0) {
            out->features_off |= (uint8_t)SHADE_FEATURE_DAY_CYCLE;
        } else if (strcmp(argv[i], "--no-fog") == 0) {
            out->features_off |= (uint8_t)SHADE_FEATURE_FOG;
        } else if (strcmp(argv[i], "--no-shadows") == 0) {
            out->features_off |= (uint8_t)SHADE_FEATURE_SHADOW;
        } else if (strcmp(argv[i], "--no-stored-light") == 0) {
            out->features_off |= (uint8_t)SHADE_FEATURE_LIGHT;
        } else if (strcmp(argv[i], "--time-of-day") == 0 && i + 1 < argc) {
            i++;
            char *end = nullptr;
            out->time_of_day = strtof(argv[i], &end);
            if (end == argv[i] || *end != '\0' || !(out->time_of_day >= 0.0f32 && out->time_of_day <= 1.0f32)) {
                fprintf(stderr, "%s: --time-of-day takes a number from 0 to 1, not %s\n", argv[0], argv[i]);
                return false;
            }
            out->has_time_of_day = 1u;
        } else if (strcmp(argv[i], "--render-distance") == 0 && i + 1 < argc) {
            i++;
            if (!main_parse_count("--render-distance", argv[i], &out->render_distance)) {
                return false;
            }
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            i++;
            out->script = argv[i];
        } else if (strcmp(argv[i], "--screenshot-dir") == 0 && i + 1 < argc) {
            i++;
            out->screenshot_dir = argv[i];
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            i++;
            if (!main_parse_count("--ticks", argv[i], &out->ticks)) {
                return false;
            }
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            i++;
            if (!main_parse_count("--width", argv[i], &out->width)) {
                return false;
            }
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            i++;
            if (!main_parse_count("--height", argv[i], &out->height)) {
                return false;
            }
        } else if (strcmp(argv[i], "--card") == 0 && i + 1 < argc) {
            i++;
            out->card = argv[i];
        } else if (strcmp(argv[i], "--connector") == 0 && i + 1 < argc) {
            i++;
            out->connector = argv[i];
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
    if ((out->card != nullptr || out->connector != nullptr) && out->test_pattern == 0) {
        fprintf(stderr, "%s: --card and --connector only mean something with --test-pattern\n", argv[0]);
        return false;
    }
    if (out->crash_test != DISPLAY_CRASH_TEST_NONE && out->test_pattern == 0) {
        fprintf(stderr, "%s: --crash-test only means something with --test-pattern\n", argv[0]);
        return false;
    }
    if (out->headless == 0 && (out->script != nullptr || out->screenshot_dir != nullptr || out->ticks != 0u ||
                               out->width != 0u || out->height != 0u)) {
        fprintf(stderr,
                "%s: --script, --screenshot-dir, --ticks, --width and --height only mean something with "
                "--headless\n",
                argv[0]);
        return false;
    }
    if (out->render_distance != 0u && out->test_world == 0) {
        fprintf(stderr, "%s: --render-distance only means something with --test-world\n", argv[0]);
        return false;
    }
    if (out->features_off != 0u && out->test_world == 0) {
        fprintf(stderr, "%s: --no-shapes and the other effects' flags only mean something with --test-world\n",
                argv[0]);
        return false;
    }
    if (out->has_time_of_day != 0u && out->test_world == 0) {
        fprintf(stderr, "%s: --time-of-day only means something with --test-world\n", argv[0]);
        return false;
    }
    if (out->render_distance > MC_GAME_RENDER_DISTANCE_MAX) {
        fprintf(stderr, "%s: --render-distance goes up to %" PRIu32 "\n", argv[0],
                (uint32_t)MC_GAME_RENDER_DISTANCE_MAX);
        return false;
    }
    if (out->test_world != 0 && (out->test_pattern != 0 || out->report != 0)) {
        fprintf(stderr, "%s: --test-world does not go with --test-pattern or --report\n", argv[0]);
        return false;
    }
    if (out->headless != 0 && (out->test_pattern != 0 || out->report != 0)) {
        fprintf(stderr, "%s: --headless does not go with --test-pattern or --report\n", argv[0]);
        return false;
    }
    return true;
}

/* The CPU check comes first: everything after it assumes these instructions exist. --report only prints, so it starts
 * nothing, and --test-pattern needs only the display. The topology is read before main pins itself, since afterwards
 * the answer would be that one CPU. */
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
    if (options->report != 0 || options->test_pattern != 0) {
        return MAIN_EXIT_OK;
    }
    /* math before job: workers inherit this thread's floating-point environment, so this is the check for them too. */
    if (!math_init(&app->math)) {
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!simd_init(&app->simd)) {
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!texture_init(&app->texture)) {
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!thread_topology_read(&app->topology) || !job_init(&app->job, &app->topology)) {
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* each has said why, and neither left anything behind */
    }
    /* Before main pins itself: any thread CUDA starts here would inherit that one CPU. */
    if (MC_GPU_ENABLED != 0 && gpu_bridge_init(&app->gpu_bridge, gpu_bridge_limits_default()) != GPU_STATUS_OK) {
        job_deinit(&app->job);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!thread_pin_self(thread_cpu_for(&app->topology, THREAD_ROLE_MAIN, 0u)) ||
        !world_init(&app->world, world_limits_default())) {
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME;
    }
    if (!terrain_init(&app->terrain, TERRAIN_SEED_DEFAULT)) {
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!render_init(&app->render, &app->gpu_bridge.device_memory, &app->job, render_limits_default())) {
        terrain_deinit(&app->terrain);
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    /* Takes nothing until game_open. */
    game_init(&app->game, &app->world, &app->terrain, &app->render, &app->job, &app->texture, &app->topology);
    return MAIN_EXIT_OK;
}

static void app_deinit(app_t *app) {
    game_deinit(&app->game);
    input_deinit(&app->input);
    display_offscreen_deinit(&app->offscreen);
    display_deinit(&app->display);
    render_deinit(&app->render);
    terrain_deinit(&app->terrain);
    world_deinit(&app->world);
    gpu_bridge_deinit(&app->gpu_bridge);
    job_deinit(&app->job);
    texture_deinit(&app->texture);
    simd_deinit(&app->simd);
    math_deinit(&app->math);
    memset(app, 0, sizeof(*app));
}

static void main_job_count(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    _Atomic uint64_t *const done = borrowed_argument;
    atomic_fetch_add_explicit(done, 1u, memory_order_relaxed);
}

/* What a run with no flags does until a menu opens a world: one round of real jobs through the real workers, so a runtime
 * that cannot run anything fails here and not in the first stage that needs it. The group's wait carries the ordering, so the
 * load below may be relaxed. */
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

static main_exit_t main_exit_from_display(display_status_t status) {
    switch (status) {
    case DISPLAY_STATUS_OK:
        return MAIN_EXIT_OK;
    case DISPLAY_STATUS_REFUSED:
        return MAIN_EXIT_DISPLAY_REFUSED;
    case DISPLAY_STATUS_ERROR:
        return MAIN_EXIT_DISPLAY_ERROR;
    }
    return MAIN_EXIT_DISPLAY_ERROR;
}

/* KMS with no world. Only this flag and --test-world open the display, so that nothing ctest runs can reach the card. */
static main_exit_t main_run_test_pattern(app_t *app, const main_options_t *options, int64_t started_ns) {
    display_config_t config;
    display_config_from_machine(&config);
    config.card_path = options->card != nullptr ? options->card : config.card_path;
    config.connector_name = options->connector != nullptr ? options->connector : config.connector_name;
    config.console_fd = STDIN_FILENO;
    config.crash_test = (display_crash_test_t)options->crash_test;
    const display_status_t status = display_init(&app->display, &config, display_device_real_ops(), nullptr);
    if (status != DISPLAY_STATUS_OK) {
        return main_exit_from_display(status);
    }
    /* Only now: reading /dev/input reads whoever is at the machine, and display_init said that this process holds the
     * console. Under X, in a pipe or under ctest, it has refused by this point. */
    if (!input_init(&app->input, INPUT_DEVICE_DIRECTORY, input_ops_real(), nullptr)) {
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    display_test_pattern_set_cursor(&app->display, input_test_pattern_cursor, input_test_pattern_discard, &app->input);
    const display_status_t ran = display_test_pattern_run(&app->display, started_ns, DISPLAY_TEST_PATTERN_NS);
    input_report(&app->input);
    return main_exit_from_display(ran);
}

/* Replays input with no display at all, on the demo scene, which needs no card: see run/src/game/README.md. */
static main_exit_t main_run_headless(app_t *app, const main_options_t *options) {
    display_headless_config_t config;
    display_offscreen_config_from_machine(&config);
    config.script_path = options->script;
    config.screenshot_dir = options->screenshot_dir;
    config.tick_count = options->ticks;
    config.width = options->width != 0u ? options->width : config.width;
    config.height = options->height != 0u ? options->height : config.height;
    if (!display_offscreen_init(&app->offscreen, &config)) {
        return MAIN_EXIT_HEADLESS;
    }
    display_headless_demo_t demo;
    display_headless_scene_t scene;
    display_headless_demo_scene(&demo, &scene);
    display_headless_t *const headless = &app->offscreen.headless;
    const bool ran = display_headless_run(headless, &scene);
    LOG_INFO("headless: %" PRIu64 " frames, %" PRIu64 " screenshots", headless->frames_presented,
             headless->screenshots_written);
    return ran ? MAIN_EXIT_OK : MAIN_EXIT_HEADLESS;
}

/* A generated world, with no display at all: what a script does to the free-fly camera, in PNGs. */
static main_exit_t main_run_test_world_headless(app_t *app, const main_options_t *options) {
    display_headless_config_t config;
    display_offscreen_config_from_machine(&config);
    config.script_path = options->script;
    config.screenshot_dir = options->screenshot_dir;
    config.tick_count = options->ticks;
    config.width = options->width != 0u ? options->width : config.width;
    config.height = options->height != 0u ? options->height : config.height;
    if (!display_offscreen_init(&app->offscreen, &config)) {
        return MAIN_EXIT_HEADLESS;
    }
    const game_config_t game = {.render_distance = options->render_distance != 0u ? options->render_distance
                                                                                  : MC_GAME_RENDER_DISTANCE_DEFAULT,
                                .width = config.width,
                                .height = config.height,
                                .features_off = options->features_off,
                                ._pad0 = {0}};
    if (!game_open(&app->game, &game)) {
        return MAIN_EXIT_RUNTIME;
    }
    if (options->has_time_of_day != 0u) {
        game_day_set(&app->game, (float64_t)options->time_of_day);
    }
    display_headless_scene_t scene;
    game_headless_scene(&app->game, &scene);
    display_headless_t *const headless = &app->offscreen.headless;
    const bool ran = display_headless_run(headless, &scene);
    LOG_INFO("headless: %" PRIu64 " frames, %" PRIu64 " screenshots", headless->frames_presented,
             headless->screenshots_written);
    game_report(&app->game);
    game_close(&app->game);
    return ran ? MAIN_EXIT_OK : MAIN_EXIT_HEADLESS;
}

/* A generated world through KMS. The display comes first: under a display server it refuses before anything has been looked at
 * or started, and input only ever opens once it said yes. */
static main_exit_t main_run_test_world(app_t *app, const main_options_t *options) {
    display_config_t config;
    display_config_from_machine(&config);
    config.console_fd = STDIN_FILENO;
    const display_status_t status = display_init(&app->display, &config, display_device_real_ops(), nullptr);
    if (status != DISPLAY_STATUS_OK) {
        return main_exit_from_display(status);
    }
    if (!input_init(&app->input, INPUT_DEVICE_DIRECTORY, input_ops_real(), nullptr)) {
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    const display_buffer_t *const buffer = &app->display.flip.buffers[0];
    const game_config_t game = {.render_distance = options->render_distance != 0u ? options->render_distance
                                                                                  : MC_GAME_RENDER_DISTANCE_DEFAULT,
                                .width = buffer->width,
                                .height = buffer->height,
                                .features_off = options->features_off,
                                ._pad0 = {0}};
    if (!game_open(&app->game, &game)) {
        return MAIN_EXIT_RUNTIME;
    }
    if (options->has_time_of_day != 0u) {
        game_day_set(&app->game, (float64_t)options->time_of_day);
    }
    game_loop_source_begin(&app->game, &app->input);
    const display_status_t ran = display_frames_run(&app->display, game_loop_source, game_loop_discard, &app->game);
    const bool loop_ok = game_loop_stop(&app->game);
    input_report(&app->input);
    game_report(&app->game);
    game_close(&app->game);
    if (ran == DISPLAY_STATUS_OK && !loop_ok) {
        return MAIN_EXIT_RUNTIME;
    }
    return main_exit_from_display(ran);
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
    const int64_t started_ns = time_now_ns();
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
    } else if (options.test_pattern != 0) {
        status = main_run_test_pattern(&app, &options, started_ns);
    } else if (options.test_world != 0) {
        status =
            options.headless != 0 ? main_run_test_world_headless(&app, &options) : main_run_test_world(&app, &options);
    } else if (options.headless != 0) {
        status = main_run_headless(&app, &options);
    } else if (!main_run(&app)) {
        status = MAIN_EXIT_RUNTIME;
    }
    /* Before deinit, which frees whatever is left on the card. */
    const bool gpu_clean = gpu_bridge_exit_check(&app.gpu_bridge);
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
    const bool heap_clean = heap_exit_check();
    const main_exit_t final_status = heap_clean && gpu_clean ? status : MAIN_EXIT_LEAK;
    if (options.pretend_leak != 0) {
        /* The workers are still running and looking at app, on this stack, so main must not return under them. */
        _exit(final_status);
    }
    return final_status;
}
