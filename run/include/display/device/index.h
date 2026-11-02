/* See run/src/display/device/README.md. */
#ifndef MC_RUN_DISPLAY_DEVICE_INDEX_H
#define MC_RUN_DISPLAY_DEVICE_INDEX_H

#include <stdint.h>

/* How a display step ended. Refused means the display was not touched, and that a person can do something about it. */
typedef enum display_status_t : uint8_t {
    DISPLAY_STATUS_OK = 0,
    DISPLAY_STATUS_REFUSED = 1,
    DISPLAY_STATUS_ERROR = 2,
} display_status_t;

/* Why the program will not take the display. Each has one message: display_device_refusal_text. */
typedef enum display_refusal_t : uint8_t {
    DISPLAY_REFUSAL_NONE = 0,
    DISPLAY_REFUSAL_X_SESSION,       /* DISPLAY is set */
    DISPLAY_REFUSAL_WAYLAND_SESSION, /* WAYLAND_DISPLAY is set */
    DISPLAY_REFUSAL_NOT_A_VT,        /* stdin is a pipe, a file, /dev/null, a pseudo terminal or /dev/tty0 */
    DISPLAY_REFUSAL_VT_NOT_VISIBLE,  /* a real console, but not the one on screen */
    DISPLAY_REFUSAL_VT_NOT_TEXT,     /* the console on screen is in graphics mode: somebody is drawing on it */
    DISPLAY_REFUSAL_CARD_BUSY,       /* opening the card said EBUSY or EINVAL */
    DISPLAY_REFUSAL_CARD_ACCESS,     /* opening the card said EACCES or EPERM */
    DISPLAY_REFUSAL_CARD_OPEN,       /* opening the card failed for any other reason */
    DISPLAY_REFUSAL_NOT_MASTER,      /* DRM_IOCTL_SET_MASTER said no: another client owns the card */
} display_refusal_t;

/* Every call this system makes to the kernel, and so the one place a test can stand in for it. The real table is only
 * ever used by the program, so no test goes near a card or a console. Each call that can fail returns a negative errno,
 * not -1 with errno set, because a fake is simpler that way and because errno is one more thing a signal handler would
 * have to look after. context is passed through untouched. The real calls are all async-signal-safe. */
typedef struct display_ops_t {
    int32_t (*open_card)(void *context, const char *path); /* a descriptor, or -errno */
    int32_t (*close_fd)(void *context, int32_t fd);
    int32_t (*ioctl_call)(void *context, int32_t fd, uint64_t request, void *argument); /* 0, or -errno */
    int32_t (*map_shared)(void *context, int32_t fd, uint64_t bytes, uint64_t offset, uint8_t **out);
    void (*unmap)(void *context, uint8_t *memory, uint64_t bytes);
    int32_t (*wait_readable)(void *context, int32_t fd, int32_t timeout_ms);     /* 1, 0 for not yet, or -errno */
    int64_t (*read_fd)(void *context, int32_t fd, uint8_t *out, uint64_t bytes); /* bytes read, or -errno */
    /* 0 with the device numbers, -ENOTTY if fd is not a character device, or another -errno. */
    int32_t (*char_device)(void *context, int32_t fd, uint32_t *out_major, uint32_t *out_minor);
    const char *(*get_env)(void *context, const char *name);
} display_ops_t;
static_assert(sizeof(display_ops_t) == 72);

/* The open card. Zeroed means nothing is open, so _deinit is safe on a device whose _init never ran. */
typedef struct display_device_t {
    const display_ops_t *borrowed_ops;
    void *borrowed_context;
    int32_t card_fd;
    int32_t console_fd;
    uint32_t console_minor;
    uint8_t is_master;           /* ours to drop */
    uint8_t console_graphics;    /* the console is in KD_GRAPHICS because of us */
    uint8_t timestamp_monotonic; /* flip events carry CLOCK_MONOTONIC times */
    uint8_t refusal;             /* display_refusal_t, when _init refused */
} display_device_t;
static_assert(sizeof(display_device_t) == 32);

/* The kernel. */
const display_ops_t *display_device_real_ops(void);

/* Says whether this process may even try to open the card, using nothing that could disturb a display server: the
 * environment, then fstat of the console, and only for a real console VT_GETSTATE and KDGETMODE. Nothing here opens the
 * card, which matters: opening a card with no master makes the opener its master. Fills out_minor on NONE. */
display_refusal_t display_device_check_console(const display_ops_t *ops, void *context, int32_t console_fd,
                                               uint32_t *out_minor);

/* The message for a refusal, naming what to do about it. Never nullptr. */
const char *display_device_refusal_text(display_refusal_t refusal);

/* Opens the card, becomes its master and checks it has dumb buffers. Call display_device_check_console first. Logs why
 * on failure, closes what it opened and leaves the device empty, keeping only device->refusal. */
display_status_t display_device_init(display_device_t *device, const display_ops_t *ops, void *context,
                                     const char *card_path, int32_t console_fd, uint32_t console_minor);

/* Drops master, puts the console back if that is still owed, and closes the card. An empty device does nothing. */
void display_device_deinit(display_device_t *device);

/* An ioctl on the card, retried a bounded number of times on EINTR and EAGAIN. Returns 0 or -errno. */
int32_t display_device_ioctl(const display_device_t *device, uint64_t request, void *argument);

/* Maps a dumb buffer: device memory, so this is the one exception to "only the heap maps". Returns 0 or -errno. */
int32_t display_device_map(const display_device_t *device, uint64_t bytes, uint64_t offset, uint8_t **out);
void display_device_unmap(const display_device_t *device, uint8_t *memory, uint64_t bytes);

/* The card's event stream. Each returns as the matching display_ops_t call does. */
int32_t display_device_wait_readable(const display_device_t *device, int32_t timeout_ms);
int64_t display_device_read(const display_device_t *device, uint8_t *out, uint64_t bytes);

/* KD_GRAPHICS: fbcon stops drawing, and nothing written to the console can be seen until the matching _text. */
int32_t display_device_console_graphics(display_device_t *device);

/* Each of these two is safe in a signal handler and does nothing if nothing is owed. Returns 0 or -errno. */
int32_t display_device_console_text(display_device_t *device);
int32_t display_device_drop_master(display_device_t *device);

/* Whether our console is still the one on screen. */
bool display_device_console_is_visible(const display_device_t *device);

#endif
