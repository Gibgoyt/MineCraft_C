# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 2: KMS flip spike

**Status:** in progress: written, tested against a fake card, and never run on real hardware; awaits Ahmed's run below. On the integration branch `worktree-wf_a4b549de-085-20`   (updated 2026-11-02)

## Goal

We know whether dumb buffers, modeset and page-flip events work on `nvidia-drm`, with X on another VT and, if that fails, with X stopped by Ahmed. Pass: solid-color flips at 120 Hz under `timeout 10`, with the console restored.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/kms_flip/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Write a raw-ioctl spike in `/tmp/mc_c_spikes/kms_flip/` using only kernel UAPI headers: open `/dev/dri/card1`, pick a connector and mode, create dumb buffers, modeset, and flip solid colors
- [ ] Measure the flip interval from page-flip events, and restore the console on exit
- [x] Give Ahmed the exact command to run with X on another VT
- [ ] If that fails, ask Ahmed to stop X and run it again; never stop X yourself
- [ ] Record results, errors and flip timings in this file

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    timeout 10 /tmp/mc_c_spikes/kms_flip/kms_flip
    pgrep -a kms_flip    # must print nothing

## How Ahmed runs it

The first box above means written, compiled and tested against a fake card only. The boxes left open need a real run, which only Ahmed can do.

If `/tmp` was cleared, rebuild from the copy of the source below. From the main checkout:

    mkdir -p /tmp/mc_c_spikes/kms_flip
    awk '/^```c$/{f=1;next} /^```$/{f=0} f' plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-2-kms-flip.md > /tmp/mc_c_spikes/kms_flip/index.c
    gcc -std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion -Wshadow -Wvla -Wpadded -Werror -march=skylake -mtune=skylake -O2 -fno-tree-vectorize -fno-tree-slp-vectorize -o /tmp/mc_c_spikes/kms_flip/kms_flip /tmp/mc_c_spikes/kms_flip/index.c

**Run 1, with X on another VT.** Press Ctrl+Alt+F3, log in, then:

    timeout -k 3 10 /tmp/mc_c_spikes/kms_flip/kms_flip; echo "exit=$?"
    pgrep -a kms_flip    # must print nothing
    cat /tmp/mc_c_spikes/kms_flip/last_result.txt

What you should see: red, green, blue and grey, changing every half second, on one monitor, until 8 seconds after the command started. Then the text console comes back and the report prints. Do not switch VT during that time. Add `--connector HDMI-A-1` or `--connector eDP-1` to choose the output; by default it takes the fastest one at 1920x1080.

| Exit | Meaning | Next step |
|---|---|---|
| 0 | Target met | Check by eye that the console came back |
| 1 | It ran but missed the target, or a restore ioctl failed | Read the `MISS` or `FAIL` lines. Below 119 Hz is the monitor's mode, not a driver failure |
| 2 | Refused: wrong tty, or another client owns the card. The display was not changed | Run 2 if it names the card or master |
| 3 | A setup ioctl failed. This is the result the spike exists to find | Record the `ERROR` line |
| 124 or 137 | It did not stop by itself, which is a spike bug | Record it. If the screen stays dark, Ctrl+Alt+F1 or F2 goes back to X |

**Run 2, only if run 1 refuses because of the card.** Stop X yourself, in whatever way you normally stop it (`sudo systemctl stop display-manager` if you use a display manager), repeat the commands of run 1, then start X again. Agents never do this.

Paste the report into **Results** below and tick the remaining boxes.

## Results

No run on hardware yet. Nothing about real flips, timings or console restore has been observed.

Tested against a fake card, 2026-11-01, gate check. `/tmp/mc_c_spikes/kms_flip/test_shim/` is an `LD_PRELOAD` shim that answers the DRM and VT ioctls and produces flip events on a clock; `run.sh` runs 14 cases with `--card /dev/dri/fake_card`, which does not exist, and stdin `/dev/null`, so nothing real could be touched even if the shim failed to load. Result: `failed cases: 0`, `pgrep -a kms_flip` rc 1.

| Case | Exit | What it proves |
|---|---|---|
| stdin not a console; X owns the VT; another client is master | 2 | Refused, with `REFUSED` text, and nothing changed |
| no dumb buffers; ADDFB2 fails | 3 | `ERROR during setup`; buffers already made are destroyed |
| normal 120 Hz | 0 | `VERDICT: PASS` with the restore complete |
| 2.5 s modeset | 0 | Passes under `timeout 10`; this failed before the fix, see the issue below |
| 5.5 s modeset | 1 | Too few intervals gives a stated `MISS` |
| 60 Hz mode; flips really at 100 Hz on a 120 Hz mode | 1 | `VERDICT: FAIL` for the right reason, so the verdict can fail |
| PAGE_FLIP error; event never arrives | 1 | Stops, reports, restores |
| SIGTERM | 124 | From `timeout`; the program reported `stopped early` and restored |
| SIGSEGV inside a flip | 139 | The handler restored, then it died of the signal |

On every path that reached `KD_GRAPHICS`, `run.sh` checks the order `SETCRTC` back to the saved framebuffer, then `DROP_MASTER`, then `KD_TEXT`. This proves the program's logic only. Whether `nvidia-drm` accepts these calls, and whether fbcon really comes back, is what Ahmed's run is for.

The refusal was also run for real, with no shim, on the built binary: `timeout 10 kms_flip --card /nonexistent/card </dev/null` prints `REFUSED: stdin is not a Linux virtual console` and exits 2.

Checked without running it, 2026-11-01:

- The strict build above exits 0 on gcc 16.2.1, with `-Wpadded -Werror`. Adding `-fanalyzer` also exited 0 with no diagnostics, on the source before the gate check's fix. The gate check repeated the strict build after the fix, not the analyzer.
- `readelf -d` shows `libc.so.6` as the only `NEEDED` library. `nm -Du` lists only glibc symbols.
- Grepping the source for banned keywords matches only the word "long" inside one comment.
- `pgrep -a kms_flip` printed nothing after the build (rc 1). The first author never executed the binary; the gate check ran it as described above, never against a real card.
- `/dev/dri/card*` was not opened. Display facts come from sysfs reads only.

## Spike source

Copied from `/tmp/mc_c_spikes/kms_flip/index.c` so that it survives `/tmp` being cleared.

