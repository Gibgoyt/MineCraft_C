/* ctest run_save_power. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "file/index.h"
#include "job/index.h"
#include "log/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "thread/index.h"
#include "ui/menu/catalog/index.h"

#include <fcntl.h>
#include <ftw.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

enum : uint32_t {
    PW_PATH_BYTES = 256u,
    PW_OPS_MAX = 4096u,
    PW_FILES_MAX = 2048u,
    PW_ENTRIES_MAX = 512u,
    PW_DIRS_MAX = 16u,
    PW_SAVES = 6u,
    PW_SAVE_NO_BLOCKS = 5u, /* this one of them only has a new state for the blocks already saved */
    PW_NO_FILE = UINT32_MAX,
    PW_IS_DIRECTORY = UINT32_MAX - 1u,
    PW_SNAPSHOT_NONE = UINT32_MAX,
    PW_COLUMN_VOXELS = COLUMN_SECTIONS * SECTION_VOXELS,
};
enum : uint64_t {
    PW_FILE_BYTES = 16ull << 20,
};
static const uint64_t PW_SEED = 4242u;

/* ---- What the program asked of the file system, in order ------------------------------------------------------------------- */

typedef enum pw_op_kind_t : uint32_t {
    PW_OP_CREATE = 0,     /* a name for a new, empty file */
    PW_OP_MKDIR = 1,      /* a name for a new directory */
    PW_OP_RENAME = 2,     /* path to other, replacing whatever other was */
    PW_OP_UNLINK = 3,     /* the name goes */
    PW_OP_FSYNC_FILE = 4, /* the file's contents as they stand, in snapshot */
    PW_OP_FSYNC_DIR = 5,  /* every name in path, as they stand */
} pw_op_kind_t;

typedef struct pw_op_t {
    char path[PW_PATH_BYTES];  /* 0 */
    char other[PW_PATH_BYTES]; /* 256 */
    uint32_t kind;             /* 512: pw_op_kind_t */
    uint32_t file;             /* 516: an index into pw_recorder_t's files, for CREATE and FSYNC_FILE */
    uint32_t snapshot;         /* 520: for FSYNC_FILE */
    uint8_t _pad0[4];          /* 524 */
} pw_op_t;
static_assert(sizeof(pw_op_t) == 528);

/* Set up by the test while one thread runs. Only a test's own memory: the program under test owns nothing here. */
typedef struct pw_recorder_t {
    pw_op_t *borrowed_ops;       /* 0 */
    uint64_t *borrowed_inodes;   /* 8: of each file, by the index in an op */
    const char *borrowed_under;  /* 16: what is recorded is what is in here */
    const char *borrowed_shadow; /* 24: where the contents of each sync are kept */
    uint8_t *borrowed_bytes;     /* 32: PW_FILE_BYTES */
    uint32_t op_count;           /* 40 */
    uint32_t file_count;         /* 44 */
    uint32_t snapshot_count;     /* 48 */
    uint8_t on;                  /* 52 */
    uint8_t _pad0[3];            /* 53 */
} pw_recorder_t;
static_assert(sizeof(pw_recorder_t) == 56);

/* The interposed calls below have no other way to find it: libc's signatures have no room for a pointer. Borrowed, and only ever from the test's arena. */
static pw_recorder_t borrowed_recorder;

static bool pw_recording(const char *path) {
    const pw_recorder_t *const recorder = &borrowed_recorder;
    return recorder->on != 0u && path != nullptr &&
           strncmp(path, recorder->borrowed_under, strlen(recorder->borrowed_under)) == 0;
}

static pw_op_t *pw_op_add(uint32_t kind, const char *path, const char *other) {
    pw_recorder_t *const recorder = &borrowed_recorder;
    ASSERT_ALWAYS(recorder->op_count < PW_OPS_MAX);
    pw_op_t *const op = &recorder->borrowed_ops[recorder->op_count++];
    memset(op, 0, sizeof(*op));
    op->kind = kind;
    op->file = PW_NO_FILE;
    op->snapshot = PW_SNAPSHOT_NONE;
    (void)snprintf(op->path, sizeof(op->path), "%s", path);
    if (other != nullptr) {
        (void)snprintf(op->other, sizeof(op->other), "%s", other);
    }
    return op;
}

/* These stand in front of libc's, for the whole of this program: file/ calls them with no idea, as it would libc's. Each does what was asked, by the system call, and
 * notes it. No write is noted: what a write did only matters when a sync says so. */

