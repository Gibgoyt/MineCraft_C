/* See run/src/input/ops/README.md. */
#ifndef MC_RUN_INPUT_OPS_INDEX_H
#define MC_RUN_INPUT_OPS_INDEX_H

#include <stdint.h>

/* Called once for each name in a directory. */
typedef void (*input_ops_on_name_t)(void *receiver, const char *name);

/* Every call this system makes to the kernel, and so the one place a test can stand in for it. A call that can fail
 * returns a negative errno, not -1 with errno set, as display_ops_t does. context is passed through untouched. */
typedef struct input_ops_t {
    int32_t (*open_device)(void *context, const char *path); /* read only, non-blocking: a descriptor, or -errno */
    int32_t (*close_fd)(void *context, int32_t fd);
    /* What the ioctl returned, which is a length for the evdev queries, or -errno. */
    int32_t (*ioctl_call)(void *context, int32_t fd, uint64_t request, void *argument);
    /* Bytes read, 0 for nothing there right now, or -errno. Never blocks. */
    int64_t (*read_fd)(void *context, int32_t fd, uint8_t *out, uint64_t bytes);
    /* A non-blocking descriptor that reports the directory's changes as inotify records, or -errno. */
    int32_t (*watch_directory)(void *context, const char *path);
    /* Calls on_name for every entry, then returns 0, or returns -errno. */
    int32_t (*list_directory)(void *context, const char *path, input_ops_on_name_t on_name, void *receiver);
} input_ops_t;
static_assert(sizeof(input_ops_t) == 48);

/* The kernel. Tests give it only a temporary directory: a path that is not under /dev/input reaches no input device. */
const input_ops_t *input_ops_real(void);

#endif
