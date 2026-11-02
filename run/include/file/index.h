/* See run/src/file/README.md. */
#ifndef MC_RUN_FILE_INDEX_H
#define MC_RUN_FILE_INDEX_H

#include <stdint.h>

/* Long enough for any path Linux accepts: PATH_MAX, with its NUL. */
enum { FILE_PATH_BYTES = 4096 };

typedef enum file_status_t : uint8_t {
    FILE_OK = 0,
    FILE_ERROR_NOT_FOUND,     /* ENOENT, ENOTDIR */
    FILE_ERROR_EXISTS,        /* a file is where file_make_dirs needs a directory */
    FILE_ERROR_ACCESS,        /* EACCES, EPERM, EROFS */
    FILE_ERROR_DISK_FULL,     /* ENOSPC, EDQUOT */
    FILE_ERROR_TOO_LARGE,     /* EFBIG, or the file does not fit the caller's buffer */
    FILE_ERROR_PATH_TOO_LONG, /* ENAMETOOLONG, or past FILE_PATH_BYTES */
    FILE_ERROR_END_OF_FILE,   /* the file ended before the bytes asked for */
    FILE_ERROR_IO,            /* anything else; errno still holds the reason */
} file_status_t;

typedef enum file_mode_t : uint8_t {
    FILE_MODE_READ = 0,   /* must exist */
    FILE_MODE_WRITE,      /* created if missing, emptied if not */
    FILE_MODE_APPEND,     /* created if missing, every write goes to the end */
    FILE_MODE_READ_WRITE, /* created if missing, contents kept */
} file_mode_t;

/* fd is -1 when closed. */
typedef struct file_t {
    int32_t fd;
    uint8_t _pad0[4];
} file_t;
static_assert(sizeof(file_t) == 8);

/* A short name for a log line: "disk full". */
const char *file_status_name(file_status_t status);

/* On failure out->fd is -1, so file_close on it is harmless. Descriptors do not survive an exec. */
[[nodiscard]] file_status_t file_open(file_t *out, const char *path, file_mode_t mode);
/* The descriptor is gone whatever this returns, and fd is -1. A failure means written data may not have reached the
 * disk. Closing a closed file_t is FILE_OK. */
file_status_t file_close(file_t *file);

/* Each of these goes on through short counts and signals until it has them all or has a real error. */

/* Reads until capacity is full or the file ends, and says how many bytes it got. Reaching the end is FILE_OK. */
[[nodiscard]] file_status_t file_read(const file_t *file, void *out, uint64_t capacity, uint64_t *out_bytes);
/* Exactly bytes bytes, or FILE_ERROR_END_OF_FILE. */
[[nodiscard]] file_status_t file_read_exact(const file_t *file, void *out, uint64_t bytes);
[[nodiscard]] file_status_t file_write_all(const file_t *file, const void *data, uint64_t bytes);
/* The same at an offset. These do not move the file position, and two threads may use one file_t at once. */
[[nodiscard]] file_status_t file_read_at(const file_t *file, uint64_t offset, void *out, uint64_t bytes);
[[nodiscard]] file_status_t file_write_at(const file_t *file, uint64_t offset, const void *data, uint64_t bytes);

[[nodiscard]] file_status_t file_size(const file_t *file, uint64_t *out_bytes);
/* Returns once the contents are on the disk. */
[[nodiscard]] file_status_t file_sync(const file_t *file);

/* The whole file into a buffer of the caller's. FILE_ERROR_TOO_LARGE if it does not fit, with no meaning in out. Works
 * on files whose stat size means nothing, such as those in /proc and /sys. */
[[nodiscard]] file_status_t file_read_whole(const char *path, void *out, uint64_t capacity, uint64_t *out_bytes);

/* Replaces path so that a crash at any moment leaves the old contents or the new, whole, and never a mix: writes
 * "<path>.tmp", syncs it, renames it over path, and syncs the directory. On failure the temporary file is removed and
 * path is as it was, unless only the last sync failed: then path has the new contents, which may not survive a power
 * cut. A crash can leave the temporary file behind, and the next call for that path overwrites it. One writer per path
 * at a time, because that name is fixed. */
[[nodiscard]] file_status_t file_write_atomic(const char *path, const void *data, uint64_t bytes);

/* The size of the file at path, without opening it. */
[[nodiscard]] file_status_t file_path_size(const char *path, uint64_t *out_bytes);
[[nodiscard]] file_status_t file_remove(const char *path);
/* Atomic, and replaces to_path if it exists. Both must be on one filesystem. */
[[nodiscard]] file_status_t file_rename(const char *from_path, const char *to_path);
/* Creates the directory and any parents it lacks. Already being there is FILE_OK. */
[[nodiscard]] file_status_t file_make_dirs(const char *path);
/* Makes a create, rename or remove in this directory reach the disk. */
[[nodiscard]] file_status_t file_sync_dir(const char *path);

#endif