int32_t open(const char *path, int32_t flags, ...) {
    uint32_t mode = 0u;
    if ((flags & (O_CREAT | O_TMPFILE)) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, uint32_t);
        va_end(arguments);
    }
    struct stat before;
    const bool was_there = stat(path, &before) == 0;
    const int32_t fd = (int32_t)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
    if (fd >= 0 && (flags & O_CREAT) != 0 && (!was_there || (flags & O_TRUNC) != 0) && pw_recording(path)) {
        struct stat after;
        ASSERT_ALWAYS(fstat(fd, &after) == 0);
        pw_recorder_t *const recorder = &borrowed_recorder;
        ASSERT_ALWAYS(recorder->file_count < PW_FILES_MAX);
        pw_op_t *const op = pw_op_add(PW_OP_CREATE, path, nullptr);
        op->file = recorder->file_count;
        recorder->borrowed_inodes[recorder->file_count++] = (uint64_t)after.st_ino;
    }
    return fd;
}

int32_t mkdir(const char *path, mode_t mode) {
    const int32_t result = (int32_t)syscall(SYS_mkdirat, AT_FDCWD, path, mode);
    if (result == 0 && pw_recording(path)) {
        (void)pw_op_add(PW_OP_MKDIR, path, nullptr);
    }
    return result;
}

int32_t rename(const char *from, const char *to) {
    const int32_t result = (int32_t)syscall(SYS_renameat2, AT_FDCWD, from, AT_FDCWD, to, 0u);
    if (result == 0 && pw_recording(from)) {
        (void)pw_op_add(PW_OP_RENAME, from, to);
    }
    return result;
}

int32_t unlink(const char *path) {
    const int32_t result = (int32_t)syscall(SYS_unlinkat, AT_FDCWD, path, 0);
    if (result == 0 && pw_recording(path)) {
        (void)pw_op_add(PW_OP_UNLINK, path, nullptr);
    }
    return result;
}

/* Where fd's file is now, by what the kernel says. */
static bool pw_fd_path(int32_t fd, char *out, uint64_t capacity) {
    char link[64];
    (void)snprintf(link, sizeof(link), "/proc/self/fd/%" PRId32, fd);
    const int64_t bytes = readlink(link, out, capacity - 1u);
    if (bytes <= 0) {
        return false;
    }
    out[bytes] = '\0';
    return true;
}

/* The contents of what fd has open, as they stand, kept under a number of their own. */
static uint32_t pw_snapshot_take(const char *path) {
    pw_recorder_t *const recorder = &borrowed_recorder;
    const uint32_t number = recorder->snapshot_count++;
    char target[PW_PATH_BYTES + 32u];
    (void)snprintf(target, sizeof(target), "%s/%" PRIu32, recorder->borrowed_shadow, number);
    uint64_t bytes = 0u;
    ASSERT_ALWAYS(file_read_whole(path, recorder->borrowed_bytes, PW_FILE_BYTES, &bytes) == FILE_OK);
    file_t file;
    ASSERT_ALWAYS(file_open(&file, target, FILE_MODE_WRITE) == FILE_OK);
    ASSERT_ALWAYS(file_write_all(&file, recorder->borrowed_bytes, bytes) == FILE_OK);
    ASSERT_ALWAYS(file_close(&file) == FILE_OK);
    return number;
}

int32_t fsync(int32_t fd) {
    pw_recorder_t *const recorder = &borrowed_recorder;
    char path[PW_PATH_BYTES];
    const int32_t result = (int32_t)syscall(SYS_fsync, fd);
    if (result != 0 || recorder->on == 0u || !pw_fd_path(fd, path, sizeof(path)) || !pw_recording(path)) {
        return result;
    }
    struct stat info;
    ASSERT_ALWAYS(fstat(fd, &info) == 0);
    if (S_ISDIR(info.st_mode)) {
        (void)pw_op_add(PW_OP_FSYNC_DIR, path, nullptr);
        return result;
    }
    /* The newest with this number: a number comes back into use when its file goes. */
    uint32_t file = recorder->file_count;
    while (file > 0u && recorder->borrowed_inodes[file - 1u] != (uint64_t)info.st_ino) {
        file--;
    }
    ASSERT_ALWAYS(file != 0u);
    const uint8_t was_on = recorder->on;
    recorder->on = 0u;
    const uint32_t snapshot = pw_snapshot_take(path);
    recorder->on = was_on;
    pw_op_t *const op = pw_op_add(PW_OP_FSYNC_FILE, path, nullptr);
    op->file = file - 1u;
    op->snapshot = snapshot;
    return result;
}

