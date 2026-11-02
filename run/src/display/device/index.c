/* See README.md. The thin layer over the kernel: every open, ioctl and map of the display goes through display_ops_t, and
 * the real table at the top of this file is the only code in the system that makes a system call for the display. */
#include "display/device/index.h"

#include "log/index.h"

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

enum : uint32_t {
    DEVICE_TTY_MAJOR = 4u,
    DEVICE_TTY_MINOR_MAX = 63u, /* minors above this are serial ports; minor 0 is /dev/tty0, "whichever is on screen" */
    DEVICE_IOCTL_TRIES = 256u,  /* so that a crash handler restoring the console cannot spin for ever */
    DEVICE_INPUT_NODES = 64u,   /* /dev/input/event0 up to this */
};

/* ---- The kernel -------------------------------------------------------------------------------------------------- */

static int32_t real_open_card(void *context, const char *path) {
    (void)context;
    const int32_t fd = open(path, O_RDWR | O_CLOEXEC);
    return fd < 0 ? -errno : fd;
}

static int32_t real_close_fd(void *context, int32_t fd) {
    (void)context;
    return close(fd) != 0 ? -errno : 0;
}

static int32_t real_ioctl_call(void *context, int32_t fd, uint64_t request, void *argument) {
    (void)context;
    return ioctl(fd, request, argument) != 0 ? -errno : 0;
}

static int32_t real_map_shared(void *context, int32_t fd, uint64_t bytes, uint64_t offset, uint8_t **out) {
    (void)context;
    void *const memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)offset);
    if (memory == MAP_FAILED) {
        return -errno;
    }
    *out = memory;
    return 0;
}

static void real_unmap(void *context, uint8_t *memory, uint64_t bytes) {
    (void)context;
    munmap(memory, bytes);
}

/* A signal counts as "not yet", so that whoever waits gets to look at why it was sent. */
static int32_t real_wait_readable(void *context, int32_t fd, int32_t timeout_ms) {
    (void)context;
    struct pollfd wait = {.fd = fd, .events = POLLIN, .revents = 0};
    const int32_t ready = poll(&wait, 1u, timeout_ms);
    if (ready < 0) {
        return errno == EINTR ? 0 : -errno;
    }
    return ready > 0 ? 1 : 0;
}

static int64_t real_read_fd(void *context, int32_t fd, uint8_t *out, uint64_t bytes) {
    (void)context;
    const int64_t count = read(fd, out, bytes);
    if (count < 0) {
        return errno == EINTR || errno == EAGAIN ? 0 : -(int64_t)errno;
    }
    return count;
}

static int32_t real_char_device(void *context, int32_t fd, uint32_t *out_major, uint32_t *out_minor) {
    (void)context;
    struct stat status;
    if (fstat(fd, &status) != 0) {
        return -errno;
    }
    if (!S_ISCHR(status.st_mode)) {
        return -ENOTTY;
    }
    *out_major = major(status.st_rdev);
    *out_minor = minor(status.st_rdev);
    return 0;
}

static const char *real_get_env(void *context, const char *name) {
    (void)context;
    return getenv(name);
}

/* A keyboard is whatever can send both of the keys the emergency exit needs. Mice and power buttons cannot. */
static bool real_is_keyboard(int32_t fd) {
    uint8_t bits[KEY_MAX / 8 + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
        return false;
    }
    const bool has_backspace = (bits[KEY_BACKSPACE / 8] >> (KEY_BACKSPACE % 8) & 1u) != 0u;
    const bool has_ctrl = (bits[KEY_LEFTCTRL / 8] >> (KEY_LEFTCTRL % 8) & 1u) != 0u;
    return has_backspace && has_ctrl;
}

/* Never EVIOCGRAB: the guard only watches, and whatever else reads these keys goes on seeing them. Only ever reached from
 * display_vt_guard_take_console, after every refusal in display_init has passed, so no test and no run inside X gets here. */
