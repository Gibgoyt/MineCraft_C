/* See README.md. The Root tier: it declares app_t, calls each system's _init in order and _deinit in reverse, and
 * allocates nothing. The CPU check must stay first. */
#include "main/index.h"

#include "display/headless/demo/index.h"
#include "display/index.h"
#include "entity/index.h"
#include "mob/index.h"
#include "item/index.h"
#include "game/index.h"
#include "game/session/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "input/index.h"
#include "job/index.h"
#include "log/index.h"
#include "machine/index.h"
#include "main/cpu_check/index.h"
#include "math/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "simd/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "types/index.h"
#include "world/index.h"

#include <errno.h>
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
    uint8_t walk;               /* --walk */
    uint8_t has_time_of_day;    /* --time-of-day */
    uint32_t features_off;      /* --no-shapes, --no-alpha, --no-sky and on: SHADE_FEATURE_* */
    float32_t time_of_day;      /* --time-of-day */
    uint32_t full_detail;       /* --full-detail, or 0 for RENDER_LOD_FULL_DETAIL_DEFAULT */
    const char *saves;          /* --saves, or nullptr for MAIN_SAVES_DEFAULT */
    const char *settings;       /* --settings, or nullptr for MAIN_SETTINGS_DEFAULT */
    const char *profile;        /* --profile, or nullptr for none */
    uint32_t pretend_gpu_lost;  /* --pretend-gpu-lost: pictures until the card really faults; 0 for never */
    uint8_t windowed;           /* --windowed */
    uint8_t has_seed;           /* --seed */
    uint8_t god_rays;           /* --god-rays */
    uint8_t menu;               /* --menu, or no flags at all on a terminal */
    uint64_t seed;              /* --seed */
} main_options_t;
static_assert(sizeof(main_options_t) == 112);

/* Where worlds live unless told otherwise: beside where the program was started, like everything else it reads. */
static const char MAIN_SAVES_DEFAULT[] = "saves";

enum : uint32_t {
    MAIN_WINDOW_WIDTH = 1280u, /* --windowed, with no --width and --height */
    MAIN_WINDOW_HEIGHT = 720u,
};
/* And where what a person chose on the Settings screen is kept. */
static const char MAIN_SETTINGS_DEFAULT[] = "settings.yaml";