/* ---- What a power cut can leave --------------------------------------------------------------------------------------------- */

/* One name, as a directory would hold it. */
typedef struct pw_entry_t {
    char path[PW_PATH_BYTES]; /* 0 */
    uint32_t file;            /* 256: or PW_IS_DIRECTORY */
    uint8_t _pad0[4];         /* 260 */
} pw_entry_t;
static_assert(sizeof(pw_entry_t) == 264);

typedef struct pw_names_t {
    pw_entry_t *borrowed_entries; /* 0: PW_ENTRIES_MAX */
    uint32_t count;               /* 8 */
    uint8_t _pad0[4];             /* 12 */
} pw_names_t;
static_assert(sizeof(pw_names_t) == 16);

static pw_entry_t *pw_names_find(const pw_names_t *names, const char *path) {
    for (uint32_t i = 0u; i < names->count; i++) {
        if (strcmp(names->borrowed_entries[i].path, path) == 0) {
            return &names->borrowed_entries[i];
        }
    }
    return nullptr;
}

static void pw_names_set(pw_names_t *names, const char *path, uint32_t file) {
    pw_entry_t *found = pw_names_find(names, path);
    if (found == nullptr) {
        ASSERT_ALWAYS(names->count < PW_ENTRIES_MAX);
        found = &names->borrowed_entries[names->count++];
        (void)snprintf(found->path, sizeof(found->path), "%s", path);
    }
    found->file = file;
}

static void pw_names_remove(pw_names_t *names, const char *path) {
    pw_entry_t *const found = pw_names_find(names, path);
    if (found != nullptr) {
        *found = names->borrowed_entries[--names->count];
    }
}

/* What a change of names does to a list of them. */
static void pw_names_apply(pw_names_t *names, const pw_op_t *op) {
    switch ((pw_op_kind_t)op->kind) {
    case PW_OP_CREATE:
        pw_names_set(names, op->path, op->file);
        break;
    case PW_OP_MKDIR:
        pw_names_set(names, op->path, PW_IS_DIRECTORY);
        break;
    case PW_OP_RENAME: {
        const pw_entry_t *const from = pw_names_find(names, op->path);
        /* With the name it came from not kept, nothing of this reached the disk that can be read. */
        if (from != nullptr) {
            const uint32_t file = from->file;
            pw_names_remove(names, op->path);
            pw_names_set(names, op->other, file);
        }
        break;
    }
    case PW_OP_UNLINK:
        pw_names_remove(names, op->path);
        break;
    case PW_OP_FSYNC_FILE:
    case PW_OP_FSYNC_DIR:
        break;
    }
}

/* The directory part of a path into out. */
static void pw_parent(const char *path, char *out) {
    (void)snprintf(out, PW_PATH_BYTES, "%s", path);
    char *const slash = strrchr(out, '/');
    if (slash != nullptr) {
        *slash = '\0';
    }
}

static bool pw_op_in(const pw_op_t *op, const char *directory) {
    char parent[PW_PATH_BYTES];
    pw_parent(op->path, parent);
    return strcmp(parent, directory) == 0;
}

/* The worst a file system may do and still keep the promises of fsync. Up to op_end, of all the changes to the names in each directory since it was last synced, some
 * are kept and some not: and a file has only what was last synced of it, whatever its name says. How many are kept in each is what a cut chooses. */
typedef struct pw_cut_t {
    pw_names_t durable;                   /* 0: as of each directory's last sync */
    pw_names_t current;                   /* 16: as they were at op_end */
    uint32_t *borrowed_synced;            /* 32: PW_FILES_MAX: each file's last snapshot, or PW_SNAPSHOT_NONE */
    char (*borrowed_dirs)[PW_PATH_BYTES]; /* 40: PW_DIRS_MAX: those with changes not synced */
    uint32_t dir_count;                   /* 48 */
    uint8_t _pad0[4];                     /* 52 */
} pw_cut_t;
static_assert(sizeof(pw_cut_t) == 56);