```c
/*
 * Stage 01.2 KMS flip spike. Throwaway, raw ioctls and kernel UAPI headers only (no libdrm).
 * It answers one question: do dumb buffers, modeset and page-flip events work on nvidia-drm?
 *
 * Build:
 *   gcc -std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion \
 *       -Wshadow -Wvla -Wpadded -Werror -march=skylake -mtune=skylake -O2 \
 *       -fno-tree-vectorize -fno-tree-slp-vectorize \
 *       -o /tmp/mc_c_spikes/kms_flip/kms_flip /tmp/mc_c_spikes/kms_flip/index.c
 * Run (only by Ahmed, on a text console, never under X):
 *   timeout -k 3 10 /tmp/mc_c_spikes/kms_flip/kms_flip [--card PATH] [--connector NAME]
 *
 * Exit codes: 0 pass; 1 ran but missed the target, or the restore failed; 2 refused, nothing was
 * changed on the display; 3 a setup ioctl failed. The report goes to stdout and last_result.txt.
 *
 * Why the color changes only every half second: flipping between two different full-screen colors
 * at 120 Hz strobes hard whenever a frame is dropped. Every flip is still a real flip between two
 * buffers; both simply show the same color for 60 flips, which is also easy to confirm by eye.
 */
#define _GNU_SOURCE
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

enum {
    KMS_EXIT_PASS = 0,
    KMS_EXIT_MISSED = 1,
    KMS_EXIT_REFUSED = 2,
    KMS_EXIT_ERROR = 3,
};

#define KMS_MAX_CRTCS 16u
#define KMS_MAX_CONNECTORS 16u
#define KMS_MAX_ENCODERS 32u
#define KMS_MAX_CONNECTOR_ENCODERS 16u
#define KMS_MAX_MODES 256u
#define KMS_MAX_INTERVALS 8192u
#define KMS_TTY_MAJOR 4u
#define KMS_TTY_MINOR_MAX 63u
#define KMS_CONNECTION_CONNECTED 1u
#define KMS_RUN_NS 8000000000ull        /* From process start, below `timeout 10`: a slow modeset must not reach the timeout. */
#define KMS_FLIP_STALL_NS 1000000000ull /* No event for this long means flips have stopped. */
#define KMS_DRAIN_NS 200000000ull
#define KMS_POLL_MS 100
#define KMS_FLIPS_PER_COLOR 60u
#define KMS_MIN_INTERVALS 360u /* Three seconds at 120 Hz: a run that ended sooner proves too little. */
#define KMS_WARMUP_EVENTS 8u /* The first events follow the modeset and are not representative. */
#define KMS_VT_CHECK_EVENTS 120u
#define KMS_TARGET_MIN_MHZ 119000ull /* A "120 Hz" mode may really be 119.88 Hz. */
#define KMS_NS_PER_SEC 1000000000ull
#define KMS_LOG_BYTES 16384u
#define KMS_RESULT_PATH "/tmp/mc_c_spikes/kms_flip/last_result.txt"

typedef struct kms_buffer_t {
    uint8_t *owned_map;
    uint64_t map_size;
    uint32_t handle;
    uint32_t fb_id;
    uint32_t pitch;
    uint32_t color;
} kms_buffer_t;
static_assert(sizeof(kms_buffer_t) == 32);

typedef struct kms_connector_t {
    uint64_t best_mhz;
    struct drm_mode_modeinfo best_mode;
    uint32_t connector_id;
    uint32_t encoder_id;
    uint32_t type;
    uint32_t type_id;
    uint32_t count_encoders;
    uint32_t tier;
    uint32_t encoders[KMS_MAX_CONNECTOR_ENCODERS];
    uint8_t _pad0[4];
} kms_connector_t;
static_assert(sizeof(kms_connector_t) == 168);

typedef struct kms_stats_t {
    uint64_t count;
    uint64_t sum_ns;
    uint64_t min_ns;
    uint64_t p50_ns;
    uint64_t p99_ns;
    uint64_t max_ns;
    uint64_t late;
} kms_stats_t;
static_assert(sizeof(kms_stats_t) == 56);

/* Lives on main()'s stack. The maps and fds are released by kms_cleanup(). */
typedef struct kms_flip_t {
    kms_connector_t connectors[KMS_MAX_CONNECTORS];
    kms_buffer_t buffers[2];
    struct drm_mode_crtc saved_crtc;
    uint64_t kernel_interval_ns[KMS_MAX_INTERVALS];
    uint64_t wake_interval_ns[KMS_MAX_INTERVALS];
    uint64_t process_start_ns;
    uint64_t period_ns;
    uint64_t refresh_mhz;
    uint64_t fill_max_ns;
    uint64_t fill_count;
    uint64_t flips_submitted;
    uint64_t events_received;
    uint64_t sequence_gaps;
    uint64_t last_kernel_ns;
    uint64_t last_wake_ns;
    uint64_t last_event_ns;
    uint64_t interval_count;
    uint64_t log_used;
    const char *card_path;
    const char *connector_name;
    int32_t card_fd;
    int32_t exit_code;
    uint32_t tty_minor;
    uint32_t crtc_id;
    uint32_t connector_id;
    uint32_t connector_count;
    uint32_t saved_connector_count;
    uint32_t saved_connectors[KMS_MAX_CONNECTORS];
    uint32_t last_sequence;
    uint32_t front;
    struct drm_mode_modeinfo mode;
    uint8_t buffers_made;
    uint8_t is_master;
    uint8_t kd_graphics;
    uint8_t crtc_saved;
    uint8_t modeset_done;
    uint8_t flip_pending;
    uint8_t kernel_clock_monotonic;
    uint8_t loop_failed;
    uint8_t restore_failed;
    uint8_t restored;
    uint8_t _pad0[6];
    char log_text[KMS_LOG_BYTES];
} kms_flip_t;
static_assert(sizeof(kms_flip_t) == 150616);

/* Borrowed: a signal handler has no other way in, and main() outlives every signal it handles. */
static kms_flip_t *borrowed_crash_state;
static volatile sig_atomic_t stop_signal;

static uint64_t kms_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * KMS_NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

[[gnu::format(printf, 2, 3)]]
static void kms_log(kms_flip_t *s, const char *format, ...) {
    uint64_t room = KMS_LOG_BYTES - s->log_used;
    if (room <= 1u) {
        return;
    }
    va_list args;
    va_start(args, format);
    int32_t wrote = vsnprintf(s->log_text + s->log_used, room, format, args);
    va_end(args);
    if (wrote > 0) {
        uint64_t used = (uint64_t)wrote;
        s->log_used += used < room ? used : room - 1u;
    }
}

/* Retries interruptions only. EBUSY and every real error go back to the caller. */
static int32_t kms_ioctl(int32_t fd, uint64_t request, void *argument) {
    int32_t rc;
    do {
        rc = ioctl(fd, request, argument);
    } while (rc == -1 && (errno == EINTR || errno == EAGAIN));
    return rc;
}

static bool kms_ioctl_logged(kms_flip_t *s, uint64_t request, void *argument, const char *name) {
    if (kms_ioctl(s->card_fd, request, argument) == 0) {
        return true;
    }
    int32_t saved_errno = errno;
    kms_log(s, "ERROR %s: errno %d (%s)\n", name, saved_errno, strerror(saved_errno));
    return false;
}

static const char *kms_connector_type_name(uint32_t type) {
    switch (type) {
    case DRM_MODE_CONNECTOR_VGA: return "VGA";
    case DRM_MODE_CONNECTOR_DVII: return "DVI-I";
    case DRM_MODE_CONNECTOR_DVID: return "DVI-D";
    case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
    case DRM_MODE_CONNECTOR_HDMIA: return "HDMI-A";
    case DRM_MODE_CONNECTOR_HDMIB: return "HDMI-B";
    case DRM_MODE_CONNECTOR_eDP: return "eDP";
    default: return "other";
    }
}

/* Returns the real refresh in millihertz, computed from timings instead of the rounded vrefresh. */
static uint64_t kms_mode_mhz(const struct drm_mode_modeinfo *mode) {
    uint64_t pixels = (uint64_t)mode->htotal * (uint64_t)mode->vtotal;
    if ((mode->flags & DRM_MODE_FLAG_DBLSCAN) != 0u) {
        pixels *= 2u;
    }
    if (mode->vscan > 1u) {
        pixels *= mode->vscan;
    }
    if (pixels == 0u || mode->clock == 0u) {
        return 0u;
    }
    return (uint64_t)mode->clock * 1000000ull / pixels;
}

/* Must run before the card is opened: nothing here can affect the display or a display server. */
static bool kms_guard_console(kms_flip_t *s) {
    struct stat tty_stat;
    if (fstat(STDIN_FILENO, &tty_stat) != 0 || !S_ISCHR(tty_stat.st_mode) ||
        major(tty_stat.st_rdev) != KMS_TTY_MAJOR || minor(tty_stat.st_rdev) == 0u ||
        minor(tty_stat.st_rdev) > KMS_TTY_MINOR_MAX) {
        kms_log(s, "REFUSED: stdin is not a Linux virtual console, so the display is not ours to touch.\n"
                   "Press Ctrl+Alt+F3, log in on the text console and run it there.\n");
        return false;
    }
    s->tty_minor = minor(tty_stat.st_rdev);

    struct vt_stat vt_state;
    if (ioctl(STDIN_FILENO, VT_GETSTATE, &vt_state) != 0 || (uint32_t)vt_state.v_active != s->tty_minor) {
        kms_log(s, "REFUSED: tty%u is not the visible virtual console.\n", s->tty_minor);
        return false;
    }

    int32_t kd_mode = -1;
    if (ioctl(STDIN_FILENO, KDGETMODE, &kd_mode) != 0 || kd_mode != KD_TEXT) {
        kms_log(s, "REFUSED: tty%u is not in text mode (a display server appears to own it).\n", s->tty_minor);
        return false;
    }
    return true;
}

static bool kms_open_and_take_master(kms_flip_t *s) {
    s->card_fd = open(s->card_path, O_RDWR | O_CLOEXEC);
    if (s->card_fd < 0) {
        int32_t saved_errno = errno;
        kms_log(s, "REFUSED: open %s: errno %d (%s)\n", s->card_path, saved_errno, strerror(saved_errno));
        if (saved_errno == EINVAL || saved_errno == EBUSY) {
            kms_log(s, "A display server probably still owns the card. X may hold it even from another VT;\n"
                       "this is the case where Ahmed stops X by hand and runs the spike again.\n");
        } else if (saved_errno == EACCES) {
            kms_log(s, "No access: log in on the text console (logind grants the ACL) or join group video.\n");
        }
        return false;
    }
    if (kms_ioctl(s->card_fd, DRM_IOCTL_SET_MASTER, nullptr) != 0) {
        int32_t saved_errno = errno;
        kms_log(s, "REFUSED: DRM_IOCTL_SET_MASTER: errno %d (%s)\n"
                   "A display server is DRM master of %s. Nothing was changed on the display.\n"
                   "If X is on another VT and this persists, Ahmed stops X by hand and runs it again.\n",
                saved_errno, strerror(saved_errno), s->card_path);
        return false;
    }
    s->is_master = 1u;
    return true;
}

static bool kms_query_card(kms_flip_t *s) {
    char driver_name[64] = {};
    struct drm_version version = {};
    version.name = driver_name;
    version.name_len = sizeof(driver_name) - 1u;
    if (!kms_ioctl_logged(s, DRM_IOCTL_VERSION, &version, "DRM_IOCTL_VERSION")) {
        return false;
    }
    kms_log(s, "driver: %s %d.%d.%d, card %s, tty%u\n", driver_name, version.version_major, version.version_minor,
            version.version_patchlevel, s->card_path, s->tty_minor);

    struct drm_get_cap cap = {.capability = DRM_CAP_DUMB_BUFFER};
    if (!kms_ioctl_logged(s, DRM_IOCTL_GET_CAP, &cap, "GET_CAP(DUMB_BUFFER)")) {
        return false;
    }
    kms_log(s, "cap DUMB_BUFFER=%llu\n", cap.value);
    if (cap.value == 0u) {
        kms_log(s, "ERROR the driver offers no dumb buffers.\n");
        return false;
    }
    cap = (struct drm_get_cap){.capability = DRM_CAP_TIMESTAMP_MONOTONIC};
    if (kms_ioctl_logged(s, DRM_IOCTL_GET_CAP, &cap, "GET_CAP(TIMESTAMP_MONOTONIC)")) {
        s->kernel_clock_monotonic = (uint8_t)(cap.value != 0u);
        kms_log(s, "cap TIMESTAMP_MONOTONIC=%llu\n", cap.value);
    }
    return true;
}

/* Higher is better: 1080p at 120 Hz is the plan's target, and a fast mode beats a slow exact one. */
static void kms_score_modes(kms_connector_t *connector, const struct drm_mode_modeinfo *modes, uint32_t count) {
    uint64_t best_area = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        const struct drm_mode_modeinfo *mode = &modes[i];
        if ((mode->flags & DRM_MODE_FLAG_INTERLACE) != 0u) {
            continue;
        }
        uint64_t mhz = kms_mode_mhz(mode);
        uint64_t area = (uint64_t)mode->hdisplay * (uint64_t)mode->vdisplay;
        bool is_1080p = mode->hdisplay == 1920u && mode->vdisplay == 1080u;
        bool is_fast = mhz >= KMS_TARGET_MIN_MHZ;
        uint32_t tier = is_1080p ? (is_fast ? 4u : 2u) : (is_fast ? 3u : 1u);
        bool better = tier > connector->tier || (tier == connector->tier && mhz > connector->best_mhz) ||
                      (tier == connector->tier && mhz == connector->best_mhz && area > best_area);
        if (better) {
            connector->tier = tier;
            connector->best_mhz = mhz;
            connector->best_mode = *mode;
            best_area = area;
        }
    }
}

static bool kms_query_connector(kms_flip_t *s, uint32_t connector_id, struct drm_mode_modeinfo *modes) {
    uint32_t encoders[KMS_MAX_CONNECTOR_ENCODERS] = {};
    struct drm_mode_get_connector info = {.connector_id = connector_id};
    /* count_modes == 0 asks the kernel to probe. It needs the second call to copy anything. */
    if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_GETCONNECTOR, &info, "GETCONNECTOR(count)")) {
        return false;
    }
    if (info.connection != KMS_CONNECTION_CONNECTED || info.count_modes == 0u) {
        return true;
    }
    if (info.count_modes > KMS_MAX_MODES || info.count_encoders > KMS_MAX_CONNECTOR_ENCODERS) {
        kms_log(s, "ERROR connector %u has %u modes and %u encoders, beyond the spike's fixed arrays.\n", connector_id,
                info.count_modes, info.count_encoders);
        return false;
    }
    info.modes_ptr = (uint64_t)(uintptr_t)modes;
    info.encoders_ptr = (uint64_t)(uintptr_t)encoders;
    info.count_props = 0u;
    if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_GETCONNECTOR, &info, "GETCONNECTOR(fill)")) {
        return false;
    }
    if (info.count_modes > KMS_MAX_MODES || info.count_encoders > KMS_MAX_CONNECTOR_ENCODERS ||
        s->connector_count == KMS_MAX_CONNECTORS) {
        kms_log(s, "ERROR connector %u changed between the two GETCONNECTOR calls.\n", connector_id);
        return false;
    }

    kms_connector_t *connector = &s->connectors[s->connector_count];
    memset(connector, 0, sizeof(*connector));
    connector->connector_id = connector_id;
    connector->encoder_id = info.encoder_id;
    connector->type = info.connector_type;
    connector->type_id = info.connector_type_id;
    connector->count_encoders = info.count_encoders;
    memcpy(connector->encoders, encoders, sizeof(encoders));
    kms_score_modes(connector, modes, info.count_modes);
    if (connector->best_mhz != 0u) {
        s->connector_count++;
    }
    return true;
}

static bool kms_connector_named(const kms_connector_t *connector, const char *name) {
    char connector_name[32];
    snprintf(connector_name, sizeof(connector_name), "%s-%u", kms_connector_type_name(connector->type),
             connector->type_id);
    return strcmp(connector_name, name) == 0;
}

static const kms_connector_t *kms_pick_connector(kms_flip_t *s) {
    const kms_connector_t *picked = nullptr;
    for (uint32_t i = 0u; i < s->connector_count; i++) {
        const kms_connector_t *connector = &s->connectors[i];
        kms_log(s, "connected: %s-%u best mode %ux%u at %" PRIu64 ".%03" PRIu64 " Hz\n",
                kms_connector_type_name(connector->type), connector->type_id, connector->best_mode.hdisplay,
                connector->best_mode.vdisplay, connector->best_mhz / 1000u, connector->best_mhz % 1000u);
        if (s->connector_name != nullptr) {
            if (kms_connector_named(connector, s->connector_name)) {
                picked = connector;
            }
        } else if (picked == nullptr || connector->tier > picked->tier ||
                   (connector->tier == picked->tier && connector->best_mhz > picked->best_mhz)) {
            picked = connector;
        }
    }
    return picked;
}

/* Reuses the CRTC already driving the connector, so the restore puts back exactly what was there. */
static uint32_t kms_pick_crtc(kms_flip_t *s, const kms_connector_t *connector, const uint32_t *crtcs,
                              uint32_t count_crtcs) {
    if (connector->encoder_id != 0u) {
        struct drm_mode_get_encoder encoder = {.encoder_id = connector->encoder_id};
        if (kms_ioctl_logged(s, DRM_IOCTL_MODE_GETENCODER, &encoder, "GETENCODER(current)") && encoder.crtc_id != 0u) {
            return encoder.crtc_id;
        }
    }
    uint32_t first_possible = 0u;
    for (uint32_t e = 0u; e < connector->count_encoders; e++) {
        struct drm_mode_get_encoder encoder = {.encoder_id = connector->encoders[e]};
        if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_GETENCODER, &encoder, "GETENCODER(possible)")) {
            continue;
        }
        for (uint32_t c = 0u; c < count_crtcs; c++) {
            if ((encoder.possible_crtcs & (1u << c)) == 0u) {
                continue;
            }
            if (first_possible == 0u) {
                first_possible = crtcs[c];
            }
            /* Prefer a free CRTC, so that another monitor keeps its picture. */
            struct drm_mode_crtc state = {.crtc_id = crtcs[c]};
            if (kms_ioctl(s->card_fd, DRM_IOCTL_MODE_GETCRTC, &state) == 0 && state.mode_valid == 0u) {
                return crtcs[c];
            }
        }
    }
    return first_possible;
}

/* Every connector on this CRTC must go back on it at exit, not only the one the spike drives. */
static void kms_collect_saved_connectors(kms_flip_t *s) {
    s->saved_connector_count = 0u;
    for (uint32_t i = 0u; i < s->connector_count; i++) {
        const kms_connector_t *connector = &s->connectors[i];
        if (connector->encoder_id == 0u) {
            continue;
        }
        struct drm_mode_get_encoder encoder = {.encoder_id = connector->encoder_id};
        if (kms_ioctl(s->card_fd, DRM_IOCTL_MODE_GETENCODER, &encoder) == 0 && encoder.crtc_id == s->crtc_id) {
            s->saved_connectors[s->saved_connector_count] = connector->connector_id;
            s->saved_connector_count++;
        }
    }
}

static bool kms_pick_output(kms_flip_t *s) {
    uint32_t crtcs[KMS_MAX_CRTCS] = {};
    uint32_t connector_ids[KMS_MAX_CONNECTORS] = {};
    uint32_t encoder_ids[KMS_MAX_ENCODERS] = {};
    struct drm_mode_card_res res = {};
    if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES(count)")) {
        return false;
    }
    kms_log(s, "resources: %u crtcs, %u connectors, %u encoders\n", res.count_crtcs, res.count_connectors,
            res.count_encoders);
    res.count_fbs = 0u;
    res.count_crtcs = res.count_crtcs < KMS_MAX_CRTCS ? res.count_crtcs : KMS_MAX_CRTCS;
    res.count_connectors = res.count_connectors < KMS_MAX_CONNECTORS ? res.count_connectors : KMS_MAX_CONNECTORS;
    res.count_encoders = res.count_encoders < KMS_MAX_ENCODERS ? res.count_encoders : KMS_MAX_ENCODERS;
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)connector_ids;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)encoder_ids;
    uint32_t count_crtcs = res.count_crtcs;
    uint32_t count_connectors = res.count_connectors;
    if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES(fill)")) {
        return false;
    }

    struct drm_mode_modeinfo modes[KMS_MAX_MODES];
    for (uint32_t i = 0u; i < count_connectors; i++) {
        if (!kms_query_connector(s, connector_ids[i], modes)) {
            return false;
        }
    }
    const kms_connector_t *connector = kms_pick_connector(s);
    if (connector == nullptr) {
        kms_log(s, "ERROR no usable connected connector%s%s.\n", s->connector_name != nullptr ? " named " : "",
                s->connector_name != nullptr ? s->connector_name : "");
        return false;
    }
    s->connector_id = connector->connector_id;
    s->mode = connector->best_mode;
    s->refresh_mhz = connector->best_mhz;
    s->period_ns = KMS_NS_PER_SEC * 1000u / s->refresh_mhz;
    s->crtc_id = kms_pick_crtc(s, connector, crtcs, count_crtcs);
    if (s->crtc_id == 0u) {
        kms_log(s, "ERROR no CRTC can drive the picked connector.\n");
        return false;
    }
    kms_log(s, "picked: %s-%u on crtc %u, mode %ux%u at %" PRIu64 ".%03" PRIu64 " Hz, period %" PRIu64 " ns\n",
            kms_connector_type_name(connector->type), connector->type_id, s->crtc_id, s->mode.hdisplay,
            s->mode.vdisplay, s->refresh_mhz / 1000u, s->refresh_mhz % 1000u, s->period_ns);

    s->saved_crtc = (struct drm_mode_crtc){.crtc_id = s->crtc_id};
    if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_GETCRTC, &s->saved_crtc, "GETCRTC(save)")) {
        return false;
    }
    kms_collect_saved_connectors(s);
    s->crtc_saved = 1u;
    kms_log(s, "saved crtc: fb %u, mode_valid %u, %ux%u, %u connectors\n", s->saved_crtc.fb_id, s->saved_crtc.mode_valid,
            s->saved_crtc.mode.hdisplay, s->saved_crtc.mode.vdisplay, s->saved_connector_count);
    return true;
}

static void kms_buffer_fill(kms_flip_t *s, kms_buffer_t *buffer, uint32_t color) {
    uint64_t started_ns = kms_now_ns();
    for (uint32_t y = 0u; y < s->mode.vdisplay; y++) {
        uint32_t *row = (uint32_t *)(void *)(buffer->owned_map + (uint64_t)y * buffer->pitch);
        for (uint32_t x = 0u; x < s->mode.hdisplay; x++) {
            row[x] = color;
        }
    }
    buffer->color = color;
    uint64_t took_ns = kms_now_ns() - started_ns;
    s->fill_max_ns = took_ns > s->fill_max_ns ? took_ns : s->fill_max_ns;
    s->fill_count++;
}

static bool kms_buffer_create(kms_flip_t *s, kms_buffer_t *buffer) {
    struct drm_mode_create_dumb create = {.height = s->mode.vdisplay, .width = s->mode.hdisplay, .bpp = 32u};
    if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_CREATE_DUMB, &create, "CREATE_DUMB")) {
        return false;
    }
    buffer->handle = create.handle;
    buffer->pitch = create.pitch;
    buffer->map_size = create.size;

    struct drm_mode_map_dumb map = {.handle = create.handle};
    if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_MAP_DUMB, &map, "MAP_DUMB")) {
        return false;
    }
    void *mapped = mmap(nullptr, buffer->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, s->card_fd, (off_t)map.offset);
    if (mapped == MAP_FAILED) {
        int32_t saved_errno = errno;
        kms_log(s, "ERROR mmap dumb buffer: errno %d (%s)\n", saved_errno, strerror(saved_errno));
        return false;
    }
    buffer->owned_map = mapped;

    struct drm_mode_fb_cmd2 fb = {.width = s->mode.hdisplay, .height = s->mode.vdisplay,
                                  .pixel_format = DRM_FORMAT_XRGB8888};
    fb.handles[0] = buffer->handle;
    fb.pitches[0] = buffer->pitch;
    if (!kms_ioctl_logged(s, DRM_IOCTL_MODE_ADDFB2, &fb, "ADDFB2")) {
        return false;
    }
    buffer->fb_id = fb.fb_id;
    kms_log(s, "dumb buffer: handle %u, fb %u, pitch %u, size %" PRIu64 "\n", buffer->handle, buffer->fb_id,
            buffer->pitch, buffer->map_size);
    return true;
}

static bool kms_modeset(kms_flip_t *s) {
    s->buffers_made = 1u;
    for (uint32_t i = 0u; i < 2u; i++) {
        if (!kms_buffer_create(s, &s->buffers[i])) {
            return false;
        }
    }
    kms_buffer_fill(s, &s->buffers[0], 0x00200000u);
    kms_buffer_fill(s, &s->buffers[1], 0x00200000u);

    /* From here until the restore, the console cannot print, so everything goes into the log. */
    if (ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS) != 0) {
        int32_t saved_errno = errno;
        kms_log(s, "ERROR KDSETMODE(KD_GRAPHICS): errno %d (%s)\n", saved_errno, strerror(saved_errno));
        return false;
    }
    s->kd_graphics = 1u;

    struct drm_mode_crtc crtc = {.crtc_id = s->crtc_id, .fb_id = s->buffers[0].fb_id, .mode_valid = 1u,
                                 .count_connectors = 1u};
    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&s->connector_id;
    crtc.mode = s->mode;
    s->modeset_done = 1u; /* Set first: a failed SETCRTC may still have disturbed the CRTC. */
    return kms_ioctl_logged(s, DRM_IOCTL_MODE_SETCRTC, &crtc, "SETCRTC(modeset)");
}

static void kms_record_flip(kms_flip_t *s, const struct drm_event_vblank *event) {
    uint64_t now_ns = kms_now_ns();
    uint64_t kernel_ns = (uint64_t)event->tv_sec * KMS_NS_PER_SEC + (uint64_t)event->tv_usec * 1000u;
    if (s->events_received > KMS_WARMUP_EVENTS && s->interval_count < KMS_MAX_INTERVALS) {
        s->kernel_interval_ns[s->interval_count] = kernel_ns >= s->last_kernel_ns ? kernel_ns - s->last_kernel_ns : 0u;
        s->wake_interval_ns[s->interval_count] = now_ns - s->last_wake_ns;
        s->interval_count++;
        if (event->sequence != 0u && event->sequence - s->last_sequence > 1u) {
            s->sequence_gaps++;
        }
    }
    s->last_kernel_ns = kernel_ns;
    s->last_wake_ns = now_ns;
    s->last_event_ns = now_ns;
    s->last_sequence = event->sequence;
    s->events_received++;
    s->flip_pending = 0u;
    s->front ^= 1u;
}

/* Returns false only on a read failure. Having no event yet is success. */
static bool kms_read_events(kms_flip_t *s) {
    struct pollfd wait = {.fd = s->card_fd, .events = POLLIN};
    int32_t ready = poll(&wait, 1u, KMS_POLL_MS);
    if (ready < 0) {
        return errno == EINTR;
    }
    if (ready == 0) {
        return true;
    }
    alignas(8) uint8_t bytes[1024];
    int64_t got = read(s->card_fd, bytes, sizeof(bytes));
    if (got < 0) {
        return errno == EINTR || errno == EAGAIN;
    }
    uint64_t at = 0u;
    while (at + sizeof(struct drm_event) <= (uint64_t)got) {
        struct drm_event header;
        memcpy(&header, bytes + at, sizeof(header));
        if (header.length < sizeof(header) || at + header.length > (uint64_t)got) {
            break;
        }
        if (header.type == DRM_EVENT_FLIP_COMPLETE && header.length >= sizeof(struct drm_event_vblank)) {
            struct drm_event_vblank event;
            memcpy(&event, bytes + at, sizeof(event));
            kms_record_flip(s, &event);
        }
        at += header.length;
    }
    return true;
}

static bool kms_vt_still_ours(const kms_flip_t *s) {
    struct vt_stat vt_state;
    return ioctl(STDIN_FILENO, VT_GETSTATE, &vt_state) == 0 && (uint32_t)vt_state.v_active == s->tty_minor;
}

static void kms_flip_loop(kms_flip_t *s) {
    static const uint32_t colors[4] = {0x00c00000u, 0x0000c000u, 0x000000c0u, 0x00c0c0c0u};
    s->last_event_ns = kms_now_ns();

    while (stop_signal == 0 && kms_now_ns() - s->process_start_ns < KMS_RUN_NS) {
        kms_buffer_t *back = &s->buffers[s->front ^ 1u];
        uint32_t color = colors[(s->flips_submitted / KMS_FLIPS_PER_COLOR) % 4u];
        if (back->color != color) {
            kms_buffer_fill(s, back, color); /* Safe: the last event proved this one is off screen. */
        }
        struct drm_mode_crtc_page_flip flip = {.crtc_id = s->crtc_id, .fb_id = back->fb_id,
                                               .flags = DRM_MODE_PAGE_FLIP_EVENT};
        if (kms_ioctl(s->card_fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) != 0) {
            int32_t saved_errno = errno;
            kms_log(s, "FAIL PAGE_FLIP number %" PRIu64 ": errno %d (%s)\n", s->flips_submitted, saved_errno,
                    strerror(saved_errno));
            s->loop_failed = 1u;
            return;
        }
        s->flips_submitted++;
        s->flip_pending = 1u;
        s->last_event_ns = kms_now_ns();

        while (s->flip_pending != 0u && stop_signal == 0) {
            if (!kms_read_events(s)) {
                int32_t saved_errno = errno;
                kms_log(s, "FAIL reading events: errno %d (%s)\n", saved_errno, strerror(saved_errno));
                s->loop_failed = 1u;
                return;
            }
            if (s->flip_pending != 0u && kms_now_ns() - s->last_event_ns > KMS_FLIP_STALL_NS) {
                kms_log(s, "FAIL no flip event within 1 s after flip number %" PRIu64 ".\n", s->flips_submitted);
                s->loop_failed = 1u;
                return;
            }
        }
        if (s->events_received % KMS_VT_CHECK_EVENTS == 0u && !kms_vt_still_ours(s)) {
            kms_log(s, "FAIL the visible VT changed during the run, so it stopped early.\n");
            s->loop_failed = 1u;
            return;
        }
    }
    if (stop_signal != 0) {
        kms_log(s, "stopped early by signal %d.\n", (int32_t)stop_signal);
        s->loop_failed = 1u;
    }
}

/* Bounded, because SETCRTC is safest with no flip in flight, and a stuck flip must not block exit. */
static void kms_drain_pending(kms_flip_t *s) {
    uint64_t started_ns = kms_now_ns();
    while (s->flip_pending != 0u && kms_now_ns() - started_ns < KMS_DRAIN_NS) {
        if (!kms_read_events(s)) {
            break;
        }
    }
}

/*
 * Async-signal-safe: only ioctl and flags, so the crash handler can use it too. The order matters.
 * The CRTC goes back while we are still master. Master is dropped before KD_TEXT, because leaving
 * KD_GRAPHICS makes fbcon re-apply its own mode, and it cannot while another client holds master.
 */
static bool kms_restore_display(kms_flip_t *s) {
    bool ok = true;
    if (s->restored != 0u) {
        return true;
    }
    s->restored = 1u;
    if (s->modeset_done != 0u && s->crtc_saved != 0u) {
        struct drm_mode_crtc crtc = s->saved_crtc;
        crtc.set_connectors_ptr = (uint64_t)(uintptr_t)s->saved_connectors;
        crtc.count_connectors = s->saved_connector_count;
        if (crtc.mode_valid == 0u || crtc.fb_id == 0u || crtc.count_connectors == 0u) {
            crtc.fb_id = 0u;
            crtc.mode_valid = 0u;
            crtc.count_connectors = 0u;
            crtc.set_connectors_ptr = 0u;
        }
        if (kms_ioctl(s->card_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) != 0) {
            /* Without the saved framebuffer, turn the CRTC off and let fbcon set it up again. */
            crtc = (struct drm_mode_crtc){.crtc_id = s->crtc_id};
            kms_ioctl(s->card_fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
            ok = false;
        }
    }
    if (s->is_master != 0u) {
        if (kms_ioctl(s->card_fd, DRM_IOCTL_DROP_MASTER, nullptr) != 0) {
            ok = false;
        }
        s->is_master = 0u;
    }
    if (s->kd_graphics != 0u) {
        if (ioctl(STDIN_FILENO, KDSETMODE, KD_TEXT) != 0) {
            ok = false;
        }
        s->kd_graphics = 0u;
    }
    return ok;
}

/* Called exactly once, from main(). The framebuffers are no longer on screen when they are removed. */
static void kms_cleanup(kms_flip_t *s) {
    kms_drain_pending(s);
    bool restore_ok = kms_restore_display(s);
    if (s->modeset_done != 0u) {
        kms_log(s, "restore: %s\n", restore_ok ? "ioctls succeeded" : "AT LEAST ONE RESTORE IOCTL FAILED");
    }
    s->restore_failed = (uint8_t)!restore_ok;
    borrowed_crash_state = nullptr;

    if (s->buffers_made != 0u) {
        for (uint32_t i = 0u; i < 2u; i++) {
            kms_buffer_t *buffer = &s->buffers[i];
            if (buffer->fb_id != 0u) {
                uint32_t fb_id = buffer->fb_id;
                kms_ioctl_logged(s, DRM_IOCTL_MODE_RMFB, &fb_id, "RMFB");
            }
            if (buffer->owned_map != nullptr) {
                munmap(buffer->owned_map, buffer->map_size);
            }
            if (buffer->handle != 0u) {
                struct drm_mode_destroy_dumb destroy = {.handle = buffer->handle};
                kms_ioctl_logged(s, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy, "DESTROY_DUMB");
            }
        }
    }
    if (s->card_fd >= 0) {
        close(s->card_fd);
        s->card_fd = -1;
    }
}

static int32_t kms_compare_u64(const void *a, const void *b) {
    uint64_t left = *(const uint64_t *)a;
    uint64_t right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

/* Sorts the array in place. */
static kms_stats_t kms_stats_of(uint64_t *intervals, uint64_t count, uint64_t period_ns) {
    kms_stats_t stats = {.count = count};
    if (count == 0u) {
        return stats;
    }
    for (uint64_t i = 0u; i < count; i++) {
        stats.sum_ns += intervals[i];
        if (intervals[i] > period_ns + period_ns / 2u) {
            stats.late++;
        }
    }
    qsort(intervals, count, sizeof(intervals[0]), kms_compare_u64);
    stats.min_ns = intervals[0];
    stats.max_ns = intervals[count - 1u];
    stats.p50_ns = intervals[count / 2u];
    stats.p99_ns = intervals[count * 99u / 100u];
    return stats;
}

static void kms_log_stats(kms_flip_t *s, const char *name, const kms_stats_t *stats) {
    if (stats->count == 0u) {
        kms_log(s, "%s: no intervals recorded\n", name);
        return;
    }
    uint64_t mean_ns = stats->sum_ns / stats->count;
    uint64_t mean_mhz = mean_ns == 0u ? 0u : KMS_NS_PER_SEC * 1000u / mean_ns;
    kms_log(s, "%s: n=%" PRIu64 " mean=%" PRIu64 " min=%" PRIu64 " p50=%" PRIu64 " p99=%" PRIu64 " max=%" PRIu64
               " ns, late(>1.5 periods)=%" PRIu64 ", rate=%" PRIu64 ".%03" PRIu64 " Hz\n",
            name, stats->count, mean_ns, stats->min_ns, stats->p50_ns, stats->p99_ns, stats->max_ns, stats->late,
            mean_mhz / 1000u, mean_mhz % 1000u);
}

/* Decides the verdict from the kernel's flip timestamps, or from wakeups if the kernel gave none. */
static int32_t kms_report(kms_flip_t *s) {
    kms_stats_t kernel_stats = kms_stats_of(s->kernel_interval_ns, s->interval_count, s->period_ns);
    kms_stats_t wake_stats = kms_stats_of(s->wake_interval_ns, s->interval_count, s->period_ns);
    kms_log(s, "flips submitted=%" PRIu64 " events=%" PRIu64 " sequence_gaps=%" PRIu64 "\n", s->flips_submitted,
            s->events_received, s->sequence_gaps);
    kms_log_stats(s, "kernel timestamps", &kernel_stats);
    kms_log_stats(s, "process wakeups", &wake_stats);
    kms_log(s, "fill: %" PRIu64 " full-screen fills, slowest %" PRIu64 " ns\n", s->fill_count, s->fill_max_ns);

    const kms_stats_t *stats = s->kernel_clock_monotonic != 0u && kernel_stats.max_ns != 0u ? &kernel_stats : &wake_stats;
    bool pass = s->loop_failed == 0u && s->restore_failed == 0u && stats->count >= KMS_MIN_INTERVALS;
    if (s->refresh_mhz < KMS_TARGET_MIN_MHZ) {
        kms_log(s, "MISS: the picked mode is below 119 Hz; try --connector with a faster output.\n");
        pass = false;
    }
    if (stats->count < KMS_MIN_INTERVALS) {
        kms_log(s, "MISS: only %" PRIu64 " intervals were measured, and a verdict needs %u.\n", stats->count, KMS_MIN_INTERVALS);
    }
    if (stats->count > 0u) {
        uint64_t mean_ns = stats->sum_ns / stats->count;
        uint64_t tolerance_ns = s->period_ns / 100u;
        if (mean_ns > s->period_ns + tolerance_ns || mean_ns + tolerance_ns < s->period_ns) {
            kms_log(s, "MISS: mean interval %" PRIu64 " ns is over 1%% away from the period %" PRIu64 " ns.\n", mean_ns,
                    s->period_ns);
            pass = false;
        }
        if (stats->late * 100u > stats->count) {
            kms_log(s, "MISS: over 1%% of the intervals were late.\n");
            pass = false;
        }
    }
    kms_log(s, "VERDICT: %s\n", pass ? "PASS (still confirm by eye that the console came back)" : "FAIL");
    return pass ? KMS_EXIT_PASS : KMS_EXIT_MISSED;
}

static void kms_on_stop(int32_t signal_number) {
    stop_signal = signal_number;
}

/* A crash must not leave the console dark. Only a SIGKILL can still do that. */
static void kms_on_crash(int32_t signal_number) {
    if (borrowed_crash_state != nullptr) {
        kms_restore_display(borrowed_crash_state);
    }
    static const char text[] = "kms_flip crashed; tried to restore the console.\n";
    int64_t ignored = write(STDERR_FILENO, text, sizeof(text) - 1u);
    (void)ignored;
    raise(signal_number); /* SA_RESETHAND is set, so this dies with the original signal. */
}

static void kms_install_signals(void) {
    struct sigaction action = {};
    sigemptyset(&action.sa_mask);
    action.sa_handler = kms_on_stop; /* No SA_RESTART: poll() must wake so the loop can see the flag. */
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);
    sigaction(SIGQUIT, &action, nullptr);

    action.sa_handler = kms_on_crash;
    action.sa_flags = (int32_t)SA_RESETHAND;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);

    action.sa_handler = SIG_IGN;
    action.sa_flags = 0;
    sigaction(SIGPIPE, &action, nullptr);
    sigaction(SIGTSTP, &action, nullptr); /* Being stopped while the console is dark would be worse. */
    sigaction(SIGTTOU, &action, nullptr);
    sigaction(SIGTTIN, &action, nullptr);
}

static bool kms_parse_arguments(kms_flip_t *s, int32_t argc, char **argv) {
    s->card_path = "/dev/dri/card1";
    for (int32_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--card") == 0 && i + 1 < argc) {
            i++;
            s->card_path = argv[i];
        } else if (strcmp(argv[i], "--connector") == 0 && i + 1 < argc) {
            i++;
            s->connector_name = argv[i];
        } else {
            kms_log(s, "usage: kms_flip [--card PATH] [--connector NAME]   (for example --connector HDMI-A-1)\n");
            return false;
        }
    }
    return true;
}

static void kms_print_log(const kms_flip_t *s) {
    fwrite(s->log_text, 1u, s->log_used, stdout);
    fflush(stdout);
    FILE *result_file = fopen(KMS_RESULT_PATH, "w");
    if (result_file == nullptr) {
        fprintf(stderr, "could not write %s\n", KMS_RESULT_PATH);
        return;
    }
    fwrite(s->log_text, 1u, s->log_used, result_file);
    fclose(result_file);
    printf("this report was also saved to %s\n", KMS_RESULT_PATH);
}

int32_t main(int32_t argc, char **argv) {
    kms_flip_t state = {};
    kms_flip_t *s = &state;
    s->card_fd = -1;
    s->exit_code = KMS_EXIT_REFUSED;
    s->process_start_ns = kms_now_ns();

    kms_install_signals();
    bool ran = false;
    if (kms_parse_arguments(s, argc, argv) && kms_guard_console(s) && kms_open_and_take_master(s)) {
        borrowed_crash_state = s;
        s->exit_code = KMS_EXIT_ERROR;
        if (kms_query_card(s) && kms_pick_output(s) && kms_modeset(s)) {
            kms_flip_loop(s);
            ran = true;
        }
    }
    /* The console is back only after this, so nothing is printed until then. */
    kms_cleanup(s);
    if (ran) {
        s->exit_code = kms_report(s);
    } else {
        kms_log(s, "VERDICT: %s\n", s->exit_code == KMS_EXIT_REFUSED ? "REFUSED" : "ERROR during setup");
    }
    kms_print_log(s);
    return s->exit_code;
}
```

