/* See README.md. Finds keyboards and mice by what they say they can do and not by name, and follows them as they come and
 * go. Silent: this runs while the console is dark, so what happens is counted, for input_report to tell afterwards. */
#include "input/discovery/index.h"

#include <errno.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>

enum : uint32_t {
    DISCOVERY_PATH_BYTES = 256u,
    DISCOVERY_RECORD_BYTES = 4096u,
    DISCOVERY_READS_PER_POLL = 8u,
    DISCOVERY_RECORD_HEADER_BYTES = 16u,
};
static_assert(sizeof(struct inotify_event) == DISCOVERY_RECORD_HEADER_BYTES);

/* ---- What a device is -------------------------------------------------------------------------------------------- */

static bool bits_has(const uint8_t *bits, uint32_t bit) {
    return ((uint32_t)bits[bit / 8u] >> (bit % 8u) & 1u) != 0u;
}

static bool bits_has_range(const uint8_t *bits, uint32_t first, uint32_t last) {
    for (uint32_t bit = first; bit <= last; bit++) {
        if (!bits_has(bits, bit)) {
            return false;
        }
    }
    return true;
}

uint8_t input_discovery_classify(const input_capabilities_t *capabilities) {
    if (!bits_has(capabilities->event_types, EV_KEY)) {
        return 0u;
    }
    const uint8_t *const keys = capabilities->keys;
    uint8_t kinds = 0;
    /* The letters are three runs of codes: the keyboard's three rows. */
    if (bits_has_range(keys, KEY_Q, KEY_P) && bits_has_range(keys, KEY_A, KEY_L) &&
        bits_has_range(keys, KEY_Z, KEY_M) && bits_has(keys, KEY_SPACE) && bits_has(keys, KEY_ENTER)) {
        kinds |= INPUT_KIND_KEYBOARD;
    }
    if (bits_has(capabilities->event_types, EV_REL) && bits_has(capabilities->relative_axes, REL_X) &&
        bits_has(capabilities->relative_axes, REL_Y) && bits_has(keys, BTN_LEFT)) {
        kinds |= INPUT_KIND_MOUSE;
    }
    return kinds;
}

/* Any of these refusing means this is no evdev device: a regular file says ENOTTY. */
static uint8_t discovery_ask_kinds(const input_discovery_t *discovery, int32_t fd, char *label) {
    const input_ops_t *const ops = discovery->borrowed_ops;
    void *const context = discovery->borrowed_context;
    input_capabilities_t capabilities;
    memset(&capabilities, 0, sizeof(capabilities));
    if (ops->ioctl_call(context, fd, EVIOCGBIT(0, sizeof(capabilities.event_types)), capabilities.event_types) < 0 ||
        ops->ioctl_call(context, fd, EVIOCGBIT(EV_KEY, sizeof(capabilities.keys)), capabilities.keys) < 0 ||
        ops->ioctl_call(context, fd, EVIOCGBIT(EV_REL, sizeof(capabilities.relative_axes)),
                        capabilities.relative_axes) < 0) {
        return 0u;
    }
    const uint8_t kinds = input_discovery_classify(&capabilities);
    if (kinds != 0u && ops->ioctl_call(context, fd, EVIOCGNAME(INPUT_DISCOVERY_LABEL_BYTES - 1u), label) < 0) {
        memcpy(label, "unnamed", sizeof("unnamed"));
    }
    return kinds;
}

/* ---- The table --------------------------------------------------------------------------------------------------- */

bool input_discovery_name_is_event(const char *name) {
    static const char prefix[] = "event";
    const uint64_t prefix_bytes = sizeof(prefix) - 1u;
    if (strncmp(name, prefix, prefix_bytes) != 0 || name[prefix_bytes] == '\0') {
        return false;
    }
    for (const char *digit = name + prefix_bytes; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '9') {
            return false;
        }
    }
    return strlen(name) < INPUT_DISCOVERY_FILE_NAME_BYTES;
}

static bool discovery_find(const input_discovery_t *discovery, const char *name, uint32_t *out_slot) {
    for (uint32_t i = 0; i < INPUT_DISCOVERY_MAX_DEVICES; i++) {
        const input_device_t *const device = &discovery->borrowed_devices[i];
        if (device->in_use != 0u && strcmp(device->file_name, name) == 0) {
            *out_slot = i;
            return true;
        }
    }
    return false;
}