static uint32_t real_open_keyboards(void *context, int32_t *out_fds, uint32_t capacity) {
    (void)context;
    uint32_t count = 0;
    for (uint32_t i = 0; i < DEVICE_INPUT_NODES && count < capacity; i++) {
        /* By hand, since the guardian calls this too, and it may have been forked from a program with other threads. */
        char path[] = "/dev/input/event00";
        uint32_t end = (uint32_t)sizeof("/dev/input/event") - 1u;
        if (i >= 10u) {
            path[end] = (char)('0' + i / 10u);
            end++;
        }
        path[end] = (char)('0' + i % 10u);
        path[end + 1u] = '\0';
        const int32_t fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        if (real_is_keyboard(fd)) {
            out_fds[count] = fd;
            count++;
        } else {
            close(fd);
        }
    }
    return count;
}

static void real_pause_ms(void *context, int32_t milliseconds) {
    (void)context;
    const struct timespec wait = {.tv_sec = milliseconds / 1000, .tv_nsec = (int64_t)(milliseconds % 1000) * 1000000};
    nanosleep(&wait, nullptr);
}

static const display_ops_t device_real_ops = {
    .open_card = real_open_card,
    .close_fd = real_close_fd,
    .ioctl_call = real_ioctl_call,
    .map_shared = real_map_shared,
    .unmap = real_unmap,
    .wait_readable = real_wait_readable,
    .read_fd = real_read_fd,
    .char_device = real_char_device,
    .get_env = real_get_env,
    .open_keyboards = real_open_keyboards,
    .pause_ms = real_pause_ms,
};

const display_ops_t *display_device_real_ops(void) {
    return &device_real_ops;
}

/* ---- May we try at all? ------------------------------------------------------------------------------------------ */

static bool device_env_is_set(const display_ops_t *ops, void *context, const char *name) {
    const char *const value = ops->get_env(context, name);
    return value != nullptr && value[0] != '\0';
}

static int32_t device_ioctl_on(const display_ops_t *ops, void *context, int32_t fd, uint64_t request, void *argument) {
    int32_t result = -EINTR;
    for (uint32_t i = 0; i < DEVICE_IOCTL_TRIES && (result == -EINTR || result == -EAGAIN); i++) {
        result = ops->ioctl_call(context, fd, request, argument);
    }
    return result;
}

display_refusal_t display_device_check_console(const display_ops_t *ops, void *context, int32_t console_fd,
                                               uint32_t *out_minor) {
    if (device_env_is_set(ops, context, "DISPLAY")) {
        return DISPLAY_REFUSAL_X_SESSION;
    }
    if (device_env_is_set(ops, context, "WAYLAND_DISPLAY")) {
        return DISPLAY_REFUSAL_WAYLAND_SESSION;
    }
    uint32_t device_major = 0;
    uint32_t device_minor = 0;
    if (ops->char_device(context, console_fd, &device_major, &device_minor) != 0 || device_major != DEVICE_TTY_MAJOR ||
        device_minor == 0u || device_minor > DEVICE_TTY_MINOR_MAX) {
        return DISPLAY_REFUSAL_NOT_A_VT;
    }
    /* From here console_fd is known to be a virtual console, so these two are only questions to that console. */
    struct vt_stat state = {.v_active = 0, .v_signal = 0, .v_state = 0};
    if (device_ioctl_on(ops, context, console_fd, VT_GETSTATE, &state) != 0 ||
        (uint32_t)state.v_active != device_minor) {
        return DISPLAY_REFUSAL_VT_NOT_VISIBLE;
    }
    int32_t kd_mode = -1;
    if (device_ioctl_on(ops, context, console_fd, KDGETMODE, &kd_mode) != 0 || kd_mode != KD_TEXT) {
        return DISPLAY_REFUSAL_VT_NOT_TEXT;
    }
    *out_minor = device_minor;
    return DISPLAY_REFUSAL_NONE;
}