## Commits

Appended as they land:

On the integration branch `worktree-wf_a4b549de-085-20`; Ahmed fast-forwards `main` at the end.

- `6c12827` Spike written and compiled, unrun
- `0f468f5` First refused landing
- `86c10e3` Second refused landing

## Issues found

### Landing was refused by the agent's worktree isolation

- **Expected:** step 5 of the task lands with `flock /tmp/mc_c_land.lock sh -c 'git rebase main && git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only <branch>'`.
- **Actual:** the harness refused that command and the plain `git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only worktree-wf_a4b549de-085-3`, with: "this command redirects git to the shared checkout via -C. Refusing to run it — a worktree-isolated agent's git operations must target its own worktree." `flock` wrapped around `git rebase main` was refused too. A plain `git rebase main` ran, and reported "up to date".
- **Done:** left alone and escalated. Moving `main` some other way, such as `git update-ref` or `git push . <branch>:main`, would get around the refusal, break the "never push" rule, and leave the main checkout's files out of step with its branch. The work stays committed on the branch, and the merge command is in `needs_ahmed`.
- **Second attempt, 2026-11-01 (branch `worktree-wf_a4b549de-085-5`):** the exact command from step 5 was refused again: "this command hands sh text naming git in a plain command, which cannot be shown to stay inside the worktree." `main` is still at `5bb5c57`. Left alone for the same reasons. This agent fast-forwarded its own branch onto `fe23e60`, so the earlier commits keep their shas. Without the land step the same refusal will meet any further retry, so another attempt from a worktree-isolated agent will not help. `worktree-wf_a4b549de-085-1` and `worktree-wf_a4b549de-085-4` also hold commits that `main` does not, and their commit subjects say landing was refused there too.