static void pw_dirs_note(pw_cut_t *cut, const char *directory) {
    for (uint32_t i = 0u; i < cut->dir_count; i++) {
        if (strcmp(cut->borrowed_dirs[i], directory) == 0) {
            return;
        }
    }
    ASSERT_ALWAYS(cut->dir_count < PW_DIRS_MAX);
    (void)snprintf(cut->borrowed_dirs[cut->dir_count], PW_PATH_BYTES, "%s", directory);
    cut->dir_count++;
}

/* Every name a directory has had made durable replaces what durable says of that directory. */
static void pw_dir_synced(pw_cut_t *cut, const char *directory) {
    for (uint32_t i = cut->durable.count; i-- > 0u;) {
        char parent[PW_PATH_BYTES];
        pw_parent(cut->durable.borrowed_entries[i].path, parent);
        if (strcmp(parent, directory) == 0) {
            pw_names_remove(&cut->durable, cut->durable.borrowed_entries[i].path);
        }
    }
    for (uint32_t i = 0u; i < cut->current.count; i++) {
        const pw_entry_t *const entry = &cut->current.borrowed_entries[i];
        char parent[PW_PATH_BYTES];
        pw_parent(entry->path, parent);
        if (strcmp(parent, directory) == 0) {
            pw_names_set(&cut->durable, entry->path, entry->file);
        }
    }
}

static void pw_cut_begin(pw_cut_t *cut) {
    cut->durable.count = 0u;
    cut->current.count = 0u;
    cut->dir_count = 0u;
    for (uint32_t i = 0u; i < PW_FILES_MAX; i++) {
        cut->borrowed_synced[i] = PW_SNAPSHOT_NONE;
    }
}

/* One more op into what is known. */
static void pw_cut_step(pw_cut_t *cut, const pw_op_t *op) {
    pw_names_apply(&cut->current, op);
    if (op->kind == PW_OP_FSYNC_FILE) {
        cut->borrowed_synced[op->file] = op->snapshot;
    } else if (op->kind == PW_OP_FSYNC_DIR) {
        pw_dir_synced(cut, op->path);
    } else {
        char parent[PW_PATH_BYTES];
        pw_parent(op->path, parent);
        pw_dirs_note(cut, parent);
    }
}

/* How many changes to a directory's names have not been synced by op_end. */
static uint32_t pw_pending_count(const pw_recorder_t *recorder, uint32_t op_end, const char *directory) {
    uint32_t pending = 0u;
    for (uint32_t i = 0u; i < op_end; i++) {
        const pw_op_t *const op = &recorder->borrowed_ops[i];
        if (op->kind == PW_OP_FSYNC_DIR && strcmp(op->path, directory) == 0) {
            pending = 0u;
        } else if (op->kind != PW_OP_FSYNC_DIR && op->kind != PW_OP_FSYNC_FILE && pw_op_in(op, directory)) {
            pending++;
        }
    }
    return pending;
}

/* What a cut keeps of the pending changes in one directory. */
typedef struct pw_choice_t {
    uint32_t kept;    /* 0: the first this many */
    uint32_t skipped; /* 4: and of those, leaving out this one, or UINT32_MAX for none */
} pw_choice_t;
static_assert(sizeof(pw_choice_t) == 8);

/* durable with a directory's chosen changes on top, into out. */
static void pw_choice_apply(pw_names_t *out, const pw_recorder_t *recorder, uint32_t op_end, const char *directory,
                            pw_choice_t choice) {
    uint32_t seen = 0u;
    uint32_t since = 0u;
    for (uint32_t i = 0u; i < op_end; i++) {
        const pw_op_t *const op = &recorder->borrowed_ops[i];
        if (op->kind == PW_OP_FSYNC_DIR && strcmp(op->path, directory) == 0) {
            since = i + 1u;
        }
    }
    for (uint32_t i = since; i < op_end; i++) {
        const pw_op_t *const op = &recorder->borrowed_ops[i];
        if (op->kind == PW_OP_FSYNC_DIR || op->kind == PW_OP_FSYNC_FILE || !pw_op_in(op, directory)) {
            continue;
        }
        if (seen < choice.kept && seen != choice.skipped) {
            pw_names_apply(out, op);
        }
        seen++;
    }
}

/* ---- Writing it out and reading it back ----------------------------------------------------------------------------------- */

static int32_t pw_tree_remove_one(const char *path, const struct stat *info, int32_t flag, struct FTW *where) {
    (void)info;
    (void)where;
    return flag == FTW_DP ? rmdir(path) : unlink(path);
}