static void discovery_open(input_discovery_t *discovery, const char *name) {
    uint32_t slot = 0;
    if (!input_discovery_name_is_event(name) || discovery_find(discovery, name, &slot)) {
        return;
    }
    if (discovery->device_count == INPUT_DISCOVERY_MAX_DEVICES) {
        discovery->stats.table_full++;
        return;
    }
    char path[DISCOVERY_PATH_BYTES];
    const int32_t path_bytes = snprintf(path, sizeof(path), "%s/%s", discovery->borrowed_directory, name);
    if (path_bytes < 0 || (uint64_t)path_bytes >= sizeof(path)) {
        discovery->stats.open_failures++;
        discovery->stats.last_errno = ENAMETOOLONG;
        return;
    }
    const int32_t fd = discovery->borrowed_ops->open_device(discovery->borrowed_context, path);
    if (fd < 0) {
        discovery->stats.open_failures++;
        discovery->stats.last_errno = -fd;
        return;
    }
    char label[INPUT_DISCOVERY_LABEL_BYTES];
    memset(label, 0, sizeof(label));
    const uint8_t kinds = discovery_ask_kinds(discovery, fd, label);
    if (kinds == 0u) {
        discovery->stats.rejected++;
        discovery->borrowed_ops->close_fd(discovery->borrowed_context, fd);
        return;
    }
    while (discovery->borrowed_devices[slot].in_use != 0u) {
        slot++; /* device_count says there is a free one */
    }
    input_device_t *const device = &discovery->borrowed_devices[slot];
    memset(device, 0, sizeof(*device));
    input_events_stream_start(&device->stream, fd);
    memcpy(device->file_name, name, strlen(name) + 1u);
    memcpy(device->label, label, sizeof(label));
    device->in_use = 1u;
    device->kinds = kinds;
    discovery->device_count++;
    discovery->stats.opened++;
}

/* events is nullptr at shutdown, when nobody will read the frame again. */
static void discovery_close(input_discovery_t *discovery, input_events_t *events, uint32_t slot) {
    input_device_t *const device = &discovery->borrowed_devices[slot];
    if (events != nullptr) {
        input_events_stream_release(events, &device->stream);
    }
    discovery->borrowed_ops->close_fd(discovery->borrowed_context, device->stream.fd);
    memset(device, 0, sizeof(*device));
    discovery->device_count--;
}

void input_discovery_lost(input_discovery_t *discovery, input_events_t *events, uint32_t slot) {
    discovery_close(discovery, events, slot);
    discovery->stats.lost++;
    discovery->rescan_soon = 1u;
}

/* ---- Watching the directory -------------------------------------------------------------------------------------- */

static void discovery_on_name(void *receiver, const char *name) {
    discovery_open(receiver, name);
}

static void discovery_rescan(input_discovery_t *discovery) {
    discovery->stats.rescans++;
    const int32_t result = discovery->borrowed_ops->list_directory(
        discovery->borrowed_context, discovery->borrowed_directory, discovery_on_name, discovery);
    if (result < 0) {
        discovery->stats.last_errno = -result;
    }
}

/* Returns whether there is now a watch. */
static bool discovery_watch_start(input_discovery_t *discovery) {
    const int32_t fd =
        discovery->borrowed_ops->watch_directory(discovery->borrowed_context, discovery->borrowed_directory);
    if (fd < 0) {
        discovery->watch_fd = -1;
        discovery->stats.last_errno = -fd;
        return false;
    }
    discovery->watch_fd = fd;
    discovery->stats.watch_starts++;
    return true;
}

/* The directory going away is not the end of the story: devtmpfs removes /dev/input along with its last node. */
static void discovery_watch_lost(input_discovery_t *discovery) {
    discovery->borrowed_ops->close_fd(discovery->borrowed_context, discovery->watch_fd);
    discovery->watch_fd = -1;
    discovery->stats.watch_losses++;
}

static void discovery_take_named(input_discovery_t *discovery, input_events_t *events, uint32_t mask,
                                 const char *name) {
    if ((mask & (IN_DELETE | IN_MOVED_FROM)) != 0u) {
        uint32_t slot = 0;
        if (discovery_find(discovery, name, &slot)) {
            discovery_close(discovery, events, slot);
            discovery->stats.removed++;
        }
    } else if ((mask & (IN_CREATE | IN_ATTRIB | IN_MOVED_TO)) != 0u) {
        /* The same node comes round again once udev has set its permissions. If it is open by then, this does nothing. */
        discovery_open(discovery, name);
    }
}