static void main_print_usage(const char *program) {
    fprintf(
        stderr,
        "usage: %s [--report] [--pretend-missing-cpu <cpu.simd key>] [--pretend-leak] [--pretend-gpu-lost <pictures>]\n"
        "       %s --test-pattern [--card <path>] [--connector <name>] [--crash-test[=<how>]] [--windowed]\n"
        "       %s --headless [--script <path>] [--screenshot-dir <dir>] [--ticks <n>] [--width <w>] [--height <h>]\n"
        "       %s --test-world [--seed <n>] [--walk] [--render-distance <chunks>] [--time-of-day <0 to 1>]\n"
        "           [--no-shapes] [--no-alpha] [--no-sky] [--no-day-cycle] [--no-fog]\n"
        "           [--no-shadows] [--no-stored-light] [--no-transparency] [--no-water-reflection]\n"
        "           [--no-water-tint] [--no-under-water] [--no-ao] [--no-taa] [--god-rays] [--no-entities]\n"
        "           [--no-outline] [--full-detail <chunks>] [--no-lod] [--headless ...]\n"
        "       %s --menu [--saves <dir>] [--settings <file>] [--render-distance <chunks>] [--headless ...]\n"
        "  --report                 print the build's defines and CPU features, then exit\n"
        "  --pretend-missing-cpu    treat this feature as absent, to see the CPU check fail; may repeat\n"
        "  --pretend-leak           skip the shutdown, to see the leak check at exit fail\n"
        "  --test-pattern           show a test pattern through KMS; in a window on a desktop, or through KMS from a "
        "text console.\n"
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
        "  --no-transparency        with --test-world: rays stop at water and part-covered texels, on the mean colour\n"
        "  --no-water-reflection, --no-water-tint\n"
        "                           with --test-world: water reflects nothing, or takes no colour with depth\n"
        "  --no-ao, --no-taa        with --test-world: no short rays to darken corners, or each frame stands alone\n"
        "  --no-entities            with --test-world: mobs and items are not drawn\n"
        "  --no-outline             with --test-world: no outline on what is aimed at, and no crack on what is mined\n"
        "  --full-detail <chunks>   with --test-world: how far the world is drawn block by block. Past it, up to\n"
        "                           --render-distance, coarse grids take over. Default 32\n"
        "  --no-lod                 with --test-world: the world stops where full detail does\n"
        "  --god-rays               with --test-world: the air shows the sun's light where it reaches\n"
        "  --test-world             fly through a generated world: in a window on a desktop, or through KMS from a "
        "text console,\n"
        "                           where WASD, Space and Shift move, the mouse looks, the wheel sets the speed, Ctrl\n"
        "                           goes faster and Esc ends the run. With --headless, the script does all of that\n"
        "  --walk                   with --test-world: walk and do not fly. WASD walks, Space jumps, swims up and "
        "climbs,\n"
        "                           Shift sneaks, Ctrl sprints, the mouse looks, the left button mines and the right\n"
        "                           button places what was mined. E opens the inventory, the right button on a "
        "crafting\n"
        "                           table, furnace or chest opens that, and E or Esc closes it\n",
        program, program, program, program, program);
    fputs(
        "  --no-under-water         with --test-world: a body's eyes under water see no blue and no short fog\n"
        "  --time-of-day <0 to 1>   with --test-world: the time to start at: 0 midnight, 0.25 sunrise, 0.5 noon\n"
        "  --seed <n>               with --test-world: this seed's land and not the default's, 0x for hex\n"
        "  --pretend-gpu-lost       make the card fault for real after this many pictures, to see a world saved\n"
        "                           and the run end. Takes only this program's context; the kernel's log has a line\n"
        "  (no flags, on a terminal) the same as --menu\n"
        "  --menu                   the game: main menu, make or choose a world, walk in it, Esc to pause. Through "
        "KMS\n"
        "                           from a text console, never inside X; with --headless, a script does all of that\n"
        "  --saves <dir>            with --menu: where worlds live. Default saves, under where it was started\n"
        "  --settings <file>        with --menu: where settings are kept. Default settings.yaml, under where it was\n"
        "                           started. Flags that say the same go over it, held to its ranges, and are kept\n"
        "                           with the rest if the Settings screen is left with Done\n"
        "  --profile <path>         time the run: a trace for about:tracing or Perfetto goes to <path> at exit, and a\n"
        "                           table to stderr. Not in a release build: see run/src/profiler/README.md\n",
        stderr);
    fputs(
        "  --windowed               on a desktop, a 1280x720 window, or --width and --height, in place of full screen\n"
        "                           From a terminal on a desktop every mode above opens a window: Alt+Tab lets go of "
        "the mouse\n",
        stderr);
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
        } else if (strcmp(argv[i], "--pretend-gpu-lost") == 0 && i + 1 < argc) {
            i++;
            if (!main_parse_count("--pretend-gpu-lost", argv[i], &out->pretend_gpu_lost)) {
                return false;
            }
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
        } else if (strcmp(argv[i], "--windowed") == 0) {
            out->windowed = 1;
        } else if (strcmp(argv[i], "--headless") == 0) {
            out->headless = 1;
        } else if (strcmp(argv[i], "--test-world") == 0) {
            out->test_world = 1;
        } else if (strcmp(argv[i], "--no-shapes") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_SHAPES;
        } else if (strcmp(argv[i], "--no-alpha") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_ALPHA;
        } else if (strcmp(argv[i], "--menu") == 0) {
            out->menu = 1u;
        } else if (strcmp(argv[i], "--saves") == 0 && i + 1 < argc) {
            i++;
            out->saves = argv[i];
        } else if (strcmp(argv[i], "--settings") == 0 && i + 1 < argc) {
            i++;
            out->settings = argv[i];
        } else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            i++;
            out->profile = argv[i];
        } else if (strcmp(argv[i], "--walk") == 0) {
            out->walk = 1;
        } else if (strcmp(argv[i], "--no-sky") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_SKY;
        } else if (strcmp(argv[i], "--no-day-cycle") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_DAY_CYCLE;
        } else if (strcmp(argv[i], "--no-transparency") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_TRANSPARENCY;
        } else if (strcmp(argv[i], "--no-water-reflection") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_WATER_REFLECTION;
        } else if (strcmp(argv[i], "--no-water-tint") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_WATER_TINT;
        } else if (strcmp(argv[i], "--no-under-water") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_UNDER_WATER;
        } else if (strcmp(argv[i], "--no-entities") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_ENTITIES;
        } else if (strcmp(argv[i], "--no-outline") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_OUTLINE;
        } else if (strcmp(argv[i], "--no-ao") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_AO;
        } else if (strcmp(argv[i], "--no-taa") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_TAA;
        } else if (strcmp(argv[i], "--no-lod") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_LOD;
        } else if (strcmp(argv[i], "--full-detail") == 0 && i + 1 < argc) {
            i++;
            if (!main_parse_count("--full-detail", argv[i], &out->full_detail)) {
                return false;
            }
        } else if (strcmp(argv[i], "--god-rays") == 0) {
            out->god_rays = 1u;
        } else if (strcmp(argv[i], "--no-fog") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_FOG;
        } else if (strcmp(argv[i], "--no-shadows") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_SHADOW;
        } else if (strcmp(argv[i], "--no-stored-light") == 0) {
            out->features_off |= (uint32_t)SHADE_FEATURE_LIGHT;
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
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            i++;
            char *seed_end = nullptr;
            errno = 0;
            out->seed = strtoull(argv[i], &seed_end, 0);
            if (errno != 0 || seed_end == argv[i] || *seed_end != '\0' || argv[i][0] == '-') {
                fprintf(stderr, "%s: --seed wants a whole number from 0 to 2^64 - 1, and '%s' is not one\n", argv[0],
                        argv[i]);
                return false;
            }
            out->has_seed = 1u;
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
    /* --windowed says how to show the game, so with no mode of its own it is the game, and the flags for that go with it. */
    if (out->windowed != 0 && out->test_world == 0 && out->test_pattern == 0 && out->report == 0 &&
        out->headless == 0 && isatty(STDIN_FILENO) != 0) {
        out->menu = 1u;
    }
    if (out->crash_test != DISPLAY_CRASH_TEST_NONE && out->test_pattern == 0) {
        fprintf(stderr, "%s: --crash-test only means something with --test-pattern\n", argv[0]);
        return false;
    }
    if (out->windowed != 0 && out->headless != 0) {
        fprintf(stderr, "%s: --windowed does not go with --headless, which has no window\n", argv[0]);
        return false;
    }
    if (out->headless == 0 && (out->script != nullptr || out->screenshot_dir != nullptr || out->ticks != 0u ||
                               (out->windowed == 0 && (out->width != 0u || out->height != 0u)))) {
        fprintf(stderr,
                "%s: --script, --screenshot-dir, --ticks, --width and --height only mean something with "
                "--headless, and the last two with --windowed\n",
                argv[0]);
        return false;
    }
    if (out->saves != nullptr && out->menu == 0u) {
        fprintf(stderr, "%s: --saves only means something with --menu\n", argv[0]);
        return false;
    }
    if (out->settings != nullptr && out->menu == 0u) {
        fprintf(stderr, "%s: --settings only means something with --menu\n", argv[0]);
        return false;
    }
    if (out->menu != 0u && (out->test_world != 0 || out->test_pattern != 0 || out->report != 0)) {
        fprintf(stderr, "%s: --menu does not go with --test-world, --test-pattern or --report\n", argv[0]);
        return false;
    }
    if (out->render_distance != 0u && out->test_world == 0 && out->menu == 0u) {
        fprintf(stderr, "%s: --render-distance only means something with --test-world or --menu\n", argv[0]);
        return false;
    }
    if (out->full_detail != 0u && out->test_world == 0 && out->menu == 0u) {
        fprintf(stderr, "%s: --full-detail only means something with --test-world or --menu\n", argv[0]);
        return false;
    }
    if ((out->features_off != 0u || out->god_rays != 0u) && out->test_world == 0 && out->menu == 0u) {
        fprintf(stderr,
                "%s: --no-shapes and the other effects' flags only mean something with --test-world or --menu\n",
                argv[0]);
        return false;
    }
    if (out->has_time_of_day != 0u && out->test_world == 0) {
        fprintf(stderr, "%s: --time-of-day only means something with --test-world\n", argv[0]);
        return false;
    }
    if (out->has_seed != 0u && out->test_world == 0) {
        fprintf(stderr, "%s: --seed only means something with --test-world\n", argv[0]);
        return false;
    }
    if (out->walk != 0 && out->test_world == 0) {
        fprintf(stderr, "%s: --walk only means something with --test-world\n", argv[0]);
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
    if (out->profile != nullptr && (out->test_pattern != 0 || out->report != 0)) {
        fprintf(stderr, "%s: --profile does not go with --test-pattern or --report\n", argv[0]);
        return false;
    }
    if (out->profile != nullptr && PROFILER_BUILT == 0) {
        fprintf(stderr,
                "%s: this build has no scopes to time, so --profile would say nothing. Build the profile preset\n",
                argv[0]);
        return false;
    }
    if (out->headless != 0 && (out->test_pattern != 0 || out->report != 0)) {
        fprintf(stderr, "%s: --headless does not go with --test-pattern or --report\n", argv[0]);
        return false;
    }
    /* The game, for a person who just runs it. Only on a terminal: anything else that starts the program with no flags, such as
     * ctest, with stdin from /dev/null, gets the check of the runtime and is nowhere near the display. Inside X it is a window. */
    if (argc == 1 && isatty(STDIN_FILENO) != 0) {
        out->menu = 1u;
    }
    return true;
}

/* What is kept, with what the flags say over it, for this run. Before job_init, which the worker count is for. */
static void main_settings_read(app_t *app, const main_options_t *options) {
    settings_load(&app->settings, options->settings != nullptr ? options->settings : MAIN_SETTINGS_DEFAULT);
    settings_values_t *const values = &app->settings.values;
    values->render_distance = options->render_distance != 0u ? options->render_distance : values->render_distance;
    values->full_detail = options->full_detail != 0u ? options->full_detail : values->full_detail;
    values->effects &= ~(uint32_t)options->features_off;
    values->effects |= options->god_rays != 0u ? (uint32_t)SHADE_FEATURE_GOD_RAYS : 0u;
    /* A flag can say what the file cannot: a full detail too big for the card, or a distance no slider has. */
    settings_persist_clamp(values);
}

/* The CPU check comes first: everything after it assumes these instructions exist. --report only prints, so it starts
 * nothing, and --test-pattern needs only the display. The topology is read before main pins itself, since afterwards
 * the answer would be that one CPU. */
[[gnu::noinline]]
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
    /* First, so that it can be stopped last. main gives it back if anything after this fails. */
    if (options->profile != nullptr && !profiler_init(&app->profiler, profiler_limits_default())) {
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
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
    if (!settings_init(&app->settings)) {
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (options->menu != 0u) {
        main_settings_read(app, options);
    }
    if (!thread_topology_read(&app->topology) ||
        !job_init_count(&app->job, &app->topology, app->settings.values.worker_count)) {
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* each has said why, and neither left anything behind */
    }
    job_profiler_set(&app->job, &app->profiler);
    /* Before main pins itself: any thread CUDA starts here would inherit that one CPU. */
    if (MC_GPU_ENABLED != 0 && gpu_bridge_init(&app->gpu_bridge, gpu_bridge_limits_default()) != GPU_STATUS_OK) {
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!thread_pin_self(thread_cpu_for(&app->topology, THREAD_ROLE_MAIN, 0u)) ||
        !world_init(&app->world, world_limits_default())) {
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME;
    }
    if (!terrain_init(&app->terrain, options->has_seed != 0u ? options->seed : TERRAIN_SEED_DEFAULT)) {
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!physics_init(&app->physics)) {
        terrain_deinit(&app->terrain);
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    player_init(&app->player, &app->physics);
    if (!entity_init(&app->entity, &app->physics)) {
        player_deinit(&app->player);
        physics_deinit(&app->physics);
        terrain_deinit(&app->terrain);
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!item_init(&app->item)) {
        entity_deinit(&app->entity);
        player_deinit(&app->player);
        physics_deinit(&app->physics);
        terrain_deinit(&app->terrain);
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME;
    }
    if (!mob_init(&app->mob, &app->job)) {
        item_deinit(&app->item);
        entity_deinit(&app->entity);
        player_deinit(&app->player);
        physics_deinit(&app->physics);
        terrain_deinit(&app->terrain);
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!render_init(&app->render, &app->gpu_bridge.device_memory, &app->job, render_limits_default())) {
        mob_deinit(&app->mob);
        item_deinit(&app->item);
        entity_deinit(&app->entity);
        player_deinit(&app->player);
        physics_deinit(&app->physics);
        terrain_deinit(&app->terrain);
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (!ui_init(&app->ui, &app->texture)) {
        render_deinit(&app->render);
        mob_deinit(&app->mob);
        item_deinit(&app->item);
        entity_deinit(&app->entity);
        player_deinit(&app->player);
        physics_deinit(&app->physics);
        terrain_deinit(&app->terrain);
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    if (options->menu != 0u && !save_init(&app->save)) {
        ui_deinit(&app->ui);
        render_deinit(&app->render);
        mob_deinit(&app->mob);
        item_deinit(&app->item);
        entity_deinit(&app->entity);
        player_deinit(&app->player);
        physics_deinit(&app->physics);
        terrain_deinit(&app->terrain);
        world_deinit(&app->world);
        gpu_bridge_deinit(&app->gpu_bridge);
        job_deinit(&app->job);
        settings_deinit(&app->settings);
        texture_deinit(&app->texture);
        simd_deinit(&app->simd);
        math_deinit(&app->math);
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    /* Takes nothing until game_open. */
    game_init(&app->game, &app->world, &app->terrain, &app->render, &app->job, &app->texture, &app->topology);
    game_ui_set(&app->game, &app->ui);
    game_profiler_set(&app->game, &app->profiler);
    game_gpu_pretend_set(&app->game, options->pretend_gpu_lost, false);
    if (options->walk != 0 || options->menu != 0u) {
        game_player_set(&app->game, &app->player);
        game_entity_set(&app->game, &app->entity);
        game_mob_set(&app->game, &app->mob);
        game_item_set(&app->game, &app->item);
    }
    if (options->menu != 0u) {
        game_save_set(&app->game, &app->save);
    }
    return MAIN_EXIT_OK;
}

/* Every thread that wrote a lane has been joined. The table goes where the log does. False, having said why, if the trace could not be
 * written: a run asked to be measured has not done what it was asked. */
static bool main_profile_finish(app_t *app, const char *path) {
    bool ok = true;
    if (path != nullptr) {
        ok = profiler_trace_dump(&app->profiler, path) == FILE_OK;
        const file_t log_file = {.fd = STDERR_FILENO, ._pad0 = {0}};
        if (profiler_summary_write(&app->profiler, &log_file) != FILE_OK) {
            LOG_WARN("profiler: could not write the table to stderr");
        }
    }
    profiler_deinit(&app->profiler);
    return ok;
}

/* False if a --profile's trace could not be written. */
[[gnu::noinline]]
static bool app_deinit(app_t *app, const char *profile_path) {
    game_deinit(&app->game);
    input_deinit(&app->input);
    display_offscreen_deinit(&app->offscreen);
    display_deinit(&app->display);
    save_deinit(&app->save);
    ui_deinit(&app->ui);
    render_deinit(&app->render);
    mob_deinit(&app->mob);
    item_deinit(&app->item);
    entity_deinit(&app->entity);
    player_deinit(&app->player);
    physics_deinit(&app->physics);
    terrain_deinit(&app->terrain);
    world_deinit(&app->world);
    gpu_bridge_deinit(&app->gpu_bridge);
    job_deinit(&app->job);
    settings_deinit(&app->settings);
    texture_deinit(&app->texture);
    simd_deinit(&app->simd);
    math_deinit(&app->math);
    const bool profiled = main_profile_finish(app, profile_path);
    memset(app, 0, sizeof(*app));
    return profiled;
}

static void main_job_count(job_worker_t *worker, void *borrowed_argument) {
    (void)worker;
    _Atomic uint64_t *const done = borrowed_argument;
    atomic_fetch_add_explicit(done, 1u, memory_order_relaxed);
}

/* What a run with no flags does until a menu opens a world: one round of real jobs through the real workers, so a runtime
 * that cannot run anything fails here and not in the first stage that needs it. The group's wait carries the ordering, so the
 * load below may be relaxed. */
[[gnu::noinline]]
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
    LOG_INFO("core runtime: %" PRIu32 " workers ran %" PRIu64 " jobs in %.3f ms; no terminal, so no game",
             (uint32_t)MC_THREADS_WORKERS, jobs_done, (libc_double_t)time_ns_to_ms(time_now_ns() - started_ns));
    return jobs_done == MC_THREADS_WORKERS;
}

/* The card hung with a thread inside it: that thread may yet run, on anything it holds, so nothing can be given back and main cannot return. The console has
 * gone back and the world has been saved by now, and the kernel takes the rest. */
static void main_exit_if_gpu_stuck(const app_t *app) {
    if (game_gpu_stuck(&app->game)) {
        _exit(MAIN_EXIT_GPU_LOST);
    }
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

/* A window only for a person at a terminal on a desktop. ctest gives every program it runs /dev/null for stdin, so nothing it runs
 * ever opens one: those go on to KMS's checks, which refuse them. */
static void main_window_config(display_config_t *config, const main_options_t *options) {
    if (getenv("DISPLAY") == nullptr || isatty(STDIN_FILENO) == 0) {
        return;
    }
    config->window = 1u;
    config->fullscreen = (uint8_t)(options->windowed != 0u ? 0u : 1u);
    if (options->windowed != 0u) {
        config->width = options->width != 0u ? options->width : MAIN_WINDOW_WIDTH;
        config->height = options->height != 0u ? options->height : MAIN_WINDOW_HEIGHT;
    }
}

static void main_sink_key(void *context, uint32_t code, int32_t value) {
    input_fed_key(context, code, value);
}

static void main_sink_motion(void *context, int32_t dx, int32_t dy) {
    input_fed_motion(context, dx, dy);
}

static void main_sink_wheel(void *context, int32_t vertical, int32_t horizontal) {
    input_fed_wheel(context, vertical, horizontal);
}

static void main_sink_release_all(void *context) {
    input_fed_release_all(context);
}

/* Joins the window's events to input, neither of which includes the other. */
static void main_window_pump(void *context, input_t *input) {
    const display_x11_sink_t sink = {.key = main_sink_key,
                                     .motion = main_sink_motion,
                                     .wheel = main_sink_wheel,
                                     .release_all = main_sink_release_all,
                                     .context = input};
    display_window_poll(context, &sink);
}

/* Devices if this took the console, and the window's own events if it is a window: /dev/input would be reading what is typed at other
 * windows. */
static bool main_input_start(app_t *app) {
    if (app->display.window != 0u) {
        return input_init_fed(&app->input, main_window_pump, &app->display);
    }
    return input_init(&app->input, INPUT_DEVICE_DIRECTORY, input_ops_real(), nullptr);
}

/* KMS with no world. Only this flag and --test-world open the display, so that nothing ctest runs can reach the card. */
[[gnu::noinline]]
static main_exit_t main_run_test_pattern(app_t *app, const main_options_t *options, int64_t started_ns) {
    display_config_t config;
    display_config_from_machine(&config);
    config.card_path = options->card != nullptr ? options->card : config.card_path;
    config.connector_name = options->connector != nullptr ? options->connector : config.connector_name;
    config.console_fd = STDIN_FILENO;
    config.crash_test = (display_crash_test_t)options->crash_test;
    main_window_config(&config, options);
    const display_status_t status = display_init(&app->display, &config, display_device_real_ops(), nullptr);
    if (status != DISPLAY_STATUS_OK) {
        return main_exit_from_display(status);
    }
    /* Only now: reading /dev/input reads whoever is at the machine, and display_init said that this process holds the
     * console. Under X, in a pipe or under ctest, it has refused by this point. */
    if (!main_input_start(app)) {
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    display_test_pattern_set_cursor(&app->display, input_test_pattern_cursor, input_test_pattern_discard, &app->input);
    const display_status_t ran = display_test_pattern_run(&app->display, started_ns, DISPLAY_TEST_PATTERN_NS);
    input_report(&app->input);
    return main_exit_from_display(ran);
}

/* Replays input with no display at all, on the demo scene, which needs no card: see run/src/game/README.md. */
[[gnu::noinline]]
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
[[gnu::noinline]]
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
                                .god_rays = options->god_rays,
                                .full_detail = options->full_detail,
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
    const bool lost = game_gpu_lost(&app->game);
    game_close(&app->game);
    if (lost) {
        return MAIN_EXIT_GPU_LOST;
    }
    return ran ? MAIN_EXIT_OK : MAIN_EXIT_HEADLESS;
}

/* A generated world through KMS. The display comes first: under a display server it refuses before anything has been looked at
 * or started, and input only ever opens once it said yes. */
[[gnu::noinline]]
static main_exit_t main_run_test_world(app_t *app, const main_options_t *options) {
    display_config_t config;
    display_config_from_machine(&config);
    config.console_fd = STDIN_FILENO;
    main_window_config(&config, options);
    const display_status_t status = display_init(&app->display, &config, display_device_real_ops(), nullptr);
    if (status != DISPLAY_STATUS_OK) {
        return main_exit_from_display(status);
    }
    if (!main_input_start(app)) {
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    const display_buffer_t *const buffer = &app->display.flip.buffers[0];
    const game_config_t game = {.render_distance = options->render_distance != 0u ? options->render_distance
                                                                                  : MC_GAME_RENDER_DISTANCE_DEFAULT,
                                .width = buffer->width,
                                .height = buffer->height,
                                .features_off = options->features_off,
                                .god_rays = options->god_rays,
                                .full_detail = options->full_detail,
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
    main_exit_if_gpu_stuck(app);
    game_report(&app->game);
    const bool lost = game_gpu_lost(&app->game);
    game_close(&app->game);
    if (lost) {
        return MAIN_EXIT_GPU_LOST;
    }
    if (ran == DISPLAY_STATUS_OK && !loop_ok) {
        return MAIN_EXIT_RUNTIME;
    }
    return main_exit_from_display(ran);
}

/* What a world opens with from the menus. The size is the display's or the headless frame's. */
static game_config_t main_menu_game_config(const main_options_t *options, uint32_t width, uint32_t height) {
    return (game_config_t){.render_distance = options->render_distance != 0u ? options->render_distance
                                                                             : MC_GAME_RENDER_DISTANCE_DEFAULT,
                           .width = width,
                           .height = height,
                           .features_off = options->features_off,
                           .god_rays = options->god_rays,
                           .full_detail = options->full_detail,
                           ._pad0 = {0}};
}

/* The game through KMS. As with --test-world, the display comes first, and input only once it said yes. */
[[gnu::noinline]]
static main_exit_t main_run_menu(app_t *app, const main_options_t *options) {
    display_config_t config;
    display_config_from_machine(&config);
    config.console_fd = STDIN_FILENO;
    main_window_config(&config, options);
    const display_status_t status = display_init(&app->display, &config, display_device_real_ops(), nullptr);
    if (status != DISPLAY_STATUS_OK) {
        return main_exit_from_display(status);
    }
    if (!main_input_start(app)) {
        return MAIN_EXIT_RUNTIME; /* it has said why, and left nothing behind */
    }
    const display_buffer_t *const buffer = &app->display.flip.buffers[0];
    const game_config_t game = main_menu_game_config(options, buffer->width, buffer->height);
    /* A world made with no seed is a new one each time. Golden-ratio mixing, to spread what is only a clock. */
    const uint64_t blank_seed = (uint64_t)time_now_ns() * 0x9E3779B97F4A7C15ull;
    game_session_t session;
    game_session_init(&session, &app->game, &app->ui, &app->terrain, &app->input, &game,
                      options->saves != nullptr ? options->saves : MAIN_SAVES_DEFAULT, blank_seed);
    game_session_settings_set(&session, &app->settings);
    const display_status_t ran = game_session_run(&session, &app->display, game_session_source, &session);
    input_report(&app->input);
    main_exit_if_gpu_stuck(app);
    const bool failed = session.failed != 0u;
    game_session_deinit(&session);
    if (game_gpu_lost(&app->game)) {
        return MAIN_EXIT_GPU_LOST;
    }
    if (ran == DISPLAY_STATUS_OK && failed) {
        return MAIN_EXIT_RUNTIME;
    }
    return main_exit_from_display(ran);
}

/* The same with no display at all: a script clicks through the menus into PNGs. A blank seed is always the default one, so that
 * one script always gives one set of pictures. */
[[gnu::noinline]]
static main_exit_t main_run_menu_headless(app_t *app, const main_options_t *options) {
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
    const game_config_t game = main_menu_game_config(options, config.width, config.height);
    game_session_t session;
    game_session_init(&session, &app->game, &app->ui, &app->terrain, nullptr, &game,
                      options->saves != nullptr ? options->saves : MAIN_SAVES_DEFAULT, TERRAIN_SEED_DEFAULT);
    game_session_settings_set(&session, &app->settings);
    display_headless_scene_t scene;
    game_session_headless_scene(&session, &scene);
    display_headless_t *const headless = &app->offscreen.headless;
    const bool ran = display_headless_run(headless, &scene);
    LOG_INFO("headless: %" PRIu64 " frames, %" PRIu64 " screenshots", headless->frames_presented,
             headless->screenshots_written);
    const bool ended = game_session_end(&session);
    game_session_deinit(&session);
    if (game_gpu_lost(&app->game)) {
        return MAIN_EXIT_GPU_LOST;
    }
    if (!ended) {
        return MAIN_EXIT_RUNTIME;
    }
    return ran ? MAIN_EXIT_OK : MAIN_EXIT_HEADLESS;
}

/* Stage 02.3 asks the binary to report its defines, to show a yaml change reaching compiled code. This path must never
 * touch a display or an input device: ctest runs it. */
[[gnu::noinline]]
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

/* app_t is most of what a frame may hold, so what main calls keeps its locals in frames of its own. At -O3 with -flto, inlined here, they came to 16448 bytes against 16384. */
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
        profiler_deinit(&app.profiler); /* all else that failed has gone; this came first */
        return status;
    }
    if (options.report != 0) {
        main_report(&app);
    } else if (options.test_pattern != 0) {
        status = main_run_test_pattern(&app, &options, started_ns);
    } else if (options.menu != 0u) {
        status = options.headless != 0 ? main_run_menu_headless(&app, &options) : main_run_menu(&app, &options);
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
    if (options.pretend_leak == 0 && !app_deinit(&app, options.profile) && status == MAIN_EXIT_OK) {
        status = MAIN_EXIT_RUNTIME;
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
