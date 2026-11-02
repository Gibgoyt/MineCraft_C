/* The fake kernel behind input_ops_t, for ctest run_input. See ../README.md. It answers as the evdev and inotify UAPI say the
 * real one does: EVIOCGBIT and friends copy as much as fits and return the length, a pulled device reads ENODEV. Any ioctl it
 * was not expecting is recorded, which is how a test sees that nothing ever asks for EVIOCGRAB. Included once, by ../index.c. */
#ifndef MC_TESTS_RUN_INPUT_FIXTURES_FAKE_EVDEV_H
#define MC_TESTS_RUN_INPUT_FIXTURES_FAKE_EVDEV_H

#include "harness/index.h"

#include "input/index.h"

#include <errno.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>

#define FAKE_DIRECTORY "/fake/input"

enum : uint32_t {
    FAKE_MAX_DEVICES = 48u, /* more than INPUT_DISCOVERY_MAX_DEVICES, to test the limit */
    FAKE_MAX_QUEUED = 4096u,
    FAKE_MAX_NAMES = 64u,
    FAKE_RECORD_BYTES = 8192u,
};
enum : int32_t {
    FAKE_WATCH_FD = 9,
    FAKE_FD_BASE = 100,
};

typedef struct fake_device_t {
    char file_name[32];
    const char *label;
    input_capabilities_t capabilities;
    input_keys_t key_state; /* what EVIOCGKEY answers */
    struct input_event queue[FAKE_MAX_QUEUED];
    uint64_t torn_bytes; /* the next read comes back this many bytes short of a whole event */
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t read_calls;
    int32_t open_errno; /* opening fails with this */
    int32_t read_errno; /* reading fails with this: ENODEV once it has been pulled */
    int32_t key_errno;  /* EVIOCGKEY fails with this */
    int32_t name_errno; /* EVIOCGNAME fails with this */
    int32_t caps_errno; /* EVIOCGBIT fails with this */
    uint32_t open_count;
    uint32_t close_count;
    uint8_t listed; /* the directory shows it */
    uint8_t is_open;
    uint8_t _pad0[6];
} fake_device_t;
static_assert(sizeof(fake_device_t) == 32 + 8 + 104 + 96 + FAKE_MAX_QUEUED * 24 + 8 + 12 + 20 + 8 + 8);

typedef struct fake_evdev_t {
    uint64_t unexpected_ioctls;
    uint64_t last_unexpected;
    uint64_t outside_directory; /* opens of anything but FAKE_DIRECTORY */
    fake_device_t devices[FAKE_MAX_DEVICES];
    const char *extra_names[FAKE_MAX_NAMES]; /* listed, and not devices */
    uint8_t records[FAKE_RECORD_BYTES];      /* inotify's queue */
    uint64_t record_bytes;
    uint32_t device_count;
    uint32_t extra_count;
    uint32_t list_calls;
    uint32_t watch_calls;
    uint32_t watch_closes;
    uint32_t watch_reads;
    int32_t watch_errno;
    int32_t watch_read_errno;
    int32_t list_errno;
    uint8_t watch_open;
    uint8_t watched_before_first_list;
    uint8_t _pad0[2];
} fake_evdev_t;
static_assert(sizeof(fake_evdev_t) ==
              24 + FAKE_MAX_DEVICES * sizeof(fake_device_t) + FAKE_MAX_NAMES * 8 + FAKE_RECORD_BYTES + 48);

static int32_t fake_open_device(void *context, const char *path) {
    fake_evdev_t *const fake = context;
    const uint64_t prefix = sizeof(FAKE_DIRECTORY) - 1u;
    if (strncmp(path, FAKE_DIRECTORY "/", prefix + 1u) != 0) {
        fake->outside_directory++;
        return -EACCES;
    }
    for (uint32_t i = 0; i < fake->device_count; i++) {
        fake_device_t *const device = &fake->devices[i];
        if (strcmp(device->file_name, path + prefix + 1u) == 0 && device->listed != 0u) {
            device->open_count++;
            if (device->open_errno != 0) {
                return -device->open_errno;
            }
            device->is_open = 1u;
            return FAKE_FD_BASE + (int32_t)i;
        }
    }
    return -ENOENT;
}

static fake_device_t *fake_device_of_fd(fake_evdev_t *fake, int32_t fd) {
    if (fd < FAKE_FD_BASE || (uint32_t)(fd - FAKE_FD_BASE) >= fake->device_count) {
        return nullptr;
    }
    return &fake->devices[fd - FAKE_FD_BASE];
}