static void pw_tree_remove(const char *path) {
    (void)nftw(path, pw_tree_remove_one, 16, FTW_DEPTH | FTW_PHYS);
}

/* The same path under another root. */
static void pw_moved(char *out, uint64_t capacity, const char *path, const char *from, const char *to) {
    (void)snprintf(out, capacity, "%s%s", to, path + strlen(from));
}

/* A list of names as a real directory tree, each file with what was last synced of it. What has lost the directory it was in is gone with it. */
static void pw_tree_write(test_context_t *t, const pw_names_t *names, const uint32_t *synced, const char *under,
                          const char *shadow, const char *to, uint8_t *bytes) {
    /* Parents before what is in them: a shorter path cannot be inside a longer one. */
    char path[PW_PATH_BYTES + 64u];
    for (uint32_t length = 0u; length < PW_PATH_BYTES; length++) {
        for (uint32_t i = 0u; i < names->count; i++) {
            const pw_entry_t *const entry = &names->borrowed_entries[i];
            if (strlen(entry->path) != length || entry->file != PW_IS_DIRECTORY) {
                continue;
            }
            pw_moved(path, sizeof(path), entry->path, under, to);
            (void)mkdir(path, 0755u); /* fails with no parent, which is what a lost parent means */
        }
    }
    for (uint32_t i = 0u; i < names->count; i++) {
        const pw_entry_t *const entry = &names->borrowed_entries[i];
        if (entry->file == PW_IS_DIRECTORY) {
            continue;
        }
        pw_moved(path, sizeof(path), entry->path, under, to);
        char parent[PW_PATH_BYTES + 64u];
        (void)snprintf(parent, sizeof(parent), "%s", path);
        *strrchr(parent, '/') = '\0';
        struct stat info;
        if (stat(parent, &info) != 0) {
            continue;
        }
        uint64_t count = 0u;
        if (synced[entry->file] != PW_SNAPSHOT_NONE) {
            char kept[PW_PATH_BYTES + 32u];
            (void)snprintf(kept, sizeof(kept), "%s/%" PRIu32, shadow, synced[entry->file]);
            CHECK_EQ_U64(file_read_whole(kept, bytes, PW_FILE_BYTES, &count), FILE_OK);
        }
        file_t file;
        if (CHECK_EQ_U64(file_open(&file, path, FILE_MODE_WRITE), FILE_OK)) {
            CHECK_EQ_U64(file_write_all(&file, bytes, count), FILE_OK);
            CHECK_EQ_U64(file_close(&file), FILE_OK);
        }
    }
}

/* What the test did, and when it was told it had been done. */
typedef struct pw_save_t {
    uint64_t generation; /* 0 */
    uint64_t tick;       /* 8 */
    uint32_t op_begin;   /* 16: ops before this came before it was asked for */
    uint32_t op_end;     /* 20: ops from this on came after it returned */
} pw_save_t;
static_assert(sizeof(pw_save_t) == 24);

typedef struct pw_model_t {
    pw_save_t saves[PW_SAVES]; /* 0 */
    uint32_t save_count;       /* 144 */
    uint32_t created_op;       /* 148: ops from this on came after the world was reported made */
    char world[PW_PATH_BYTES]; /* 152: relative to the root */
} pw_model_t;
static_assert(sizeof(pw_model_t) == 408);

static void pw_stamp_cell(uint32_t stamp, int32_t *x, int32_t *y, int32_t *z) {
    /* In turn in the columns of two regions, so that a save writes a file or two and some replace what came before. */
    const bool far = (stamp & 1u) != 0u;
    *x = (far ? -16 : 0) + (int32_t)(stamp * 3u);
    *y = (far ? -16 : 0) + (int32_t)(stamp * 2u);
    *z = 400 + (int32_t)stamp;
}

typedef struct pw_reader_t {
    uint16_t *states; /* 0 */
    uint8_t *scratch; /* 8 */
    save_t *save;     /* 16 */
    uint8_t *bytes;   /* 24 */
} pw_reader_t;
static_assert(sizeof(pw_reader_t) == 32);

