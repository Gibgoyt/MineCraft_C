/* ctest run_file. See README.md. */
#include "harness/index.h"

#include "file/index.h"
#include "time/index.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* A path under this test's own scratch directory, made if it is not there yet. */
static const char *scratch_path(test_context_t *t, const char *name) {
    char *path = test_alloc(t, FILE_PATH_BYTES);
    if (t->arg_count != 1) {
        test_check(t, false, __FILE__, __LINE__, "the scratch directory is the one argument");
        return "/nonexistent/no_scratch_directory";
    }
    const int32_t length = snprintf(path, FILE_PATH_BYTES, "%s/%s", t->borrowed_args[0], t->name);
    test_check(t, length > 0 && length < FILE_PATH_BYTES, __FILE__, __LINE__, "scratch directory fits");
    test_check(t, file_make_dirs(path) == FILE_OK, __FILE__, __LINE__, "scratch directory made");
    const int32_t full = snprintf(path, FILE_PATH_BYTES, "%s/%s/%s", t->borrowed_args[0], t->name, name);
    test_check(t, full > 0 && full < FILE_PATH_BYTES, __FILE__, __LINE__, "scratch path fits");
    return path;
}

/* Bytes that show where they came from, so a copy in the wrong place cannot match by accident. */
static void fill_pattern(uint8_t *out, uint64_t bytes, uint32_t seed) {
    uint32_t state = seed * 2654435761u + 1u;
    for (uint64_t i = 0; i < bytes; i++) {
        state = state * 1664525u + 1013904223u;
        out[i] = (uint8_t)(state >> 24u);
    }
}

/* ---- Opening, reading, writing ----------------------------------------------------------------------------------- */

TEST(round_trip) {
    const char *path = scratch_path(t, "data");
    enum : uint64_t { BYTES = 300000u };
    uint8_t *written = test_alloc(t, BYTES);
    uint8_t *read_back = test_alloc(t, BYTES);
    fill_pattern(written, BYTES, 1u);

    file_t file;
    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK(file.fd >= 0);
    CHECK_EQ_U64(file_write_all(&file, written, BYTES), FILE_OK);
    CHECK_EQ_U64(file_sync(&file), FILE_OK);
    uint64_t size = 0;
    CHECK_EQ_U64(file_size(&file, &size), FILE_OK);
    CHECK_EQ_U64(size, BYTES);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    CHECK_EQ_I64(file.fd, -1);

    REQUIRE(file_open(&file, path, FILE_MODE_READ) == FILE_OK);
    CHECK_EQ_U64(file_read_exact(&file, read_back, BYTES), FILE_OK);
    CHECK_MEM_EQ(read_back, written, BYTES);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
}

