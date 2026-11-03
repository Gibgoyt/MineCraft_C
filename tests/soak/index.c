/* The program soak. See README.md. */
#include "harness/index.h"

#include "file/index.h"
#include "game/index.h"
#include "gpu_bridge/index.h"
#include "heap/index.h"
#include "item/index.h"
#include "job/index.h"
#include "log/index.h"
#include "mob/index.h"
#include "physics/index.h"
#include "player/index.h"
#include "render/index.h"
#include "save/index.h"
#include "terrain/index.h"
#include "texture/index.h"
#include "thread/index.h"
#include "time/index.h"
#include "ui/menu/catalog/index.h"
#include "world/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum : uint64_t {
    /* It must end by itself under the timeout of the stage's own command, 14400 s, and be able to finish a session and look at what it left. */
    SOAK_SECONDS_DEFAULT = 14100u,
    SOAK_LOG_BYTES = 32ull << 20,
    SOAK_SCRIPT_BYTES = 1ull << 20,
    SOAK_MIB = 1ull << 20,
    /* A process may hold this much more at its most than the first of its kind did. Some growth has every right to be there: pages touched for the first time in
     * pools already reserved. What this is here to catch is a slope. */
    SOAK_GROWTH_ALLOWED_MIB = 256u,
    /* The hard rule for tests on this machine: the desktop shares the card. */
    SOAK_VRAM_LIMIT_MIB = 3900u,
    /* A session past its time by this share of the soak is stopped: 220 s of the default, inside its 300 s to the timeout. */
    SOAK_OVERRUN_ONE_IN = 64u,
};
enum : uint32_t {
    SOAK_PATH_BYTES = 1024u,
    SOAK_WIDTH = 640u,
    SOAK_HEIGHT = 480u,
    SOAK_RENDER_DISTANCE = 8u,
    SOAK_SESSION_SECONDS_LEAST = 60u,
    SOAK_ROUNDS_MOST = 4096u,
    SOAK_SEED = 424242u,
    /* World ticks between saves in the threaded part: far oftener than the 600 of a game, since saves are what is being soaked. */
    SOAK_AUTOSAVE_TICKS = 100u,
    SOAK_SAMPLE_MS = 500u,
    SOAK_VRAM_SAMPLE_SECONDS = 30u,
};

/* What one child came to, as seen from outside it. */
typedef struct soak_child_t {
    uint64_t rss_settled_mib;  /* 0: when a tenth of its time had gone */
    uint64_t rss_end_mib;      /* 8: the last sample */
    uint64_t rss_peak_mib;     /* 16 */
    uint64_t vram_peak_mib;    /* 24 */
    uint64_t seconds;          /* 32 */
    uint32_t fds_settled;      /* 40 */
    uint32_t fds_end;          /* 44 */
    uint32_t threads_settled;  /* 48 */
    uint32_t threads_end;      /* 52 */
    int32_t exit_code;         /* 56: 128 + signal if a signal ended it */
    uint8_t timed_out;         /* 60: past its limit, and killed by this */
    uint8_t killed_on_purpose; /* 61 */
    uint8_t _pad0[2];          /* 62 */
} soak_child_t;
static_assert(sizeof(soak_child_t) == 64);

typedef struct soak_t {
    test_context_t *borrowed_test; /* 0 */
    char *program;                 /* 8 */
    char *root;                    /* 16 */
    char *saves;                   /* 24 */
    char *world_directory;         /* 32 */
    char *threaded_world;          /* 40 */
    char *script;                  /* 48: SOAK_SCRIPT_BYTES of text */
    char *log_text;                /* 56: SOAK_LOG_BYTES */
    FILE *report;                  /* 64 */
    uint64_t failures;             /* 72 */
    uint64_t seconds;              /* 80 */
    uint64_t session_seconds;      /* 88 */
    uint64_t ticks_per_second;     /* 96: of a headless session, as last measured */
    uint64_t sessions;             /* 104 */
    uint64_t threaded_rss_first;   /* 112: at the end of the first threaded session that was given time to fill */
    uint64_t menu_rss_first;       /* 120 */
    uint64_t script_rng;           /* 128 */
    uint64_t vram_peak_mib;        /* 136 */
    uint32_t round;                /* 144 */
    uint8_t keep;                  /* 148: the directory stays, for looking at */
    uint8_t _pad0[3];              /* 149 */
    uint64_t reentries;            /* 152: sessions that went back to the title and into the world again */
    uint64_t kills;                /* 160: sessions killed once they had saved */
    uint64_t stop_seconds;         /* 168: from --stop-at; 0: as worked out from seconds */
} soak_t;
static_assert(sizeof(soak_t) == 176);

[[gnu::format(printf, 2, 3)]] static void soak_say(const soak_t *soak, const char *format, ...);

static void soak_say(const soak_t *soak, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    va_list second;
    va_copy(second, arguments);
    (void)vprintf(format, arguments);
    (void)printf("\n");
    (void)fflush(stdout);
    if (soak->report != nullptr) {
        (void)vfprintf(soak->report, format, second);
        (void)fprintf(soak->report, "\n");
        (void)fflush(soak->report);
    }
    va_end(second);
    va_end(arguments);
}

/* ---- Looking at a child from outside --------------------------------------------------------------------------------- */

/* A number after a name in /proc/<pid>/status, or 0. */
static uint64_t soak_status_number(const char *text, const char *name) {
    const char *const found = strstr(text, name);
    return found == nullptr ? 0u : strtoull(found + strlen(name), nullptr, 10);
}