/* What is in a directory that a power cut left, against what the program has been told and the promises it makes. Returns how many of them it broke. */
static uint32_t pw_cut_check(const pw_reader_t *reader, const pw_model_t *model, uint32_t op_end,
                             const char *cut_root) {
    char world[PW_PATH_BYTES + 64u];
    (void)snprintf(world, sizeof(world), "%s/%s", cut_root, model->world);
    struct stat info;
    uint32_t broken = 0u;
    /* Until it was reported made, it may or may not be there. */
    if (stat(world, &info) != 0) {
        return op_end >= model->created_op ? 1u : 0u;
    }
    /* Asked for once it was reported: the most recent save that returned is the least there may be. */
    uint64_t least_generation = 0u, least_tick = 0u;
    for (uint32_t i = 0u; i < model->save_count; i++) {
        if (model->saves[i].op_end <= op_end) {
            least_generation = model->saves[i].generation;
            least_tick = model->saves[i].tick;
        }
    }
    save_t *const save = reader->save;
    const save_world_status_t status = save_open(save, world, PW_SEED);
    if (status != SAVE_WORLD_OK) {
        /* A world whose first save has not been asked for has nothing to lose. */
        return 1u;
    }
    const uint64_t generation = save->world.generation;
    if (generation < least_generation) {
        broken++;
    }
    /* Blocks: those of this save, and no others. */
    uint64_t tick = 0u;
    if (save->state.kept != 0u) {
        uint64_t bytes = 0u;
        const uint8_t *const data = save_state_section_find(save->state.borrowed_image, save->state.image_bytes,
                                                            SAVE_STATE_SECTION_TIME, &bytes);
        if (data != nullptr) {
            save_state_time_t time;
            memcpy(&time, data, sizeof(time));
            tick = time.world_ticks;
        }
    } else if (generation != 0u) {
        broken++; /* blocks with no state to go with them: the inventory would be gone */
    }
    if (tick < least_tick) {
        broken++;
    }
    /* Whatever state it read belongs with these blocks. */
    if (generation != 0u) {
        bool found = false;
        for (uint32_t i = 0u; i < model->save_count; i++) {
            found = found || (model->saves[i].generation == generation && model->saves[i].tick == tick);
        }
        broken += found ? 0u : 1u;
    }
    for (uint32_t stamp = 1u; stamp <= model->save_count; stamp++) {
        if (stamp == PW_SAVE_NO_BLOCKS) {
            continue;
        }
        int32_t x, y, z;
        pw_stamp_cell(stamp, &x, &y, &z);
        memset(reader->states, 0, PW_COLUMN_VOXELS * sizeof(uint16_t));
        (void)save_world_column_overlay(&save->world, column_chunk_of(x), column_chunk_of(y), reader->states,
                                        reader->scratch);
        const uint32_t storage = (uint32_t)(z - COLUMN_Z_STORAGE_MIN);
        const uint32_t voxel = section_voxel_index(column_local_of(x), column_local_of(y), storage % SECTION_SIZE);
        const uint16_t found = reader->states[(uint64_t)(storage / SECTION_SIZE) * SECTION_VOXELS + voxel];
        const pw_save_t *const stamped = &model->saves[stamp - 1u];
        if ((found == BLOCK_ID_STONE) != (stamped->generation <= generation && stamped->generation != 0u)) {
            broken++;
        }
    }
    if (atomic_load_explicit(&save->world.sections_damaged, memory_order_relaxed) != 0u) {
        broken++;
    }
    save_close(save);
    return broken;
}

static const char *pw_op_name(uint32_t kind) {
    static const char *const names[] = {"create", "mkdir", "rename", "unlink", "fsync file", "fsync dir"};
    return names[kind];
}

/* ---- The test --------------------------------------------------------------------------------------------------------------- */