### Blocked on Ahmed; later stages assume KMS works (open risk)

- **Expected:** this stage measures flips on `nvidia-drm` before any code depends on them (plan section "Risks": "Stage 01.2 before any code depends on them").
- **Actual:** agents may not take the display while Ahmed's X desktop is live, and Ahmed is unavailable until the end. The spike is compiled and has never run, so the question this stage asks is still unanswered.
- **Done:** escalated to Ahmed through `needs_ahmed`, with the commands above. By instruction for this run, later stages proceed on the **assumption that dumb buffers, modeset and page-flip events work**. Nothing has confirmed this.
- **Risk if the assumption is wrong:** Stages 05.1 and 05.2 (KMS backend, console guard), the present step of 08.4, and the scanout half of 01.4 would need rework, and the first-picture gate could not be run on KMS. The phase file names X11 raw protocol as the only known alternative, and that choice is Ahmed's. Headless work, which does not depend on KMS, would stand.

### X may hold the card even from another VT (unverified)

- **Expected:** the stage assumes X on another VT leaves the card free.
- **Evidence:** with X running, `/sys/class/drm/card1-HDMI-A-1/enabled` and `/sys/class/drm/card1-eDP-1/enabled` both read `disabled` although both connectors read `connected`, so DRM's own state shows no output in use. `/proc/driver/nvidia/version` is the open kernel module 615.71.09. `/sys/module/nvidia_drm/parameters/modeset` and `fbdev` are both `Y`.
- **Interpretation, not a measurement:** this suggests X drives the outputs through NVIDIA's own path, not through DRM. From memory, nvidia-drm may then reject becoming master while X holds modeset ownership. The module source is not on this machine (`/usr/src/nvidia*` does not exist), so this was not checked.
- **Done:** the spike treats `EINVAL`, `EBUSY` and `EACCES` from opening the card or `SET_MASTER` as a clear refusal, before changing anything. That is the case for run 2.