static void soak_sample(pid_t pid, uint64_t *out_rss_mib, uint32_t *out_threads, uint32_t *out_fds) {
    char path[64];
    (void)snprintf(path, sizeof(path), "/proc/%" PRId32 "/status", (int32_t)pid);
    char text[8192];
    uint64_t bytes = 0u;
    *out_rss_mib = 0u;
    *out_threads = 0u;
    *out_fds = 0u;
    if (file_read_whole(path, text, sizeof(text) - 1u, &bytes) != FILE_OK) {
        return;
    }
    text[bytes] = '\0';
    *out_rss_mib = soak_status_number(text, "VmRSS:") / 1024u;
    *out_threads = (uint32_t)soak_status_number(text, "Threads:");
    (void)snprintf(path, sizeof(path), "/proc/%" PRId32 "/fd", (int32_t)pid);
    DIR *const handle = opendir(path);
    if (handle != nullptr) {
        uint32_t count = 0u;
        for (const struct dirent *entry = readdir(handle); entry != nullptr; entry = readdir(handle)) {
            count += entry->d_name[0] != '.' ? 1u : 0u;
        }
        (void)closedir(handle);
        *out_fds = count;
    }
}

/* What one process has on the card, in MiB, by asking the driver's own tool. 0 if there is no telling. */
static uint64_t soak_vram_of(pid_t pid) {
    FILE *const pipe =
        popen("nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits 2>/dev/null", "r");
    if (pipe == nullptr) {
        return 0u;
    }
    uint64_t mib = 0u;
    char line[256];
    while (fgets(line, sizeof(line), pipe) != nullptr) {
        char *comma = nullptr;
        const int64_t found = strtoll(line, &comma, 10);
        if (found == (int64_t)pid && comma != nullptr && *comma == ',') {
            mib = strtoull(comma + 1, nullptr, 10);
        }
    }
    (void)pclose(pipe);
    return mib;
}

/* ---- Starting, watching and ending a child ---------------------------------------------------------------------------- */

/* A child, in its own group, with its output in log_path. It dies with this, whatever happens to this. */
static pid_t soak_child_start(char *const *argv, const char *log_path) {
    (void)fflush(stdout);
    const pid_t child = fork();
    if (child == 0) {
        (void)setpgid(0, 0);
        (void)prctl(PR_SET_PDEATHSIG, SIGKILL);
        const int32_t nothing = open("/dev/null", O_RDONLY | O_CLOEXEC);
        const int32_t out = open(log_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (nothing < 0 || out < 0 || dup2(nothing, 0) < 0 || dup2(out, 1) < 0 || dup2(out, 2) < 0) {
            _exit(120);
        }
        (void)execv(argv[0], argv);
        _exit(121);
    }
    if (child > 0) {
        (void)setpgid(child, child);
    }
    return child;
}

/* True if this is what is being waited for: a save having been made since a child started. */
typedef bool (*soak_until_t)(const soak_t *soak);

/* Watches a child until it ends by itself, or until is true, which kills it, or past limit_seconds, which also kills it. */
static void soak_child_watch(soak_t *soak, pid_t child, uint64_t limit_seconds, uint64_t settle_seconds,
                             soak_until_t until, soak_child_t *out) {
    memset(out, 0, sizeof(*out));
    const int64_t started_ns = time_now_ns();
    int64_t next_vram_ns = started_ns + 10ll * TIME_NS_PER_SECOND;
    bool settled = false;
    bool killed = false;
    for (;;) {
        int32_t status = 0;
        const pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child) {
            out->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            break;
        }
        const int64_t now_ns = time_now_ns();
        const uint64_t passed = (uint64_t)((now_ns - started_ns) / TIME_NS_PER_SECOND);
        uint64_t rss = 0u;
        uint32_t threads = 0u;
        uint32_t fds = 0u;
        soak_sample(child, &rss, &threads, &fds);
        if (rss != 0u) {
            out->rss_end_mib = rss;
            out->threads_end = threads;
            out->fds_end = fds;
            out->rss_peak_mib = rss > out->rss_peak_mib ? rss : out->rss_peak_mib;
            if (!settled && passed >= settle_seconds) {
                settled = true;
                out->rss_settled_mib = rss;
                out->threads_settled = threads;
                out->fds_settled = fds;
            }
        }
        if (now_ns >= next_vram_ns) {
            next_vram_ns = now_ns + (int64_t)SOAK_VRAM_SAMPLE_SECONDS * TIME_NS_PER_SECOND;
            const uint64_t vram = soak_vram_of(child);
            out->vram_peak_mib = vram > out->vram_peak_mib ? vram : out->vram_peak_mib;
            soak->vram_peak_mib = vram > soak->vram_peak_mib ? vram : soak->vram_peak_mib;
            if (vram > SOAK_VRAM_LIMIT_MIB) {
                soak_say(soak, "soak: a child holds %" PRIu64 " MiB of the card, over the limit, and is stopped", vram);
                soak->failures++;
                (void)kill(-child, SIGKILL);
                killed = true;
            }
        }
        if (!killed && until != nullptr && until(soak)) {
            (void)kill(-child, SIGKILL);
            out->killed_on_purpose = 1u;
            killed = true;
        }
        if (!killed && passed > limit_seconds) {
            (void)kill(-child, SIGKILL);
            out->timed_out = 1u;
            killed = true;
        }
        time_sleep_ns((int64_t)SOAK_SAMPLE_MS * 1000000);
    }
    out->seconds = (uint64_t)((time_now_ns() - started_ns) / TIME_NS_PER_SECOND);
}

/* Reads what a child said, whole or the end of it. */
static const char *soak_log_read(const soak_t *soak, const char *log_path) {
    uint64_t bytes = 0u;
    const file_status_t status = file_read_whole(log_path, soak->log_text, SOAK_LOG_BYTES - 1u, &bytes);
    soak->log_text[status == FILE_OK ? bytes : 0u] = '\0';
    return soak->log_text;
}

/* A failure of a soak: it says so, with the end of what the child said, and goes on to the end if it can. */
static void soak_failed(soak_t *soak, const char *what, const char *log_text) {
    soak->failures++;
    const uint64_t length = strlen(log_text);
    const uint64_t shown = length > 8192u ? 8192u : length;
    soak_say(soak, "soak: FAILED in round %" PRIu32 ": %s\n---- the end of what it said ----\n%s\n---- end ----",
             soak->round, what, log_text + length - shown);
}