const char *display_device_refusal_text(display_refusal_t refusal) {
    switch (refusal) {
    case DISPLAY_REFUSAL_NONE:
        return "not refused";
    case DISPLAY_REFUSAL_X_SESSION:
        return "refused: DISPLAY is set, so this is running inside X, and X owns the display. The card was not opened. "
               "Press Ctrl+Alt+F3, log in on the text console and run it there";
    case DISPLAY_REFUSAL_WAYLAND_SESSION:
        return "refused: WAYLAND_DISPLAY is set, so this is running inside a Wayland session, and its compositor owns "
               "the display. The card was not opened. Press Ctrl+Alt+F3, log in on the text console and run it there";
    case DISPLAY_REFUSAL_NOT_A_VT:
        return "refused: stdin is not a Linux virtual console, so the display is not ours to touch. The card was not "
               "opened. Press Ctrl+Alt+F3, log in on the text console and run it there";
    case DISPLAY_REFUSAL_VT_NOT_VISIBLE:
        return "refused: this virtual console is not the one on screen. The card was not opened. Switch to it and run "
               "it again";
    case DISPLAY_REFUSAL_VT_NOT_TEXT:
        return "refused: this virtual console is not in text mode, so a display server appears to own it. The card was "
               "not opened";
    case DISPLAY_REFUSAL_CARD_BUSY:
        return "refused: the driver says the card is busy, so a display server probably owns it, even from another "
               "virtual console. Nothing on the display was changed. Stop the display server by hand and run it again";
    case DISPLAY_REFUSAL_CARD_ACCESS:
        return "refused: no access to the card. Log in on the text console, which grants it through logind, or join "
               "group video. Nothing on the display was changed";
    case DISPLAY_REFUSAL_CARD_OPEN:
        return "refused: the card would not open. Check display.device in config/machine.yaml, or pass --card. Nothing "
               "on the display was changed";
    case DISPLAY_REFUSAL_NOT_MASTER:
        return "refused: another client is the card's master, so a display server owns it, even from another virtual "
               "console. Nothing on the display was changed. Stop the display server by hand and run it again";
    }
    return "refused for a reason this build cannot name";
}

/* ---- The card ---------------------------------------------------------------------------------------------------- */

int32_t display_device_ioctl(const display_device_t *device, uint64_t request, void *argument) {
    return device_ioctl_on(device->borrowed_ops, device->borrowed_context, device->card_fd, request, argument);
}

static bool device_query_card(display_device_t *device, const char *card_path) {
    char driver_name[64] = {0};
    struct drm_version version = {0};
    version.name = driver_name;
    version.name_len = sizeof(driver_name) - 1u;
    int32_t result = display_device_ioctl(device, DRM_IOCTL_VERSION, &version);
    if (result != 0) {
        LOG_ERROR("display: DRM_IOCTL_VERSION on %s: %s", card_path, strerror(-result));
        return false;
    }
    LOG_INFO("display: %s is driver %s %" PRId32 ".%" PRId32 ".%" PRId32 ", on tty%" PRIu32, card_path, driver_name,
             (int32_t)version.version_major, (int32_t)version.version_minor, (int32_t)version.version_patchlevel,
             device->console_minor);

    struct drm_get_cap capability = {.capability = DRM_CAP_DUMB_BUFFER, .value = 0};
    result = display_device_ioctl(device, DRM_IOCTL_GET_CAP, &capability);
    if (result != 0) {
        LOG_ERROR("display: DRM_CAP_DUMB_BUFFER on %s: %s", card_path, strerror(-result));
        return false;
    }
    if (capability.value == 0u) {
        LOG_ERROR("display: driver %s offers no dumb buffers", driver_name);
        return false;
    }
    /* Optional: without it the kernel's event times are not on our clock, and display/flip uses its own wake-ups. */
    capability = (struct drm_get_cap){.capability = DRM_CAP_TIMESTAMP_MONOTONIC, .value = 0};
    device->timestamp_monotonic =
        (uint8_t)(display_device_ioctl(device, DRM_IOCTL_GET_CAP, &capability) == 0 && capability.value != 0u);
    return true;
}