### 120 Hz on these monitors is unknown

- **Expected:** the pass criterion is 120 Hz.
- **Evidence:** two connectors are connected, `eDP-1` and `HDMI-A-1`, and both list 1920x1080 in sysfs. Sysfs `modes` gives no refresh rates.
- **Done:** the stage says to pick a connector, so the spike takes the fastest at 1920x1080 and `--connector` overrides it. It logs every connected output with its best mode, and reports a miss below 119 Hz, so a 60 Hz monitor cannot be mistaken for a driver failure. Refresh is computed from the mode's timings, not the rounded `vrefresh`.

### Console restore has limits and rests on unrun reasoning

- **Expected:** the console is restored on exit.
- **Actual:** not observed. The order is: put the CRTC back, drop master, then `KD_TEXT`. It comes from my memory of how fbcon re-applies its mode (it should not be able to while another client is master), not from a test.
- **Limit:** `SIGTERM`, `SIGINT`, `SIGHUP` and `SIGQUIT` end the loop and run the full restore. `SIGSEGV`, `SIGBUS`, `SIGFPE`, `SIGILL` and `SIGABRT` restore from the handler. `SIGKILL`, including `timeout -k` firing, cannot be handled and can leave the VT in graphics mode. Ctrl+Alt+F1 or F2 should still reach X. The 8-second self-limit should keep that path from being reached.