/* What every child is held to, whichever kind. */
static void soak_child_check(soak_t *soak, const char *kind, const soak_child_t *child, const char *log_text) {
    char what[256];
    if (child->timed_out != 0u) {
        (void)snprintf(what, sizeof(what), "%s ran past its limit, so it stuck", kind);
        soak_failed(soak, what, log_text);
    } else if (child->killed_on_purpose == 0u && child->exit_code != 0) {
        (void)snprintf(what, sizeof(what), "%s ended with status %" PRId32, kind, child->exit_code);
        soak_failed(soak, what, log_text);
    }
    static const char *const never[] = {"AddressSanitizer", "LeakSanitizer", "runtime error:", "SUMMARY:"};
    for (uint32_t i = 0u; i < sizeof(never) / sizeof(never[0]); i++) {
        if (strstr(log_text, never[i]) != nullptr) {
            (void)snprintf(what, sizeof(what), "%s said '%s'", kind, never[i]);
            soak_failed(soak, what, log_text);
        }
    }
    if (child->fds_end > child->fds_settled + 8u && child->fds_settled != 0u) {
        (void)snprintf(what, sizeof(what), "%s went from %" PRIu32 " descriptors to %" PRIu32, kind, child->fds_settled,
                       child->fds_end);
        soak_failed(soak, what, log_text);
    }
}

/* Against the first of its kind: a slope across a soak is the thing no short test can see. By the most each ever held, as the last sample can land anywhere in a shutdown:
 * measured 503 MiB of a session whose most was 1759, with three of its threads already gone. */
static void soak_growth_check(soak_t *soak, const char *kind, const soak_child_t *child, uint64_t *first_mib,
                              const char *log_text) {
    if (child->rss_peak_mib == 0u) {
        return;
    }
    if (*first_mib == 0u) {
        *first_mib = child->rss_peak_mib;
        return;
    }
    if (child->rss_peak_mib > *first_mib + SOAK_GROWTH_ALLOWED_MIB) {
        char what[256];
        (void)snprintf(what, sizeof(what),
                       "%s held up to %" PRIu64 " MiB, where the first of its kind held %" PRIu64 " MiB", kind,
                       child->rss_peak_mib, *first_mib);
        soak_failed(soak, what, log_text);
    }
}

static void soak_child_say(const soak_t *soak, const char *kind, const soak_child_t *child, const char *extra) {
    soak_say(soak,
             "soak: round %" PRIu32 " %s: %" PRIu64 " s, status %" PRId32 "%s; memory %" PRIu64 " MiB settled, %" PRIu64
             " at the end, %" PRIu64 " at most; %" PRIu64 " MiB on the card; threads %" PRIu32 " then %" PRIu32
             "; descriptors %" PRIu32 " then %" PRIu32 "%s",
             soak->round, kind, child->seconds, child->exit_code,
             child->killed_on_purpose != 0u ? " (killed, on purpose)" : "", child->rss_settled_mib, child->rss_end_mib,
             child->rss_peak_mib, child->vram_peak_mib, child->threads_settled, child->threads_end, child->fds_settled,
             child->fds_end, extra);
}

/* ---- The built program, through its menus ----------------------------------------------------------------------------- */

static uint64_t soak_next(soak_t *soak) {
    uint64_t x = soak->script_rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    soak->script_rng = x;
    return x;
}

/* Appends a line. Past the end of what a script may hold, it stops adding, and the script is just shorter. */
[[gnu::format(printf, 3, 4)]] static void soak_line(const soak_t *soak, uint64_t *at, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const uint64_t room = SOAK_SCRIPT_BYTES - *at;
    const int32_t written = vsnprintf(soak->script + *at, room, format, arguments);
    va_end(arguments);
    if (written > 0 && (uint64_t)written < room) {
        *at += (uint64_t)written;
    }
}

/* From the title screen into the first world on the list. At 640 by 480, scale 2: see tests/run/ui/menu/fixtures/flow.script for how these clicks were worked
 * out. The pointer starts in the middle. */
static void soak_script_enter(const soak_t *soak, uint64_t *at, int32_t move_x, int32_t move_y) {
    soak_line(soak, at, "+1 move %" PRId32 " %" PRId32 "\n", move_x, move_y);
    soak_line(soak, at, "+1 button left down\n+1 button left up\n");
    soak_line(soak, at, "+1 move -20 -122\n+1 button left down\n+1 button left up\n");
    soak_line(soak, at, "+1 move -112 318\n+1 button left down\n+1 button left up\n");
}