/* Records are read as bytes, and nothing in one is believed until it is known to fit. Returns false once the watch has
 * gone, since whatever follows no longer means anything. */
static bool discovery_take_records(input_discovery_t *discovery, input_events_t *events, const uint8_t *bytes,
                                   uint64_t length) {
    uint64_t offset = 0;
    while (length - offset >= DISCOVERY_RECORD_HEADER_BYTES) {
        struct inotify_event record;
        memcpy(&record, bytes + offset, DISCOVERY_RECORD_HEADER_BYTES);
        if (record.len > length - offset - DISCOVERY_RECORD_HEADER_BYTES) {
            return true;
        }
        if ((record.mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT)) != 0u) {
            discovery_watch_lost(discovery);
            return false;
        }
        if ((record.mask & IN_Q_OVERFLOW) != 0u) {
            discovery->rescan_now = 1u;
        } else if (record.len != 0u) {
            const char *const name = (const char *)(bytes + offset + DISCOVERY_RECORD_HEADER_BYTES);
            const uint64_t name_bytes = strnlen(name, record.len);
            /* Terminated and short enough, or it cannot be one of ours. */
            if (name_bytes < record.len && name_bytes < INPUT_DISCOVERY_FILE_NAME_BYTES) {
                discovery_take_named(discovery, events, record.mask, name);
            }
        }
        offset += DISCOVERY_RECORD_HEADER_BYTES + record.len;
    }
    return true;
}

static void discovery_read_watch(input_discovery_t *discovery, input_events_t *events) {
    alignas(8) uint8_t bytes[DISCOVERY_RECORD_BYTES];
    for (uint32_t read_number = 0; read_number < DISCOVERY_READS_PER_POLL; read_number++) {
        const int64_t got =
            discovery->borrowed_ops->read_fd(discovery->borrowed_context, discovery->watch_fd, bytes, sizeof(bytes));
        if (got < 0) {
            discovery->stats.last_errno = (int32_t)-got;
            discovery_watch_lost(discovery);
            return;
        }
        if (got == 0 || !discovery_take_records(discovery, events, bytes, (uint64_t)got)) {
            return;
        }
    }
}

void input_discovery_init(input_discovery_t *discovery, arena_t *arena, const input_ops_t *ops, void *context,
                          const char *directory) {
    memset(discovery, 0, sizeof(*discovery));
    discovery->borrowed_ops = ops;
    discovery->borrowed_context = context;
    discovery->borrowed_directory = directory;
    discovery->borrowed_devices =
        arena_alloc_zeroed(arena, sizeof(input_device_t) * INPUT_DISCOVERY_MAX_DEVICES, alignof(input_device_t));
    discovery_watch_start(discovery);
    discovery_rescan(discovery);
}

void input_discovery_deinit(input_discovery_t *discovery) {
    for (uint32_t i = 0; i < INPUT_DISCOVERY_MAX_DEVICES; i++) {
        if (discovery->borrowed_devices[i].in_use != 0u) {
            discovery_close(discovery, nullptr, i);
        }
    }
    if (discovery->watch_fd >= 0) {
        discovery->borrowed_ops->close_fd(discovery->borrowed_context, discovery->watch_fd);
    }
    memset(discovery, 0, sizeof(*discovery));
}

void input_discovery_poll(input_discovery_t *discovery, input_events_t *events) {
    discovery->polls++;
    if (discovery->watch_fd >= 0) {
        discovery_read_watch(discovery, events);
    }
    if (discovery->polls % INPUT_DISCOVERY_RETRY_POLLS == 0u) {
        if (discovery->watch_fd < 0 && discovery_watch_start(discovery)) {
            discovery->rescan_now = 1u; /* it was blind until now */
        }
        if (discovery->rescan_soon != 0u) {
            discovery->rescan_now = 1u;
        }
    }
    if (discovery->rescan_now != 0u) {
        discovery->rescan_now = 0u;
        discovery->rescan_soon = 0u;
        discovery_rescan(discovery);
    }
}