TEST(modes) {
    const char *path = scratch_path(t, "data");
    char text[16] = {0};
    uint64_t got = 0;
    file_t file;

    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_write_all(&file, "first", 5u), FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);

    /* Append adds, whatever the position would have been. */
    REQUIRE(file_open(&file, path, FILE_MODE_APPEND) == FILE_OK);
    CHECK_EQ_U64(file_write_all(&file, "+more", 5u), FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    CHECK_EQ_U64(file_read_whole(path, text, sizeof(text) - 1u, &got), FILE_OK);
    CHECK_STR_EQ(text, "first+more");

    /* Read-write keeps what is there. */
    REQUIRE(file_open(&file, path, FILE_MODE_READ_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_write_at(&file, 0u, "FIRST", 5u), FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    memset(text, 0, sizeof(text));
    CHECK_EQ_U64(file_read_whole(path, text, sizeof(text) - 1u, &got), FILE_OK);
    CHECK_STR_EQ(text, "FIRST+more");

    /* Write empties it. */
    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    CHECK_EQ_U64(file_path_size(path, &got), FILE_OK);
    CHECK_EQ_U64(got, 0u);

    /* Read cannot write. */
    REQUIRE(file_open(&file, path, FILE_MODE_READ) == FILE_OK);
    CHECK_EQ_U64(file_write_all(&file, "x", 1u), FILE_ERROR_IO);
    CHECK_EQ_I64(errno, EBADF);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
}

TEST(descriptors_do_not_survive_an_exec) {
    const char *path = scratch_path(t, "data");
    file_t file;
    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK((fcntl(file.fd, F_GETFD) & FD_CLOEXEC) != 0);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
}

TEST(reads_past_the_end) {
    const char *path = scratch_path(t, "data");
    file_t file;
    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_write_all(&file, "abc", 3u), FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);

    REQUIRE(file_open(&file, path, FILE_MODE_READ) == FILE_OK);
    uint8_t buffer[8];
    uint64_t got = 99;
    /* file_read calls the end normal and says how much it got. */
    CHECK_EQ_U64(file_read(&file, buffer, sizeof(buffer), &got), FILE_OK);
    CHECK_EQ_U64(got, 3u);
    CHECK_MEM_EQ(buffer, "abc", 3u);
    CHECK_EQ_U64(file_read(&file, buffer, sizeof(buffer), &got), FILE_OK);
    CHECK_EQ_U64(got, 0u);
    /* The others treat it as an error. */
    CHECK_EQ_U64(file_read_exact(&file, buffer, 1u), FILE_ERROR_END_OF_FILE);
    CHECK_EQ_U64(file_read_at(&file, 1u, buffer, 2u), FILE_OK);
    CHECK_MEM_EQ(buffer, "bc", 2u);
    CHECK_EQ_U64(file_read_at(&file, 1u, buffer, 3u), FILE_ERROR_END_OF_FILE);
    CHECK_EQ_U64(file_read_at(&file, 100u, buffer, 1u), FILE_ERROR_END_OF_FILE);
    /* And asking for nothing always succeeds. */
    CHECK_EQ_U64(file_read_exact(&file, buffer, 0u), FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
}

TEST(offsets_do_not_move_the_position) {
    const char *path = scratch_path(t, "data");
    file_t file;
    REQUIRE(file_open(&file, path, FILE_MODE_READ_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_write_all(&file, "0123456789", 10u), FILE_OK);
    CHECK_EQ_U64(file_write_at(&file, 2u, "ab", 2u), FILE_OK);
    /* A write at an offset past the end grows the file, and the gap reads as zero. */
    CHECK_EQ_U64(file_write_at(&file, 12u, "z", 1u), FILE_OK);
    CHECK_EQ_U64(file_write_all(&file, "END", 3u), FILE_OK); /* still at 10, so it covers 10 to 12 */

    uint8_t read_back[13];
    CHECK_EQ_U64(file_read_at(&file, 0u, read_back, sizeof(read_back)), FILE_OK);
    CHECK_MEM_EQ(read_back, "01ab456789END", sizeof(read_back));
    CHECK_EQ_I64(lseek(file.fd, 0, SEEK_CUR), 13);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
}

TEST(a_closed_file_is_an_error_and_closing_twice_is_not) {
    const char *path = scratch_path(t, "data");
    file_t file;
    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    uint8_t byte = 0;
    CHECK_EQ_U64(file_write_all(&file, &byte, 1u), FILE_ERROR_IO);
    CHECK_EQ_U64(file_read_exact(&file, &byte, 1u), FILE_ERROR_IO);
    CHECK_EQ_U64(file_size(&file, &(uint64_t){0}), FILE_ERROR_IO);
    CHECK_EQ_U64(file_sync(&file), FILE_ERROR_IO);
}

/* ---- What failures come back as ---------------------------------------------------------------------------------- */

TEST(failures_have_the_right_status) {
    const char *missing = scratch_path(t, "missing");
    file_t file;
    CHECK_EQ_U64(file_open(&file, missing, FILE_MODE_READ), FILE_ERROR_NOT_FOUND);
    CHECK_EQ_I64(file.fd, -1);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    CHECK_EQ_U64(file_remove(missing), FILE_ERROR_NOT_FOUND);
    CHECK_EQ_U64(file_path_size(missing, &(uint64_t){0}), FILE_ERROR_NOT_FOUND);
    CHECK_EQ_U64(file_rename(missing, scratch_path(t, "other")), FILE_ERROR_NOT_FOUND);
    CHECK_EQ_U64(file_sync_dir(missing), FILE_ERROR_NOT_FOUND);

    /* A file used as a directory. */
    const char *plain = scratch_path(t, "plain");
    REQUIRE(file_open(&file, plain, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    char *below = test_alloc(t, FILE_PATH_BYTES);
    snprintf(below, FILE_PATH_BYTES, "%s/below", plain);
    CHECK_EQ_U64(file_open(&file, below, FILE_MODE_WRITE), FILE_ERROR_NOT_FOUND);

    /* A name longer than a filesystem allows. */
    char *long_name = test_alloc(t, 400u);
    memset(long_name, 'x', 399u);
    CHECK_EQ_U64(file_open(&file, scratch_path(t, long_name), FILE_MODE_WRITE), FILE_ERROR_PATH_TOO_LONG);

    /* A directory cannot be written. */
    char *directory = test_alloc(t, FILE_PATH_BYTES);
    snprintf(directory, FILE_PATH_BYTES, "%s/%s", t->borrowed_args[0], t->name);
    CHECK_EQ_U64(file_open(&file, directory, FILE_MODE_WRITE), FILE_ERROR_IO);
    CHECK_EQ_I64(errno, EISDIR);

    /* A status is always nameable. */
    CHECK_STR_EQ(file_status_name(FILE_ERROR_DISK_FULL), "disk full");
    CHECK_STR_EQ(file_status_name(FILE_OK), "ok");
}

TEST(no_permission_is_access_denied) {
    if (geteuid() == 0) {
        return; /* root is not refused, so there is nothing to see */
    }
    const char *path = scratch_path(t, "locked");
    file_t file;
    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    REQUIRE(chmod(path, 0) == 0);
    CHECK_EQ_U64(file_open(&file, path, FILE_MODE_READ), FILE_ERROR_ACCESS);
    CHECK_EQ_U64(file_open(&file, path, FILE_MODE_WRITE), FILE_ERROR_ACCESS);
    REQUIRE(chmod(path, 0644) == 0);
}

/* The kernel's own full disk: every write to /dev/full fails with ENOSPC. Stage 18.2's disk-full handling rests on
 * this. */
TEST(a_full_disk_is_reported_as_one) {
    file_t file;
    REQUIRE(file_open(&file, "/dev/full", FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_write_all(&file, "x", 1u), FILE_ERROR_DISK_FULL);
    CHECK_EQ_U64(file_write_at(&file, 0u, "x", 1u), FILE_ERROR_DISK_FULL);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
}

/* ---- Whole files ------------------------------------------------------------------------------------------------- */

TEST(read_whole_fits_or_says_not) {
    const char *path = scratch_path(t, "data");
    file_t file;
    REQUIRE(file_open(&file, path, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_write_all(&file, "0123456789", 10u), FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);

    uint8_t buffer[16];
    uint64_t got = 0;
    CHECK_EQ_U64(file_read_whole(path, buffer, 16u, &got), FILE_OK);
    CHECK_EQ_U64(got, 10u);
    CHECK_MEM_EQ(buffer, "0123456789", 10u);
    /* Exactly full is not too large, and one byte short is. */
    CHECK_EQ_U64(file_read_whole(path, buffer, 10u, &got), FILE_OK);
    CHECK_EQ_U64(got, 10u);
    CHECK_EQ_U64(file_read_whole(path, buffer, 9u, &got), FILE_ERROR_TOO_LARGE);
    CHECK_EQ_U64(file_read_whole(path, buffer, 0u, &got), FILE_ERROR_TOO_LARGE);
    CHECK_EQ_U64(file_read_whole(scratch_path(t, "missing"), buffer, 16u, &got), FILE_ERROR_NOT_FOUND);
}

/* /sys files say they are 4096 bytes long and are not, and /proc files say 0. That is why the topology reader can use
 * this. */
TEST(read_whole_ignores_what_stat_claims) {
    char text[256];
    uint64_t got = 0;
    CHECK_EQ_U64(
        file_read_whole("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list", text, sizeof(text), &got),
        FILE_OK);
    CHECK(got > 0u);
    CHECK(got < 4096u);
    char *large = test_alloc(t, 1u << 20u);
    CHECK_EQ_U64(file_read_whole("/proc/self/status", large, (1u << 20u) - 1u, &got), FILE_OK);
    CHECK(got > 0u);
    CHECK_CONTAINS(large, "Pid:");
}

/* ---- Directories ------------------------------------------------------------------------------------------------- */

TEST(make_dirs_makes_all_of_them_and_is_idempotent) {
    const char *root = scratch_path(t, "a");
    char *deep = test_alloc(t, FILE_PATH_BYTES);
    snprintf(deep, FILE_PATH_BYTES, "%s/b/c/d", root);
    CHECK_EQ_U64(file_make_dirs(deep), FILE_OK);
    CHECK_EQ_U64(file_make_dirs(deep), FILE_OK);
    struct stat info;
    REQUIRE(stat(deep, &info) == 0);
    CHECK(S_ISDIR(info.st_mode));

    /* Doubled and trailing slashes change nothing. */
    char *odd = test_alloc(t, FILE_PATH_BYTES);
    snprintf(odd, FILE_PATH_BYTES, "%s//b//e/", root);
    CHECK_EQ_U64(file_make_dirs(odd), FILE_OK);
    char *plain_e = test_alloc(t, FILE_PATH_BYTES);
    snprintf(plain_e, FILE_PATH_BYTES, "%s/b/e", root);
    REQUIRE(stat(plain_e, &info) == 0);
    CHECK(S_ISDIR(info.st_mode));

    /* A file in the way, at the end and in the middle. */
    char *file_path = test_alloc(t, FILE_PATH_BYTES);
    snprintf(file_path, FILE_PATH_BYTES, "%s/b/plain", root);
    file_t file;
    REQUIRE(file_open(&file, file_path, FILE_MODE_WRITE) == FILE_OK);
    CHECK_EQ_U64(file_close(&file), FILE_OK);
    CHECK_EQ_U64(file_make_dirs(file_path), FILE_ERROR_EXISTS);
    char *past_file = test_alloc(t, FILE_PATH_BYTES);
    snprintf(past_file, FILE_PATH_BYTES, "%s/more", file_path);
    CHECK_EQ_U64(file_make_dirs(past_file), FILE_ERROR_EXISTS);

    CHECK_EQ_U64(file_make_dirs(""), FILE_ERROR_NOT_FOUND);
    char *too_long = test_alloc(t, FILE_PATH_BYTES + 1u);
    memset(too_long, 'x', FILE_PATH_BYTES);
    CHECK_EQ_U64(file_make_dirs(too_long), FILE_ERROR_PATH_TOO_LONG);
    CHECK_EQ_U64(file_sync_dir(deep), FILE_OK);
}

TEST(rename_replaces_and_remove_removes) {
    const char *from = scratch_path(t, "from");
    const char *to = scratch_path(t, "to");
    CHECK_EQ_U64(file_write_atomic(from, "new", 3u), FILE_OK);
    CHECK_EQ_U64(file_write_atomic(to, "old!", 4u), FILE_OK);
    CHECK_EQ_U64(file_rename(from, to), FILE_OK);
    CHECK_EQ_U64(file_path_size(from, &(uint64_t){0}), FILE_ERROR_NOT_FOUND);
    uint64_t size = 0;
    CHECK_EQ_U64(file_path_size(to, &size), FILE_OK);
    CHECK_EQ_U64(size, 3u);
    CHECK_EQ_U64(file_remove(to), FILE_OK);
    CHECK_EQ_U64(file_path_size(to, &size), FILE_ERROR_NOT_FOUND);
}

/* ---- Short counts and signals ------------------------------------------------------------------------------------ */

static _Atomic uint32_t signals_seen;

static void count_signal([[maybe_unused]] int32_t number) {
    atomic_fetch_add_explicit(&signals_seen, 1u, memory_order_relaxed);
}

enum : uint64_t { PIPE_BYTES = 4u * 1024u * 1024u };

/* The far end of a pipe: writes or reads in small uneven pieces, with pauses, so the other side sees short counts and
 * is blocked when signals arrive. */
typedef struct pipe_peer_t {
    uint8_t *borrowed_data;
    uint64_t bytes;
    int32_t fd;
    uint8_t writes;
    uint8_t failed;
    uint8_t _pad0[2];
} pipe_peer_t;
static_assert(sizeof(pipe_peer_t) == 24);

static void *pipe_peer_thread(void *raw) {
    pipe_peer_t *peer = raw;
    uint64_t done = 0;
    uint64_t piece = 1;
    while (done < peer->bytes) {
        const uint64_t want = piece < peer->bytes - done ? piece : peer->bytes - done;
        const int64_t count = peer->writes != 0 ? write(peer->fd, peer->borrowed_data + done, want)
                                                : read(peer->fd, peer->borrowed_data + done, want);
        if (count > 0) {
            done += (uint64_t)count;
        } else if (count == 0 || errno != EINTR) {
            peer->failed = 1;
            break;
        }
        piece = piece * 7u + 13u;
        if (piece > 200000u) {
            piece = 1u;
            time_sleep_ns(TIME_NS_PER_MS);
        }
    }
    return nullptr;
}

/* The harness's process is the test's, so the signal is put back before returning. */
static bool install_counting_signal(struct sigaction *previous) {
    /* No SA_RESTART, so a signal really does end a blocked read or write early. */
    struct sigaction action = {.sa_handler = count_signal, .sa_flags = 0};
    atomic_store(&signals_seen, 0u);
    return sigemptyset(&action.sa_mask) == 0 && sigaction(SIGUSR1, &action, previous) == 0;
}

typedef struct pipe_side_t {
    file_t file;
    uint8_t *borrowed_data;
    uint64_t bytes;
    _Atomic uint32_t finished;
    file_status_t status;
    uint8_t writes;
    uint8_t _pad0[2];
} pipe_side_t;
static_assert(sizeof(pipe_side_t) == 32);

static void *pipe_side_thread(void *raw) {
    pipe_side_t *side = raw;
    side->status = side->writes != 0 ? file_write_all(&side->file, side->borrowed_data, side->bytes)
                                     : file_read_exact(&side->file, side->borrowed_data, side->bytes);
    atomic_store_explicit(&side->finished, 1u, memory_order_release);
    return nullptr;
}

/* Pipes fill and empty in pieces, and every signal without SA_RESTART gives the blocked call EINTR or a short count. */
static void run_through_pipe(test_context_t *t, bool we_write) {
    uint8_t *sent = test_alloc(t, PIPE_BYTES);
    uint8_t *received = test_alloc(t, PIPE_BYTES);
    fill_pattern(sent, PIPE_BYTES, 7u);
    struct sigaction previous;
    REQUIRE(install_counting_signal(&previous));

    int32_t ends[2];
    REQUIRE(pipe(ends) == 0);
    pipe_side_t side = {.file = {.fd = we_write ? ends[1] : ends[0], ._pad0 = {0}},
                        .borrowed_data = we_write ? sent : received,
                        .bytes = PIPE_BYTES,
                        .finished = 0,
                        .status = FILE_ERROR_IO,
                        .writes = we_write ? 1 : 0,
                        ._pad0 = {0}};
    pipe_peer_t peer = {.borrowed_data = we_write ? received : sent,
                        .bytes = PIPE_BYTES,
                        .fd = we_write ? ends[0] : ends[1],
                        .writes = we_write ? 0 : 1,
                        .failed = 0,
                        ._pad0 = {0}};
    pthread_t side_thread;
    pthread_t peer_thread;
    REQUIRE(pthread_create(&side_thread, nullptr, pipe_side_thread, &side) == 0);
    REQUIRE(pthread_create(&peer_thread, nullptr, pipe_peer_thread, &peer) == 0);

    while (atomic_load_explicit(&side.finished, memory_order_acquire) == 0u) {
        (void)pthread_kill(side_thread, SIGUSR1);
        time_sleep_ns(100000);
    }
    REQUIRE(pthread_join(side_thread, nullptr) == 0);
    /* If that side gave up early, the peer would wait for ever on a pipe nobody serves. Closing its end ends that with
     * end of file or EPIPE, and only the failed checks below remain. */
    (void)signal(SIGPIPE, SIG_IGN);
    REQUIRE(close(we_write ? ends[1] : ends[0]) == 0);
    REQUIRE(pthread_join(peer_thread, nullptr) == 0);
    REQUIRE(close(we_write ? ends[0] : ends[1]) == 0);
    (void)signal(SIGPIPE, SIG_DFL);
    REQUIRE(sigaction(SIGUSR1, &previous, nullptr) == 0);

    CHECK_EQ_U64(side.status, FILE_OK);
    CHECK_EQ_U64(peer.failed, 0u);
    CHECK(atomic_load(&signals_seen) > 0u);
    CHECK_MEM_EQ(received, sent, PIPE_BYTES);
}

TEST(read_exact_goes_on_through_short_counts_and_signals) {
    run_through_pipe(t, false);
}

TEST(write_all_goes_on_through_short_counts_and_signals) {
    run_through_pipe(t, true);
}

/* ---- file_write_atomic ------------------------------------------------------------------------------------------- */

TEST(atomic_write_replaces_and_leaves_no_temporary) {
    const char *path = scratch_path(t, "data");
    const char *temporary = scratch_path(t, "data.tmp");
    CHECK_EQ_U64(file_write_atomic(path, "first", 5u), FILE_OK);
    CHECK_EQ_U64(file_write_atomic(path, "second!", 7u), FILE_OK);
    char text[16] = {0};
    uint64_t got = 0;
    CHECK_EQ_U64(file_read_whole(path, text, sizeof(text) - 1u, &got), FILE_OK);
    CHECK_STR_EQ(text, "second!");
    CHECK_EQ_U64(file_path_size(temporary, &got), FILE_ERROR_NOT_FOUND);

    /* Empty is a valid file, and needs no data pointer. */
    CHECK_EQ_U64(file_write_atomic(path, nullptr, 0u), FILE_OK);
    CHECK_EQ_U64(file_path_size(path, &got), FILE_OK);
    CHECK_EQ_U64(got, 0u);
}

TEST(atomic_write_overwrites_a_temporary_a_crash_left) {
    const char *path = scratch_path(t, "data");
    const char *temporary = scratch_path(t, "data.tmp");
    CHECK_EQ_U64(file_write_atomic(temporary, "left behind, and much longer than what comes next", 49u), FILE_OK);
    CHECK_EQ_U64(file_write_atomic(path, "ok", 2u), FILE_OK);
    uint64_t size = 0;
    CHECK_EQ_U64(file_path_size(path, &size), FILE_OK);
    CHECK_EQ_U64(size, 2u);
    CHECK_EQ_U64(file_path_size(temporary, &size), FILE_ERROR_NOT_FOUND);
}

TEST(atomic_write_without_a_directory_part_uses_the_current_one) {
    /* The one way to get a path with no slash in it: run from the scratch directory. */
    const char *marker = scratch_path(t, "marker");
    char *directory = test_alloc(t, FILE_PATH_BYTES);
    memcpy(directory, marker, strlen(marker) - strlen("/marker"));
    char *back = test_alloc(t, FILE_PATH_BYTES);
    REQUIRE(getcwd(back, FILE_PATH_BYTES) != nullptr);
    REQUIRE(chdir(directory) == 0);
    const file_status_t status = file_write_atomic("no_slash", "x", 1u);
    uint64_t size = 0;
    const file_status_t size_status = file_path_size("no_slash", &size);
    REQUIRE(chdir(back) == 0);
    CHECK_EQ_U64(status, FILE_OK);
    CHECK_EQ_U64(size_status, FILE_OK);
    CHECK_EQ_U64(size, 1u);
}

TEST(atomic_write_refuses_bad_paths) {
    CHECK_EQ_U64(file_write_atomic("", "x", 1u), FILE_ERROR_NOT_FOUND);
    /* 4091 bytes of path plus ".tmp" and its NUL is 4096, and so fits a path buffer but is refused by the OS; one more
     * does not fit at all. Either way nothing is written and nothing overruns. */
    char *too_long = test_alloc(t, FILE_PATH_BYTES + 1u);
    memset(too_long, 'x', FILE_PATH_BYTES);
    CHECK_EQ_U64(file_write_atomic(too_long, "x", 1u), FILE_ERROR_PATH_TOO_LONG);
    memset(too_long, 0, FILE_PATH_BYTES);
    memset(too_long, 'x', FILE_PATH_BYTES - 5u);
    CHECK_EQ_U64(file_write_atomic(too_long, "x", 1u), FILE_ERROR_PATH_TOO_LONG);
    /* A directory that is not there. */
    CHECK_EQ_U64(file_write_atomic("/nonexistent_directory_of_run_file/data", "x", 1u), FILE_ERROR_NOT_FOUND);
}

/* A write that fails partway has to leave the old file alone and clean up. RLIMIT_FSIZE makes the kernel refuse past a
 * size with EFBIG, once SIGXFSZ is out of the way. A child process, so the limit does not stay on this one. */
TEST(atomic_write_failing_partway_keeps_the_old_contents) {
    enum : uint64_t { LIMIT_BYTES = 4096u, TRY_BYTES = 65536u };
    const char *path = scratch_path(t, "data");
    const char *temporary = scratch_path(t, "data.tmp");
    uint8_t *large = test_alloc(t, TRY_BYTES);
    CHECK_EQ_U64(file_write_atomic(path, "the old contents", 16u), FILE_OK);

    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        (void)signal(SIGXFSZ, SIG_IGN);
        const struct rlimit limit = {.rlim_cur = LIMIT_BYTES, .rlim_max = LIMIT_BYTES};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
            _exit(100);
        }
        _exit((int32_t)file_write_atomic(path, large, TRY_BYTES));
    }
    int32_t wait_status = 0;
    REQUIRE(waitpid(child, &wait_status, 0) == child);
    REQUIRE(WIFEXITED(wait_status));
    CHECK_EQ_I64(WEXITSTATUS(wait_status), FILE_ERROR_TOO_LARGE);

    char text[32] = {0};
    uint64_t got = 0;
    CHECK_EQ_U64(file_read_whole(path, text, sizeof(text) - 1u, &got), FILE_OK);
    CHECK_STR_EQ(text, "the old contents");
    CHECK_EQ_U64(file_path_size(temporary, &got), FILE_ERROR_NOT_FOUND);
}

/* One version of the file, whole: the right length, and all one byte value, so a mix of two versions or a cut-off one
 * cannot pass. Comparing the buffer with itself one byte along is the same as comparing every byte with the first. */
static bool is_one_whole_version(const char *path, uint8_t *buffer, uint64_t version_bytes) {
    uint64_t got = 0;
    return file_read_whole(path, buffer, version_bytes + 1u, &got) == FILE_OK && got == version_bytes &&
           memcmp(buffer, buffer + 1, version_bytes - 1u) == 0;
}

/* A child replaces the file as fast as it can, while this process keeps reading it and then kills the child at a
 * different moment each round. Large versions, so that a write takes long enough for both to land inside it. A file
 * written in place fails this: that was tried, by making the temporary name the path itself. */
TEST(atomic_write_read_during_and_killed_at_any_moment_shows_one_whole_version) {
    enum : uint64_t { VERSION_BYTES = 4u * 1024u * 1024u };
    enum : uint32_t { ROUNDS = 8u };
    const char *path = scratch_path(t, "data");
    uint8_t *version = test_alloc(t, VERSION_BYTES);
    uint8_t *read_back = test_alloc(t, VERSION_BYTES + 1u);
    memset(version, 0xAA, VERSION_BYTES);
    REQUIRE(file_write_atomic(path, version, VERSION_BYTES) == FILE_OK);

    uint64_t reads = 0;
    uint64_t bad_reads = 0;
    for (uint32_t round = 0; round < ROUNDS; round++) {
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            (void)alarm(30u); /* in case this process dies and never kills it */
            for (uint8_t value = 0;; value++) {
                memset(version, value, VERSION_BYTES);
                if (file_write_atomic(path, version, VERSION_BYTES) != FILE_OK) {
                    _exit(1);
                }
            }
        }
        const int64_t kill_at = time_now_ns() + (int64_t)(round + 1u) * 5 * TIME_NS_PER_MS;
        while (time_now_ns() < kill_at) {
            bad_reads += is_one_whole_version(path, read_back, VERSION_BYTES) ? 0u : 1u;
            reads++;
        }
        CHECK_EQ_I64(kill(child, SIGKILL), 0);
        int32_t wait_status = 0;
        REQUIRE(waitpid(child, &wait_status, 0) == child);
        CHECK(WIFSIGNALED(wait_status) && WTERMSIG(wait_status) == SIGKILL);
        bad_reads += is_one_whole_version(path, read_back, VERSION_BYTES) ? 0u : 1u;
        reads++;
    }
    CHECK(reads >= ROUNDS);
    CHECK_EQ_U64(bad_reads, 0u);
    /* What the kill may have left behind does not stop the next write. */
    CHECK_EQ_U64(file_write_atomic(path, "after", 5u), FILE_OK);
}

TEST_MAIN()