/* A person's worth of doing things: anything a tick can ask for that has a cost later. Returns the ticks it wrote. */
static uint64_t soak_script_play(soak_t *soak, uint64_t *at, uint64_t ticks, bool pauses) {
    uint64_t passed = 0u;
    static const char *const moves[] = {"w", "a", "s", "d"};
    while (passed < ticks) {
        const uint64_t gap = 10u + soak_next(soak) % 90u;
        uint64_t what = soak_next(soak) % 11u;
        /* A dead player's screen takes no Esc, so the second of a pair can start a pause that lasts to the end of the script, in which nothing changes and nothing
         * saves. A session that is waiting to save cannot have that. Measured: a whole session of 200000 ticks, in 114 s, at one generation. */
        what = what == 8u && !pauses ? 10u : what;
        switch (what) {
        case 0u:
        case 1u:
        case 2u: { /* a good long walk, sometimes running, which brings in new columns and lets go of old ones */
            const char *const key = moves[soak_next(soak) % 4u];
            const bool run = soak_next(soak) % 2u == 0u;
            soak_line(soak, at, "+1 key %s down\n", key);
            if (run) {
                soak_line(soak, at, "+0 key lctrl down\n");
            }
            soak_line(soak, at, "+%" PRIu64 " key %s up\n", gap * 4u, key);
            if (run) {
                soak_line(soak, at, "+0 key lctrl up\n");
            }
            passed += gap * 4u + 1u;
            break;
        }
        case 3u: /* looks about */
            soak_line(soak, at, "+%" PRIu64 " move %" PRId32 " %" PRId32 "\n", gap,
                      (int32_t)(soak_next(soak) % 800u) - 400, (int32_t)(soak_next(soak) % 400u) - 200);
            passed += gap;
            break;
        case 4u: /* looks somewhere, mostly down, and digs. Long enough for stone by hand, 150 ticks, or the first shaft to reach stone is the last block it ever breaks */
            soak_line(soak, at, "+1 move %" PRId32 " %" PRId32 "\n+1 button left down\n+%" PRIu64 " button left up\n",
                      (int32_t)(soak_next(soak) % 400u) - 200, (int32_t)(soak_next(soak) % 700u) - 200, gap * 3u);
            passed += gap * 3u + 2u;
            break;
        case 5u: /* puts what it has down */
            soak_line(soak, at, "+1 button right down\n+%" PRIu64 " button right up\n", gap / 4u + 1u);
            passed += gap / 4u + 2u;
            break;
        case 6u: /* jumps */
            soak_line(soak, at, "+1 key space down\n+%" PRIu64 " key space up\n", gap);
            passed += gap + 1u;
            break;
        case 7u: /* through the inventory */
            soak_line(soak, at, "+1 key e down\n+1 key e up\n+%" PRIu64 " key e down\n+1 key e up\n", gap);
            passed += gap + 3u;
            break;
        case 8u: /* a pause, and back by the same key */
            soak_line(soak, at,
                      "+1 key escape down\n+1 key escape up\n+%" PRIu64 " key escape down\n+1 key escape up\n", gap);
            passed += gap + 3u;
            break;
        case 9u: /* the overlay, and another hotbar slot */
            soak_line(soak, at, "+1 key f3 down\n+1 key f3 up\n+1 key %" PRIu64 " down\n+1 key %" PRIu64 " up\n",
                      soak_next(soak) % 9u + 1u, soak_next(soak) % 9u + 1u);
            passed += 4u;
            break;
        default: /* stands there, as things go on around it */
            soak_line(soak, at, "+%" PRIu64 " wheel 1\n", gap * 2u);
            passed += gap * 2u;
            break;
        }
    }
    return passed;
}

/* Back to the title and into the world again, in one process, which is the only time anything opens twice in one. The pointer is put in a known place by
 * running it into the corner. */
static void soak_script_reenter(const soak_t *soak, uint64_t *at) {
    soak_line(soak, at, "+1 key escape down\n+1 key escape up\n");
    soak_line(soak, at, "+1 move -5000 -5000\n+1 move 320 296\n");
    soak_line(soak, at, "+1 button left down\n+1 button left up\n");
    /* 320, 296 and then Singleplayer at 320, 200. */
    soak_script_enter(soak, at, 0, -96);
}

static uint64_t soak_manifest_generation(const char *world_directory) {
    char path[SOAK_PATH_BYTES + 32u];
    (void)snprintf(path, sizeof(path), "%s/blocks/manifest", world_directory);
    save_world_manifest_header_t header;
    file_t file = {.fd = -1, ._pad0 = {0}};
    if (file_open(&file, path, FILE_MODE_READ) != FILE_OK) {
        return 0u;
    }
    const file_status_t status = file_read_exact(&file, &header, sizeof(header));
    (void)file_close(&file);
    return status == FILE_OK ? header.generation : 0u;
}

/* The generation a killed session has to get past. */
static uint64_t soak_kill_after_generation;

/* A script is ticks, and the machine says how long those take, so a last session could run on past what its caller's timeout allows, which would lose all of the soak's
 * results to save one session's. At this it is stopped and goes unjudged. */
static int64_t soak_stop_ns;

/* And when to stop waiting for that. A world where nothing changes never saves: measured 634 s of a session that had nothing to do, which took a 1620 s soak to 1793 s. */
static int64_t soak_kill_give_up_ns;

static bool soak_saved_since(const soak_t *soak) {
    return soak_manifest_generation(soak->world_directory) > soak_kill_after_generation ||
           time_now_ns() > soak_kill_give_up_ns || time_now_ns() > soak_stop_ns;
}

static bool soak_out_of_time(const soak_t *soak) {
    (void)soak;
    return time_now_ns() > soak_stop_ns;
}

