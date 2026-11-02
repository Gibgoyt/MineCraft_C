/* See README.md. The thin layer over the kernel: this is the only code in the system that makes a system call for input.
 * It never asks for EVIOCGRAB: see README.md. */
#include "input/ops/index.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Non-blocking, so that a frame can never wait on a device. Read only, because nothing here writes to one. */
static int32_t real_open_device(void *context, const char *path) {
    (void)context;
    const int32_t fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    return fd < 0 ? -errno : fd;
}

static int32_t real_close_fd(void *context, int32_t fd) {
    (void)context;
    return close(fd) != 0 ? -errno : 0;
}

static int32_t real_ioctl_call(void *context, int32_t fd, uint64_t request, void *argument) {
    (void)context;
    const int32_t result = ioctl(fd, request, argument);
    return result < 0 ? -errno : result;
}

static int64_t real_read_fd(void *context, int32_t fd, uint8_t *out, uint64_t bytes) {
    (void)context;
    const int64_t count = read(fd, out, bytes);
    if (count < 0) {
        return errno == EAGAIN || errno == EINTR ? 0 : -(int64_t)errno;
    }
    return count;
}

/* IN_ATTRIB is in the mask because a new node first appears owned by root, and is ours to open only once udev has set its
 * group and ACL. IN_MOVE_SELF and IN_DELETE_SELF say that the watch no longer means this path. */
static int32_t real_watch_directory(void *context, const char *path) {
    (void)context;
    const int32_t fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    const uint32_t mask =
        IN_CREATE | IN_ATTRIB | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;
    if (inotify_add_watch(fd, path, mask) < 0) {
        const int32_t error = errno;
        close(fd);
        return -error;
    }
    return fd;
}

static int32_t real_list_directory(void *context, const char *path, input_ops_on_name_t on_name, void *receiver) {
    (void)context;
    DIR *const directory = opendir(path);
    if (directory == nullptr) {
        return -errno;
    }
    for (const struct dirent *entry = readdir(directory); entry != nullptr; entry = readdir(directory)) {
        on_name(receiver, entry->d_name);
    }
    closedir(directory);
    return 0;
}

static const input_ops_t real_ops = {
    .open_device = real_open_device,
    .close_fd = real_close_fd,
    .ioctl_call = real_ioctl_call,
    .read_fd = real_read_fd,
    .watch_directory = real_watch_directory,
    .list_directory = real_list_directory,
};

const input_ops_t *input_ops_real(void) {
    return &real_ops;
}