### Source kept in the repo, against the phase's "only the results stay"

- **Expected:** the phase file says spike code stays in `/tmp` and only results stay.
- **Actual:** the source is in this file, because this run's instructions require it to survive `/tmp` being cleared and nobody can re-create results without it.
- **Done:** kept as a fenced block in this file only. No new path was added, and `plans/REPO_STRUCTURE.md` is unchanged.

### Compile errors on the first build

- **Expected:** a clean build.
- **Actual:** `-Werror=padded` rejected the first layout of `kms_connector_t` and `kms_flip_t`, and `-Wconversion` and `-Wsign-conversion` rejected two assignments. Reading the code before the second build also found that `kms_cleanup` could run twice and remove the framebuffers twice.
- **Done:** fields reordered with written-out padding, conversions made explicit, and `kms_cleanup` now runs exactly once. The strict build passes.

### Gate check: a slow modeset would have reported a false FAIL

- **Expected:** a run that works ends by itself inside `timeout 10`, as the source's comment said.
- **Actual:** the 8 seconds were counted from the start of the flip loop, after probing and modeset. Against the fake card with a 2.5 s modeset: exit 124, `stopped early by signal 15`, `VERDICT: FAIL`, after 890 clean intervals at 120.000 Hz. Link training can plausibly take that long, though nobody has measured it here. It would also have put SIGKILL (`timeout -k 3`) closer to the restore. `KMS_MIN_INTERVALS` could also fail a run with no line saying why.
- **Done:** fixed. `KMS_RUN_NS` now counts from process start, `KMS_MIN_INTERVALS` went from 600 to 360, three seconds at 120 Hz, and a run with too few intervals logs `MISS: only N intervals`. The copy of the source above is the fixed one, and re-extracting it with the `awk` command above matches `/tmp/mc_c_spikes/kms_flip/index.c`.