/* One run of the built program. kill_when_saved kills it once it has saved, which leaves its files as a power cut would. */
static void soak_menu_session(soak_t *soak, bool kill_when_saved) {
    /* A script is ticks, and the time they take is for the machine to say. A killed session is given more than it will use. */
    const uint64_t rate = soak->ticks_per_second != 0u ? soak->ticks_per_second : 10u;
    const uint64_t ticks = kill_when_saved ? 200000u : (soak->session_seconds * rate + 200u);
    uint64_t at = 0u;
    soak_script_enter(soak, &at, 0, -40);
    uint64_t passed = soak_script_play(soak, &at, ticks / 2u, !kill_when_saved);
    if (!kill_when_saved) {
        soak_script_reenter(soak, &at);
    }
    passed += soak_script_play(soak, &at, ticks - ticks / 2u, !kill_when_saved);
    soak_line(soak, &at, "+4 end\n");

    char script_path[SOAK_PATH_BYTES + 32u];
    char log_path[SOAK_PATH_BYTES + 32u];
    (void)snprintf(script_path, sizeof(script_path), "%s/session.script", soak->root);
    (void)snprintf(log_path, sizeof(log_path), "%s/session.log", soak->root);
    if (file_write_atomic(script_path, soak->script, at) != FILE_OK) {
        soak_failed(soak, "the script could not be written", "");
        return;
    }
    char distance[16];
    (void)snprintf(distance, sizeof(distance), "%" PRIu32, (uint32_t)SOAK_RENDER_DISTANCE);
    char width[16];
    char height[16];
    (void)snprintf(width, sizeof(width), "%" PRIu32, (uint32_t)SOAK_WIDTH);
    (void)snprintf(height, sizeof(height), "%" PRIu32, (uint32_t)SOAK_HEIGHT);
    /* Always --headless, which opens no card, console or input device. */
    char *const argv[] = {soak->program,
                          (char *)"--headless",
                          (char *)"--menu",
                          (char *)"--saves",
                          soak->saves,
                          (char *)"--script",
                          script_path,
                          (char *)"--screenshot-dir",
                          soak->root,
                          (char *)"--width",
                          width,
                          (char *)"--height",
                          height,
                          (char *)"--render-distance",
                          distance,
                          nullptr};
    soak_kill_after_generation = soak_manifest_generation(soak->world_directory);
    soak_kill_give_up_ns = time_now_ns() + (int64_t)soak->session_seconds * TIME_NS_PER_SECOND;
    const pid_t child = soak_child_start(argv, log_path);
    if (child < 0) {
        soak_failed(soak, "the program would not start", "");
        return;
    }
    soak_child_t watched;
    const uint64_t limit = soak->session_seconds * 4u + 300u;
    soak_child_watch(soak, child, limit, soak->session_seconds / 10u,
                     kill_when_saved ? soak_saved_since : soak_out_of_time, &watched);
    const char *const log_text = soak_log_read(soak, log_path);
    const char *const kind = kill_when_saved ? "killed session" : "session";
    soak_child_check(soak, kind, &watched, log_text);
    soak->sessions++;
    if (kill_when_saved) {
        /* Nothing changed means nothing to save, which can be so of a player who digs nothing: no fault of one session. The soak as a whole is held to it at the end. */
        soak->kills += soak_manifest_generation(soak->world_directory) > soak_kill_after_generation ? 1u : 0u;
        soak_child_say(soak, kind, &watched, "");
        return;
    }
    if (watched.killed_on_purpose != 0u) {
        soak_child_say(soak, kind, &watched, "; stopped at the end of the soak's time, and not judged");
        return;
    }
    if (watched.seconds != 0u) {
        const uint64_t measured = passed / watched.seconds;
        soak->ticks_per_second = measured != 0u ? measured : 1u;
    }
    /* That it did what its script says, and did not just run the clock out on a screen. */
    uint32_t opened = 0u;
    for (const char *found = strstr(log_text, "session: world '"); found != nullptr;
         found = strstr(found + 1, "session: world '")) {
        opened++;
    }
    /* A player can be dead when the script asks for the pause screen, and then the clicks fall on nothing and the session plays on. That is the script's blindness and
     * no fault, so one session is not held to it. The soak as a whole is, at its end. */
    if (opened == 0u) {
        soak_failed(soak, "a session never opened its world, so its clicks missed", log_text);
    }
    if (opened >= 2u && strstr(log_text, "session: back on the title screen") != nullptr) {
        soak->reentries++;
    }
    if (strstr(log_text, " 0 bytes live") == nullptr) {
        soak_failed(soak, "a session did not end with no heap live", log_text);
    }
    if (strstr(log_text, " 0 past reading") == nullptr) {
        soak_failed(soak, "a session read a damaged section of what it had saved", log_text);
    }
    char said[64];
    (void)snprintf(said, sizeof(said), "; opened %" PRIu32 " times", opened);
    soak_child_say(soak, kind, &watched, said);
    soak_growth_check(soak, "a session", &watched, &soak->menu_rss_first, log_text);
}

/* ---- What the world's files have come to ----------------------------------------------------------------------------- */

/* Every file a world holds, from this process, as a program that had just started would read them. */
static void soak_world_verify(soak_t *soak, save_t *save, const char *directory, uint64_t seed, uint16_t *states,
                              uint8_t *scratch) {
    const int32_t previous = log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
    const save_world_status_t status = save_open(save, directory, seed);
    if (status != SAVE_WORLD_OK) {
        (void)close(log_set_fd(previous));
        char what[256];
        (void)snprintf(what, sizeof(what), "the world is refused: %s", save_world_status_name(status));
        soak_failed(soak, what, "");
        return;
    }
    const save_world_t *const world = &save->world;
    const save_world_region_t *const regions = world->borrowed_manifest[world->current];
    const uint32_t region_count = world->region_count;
    uint64_t sections = 0u;
    for (uint32_t r = 0u; r < region_count; r++) {
        for (uint32_t local = 0u; local < SAVE_WORLD_REGION_COLUMNS; local++) {
            const int32_t chunk_x =
                regions[r].region_x * (int32_t)SAVE_WORLD_REGION_SIDE + (int32_t)(local % SAVE_WORLD_REGION_SIDE);
            const int32_t chunk_y =
                regions[r].region_y * (int32_t)SAVE_WORLD_REGION_SIDE + (int32_t)(local / SAVE_WORLD_REGION_SIDE);
            const uint64_t mask = save_world_column_overlay(&save->world, chunk_x, chunk_y, states, scratch);
            sections += (uint64_t)__builtin_popcountll(mask);
        }
    }
    const uint64_t damaged = atomic_load(&save->world.sections_damaged);
    const uint64_t generation = world->generation;
    const uint8_t state_kept = save->state.kept;
    save_close(save);
    (void)close(log_set_fd(previous));
    soak_say(soak,
             "soak: round %" PRIu32 " %s: at save %" PRIu64 ", %" PRIu32 " regions, %" PRIu64 " sections, %" PRIu64
             " damaged, state %s",
             soak->round, directory, generation, region_count, sections, damaged, state_kept != 0u ? "kept" : "none");
    if (damaged != 0u) {
        soak_failed(soak, "sections a save wrote are past reading", "");
    }
    if (generation != 0u && state_kept == 0u) {
        soak_failed(soak, "a world has blocks saved and no state to go with them", "");
    }
}

/* ---- The threaded game, in a child of this program ----------------------------------------------------------------------- */

typedef struct soak_rig_t {
    gpu_bridge_t *bridge;        /* 0 */
    job_system_t *job;           /* 8 */
    world_t *world;              /* 16 */
    terrain_t *terrain;          /* 24 */
    render_t *render;            /* 32 */
    texture_t *texture;          /* 40 */
    game_t *game;                /* 48 */
    save_t *save;                /* 56 */
    thread_topology_t *topology; /* 64 */
    physics_t *physics;          /* 72 */
    player_t *player;            /* 80 */
    item_t *item;                /* 88 */
    entity_t *entity;            /* 96 */
    mob_t *mob;                  /* 104 */
} soak_rig_t;
static_assert(sizeof(soak_rig_t) == 112);

