/* See README.md. Reports and does not log: a missing file is an error here and normal to many callers, so what to say
 * about it is theirs to decide. Every function that can fail returns a status, and errno still holds the reason. */
#include "file/index.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum : uint32_t {
    FILE_PERMISSIONS = 0644,
    FILE_DIR_PERMISSIONS = 0755,
};

static file_status_t file_status_from_errno(void) {
    switch (errno) {
    case ENOENT:
    case ENOTDIR:
        return FILE_ERROR_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS:
        return FILE_ERROR_ACCESS;
    case ENOSPC:
    case EDQUOT:
        return FILE_ERROR_DISK_FULL;
    case EFBIG:
        return FILE_ERROR_TOO_LARGE;
    case ENAMETOOLONG:
        return FILE_ERROR_PATH_TOO_LONG;
    default:
        return FILE_ERROR_IO;
    }
}

const char *file_status_name(file_status_t status) {
    switch (status) {
    case FILE_OK:
        return "ok";
    case FILE_ERROR_NOT_FOUND:
        return "not found";
    case FILE_ERROR_EXISTS:
        return "already exists";
    case FILE_ERROR_ACCESS:
        return "access denied";
    case FILE_ERROR_DISK_FULL:
        return "disk full";
    case FILE_ERROR_TOO_LARGE:
        return "too large";
    case FILE_ERROR_PATH_TOO_LONG:
        return "path too long";
    case FILE_ERROR_END_OF_FILE:
        return "unexpected end of file";
    case FILE_ERROR_IO:
        return "i/o error";
    }
    return "?";
}

static int32_t file_open_flags(file_mode_t mode) {
    switch (mode) {
    case FILE_MODE_READ:
        return O_RDONLY;
    case FILE_MODE_WRITE:
        return O_WRONLY | O_CREAT | O_TRUNC;
    case FILE_MODE_APPEND:
        return O_WRONLY | O_CREAT | O_APPEND;
    case FILE_MODE_READ_WRITE:
        return O_RDWR | O_CREAT;
    }
    return O_RDONLY;
}

file_status_t file_open(file_t *out, const char *path, file_mode_t mode) {
    *out = (file_t){.fd = -1, ._pad0 = {0}};
    int32_t fd = -1;
    do {
        fd = open(path, file_open_flags(mode) | O_CLOEXEC, FILE_PERMISSIONS);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return file_status_from_errno();
    }
    out->fd = fd;
    return FILE_OK;
}

file_status_t file_close(file_t *file) {
    const int32_t fd = file->fd;
    file->fd = -1;
    /* No retry on EINTR: on Linux the descriptor is already released by then, and another thread may have been given
     * the same number since. */
    if (fd >= 0 && close(fd) != 0 && errno != EINTR) {
        return file_status_from_errno();
    }
    return FILE_OK;
}

file_status_t file_read(const file_t *file, void *out, uint64_t capacity, uint64_t *out_bytes) {
    uint8_t *cursor = out;
    uint64_t total = 0;
    file_status_t status = FILE_OK;
    while (total < capacity) {
        const int64_t count = read(file->fd, cursor + total, capacity - total);
        if (count > 0) {
            total += (uint64_t)count;
        } else if (count == 0) {
            break;
        } else if (errno != EINTR) {
            status = file_status_from_errno();
            break;
        }
    }
    *out_bytes = total;
    return status;
}

file_status_t file_read_exact(const file_t *file, void *out, uint64_t bytes) {
    uint64_t got = 0;
    const file_status_t status = file_read(file, out, bytes, &got);
    if (status != FILE_OK) {
        return status;
    }
    return got == bytes ? FILE_OK : FILE_ERROR_END_OF_FILE;
}

file_status_t file_write_all(const file_t *file, const void *data, uint64_t bytes) {
    const uint8_t *cursor = data;
    uint64_t total = 0;
    while (total < bytes) {
        const int64_t count = write(file->fd, cursor + total, bytes - total);
        if (count > 0) {
            total += (uint64_t)count;
        } else if (count == 0) {
            errno = EIO; /* a write of more than zero bytes that took none and gave no reason */
            return FILE_ERROR_IO;
        } else if (errno != EINTR) {
            return file_status_from_errno();
        }
    }
    return FILE_OK;
}

file_status_t file_read_at(const file_t *file, uint64_t offset, void *out, uint64_t bytes) {
    uint8_t *cursor = out;
    uint64_t total = 0;
    while (total < bytes) {
        const int64_t count = pread(file->fd, cursor + total, bytes - total, (int64_t)(offset + total));
        if (count > 0) {
            total += (uint64_t)count;
        } else if (count == 0) {
            return FILE_ERROR_END_OF_FILE;
        } else if (errno != EINTR) {
            return file_status_from_errno();
        }
    }
    return FILE_OK;
}

file_status_t file_write_at(const file_t *file, uint64_t offset, const void *data, uint64_t bytes) {
    const uint8_t *cursor = data;
    uint64_t total = 0;
    while (total < bytes) {
        const int64_t count = pwrite(file->fd, cursor + total, bytes - total, (int64_t)(offset + total));
        if (count > 0) {
            total += (uint64_t)count;
        } else if (count == 0) {
            errno = EIO;
            return FILE_ERROR_IO;
        } else if (errno != EINTR) {
            return file_status_from_errno();
        }
    }
    return FILE_OK;
}

file_status_t file_size(const file_t *file, uint64_t *out_bytes) {
    struct stat info;
    if (fstat(file->fd, &info) != 0) {
        return file_status_from_errno();
    }
    *out_bytes = (uint64_t)info.st_size;
    return FILE_OK;
}