static int32_t fake_close_fd(void *context, int32_t fd) {
    fake_evdev_t *const fake = context;
    if (fd == FAKE_WATCH_FD) {
        fake->watch_closes++;
        fake->watch_open = 0u;
        return 0;
    }
    fake_device_t *const device = fake_device_of_fd(fake, fd);
    if (device == nullptr || device->is_open == 0u) {
        fake->unexpected_ioctls++; /* closing what is not open is as wrong as an unknown ioctl */
        return -EBADF;
    }
    device->is_open = 0u;
    device->close_count++;
    return 0;
}

static int32_t fake_copy(void *argument, uint64_t room, const void *source, uint64_t bytes) {
    const uint64_t count = room < bytes ? room : bytes;
    memcpy(argument, source, count);
    return (int32_t)count;
}

static int32_t fake_ioctl_call(void *context, int32_t fd, uint64_t request, void *argument) {
    fake_evdev_t *const fake = context;
    fake_device_t *const device = fake_device_of_fd(fake, fd);
    if (device == nullptr || device->is_open == 0u) {
        fake->unexpected_ioctls++;
        return -EBADF;
    }
    const uint64_t room = _IOC_SIZE(request);
    const uint64_t without_size = request & ~(uint64_t)(_IOC_SIZEMASK << _IOC_SIZESHIFT);
    const input_capabilities_t *const caps = &device->capabilities;
    if (without_size == (EVIOCGBIT(0, 0) & ~(uint64_t)(_IOC_SIZEMASK << _IOC_SIZESHIFT))) {
        return device->caps_errno != 0 ? -device->caps_errno
                                       : fake_copy(argument, room, caps->event_types, sizeof(caps->event_types));
    }
    if (without_size == (uint64_t)EVIOCGBIT(EV_KEY, 0)) {
        return fake_copy(argument, room, caps->keys, sizeof(caps->keys));
    }
    if (without_size == (uint64_t)EVIOCGBIT(EV_REL, 0)) {
        return fake_copy(argument, room, caps->relative_axes, sizeof(caps->relative_axes));
    }
    if (without_size == (uint64_t)EVIOCGNAME(0)) {
        if (device->name_errno != 0) {
            return -device->name_errno;
        }
        return fake_copy(argument, room, device->label, strlen(device->label) + 1u);
    }
    if (without_size == (uint64_t)EVIOCGKEY(0)) {
        return device->key_errno != 0 ? -device->key_errno
                                      : fake_copy(argument, room, &device->key_state, sizeof(device->key_state));
    }
    fake->unexpected_ioctls++;
    fake->last_unexpected = request;
    return -EINVAL;
}

static int64_t fake_read_fd(void *context, int32_t fd, uint8_t *out, uint64_t bytes) {
    fake_evdev_t *const fake = context;
    if (fd == FAKE_WATCH_FD) {
        fake->watch_reads++;
        if (fake->watch_read_errno != 0) {
            return -(int64_t)fake->watch_read_errno;
        }
        const uint64_t count = fake->record_bytes < bytes ? fake->record_bytes : bytes;
        memcpy(out, fake->records, count);
        memmove(fake->records, fake->records + count, fake->record_bytes - count);
        fake->record_bytes -= count;
        return (int64_t)count;
    }
    fake_device_t *const device = fake_device_of_fd(fake, fd);
    if (device == nullptr || device->is_open == 0u) {
        fake->unexpected_ioctls++;
        return -EBADF;
    }
    device->read_calls++;
    if (device->read_errno != 0) {
        return -(int64_t)device->read_errno;
    }
    const uint32_t queued = device->queue_tail - device->queue_head;
    const uint64_t room = bytes / sizeof(struct input_event);
    const uint32_t count = queued < room ? queued : (uint32_t)room;
    const uint64_t got = count * sizeof(struct input_event);
    memcpy(out, &device->queue[device->queue_head], got);
    device->queue_head += count;
    if (device->torn_bytes != 0u && got != 0u) {
        const uint64_t torn = device->torn_bytes;
        device->torn_bytes = 0u;
        return (int64_t)(got - torn);
    }
    return (int64_t)got;
}

static int32_t fake_watch_directory(void *context, const char *path) {
    fake_evdev_t *const fake = context;
    if (strcmp(path, FAKE_DIRECTORY) != 0) {
        fake->outside_directory++;
    }
    fake->watch_calls++;
    if (fake->watch_errno != 0) {
        return -fake->watch_errno;
    }
    fake->watched_before_first_list = fake->list_calls == 0u ? 1u : fake->watched_before_first_list;
    fake->watch_open = 1u;
    return FAKE_WATCH_FD;
}