static void *soak_aligned(test_context_t *t, uint64_t bytes) {
    const uintptr_t raw = (uintptr_t)test_alloc(t, bytes + 64u);
    return (void *)((raw + 63u) & ~(uintptr_t)63u);
}

/* All the threads of a real run, with nothing shown: sim, gpu and workers, saving all the time, with a body walking and digging. */
static void soak_threaded_child(test_context_t *t, const char *directory, uint64_t seconds) {
    soak_rig_t rig = {
        .bridge = soak_aligned(t, sizeof(gpu_bridge_t)),
        .job = soak_aligned(t, sizeof(job_system_t)),
        .world = soak_aligned(t, sizeof(world_t)),
        .terrain = soak_aligned(t, sizeof(terrain_t)),
        .render = soak_aligned(t, sizeof(render_t)),
        .texture = soak_aligned(t, sizeof(texture_t)),
        .game = soak_aligned(t, sizeof(game_t)),
        .save = soak_aligned(t, sizeof(save_t)),
        .topology = test_alloc(t, sizeof(thread_topology_t)),
        .physics = soak_aligned(t, sizeof(physics_t)),
        .player = soak_aligned(t, sizeof(player_t)),
        .item = soak_aligned(t, sizeof(item_t)),
        .entity = soak_aligned(t, sizeof(entity_t)),
        .mob = soak_aligned(t, sizeof(mob_t)),
    };
    REQUIRE(gpu_bridge_init(rig.bridge, gpu_bridge_limits_default()) == GPU_STATUS_OK);
    REQUIRE(thread_topology_read(rig.topology));
    REQUIRE(job_init(rig.job, rig.topology));
    REQUIRE(texture_init(rig.texture));
    REQUIRE(world_init(rig.world, world_limits_default()));
    REQUIRE(terrain_init(rig.terrain, SOAK_SEED));
    REQUIRE(render_init(rig.render, &rig.bridge->device_memory, rig.job, render_limits_default()));
    REQUIRE(save_init(rig.save));
    game_init(rig.game, rig.world, rig.terrain, rig.render, rig.job, rig.texture, rig.topology);
    game_save_set(rig.game, rig.save);
    REQUIRE(physics_init(rig.physics));
    player_init(rig.player, rig.physics);
    REQUIRE(item_init(rig.item));
    REQUIRE(entity_init(rig.entity, rig.physics));
    REQUIRE(mob_init(rig.mob, rig.job));
    game_player_set(rig.game, rig.player);
    game_item_set(rig.game, rig.item);
    game_entity_set(rig.game, rig.entity);
    game_mob_set(rig.game, rig.mob);

    REQUIRE(save_open(rig.save, directory, SOAK_SEED) == SAVE_WORLD_OK);
    const game_config_t config = {
        .render_distance = SOAK_RENDER_DISTANCE, .width = SOAK_WIDTH, .height = SOAK_HEIGHT, ._pad0 = {0}};
    REQUIRE(game_open(rig.game, &config));
    REQUIRE(game_settle(rig.game));
    save_autosave_interval_set(&rig.save->autosave, SOAK_AUTOSAVE_TICKS);

    const uint64_t screen_bytes = (uint64_t)SOAK_WIDTH * SOAK_HEIGHT * 4u;
    display_buffer_t target = {.owned_map = soak_aligned(t, screen_bytes),
                               .map_bytes = screen_bytes,
                               .pitch = SOAK_WIDTH * 4u,
                               .width = SOAK_WIDTH,
                               .height = SOAK_HEIGHT};
    REQUIRE(game_loop_start(rig.game, nullptr));

    /* The heap's calls are counted once all has come in. In play none should be made: see plans/CODING_STANDARD.md section 4. */
    heap_counters_t counters_settled = {0};
    bool counted = false;
    const int64_t started_ns = time_now_ns();
    const int64_t until_ns = started_ns + (int64_t)seconds * TIME_NS_PER_SECOND;
    int64_t last_ns = started_ns;
    uint64_t frames = 0u;
    /* Of what the world has come to, so that each round goes its own way, and a run can still be had again. */
    uint64_t rng = 0x9E3779B97F4A7C15ull ^ (rig.save->world.generation * 0xD1342543DE82EF95ull);
    render_fly_input_t input = {0};
    bool attack = false;
    bool place = false;
    bool jump = false;
    bool sneak = false;
    int64_t next_ns = 0;
    int8_t turning = 0;
    bool running = true;
    while (running && time_now_ns() < until_ns) {
        const int64_t now_ns = time_now_ns();
        if (!counted && now_ns - started_ns > (int64_t)seconds * TIME_NS_PER_SECOND / 5) {
            heap_counters_read(&counters_settled);
            counted = true;
        }
        /* A new thing to do every so often, by the clock: frames come by the hundred a second, and a block by hand takes seconds of looking at it. Measured, with a new
         * thing every 60 to 300 frames and the view turning in every one: 3 blocks dug in 60 s. Half of its time goes to digging, of which a turn only at the start. */
        input.mouse_dx = turning;
        input.mouse_dy = 0;
        if (now_ns >= next_ns) {
            rng ^= rng << 13;
            rng ^= rng >> 7;
            rng ^= rng << 17;
            const uint32_t what = (uint32_t)(rng % 8u);
            const bool digs = what < 4u;
            /* Stone by hand takes 7.5 s, so some of these get through it. */
            const int64_t millis = digs ? 2000 + (int64_t)((rng >> 8) % 10000u) : 500 + (int64_t)((rng >> 8) % 3500u);
            next_ns = now_ns + millis * 1000000;
            input.forward = digs ? 0 : (int8_t)((rng >> 24) % 3u) - 1;
            input.right = digs ? 0 : (int8_t)((rng >> 26) % 3u) - 1;
            input.fast = (uint8_t)((rng >> 28) % 2u);
            attack = digs;
            /* Puts back what it picked up, where it is looking. */
            place = what == 4u;
            jump = !digs && (rng >> 29) % 2u == 0u;
            sneak = !digs && (rng >> 30) % 8u == 0u;
            turning = digs ? 0 : (int8_t)((rng >> 33) % 5u) - 2;
            input.mouse_dx = (int64_t)((rng >> 36) % 801u) - 400;
            input.mouse_dy = (int64_t)((rng >> 46) % 701u) - 200;
        }
        game_walk_send(rig.game, &input, jump, sneak, now_ns);
        game_hands_send(rig.game, attack, place);
        const float32_t passed = (float32_t)time_ns_to_seconds(now_ns - last_ns);
        last_ns = now_ns;
        running = game_loop_frame(rig.game, &input, passed, &target);
        frames++;
        time_sleep_ns(2000000);
    }
    CHECK(running);
    heap_counters_t counters_end;
    heap_counters_read(&counters_end);
    const uint64_t pictures = rig.game->pictures_shown;
    CHECK(game_loop_stop(rig.game));
    CHECK(game_save_finish(rig.game) == SAVE_WORLD_OK);

    printf("threaded: %" PRIu64 " frames, %" PRIu64 " pictures in %" PRIu64 " s; %" PRIu64 " saves, %" PRIu64
           " failed; %" PRIu64 " sections written; %" PRIu64 " blocks dug; longest capture %.3f ms, write %.3f ms; "
           "heap calls in play: %" PRIu64 " reserves, %" PRIu64 " releases\n",
           frames, pictures, seconds, rig.save->autosave.saves_done, rig.save->autosave.saves_failed,
           rig.save->world.sections_written, rig.player->interaction.mined_count,
           (libc_double_t)time_ns_to_ms(rig.save->autosave.capture_ns_most),
           (libc_double_t)time_ns_to_ms(rig.save->autosave.write_ns_most),
           counters_end.reserve_calls - counters_settled.reserve_calls,
           counters_end.release_calls - counters_settled.release_calls);
    CHECK(frames > 10u);
    CHECK(pictures > 10u);
    CHECK(rig.save->autosave.saves_done >= 1u);
    CHECK_EQ_U64(rig.save->autosave.saves_failed, 0u);
    /* If it digs nothing the saves have nothing to do, and the soak of them is no soak. */
    CHECK(rig.player->interaction.mined_count >= 1u);
    /* Far from the only thing in play that might, and this is the one that no short run can see adding up. */
    CHECK_EQ_U64(counters_end.reserve_calls - counters_settled.reserve_calls, 0u);

    game_close(rig.game);
    save_close(rig.save);
    game_deinit(rig.game);
    save_deinit(rig.save);
    mob_deinit(rig.mob);
    entity_deinit(rig.entity);
    item_deinit(rig.item);
    player_deinit(rig.player);
    physics_deinit(rig.physics);
    render_deinit(rig.render);
    CHECK(gpu_bridge_exit_check(rig.bridge));
    terrain_deinit(rig.terrain);
    world_deinit(rig.world);
    texture_deinit(rig.texture);
    job_deinit(rig.job);
    gpu_bridge_deinit(rig.bridge);
    CHECK(heap_exit_check());
}