TEST(whatever_a_power_cut_leaves_opens_as_one_whole_save_no_older_than_the_last_one_reported) {
    char root[64] = "/tmp/mc_save_power_XXXXXX";
    REQUIRE(mkdtemp(root) != nullptr);
    char *const saves = test_alloc(t, PW_PATH_BYTES);
    char *const shadow = test_alloc(t, PW_PATH_BYTES);
    char *const cuts = test_alloc(t, PW_PATH_BYTES);
    (void)snprintf(saves, PW_PATH_BYTES, "%s/saves", root);
    (void)snprintf(shadow, PW_PATH_BYTES, "%s/shadow", root);
    (void)snprintf(cuts, PW_PATH_BYTES, "%s/cuts", root);
    REQUIRE(mkdir(shadow, 0755u) == 0);
    REQUIRE(mkdir(cuts, 0755u) == 0);

    borrowed_recorder = (pw_recorder_t){.borrowed_ops = test_alloc(t, PW_OPS_MAX * sizeof(pw_op_t)),
                                        .borrowed_inodes = test_alloc(t, PW_FILES_MAX * sizeof(uint64_t)),
                                        .borrowed_under = root,
                                        .borrowed_shadow = shadow,
                                        .borrowed_bytes = test_alloc(t, PW_FILE_BYTES)};
    pw_recorder_t *const recorder = &borrowed_recorder;
    pw_model_t *const model = test_alloc(t, sizeof(pw_model_t));
    memset(model, 0, sizeof(*model));

    test_world_t w;
    REQUIRE(test_world_begin(t, &w));
    job_system_t *const job = test_world_aligned(t, sizeof(job_system_t));
    thread_topology_t *const topology = test_alloc(t, sizeof(thread_topology_t));
    REQUIRE(thread_topology_read(topology) && job_init(job, topology));
    save_t *const save = test_world_aligned(t, sizeof(save_t));
    REQUIRE(save_init(save));

    /* Everything from the world being made on: it is all recorded, none of it faked. */
    recorder->on = 1u;
    ui_menu_world_t made;
    REQUIRE(ui_menu_catalog_create(saves, "w", PW_SEED, &made) == FILE_OK);
    model->created_op = recorder->op_count;
    (void)snprintf(model->world, sizeof(model->world), "saves/%s", made.directory);
    char *const world = test_alloc(t, 2u * PW_PATH_BYTES);
    (void)snprintf(world, 2u * PW_PATH_BYTES, "%s/%s", root, model->world);
    REQUIRE(save_open(save, world, PW_SEED) == SAVE_WORLD_OK);
    save_autosave_start(&save->autosave, job, &save->world, &save->state, 0u);

    save_state_parts_t parts = {0};
    uint64_t tick = 100u;
    for (uint32_t stamp = 1u; stamp <= PW_SAVES; stamp++) {
        pw_save_t *const each = &model->saves[stamp - 1u];
        each->op_begin = recorder->op_count;
        parts.time.world_ticks = tick;
        if (stamp != PW_SAVE_NO_BLOCKS) {
            int32_t x, y, z;
            pw_stamp_cell(stamp, &x, &y, &z);
            REQUIRE(world_edit_block_set(&w.world->edit, x, y, z, BLOCK_ID_STONE) == WORLD_EDIT_STATUS_CHANGED);
        }
        REQUIRE(save_autosave_finish(&save->autosave, &w.world->edit, &parts) == SAVE_WORLD_OK);
        each->op_end = recorder->op_count;
        each->generation = save->world.generation;
        each->tick = tick;
        tick += 100u;
        model->save_count = stamp;
    }
    save_close(save);
    recorder->on = 0u;
    const uint32_t op_count = recorder->op_count;
    /* Plenty of each, or something stopped being recorded and this proves nothing. */
    uint32_t kinds[6] = {0};
    for (uint32_t i = 0u; i < op_count; i++) {
        kinds[recorder->borrowed_ops[i].kind]++;
    }
    printf("  %" PRIu32 " ops: %" PRIu32 " create, %" PRIu32 " mkdir, %" PRIu32 " rename, %" PRIu32 " unlink, %" PRIu32
           " fsync of a file, %" PRIu32 " of a directory; %" PRIu64 " generations\n",
           op_count, kinds[PW_OP_CREATE], kinds[PW_OP_MKDIR], kinds[PW_OP_RENAME], kinds[PW_OP_UNLINK],
           kinds[PW_OP_FSYNC_FILE], kinds[PW_OP_FSYNC_DIR], model->saves[PW_SAVES - 1u].generation);
    CHECK(kinds[PW_OP_CREATE] >= 10u && kinds[PW_OP_MKDIR] >= 2u && kinds[PW_OP_RENAME] >= 10u &&
          kinds[PW_OP_UNLINK] >= 3u && kinds[PW_OP_FSYNC_FILE] >= 10u && kinds[PW_OP_FSYNC_DIR] >= 10u);
    CHECK_EQ_U64(model->saves[PW_SAVES - 1u].generation, PW_SAVES - 1u);

    pw_cut_t *const cut = test_alloc(t, sizeof(pw_cut_t));
    cut->durable.borrowed_entries = test_alloc(t, PW_ENTRIES_MAX * sizeof(pw_entry_t));
    cut->current.borrowed_entries = test_alloc(t, PW_ENTRIES_MAX * sizeof(pw_entry_t));
    cut->borrowed_synced = test_alloc(t, PW_FILES_MAX * sizeof(uint32_t));
    cut->borrowed_dirs = test_alloc(t, PW_DIRS_MAX * PW_PATH_BYTES);
    pw_names_t chosen = {.borrowed_entries = test_alloc(t, PW_ENTRIES_MAX * sizeof(pw_entry_t)), .count = 0u};
    const pw_reader_t reader = {.states = test_alloc(t, PW_COLUMN_VOXELS * sizeof(uint16_t)),
                                .scratch = test_alloc(t, SAVE_WORLD_OVERLAY_SCRATCH_BYTES),
                                .save = save,
                                .bytes = recorder->borrowed_bytes};
    char *const cut_root = test_alloc(t, PW_PATH_BYTES);
    (void)snprintf(cut_root, PW_PATH_BYTES, "%s/one", cuts);

    const int32_t quiet = open("/dev/null", O_WRONLY | O_CLOEXEC);
    const int32_t was = log_set_fd(quiet);
    uint64_t cuts_tried = 0u, cuts_broken = 0u;
    pw_cut_begin(cut);
    for (uint32_t op_end = 0u; op_end <= op_count; op_end++) {
        if (op_end > 0u) {
            pw_cut_step(cut, &recorder->borrowed_ops[op_end - 1u]);
        }
        /* One directory at a time takes each choice there is, the others keeping all or none. */
        for (uint32_t turn = 0u; turn < cut->dir_count; turn++) {
            for (uint32_t others_all = 0u; others_all < 2u; others_all++) {
                const uint32_t pending = pw_pending_count(recorder, op_end, cut->borrowed_dirs[turn]);
                /* Each first so many, then all but one of them. */
                for (uint32_t choice = 0u; choice <= 2u * pending; choice++) {
                    pw_choice_t ours = {.kept = choice <= pending ? choice : pending,
                                        .skipped = choice <= pending ? UINT32_MAX : choice - pending - 1u};
                    chosen.count = cut->durable.count;
                    memcpy(chosen.borrowed_entries, cut->durable.borrowed_entries,
                           (uint64_t)cut->durable.count * sizeof(pw_entry_t));
                    for (uint32_t d = 0u; d < cut->dir_count; d++) {
                        const uint32_t its = pw_pending_count(recorder, op_end, cut->borrowed_dirs[d]);
                        const pw_choice_t theirs = {.kept = others_all != 0u ? its : 0u, .skipped = UINT32_MAX};
                        pw_choice_apply(&chosen, recorder, op_end, cut->borrowed_dirs[d], d == turn ? ours : theirs);
                    }
                    (void)mkdir(cut_root, 0755u);
                    pw_tree_write(t, &chosen, cut->borrowed_synced, root, shadow, cut_root, reader.bytes);
                    const uint32_t broken = pw_cut_check(&reader, model, op_end, cut_root);
                    cuts_tried++;
                    if (broken != 0u) {
                        if (cuts_broken < 8u) {
                            const pw_op_t *const last = op_end > 0u ? &recorder->borrowed_ops[op_end - 1u] : nullptr;
                            printf("  cut after op %" PRIu32 " (%s %s): %s keeps %" PRIu32 " of %" PRIu32
                                   " changes, %s the others': %" PRIu32 " broken\n",
                                   op_end, last != nullptr ? pw_op_name(last->kind) : "nothing",
                                   last != nullptr ? last->path + strlen(root) : "",
                                   cut->borrowed_dirs[turn] + strlen(root), ours.kept, pending,
                                   others_all != 0u ? "all of" : "none of", broken);
                        }
                        cuts_broken++;
                    }
                    pw_tree_remove(cut_root);
                }
            }
        }
    }
    (void)log_set_fd(was);
    (void)close(quiet);
    printf("  %" PRIu64 " cuts tried, across %" PRIu32 " moments\n", cuts_tried, op_count);
    CHECK(cuts_tried >= 200u);
    CHECK_EQ_U64(cuts_broken, 0u);

    save_deinit(save);
    job_deinit(job);
    test_world_end(&w);
    pw_tree_remove(root);
    borrowed_recorder = (pw_recorder_t){0};
}

TEST_MAIN()