static int32_t fake_list_directory(void *context, const char *path, input_ops_on_name_t on_name, void *receiver) {
    fake_evdev_t *const fake = context;
    if (strcmp(path, FAKE_DIRECTORY) != 0) {
        fake->outside_directory++;
    }
    fake->list_calls++;
    if (fake->list_errno != 0) {
        return -fake->list_errno;
    }
    on_name(receiver, ".");
    on_name(receiver, "..");
    for (uint32_t i = 0; i < fake->extra_count; i++) {
        on_name(receiver, fake->extra_names[i]);
    }
    for (uint32_t i = 0; i < fake->device_count; i++) {
        if (fake->devices[i].listed != 0u) {
            on_name(receiver, fake->devices[i].file_name);
        }
    }
    return 0;
}

static const input_ops_t fake_ops = {
    .open_device = fake_open_device,
    .close_fd = fake_close_fd,
    .ioctl_call = fake_ioctl_call,
    .read_fd = fake_read_fd,
    .watch_directory = fake_watch_directory,
    .list_directory = fake_list_directory,
};

/* ---- Devices ----------------------------------------------------------------------------------------------------- */

static void fake_bit_set(uint8_t *bits, uint32_t bit) {
    bits[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
}

static void fake_caps_keyboard(input_capabilities_t *caps) {
    fake_bit_set(caps->event_types, EV_SYN);
    fake_bit_set(caps->event_types, EV_KEY);
    fake_bit_set(caps->event_types, EV_MSC);
    fake_bit_set(caps->event_types, EV_LED);
    fake_bit_set(caps->event_types, EV_REP);
    for (uint32_t key = KEY_ESC; key <= KEY_MICMUTE; key++) {
        fake_bit_set(caps->keys, key);
    }
}

static void fake_caps_mouse(input_capabilities_t *caps) {
    fake_bit_set(caps->event_types, EV_SYN);
    fake_bit_set(caps->event_types, EV_KEY);
    fake_bit_set(caps->event_types, EV_REL);
    fake_bit_set(caps->event_types, EV_MSC);
    for (uint32_t button = BTN_LEFT; button <= BTN_TASK; button++) {
        fake_bit_set(caps->keys, button);
    }
    fake_bit_set(caps->relative_axes, REL_X);
    fake_bit_set(caps->relative_axes, REL_Y);
    fake_bit_set(caps->relative_axes, REL_WHEEL);
    fake_bit_set(caps->relative_axes, REL_HWHEEL);
}

/* What a power button says: keys, and none of them letters. */
static void fake_caps_power_button(input_capabilities_t *caps) {
    fake_bit_set(caps->event_types, EV_SYN);
    fake_bit_set(caps->event_types, EV_KEY);
    fake_bit_set(caps->keys, KEY_POWER);
}

/* What a touchpad says: a left button and absolute axes, with nothing relative. */
static void fake_caps_touchpad(input_capabilities_t *caps) {
    fake_bit_set(caps->event_types, EV_SYN);
    fake_bit_set(caps->event_types, EV_KEY);
    fake_bit_set(caps->event_types, EV_ABS);
    fake_bit_set(caps->keys, BTN_LEFT);
    fake_bit_set(caps->keys, BTN_TOOL_FINGER);
    fake_bit_set(caps->keys, BTN_TOUCH);
}

static fake_evdev_t *fake_new(test_context_t *t) {
    return test_alloc(t, sizeof(fake_evdev_t));
}

/* A device is usually listed from the start. Unlisted, it is waiting to be plugged in. */
static fake_device_t *fake_add(fake_evdev_t *fake, const char *file_name, const char *label,
                               void (*caps)(input_capabilities_t *), bool listed) {
    fake_device_t *const device = &fake->devices[fake->device_count];
    fake->device_count++;
    snprintf(device->file_name, sizeof(device->file_name), "%s", file_name);
    device->label = label;
    caps(&device->capabilities);
    device->listed = listed ? 1u : 0u;
    return device;
}

static void fake_replay(fake_device_t *device, const struct input_event *events, uint32_t count) {
    memcpy(&device->queue[device->queue_tail], events, count * sizeof(*events));
    device->queue_tail += count;
}

/* An inotify record as the kernel lays it out: the name padded with NULs to the record's len. */
static void fake_record(fake_evdev_t *fake, uint32_t mask, const char *name) {
    struct inotify_event record = {.wd = 1, .mask = mask, .cookie = 0u, .len = 0u};
    if (name != nullptr) {
        record.len = (uint32_t)((strlen(name) + 1u + 15u) / 16u * 16u);
    }
    memcpy(fake->records + fake->record_bytes, &record, sizeof(record));
    fake->record_bytes += sizeof(record);
    if (name != nullptr) {
        memset(fake->records + fake->record_bytes, 0, record.len);
        memcpy(fake->records + fake->record_bytes, name, strlen(name));
        fake->record_bytes += record.len;
    }
}

#endif