static void soak_threaded_round(soak_t *soak) {
    char seconds[32];
    char log_path[SOAK_PATH_BYTES + 32u];
    (void)snprintf(seconds, sizeof(seconds), "%" PRIu64, soak->session_seconds);
    (void)snprintf(log_path, sizeof(log_path), "%s/threaded.log", soak->root);
    char self[SOAK_PATH_BYTES];
    const int64_t self_bytes = readlink("/proc/self/exe", self, sizeof(self) - 1u);
    if (self_bytes <= 0) {
        soak_failed(soak, "this program cannot find itself", "");
        return;
    }
    self[self_bytes] = '\0';
    char *const argv[] = {self, (char *)"--threaded-child", soak->threaded_world, seconds, nullptr};
    const pid_t child = soak_child_start(argv, log_path);
    if (child < 0) {
        soak_failed(soak, "the threaded game would not start", "");
        return;
    }
    soak_child_t watched;
    soak_child_watch(soak, child, soak->session_seconds * 2u + 240u, soak->session_seconds / 5u, nullptr, &watched);
    const char *const log_text = soak_log_read(soak, log_path);
    soak_child_check(soak, "the threaded game", &watched, log_text);
    const char *const line = strstr(log_text, "threaded: ");
    char said[512] = "";
    if (line != nullptr) {
        (void)snprintf(said, sizeof(said), "; %.*s", (int32_t)strcspn(line, "\n"), line);
    } else {
        soak_failed(soak, "the threaded game did not say how it went", log_text);
    }
    soak_child_say(soak, "threaded", &watched, said);
    soak_growth_check(soak, "the threaded game", &watched, &soak->threaded_rss_first, log_text);
}

/* ---- The whole soak ---------------------------------------------------------------------------------------------------- */

static bool soak_arguments_read(test_context_t *t, soak_t *soak, const char **out_report_path) {
    soak->seconds = SOAK_SECONDS_DEFAULT;
    for (int32_t i = 0; i < t->arg_count; i += 2) {
        const char *const name = t->borrowed_args[i];
        char *const value = i + 1 < t->arg_count ? t->borrowed_args[i + 1] : nullptr;
        if (strcmp(name, "--keep") == 0) {
            soak->keep = 1u;
            i--;
        } else if (value == nullptr) {
            (void)printf("soak: %s needs a value\n", name);
            return false;
        } else if (strcmp(name, "--seconds") == 0) {
            soak->seconds = strtoull(value, nullptr, 10);
        } else if (strcmp(name, "--stop-at") == 0) {
            soak->stop_seconds = strtoull(value, nullptr, 10);
        } else if (strcmp(name, "--program") == 0) {
            soak->program = value;
        } else if (strcmp(name, "--report") == 0) {
            *out_report_path = value;
        } else {
            (void)printf("soak: no such argument as %s\n", name);
            return false;
        }
    }
    return true;
}