display_status_t display_device_init(display_device_t *device, const display_ops_t *ops, void *context,
                                     const char *card_path, int32_t console_fd, uint32_t console_minor) {
    memset(device, 0, sizeof(*device));
    const int32_t fd = ops->open_card(context, card_path);
    if (fd < 0) {
        const int32_t error = -fd;
        const display_refusal_t refusal = error == EBUSY || error == EINVAL   ? DISPLAY_REFUSAL_CARD_BUSY
                                          : error == EACCES || error == EPERM ? DISPLAY_REFUSAL_CARD_ACCESS
                                                                              : DISPLAY_REFUSAL_CARD_OPEN;
        LOG_ERROR("display: %s: %s. %s", card_path, strerror(error), display_device_refusal_text(refusal));
        device->refusal = refusal;
        return DISPLAY_STATUS_REFUSED;
    }
    device->borrowed_ops = ops;
    device->borrowed_context = context;
    device->card_fd = fd;
    device->console_fd = console_fd;
    device->console_minor = console_minor;

    const int32_t master = display_device_ioctl(device, DRM_IOCTL_SET_MASTER, nullptr);
    if (master != 0) {
        LOG_ERROR("display: DRM_IOCTL_SET_MASTER on %s: %s. %s", card_path, strerror(-master),
                  display_device_refusal_text(DISPLAY_REFUSAL_NOT_MASTER));
        display_device_deinit(device);
        device->refusal = DISPLAY_REFUSAL_NOT_MASTER;
        return DISPLAY_STATUS_REFUSED;
    }
    device->is_master = 1u;
    if (!device_query_card(device, card_path)) {
        display_device_deinit(device);
        return DISPLAY_STATUS_ERROR;
    }
    return DISPLAY_STATUS_OK;
}

void display_device_deinit(display_device_t *device) {
    if (device->borrowed_ops == nullptr) {
        return;
    }
    const int32_t dropped = display_device_drop_master(device);
    const int32_t text = display_device_console_text(device);
    if (dropped != 0) {
        LOG_WARN("display: DROP_MASTER: %s", strerror(-dropped));
    }
    if (text != 0) {
        LOG_WARN("display: KDSETMODE(KD_TEXT): %s", strerror(-text));
    }
    device->borrowed_ops->close_fd(device->borrowed_context, device->card_fd);
    memset(device, 0, sizeof(*device));
}

int32_t display_device_map(const display_device_t *device, uint64_t bytes, uint64_t offset, uint8_t **out) {
    return device->borrowed_ops->map_shared(device->borrowed_context, device->card_fd, bytes, offset, out);
}

void display_device_unmap(const display_device_t *device, uint8_t *memory, uint64_t bytes) {
    device->borrowed_ops->unmap(device->borrowed_context, memory, bytes);
}

int32_t display_device_wait_readable(const display_device_t *device, int32_t timeout_ms) {
    return device->borrowed_ops->wait_readable(device->borrowed_context, device->card_fd, timeout_ms);
}

int64_t display_device_read(const display_device_t *device, uint8_t *out, uint64_t bytes) {
    return device->borrowed_ops->read_fd(device->borrowed_context, device->card_fd, out, bytes);
}

/* ---- The console ------------------------------------------------------------------------------------------------- */

static int32_t device_set_kd_mode(const display_device_t *device, uintptr_t mode) {
    /* KDSETMODE takes the value itself where other ioctls take a pointer. */
    return device_ioctl_on(device->borrowed_ops, device->borrowed_context, device->console_fd, KDSETMODE, (void *)mode);
}

int32_t display_device_console_graphics(display_device_t *device) {
    const int32_t result = device_set_kd_mode(device, KD_GRAPHICS);
    if (result == 0) {
        device->console_graphics = 1u;
    }
    return result;
}

int32_t display_device_console_text(display_device_t *device) {
    if (device->console_graphics == 0u) {
        return 0;
    }
    device->console_graphics = 0u;
    return device_set_kd_mode(device, KD_TEXT);
}

int32_t display_device_drop_master(display_device_t *device) {
    if (device->is_master == 0u) {
        return 0;
    }
    device->is_master = 0u;
    return display_device_ioctl(device, DRM_IOCTL_DROP_MASTER, nullptr);
}

int32_t display_device_set_master(display_device_t *device) {
    const int32_t result = display_device_ioctl(device, DRM_IOCTL_SET_MASTER, nullptr);
    device->is_master = (uint8_t)(result == 0);
    return result;
}

bool display_device_console_is_visible(const display_device_t *device) {
    struct vt_stat state = {.v_active = 0, .v_signal = 0, .v_state = 0};
    return device_ioctl_on(device->borrowed_ops, device->borrowed_context, device->console_fd, VT_GETSTATE, &state) ==
               0 &&
           (uint32_t)state.v_active == device->console_minor;
}