file_status_t file_sync(const file_t *file) {
    int32_t result = 0;
    do {
        result = fsync(file->fd);
    } while (result != 0 && errno == EINTR);
    return result == 0 ? FILE_OK : file_status_from_errno();
}

file_status_t file_read_whole(const char *path, void *out, uint64_t capacity, uint64_t *out_bytes) {
    file_t file;
    file_status_t status = file_open(&file, path, FILE_MODE_READ);
    if (status != FILE_OK) {
        return status;
    }
    status = file_read(&file, out, capacity, out_bytes);
    if (status == FILE_OK && *out_bytes == capacity) {
        /* Exactly full says nothing about whether more is coming, and st_size cannot be trusted to tell. So ask. */
        uint8_t extra = 0;
        uint64_t extra_bytes = 0;
        status = file_read(&file, &extra, sizeof(extra), &extra_bytes);
        if (status == FILE_OK && extra_bytes != 0) {
            status = FILE_ERROR_TOO_LARGE;
        }
    }
    const int32_t saved_errno = errno;
    /* Nothing was written, so a failure to close loses nothing. */
    (void)file_close(&file);
    errno = saved_errno;
    return status;
}

/* Copies the directory part of path into out. "a/b" gives "a", "/b" gives "/", and "b" gives ".". The caller has
 * checked that path fits out. */
static void file_parent_of(const char *path, char *out) {
    const char *slash = strrchr(path, '/');
    if (slash == nullptr) {
        memcpy(out, ".", 2u);
        return;
    }
    const uint64_t parent_length = slash == path ? 1u : (uint64_t)(slash - path);
    memcpy(out, path, parent_length);
    out[parent_length] = '\0';
}

file_status_t file_write_atomic(const char *path, const void *data, uint64_t bytes) {
    static const char suffix[] = ".tmp";
    const uint64_t length = strlen(path);
    if (length == 0) {
        errno = ENOENT;
        return FILE_ERROR_NOT_FOUND;
    }
    if (length + sizeof(suffix) > FILE_PATH_BYTES) {
        errno = ENAMETOOLONG;
        return FILE_ERROR_PATH_TOO_LONG;
    }
    char temporary[FILE_PATH_BYTES];
    memcpy(temporary, path, length);
    memcpy(temporary + length, suffix, sizeof(suffix));
    char parent[FILE_PATH_BYTES];
    file_parent_of(path, parent);

    file_t file;
    file_status_t status = file_open(&file, temporary, FILE_MODE_WRITE);
    if (status != FILE_OK) {
        return status;
    }
    status = file_write_all(&file, data, bytes);
    if (status == FILE_OK) {
        status = file_sync(&file);
    }
    const int32_t saved_errno = errno;
    const file_status_t close_status = file_close(&file);
    if (status == FILE_OK) {
        status = close_status;
    } else {
        errno = saved_errno;
    }
    if (status == FILE_OK) {
        status = file_rename(temporary, path);
    }
    if (status != FILE_OK) {
        const int32_t failure_errno = errno;
        (void)file_remove(temporary); /* the failure above is the one to report */
        errno = failure_errno;
        return status;
    }
    return file_sync_dir(parent);
}

file_status_t file_path_size(const char *path, uint64_t *out_bytes) {
    struct stat info;
    if (stat(path, &info) != 0) {
        return file_status_from_errno();
    }
    *out_bytes = (uint64_t)info.st_size;
    return FILE_OK;
}

file_status_t file_remove(const char *path) {
    return unlink(path) == 0 ? FILE_OK : file_status_from_errno();
}

file_status_t file_rename(const char *from_path, const char *to_path) {
    return rename(from_path, to_path) == 0 ? FILE_OK : file_status_from_errno();
}

/* EEXIST is only fine if what exists is a directory. */
static file_status_t file_make_one_dir(const char *path) {
    if (mkdir(path, FILE_DIR_PERMISSIONS) == 0) {
        return FILE_OK;
    }
    if (errno != EEXIST) {
        return file_status_from_errno();
    }
    struct stat info;
    if (stat(path, &info) != 0) {
        return file_status_from_errno();
    }
    if (!S_ISDIR(info.st_mode)) {
        errno = EEXIST;
        return FILE_ERROR_EXISTS;
    }
    return FILE_OK;
}

file_status_t file_make_dirs(const char *path) {
    const uint64_t length = strlen(path);
    if (length == 0) {
        errno = ENOENT;
        return FILE_ERROR_NOT_FOUND;
    }
    if (length >= FILE_PATH_BYTES) {
        errno = ENAMETOOLONG;
        return FILE_ERROR_PATH_TOO_LONG;
    }
    char partial[FILE_PATH_BYTES];
    memcpy(partial, path, length + 1u);
    /* Starts at 1, because a leading slash is the root and not a directory to make. */
    for (uint64_t i = 1; i < length; i++) {
        if (partial[i] == '/' && partial[i - 1u] != '/') {
            partial[i] = '\0';
            const file_status_t status = file_make_one_dir(partial);
            partial[i] = '/';
            if (status != FILE_OK) {
                return status;
            }
        }
    }
    return file_make_one_dir(partial);
}

file_status_t file_sync_dir(const char *path) {
    int32_t fd = -1;
    do {
        fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return file_status_from_errno();
    }
    file_t directory = {.fd = fd, ._pad0 = {0}};
    file_status_t status = file_sync(&directory);
    const int32_t saved_errno = errno;
    const file_status_t close_status = file_close(&directory);
    if (status == FILE_OK) {
        status = close_status;
    } else {
        errno = saved_errno;
    }
    return status;
}