TEST(soak) {
    if (t->arg_count >= 3 && strcmp(t->borrowed_args[0], "--threaded-child") == 0) {
        soak_threaded_child(t, t->borrowed_args[1], strtoull(t->borrowed_args[2], nullptr, 10));
        return;
    }
    soak_t *const soak = test_alloc(t, sizeof(*soak));
    soak->borrowed_test = t;
    const char *report_path = nullptr;
    REQUIRE(soak_arguments_read(t, soak, &report_path));
    if (report_path != nullptr) {
        soak->report = fopen(report_path, "w");
        REQUIRE(soak->report != nullptr);
    }
    /* Beside this program, unless told. */
    if (soak->program == nullptr) {
        soak->program = test_alloc(t, SOAK_PATH_BYTES);
        const int64_t bytes = readlink("/proc/self/exe", soak->program, SOAK_PATH_BYTES - 32u);
        REQUIRE(bytes > 0);
        soak->program[bytes] = '\0';
        char *const slash = strrchr(soak->program, '/');
        REQUIRE(slash != nullptr);
        (void)snprintf(slash, 32u, "/minecraft_c");
    }
    REQUIRE(access(soak->program, X_OK) == 0);
    soak->root = test_alloc(t, SOAK_PATH_BYTES);
    (void)snprintf(soak->root, SOAK_PATH_BYTES, "/tmp/mc_soak_XXXXXX");
    REQUIRE(mkdtemp(soak->root) != nullptr);
    soak->saves = test_alloc(t, SOAK_PATH_BYTES);
    (void)snprintf(soak->saves, SOAK_PATH_BYTES, "%s/saves", soak->root);
    soak->threaded_world = test_alloc(t, SOAK_PATH_BYTES);
    (void)snprintf(soak->threaded_world, SOAK_PATH_BYTES, "%s/threaded", soak->root);
    REQUIRE(mkdir(soak->threaded_world, 0755) == 0);
    soak->script = test_alloc(t, SOAK_SCRIPT_BYTES);
    soak->log_text = test_alloc(t, SOAK_LOG_BYTES);
    soak->script_rng = 0x2545F4914F6CDD1Dull;
    soak->session_seconds = soak->seconds / 12u;
    if (soak->session_seconds < SOAK_SESSION_SECONDS_LEAST) {
        soak->session_seconds = SOAK_SESSION_SECONDS_LEAST;
    }

    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(soak->saves, "soak", TERRAIN_SEED_DEFAULT, &made) == FILE_OK);
    soak->world_directory = test_alloc(t, SOAK_PATH_BYTES);
    (void)snprintf(soak->world_directory, SOAK_PATH_BYTES, "%s/%s", soak->saves, made.directory);

    /* What this process itself uses to look at what those leave. */
    save_t *const save = soak_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));
    uint16_t *const states = test_alloc(t, (uint64_t)COLUMN_SECTIONS * SECTION_VOXELS * sizeof(uint16_t));
    uint8_t *const scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES);

    const int64_t started_ns = time_now_ns();
    const int64_t until_ns = started_ns + (int64_t)soak->seconds * TIME_NS_PER_SECOND;
    soak_stop_ns = until_ns + (int64_t)(soak->seconds / SOAK_OVERRUN_ONE_IN) * TIME_NS_PER_SECOND;
    if (soak->stop_seconds != 0u) {
        soak_stop_ns = started_ns + (int64_t)soak->stop_seconds * TIME_NS_PER_SECOND;
    }
    soak_say(soak, "soak: %" PRIu64 " s, sessions of %" PRIu64 " s, in %s, of %s", soak->seconds, soak->session_seconds,
             soak->root, soak->program);
    /* Stops before starting what would take it past its time: a round is two sessions, a look, and now and then a third that is killed. */
    const int64_t round_ns = 3 * (int64_t)soak->session_seconds * TIME_NS_PER_SECOND;
    while (soak->round < SOAK_ROUNDS_MOST && soak->failures == 0u && time_now_ns() + round_ns < until_ns) {
        soak->round++;
        soak_threaded_round(soak);
        soak_world_verify(soak, save, soak->threaded_world, SOAK_SEED, states, scratch);
        soak_menu_session(soak, false);
        soak_world_verify(soak, save, soak->world_directory, TERRAIN_SEED_DEFAULT, states, scratch);
        /* Killed in play now and then, and the next session goes on from what that left. */
        if (soak->round % 2u == 1u && soak->failures == 0u && time_now_ns() < soak_stop_ns) {
            soak_menu_session(soak, true);
            soak_world_verify(soak, save, soak->world_directory, TERRAIN_SEED_DEFAULT, states, scratch);
        }
    }
    save_deinit(save);
    /* Enough sessions that some player was alive at the moment, or the way back to the title is broken. */
    if (soak->sessions >= 6u && soak->reentries == 0u) {
        soak_failed(soak, "no session in all of them got back to the title and into the world again", "");
    }
    if (soak->round >= 3u && soak->kills == 0u) {
        soak_failed(soak, "no session ever saved so that it could be killed", "");
    }
    const uint64_t took = (uint64_t)((time_now_ns() - started_ns) / TIME_NS_PER_SECOND);
    soak_say(soak,
             "soak: %s: %" PRIu32 " rounds, %" PRIu64 " program runs, %" PRIu64
             " of them back through the title and %" PRIu64 " killed, in %" PRIu64 " s; %" PRIu64
             " failures; at most %" PRIu64 " MiB on the card",
             soak->failures == 0u ? "PASSED" : "FAILED", soak->round, soak->sessions, soak->reentries, soak->kills,
             took, soak->failures, soak->vram_peak_mib);
    CHECK_EQ_U64(soak->failures, 0u);
    CHECK(soak->round >= 1u);
    if (soak->report != nullptr) {
        (void)fclose(soak->report);
    }
    /* Kept if it failed, as what is in there is what a person would want. */
    if (soak->failures == 0u && soak->keep == 0u) {
        char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", soak->root, nullptr};
        test_process_t process;
        test_process_run(t, argv, 60000u, &process);
        CHECK_EQ_I64(process.exit_code, 0);
    } else {
        (void)printf("soak: its files are in %s\n", soak->root);
    }
}

TEST_MAIN()