### Gate check: this stage's logic had never run at all

- **Expected:** a ticked "write the spike" means more than that it compiles, because this code will run once, by hand, on the real display.
- **Actual:** the binary had never been executed. A 1,000-line program whose failure paths leave a dark console had only compile checks behind it.
- **Done:** wrote the fake card and 14 cases above. They found the issue before this one. The remaining todo boxes stay unticked, because a fake card cannot answer them.

### Second gate check: what nvidia-drm's source says, read but not run

- **Expected:** the earlier issue "X may hold the card" and "Console restore" rest on memory, because the module source is not on this machine.
- **Actual:** the driver's source was read on GitHub on 2026-11-02, from `NVIDIA/open-gpu-kernel-modules`, branch `main`, files `kernel-open/nvidia-drm/nvidia-drm-drv.c` and `nvidia-drm-gem-nvkms-memory.c`, through a web fetch that summarizes. **`main` is not necessarily the installed 615.71.09, and nothing here was run.** It says:
  - With `modeset=1` the driver sets `DRIVER_MODESET | DRIVER_ATOMIC` and provides `dumb_create` and `dumb_map_offset`, so dumb buffers exist as an interface. `/sys/module/nvidia_drm/parameters/modeset` reads `Y` here.
  - `__nv_drm_master_set` returns `-EINVAL` when `!hasFramebufferConsole && !grabOwnership()`. So the spike's `EINVAL` refusal is the right case for "someone else owns NVKMS". With a framebuffer console the grab is skipped, because the driver takes ownership at load. `fbdev` reads `Y` here, so `SET_MASTER` may succeed, which the earlier issue did not expect. Whether X and the console really share that ownership is not answered by reading this.
  - `nv_drm_master_drop` differs on that same flag. Without a framebuffer console it disables every output and releases NVKMS ownership. With one, it only resets the input colorspace.
- **What that changes:** the restore order in the spike (put the CRTC back, drop master, `KD_TEXT`) still looks right, since on that path the drop itself neither disables nor restores outputs. It also gives a hard reason why **agents must not open this card even just to look**: sysfs shows no output enabled through DRM, so there may be no DRM master at all, and in that case the kernel makes the opener master on `open` and calls this `master_set` and `master_drop` on close. This is inference from the source, not something observed.
- **Done:** recorded. The boxes stay unticked and the risk entry above stays open. This narrows what Ahmed's run has to answer, and it does not replace the run.

### 2026-11-02: the landing process changed, so the "not on `main`" text above is history.
- Expected: this stage to land on `main` through the workflow's landing command (see "Landing was refused" above).
- Actual: every landing from a worktree was refused, and `main` is still `5bb5c57`. The refusals are recorded above as they happened, and are left as written. The Commits section used to say the work was unlanded, on branch `worktree-wf_a4b549de-085-10`.
- Done: the process changed. Agent branches now merge into `worktree-wf_a4b549de-085-20`, which stands in for `main`, and Ahmed fast-forwards `main` at the end. The landing commands written above are no longer anyone's to run. The Commits section now lists the shas on the integration branch.
