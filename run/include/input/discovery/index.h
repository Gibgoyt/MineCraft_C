/* See run/src/input/discovery/README.md. */
#ifndef MC_RUN_INPUT_DISCOVERY_INDEX_H
#define MC_RUN_INPUT_DISCOVERY_INDEX_H

#include "arena/index.h"
#include "input/events/index.h"
#include "input/ops/index.h"

#include <linux/input-event-codes.h>
#include <stdint.h>

enum : uint32_t {
    INPUT_DISCOVERY_MAX_DEVICES = 32u,
    INPUT_DISCOVERY_FILE_NAME_BYTES = 24u,
    INPUT_DISCOVERY_LABEL_BYTES = 60u,
    /* How many polls apart it tries again for a lost watch or a lost device's node: about a second at the display's rate. */
    INPUT_DISCOVERY_RETRY_POLLS = 120u,
};

/* What a device is, as a set of bits. Zero means the game has no use for it. */
enum : uint8_t {
    INPUT_KIND_KEYBOARD = 1u,
    INPUT_KIND_MOUSE = 2u,
};

/* What a device says it can do, as the kernel's EVIOCGBIT bitmaps. */
typedef struct input_capabilities_t {
    uint8_t keys[KEY_CNT / 8];
    uint8_t event_types[EV_CNT / 8];
    uint8_t relative_axes[REL_CNT / 8];
    uint8_t _pad0[2];
} input_capabilities_t;
static_assert(sizeof(input_capabilities_t) == 104);

/* One slot of the fixed table. */
typedef struct input_device_t {
    input_stream_t stream;
    char file_name[INPUT_DISCOVERY_FILE_NAME_BYTES]; /* event3 */
    char label[INPUT_DISCOVERY_LABEL_BYTES];         /* what the device calls itself */
    uint8_t in_use;
    uint8_t kinds; /* INPUT_KIND_* */
    uint8_t _pad0[2];
} input_device_t;
static_assert(sizeof(input_device_t) == 192);

/* Plain counters: this runs while the console is dark, so nothing is told until input_report. */
typedef struct input_discovery_stats_t {
    uint64_t opened;        /* devices taken into the table */
    uint64_t rejected;      /* opened and looked at, then closed: not a keyboard or a mouse, or not a device at all */
    uint64_t open_failures; /* EACCES is normal for a moment: udev sets the permissions after the node appears */
    uint64_t removed;       /* the directory said it went */
    uint64_t lost;          /* a read said it went */
    uint64_t table_full;    /* a device was left out, for want of a slot */
    uint64_t rescans;       /* the whole directory was listed */
    uint64_t watch_starts;
    uint64_t watch_losses;
    int32_t last_errno; /* of the last open, list or watch that failed */
    uint8_t _pad0[4];
} input_discovery_stats_t;
static_assert(sizeof(input_discovery_stats_t) == 80);

typedef struct input_discovery_t {
    const input_ops_t *borrowed_ops;
    void *borrowed_context;
    const char *borrowed_directory;
    input_device_t *borrowed_devices; /* INPUT_DISCOVERY_MAX_DEVICES of them, from the arena */
    input_discovery_stats_t stats;
    uint64_t polls;
    int32_t watch_fd; /* negative while there is no watch */
    uint32_t device_count;
    uint8_t rescan_now;  /* the kernel lost track of changes */
    uint8_t rescan_soon; /* a device was lost by reading, so its name may be free again with no record saying so */
    uint8_t _pad0[6];
} input_discovery_t;
static_assert(sizeof(input_discovery_t) == 136);

/* Whether a directory entry can be an evdev device: "event" and then digits. mouse0, js0 and by-id are not. */
bool input_discovery_name_is_event(const char *name);

/* What a device is, from what it says it can do. It is a keyboard if it has every letter, a space and an enter, which a
 * power button or a volume knob does not. It is a mouse if it moves along X and Y and has a left button, which a touchpad
 * or a joystick does not. One device may be both. */
uint8_t input_discovery_classify(const input_capabilities_t *capabilities);

/* Starts watching directory and then opens every keyboard and mouse in it, in that order so that nothing that appears in
 * between is missed. The directory need not exist yet. directory must outlive this. */
void input_discovery_init(input_discovery_t *discovery, arena_t *arena, const input_ops_t *ops, void *context,
                          const char *directory);

/* Closes everything. */
void input_discovery_deinit(input_discovery_t *discovery);

/* Takes in whatever the directory says changed: new devices open, removed ones close and let go of their keys. */
void input_discovery_poll(input_discovery_t *discovery, input_events_t *events);

/* Closes a device that a read said is gone, and lets go of its keys. */
void input_discovery_lost(input_discovery_t *discovery, input_events_t *events, uint32_t slot);

#endif
