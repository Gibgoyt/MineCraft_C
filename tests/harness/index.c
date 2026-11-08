/* See README.md. */
#include "harness/index.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* A test may carve an arena_t out of test_alloc memory and return without arena_deinit; under the sanitize preset that
 * arena leaves its unused bytes poisoned (run/src/arena/), and the harness, which owns the memory, unpoisons it before
 * zeroing it for the next test. */
#ifdef __SANITIZE_ADDRESS__
#include <sanitizer/asan_interface.h>
#define HARNESS_UNPOISON(memory, bytes) __asan_unpoison_memory_region((memory), (bytes))
#else
#define HARNESS_UNPOISON(memory, bytes) ((void)(memory), (void)(bytes))
#endif

typedef struct test_case_t {
    const char *name;
    test_function_t function;
} test_case_t;
static_assert(sizeof(test_case_t) == 16);

/* Not owned memory: names and functions live in the program image. Constructors fill this before main runs, when no
 * context exists yet, which is why it cannot live in one. */
static test_case_t test_cases[TEST_MAX_CASES];
static uint32_t test_case_count;

enum : uint64_t { TEST_OUTPUT_MAX = 16ull << 20 };

void test_register(const char *name, test_function_t function) {
    if (test_case_count >= TEST_MAX_CASES) {
        fprintf(stderr, "harness: more than %d tests\n", TEST_MAX_CASES);
        exit(2);
    }
    test_cases[test_case_count] = (test_case_t){.name = name, .function = function};
    test_case_count++;
}

void *test_alloc(test_context_t *t, uint64_t bytes) {
    const uint64_t aligned = (bytes + 15u) & ~(uint64_t)15u;
    if (aligned > TEST_ARENA_BYTES - t->arena_used) {
        fprintf(stderr, "harness: %s: test arena exhausted asking for %" PRIu64 " bytes\n", t->name, bytes);
        exit(2);
    }
    void *result = t->owned_arena + t->arena_used;
    t->arena_used += aligned;
    return result;
}

static bool test_report(test_context_t *t, bool ok) {
    t->check_count++;
    if (!ok) {
        t->failure_count++;
    }
    return ok;
}

bool test_check(test_context_t *t, bool ok, const char *file, int32_t line, const char *expression) {
    if (!ok) {
        fprintf(stderr, "%s:%" PRId32 ": %s: CHECK failed: %s\n", file, line, t->name, expression);
    }
    return test_report(t, ok);
}

bool test_check_u64(test_context_t *t, uint64_t left, uint64_t right, const char *file, int32_t line,
                    const char *left_text, const char *right_text) {
    const bool ok = left == right;
    if (!ok) {
        fprintf(stderr, "%s:%" PRId32 ": %s: CHECK failed: %s == %s: %" PRIu64 " != %" PRIu64 "\n", file, line, t->name,
                left_text, right_text, left, right);
    }
    return test_report(t, ok);
}

bool test_check_i64(test_context_t *t, int64_t left, int64_t right, const char *file, int32_t line,
                    const char *left_text, const char *right_text) {
    const bool ok = left == right;
    if (!ok) {
        fprintf(stderr, "%s:%" PRId32 ": %s: CHECK failed: %s == %s: %" PRId64 " != %" PRId64 "\n", file, line, t->name,
                left_text, right_text, left, right);
    }
    return test_report(t, ok);
}

bool test_check_str(test_context_t *t, const char *left, const char *right, const char *file, int32_t line,
                    const char *left_text, const char *right_text) {
    const bool ok = strcmp(left, right) == 0;
    if (!ok) {
        fprintf(stderr, "%s:%" PRId32 ": %s: CHECK failed: %s == %s: \"%s\" != \"%s\"\n", file, line, t->name,
                left_text, right_text, left, right);
    }
    return test_report(t, ok);
}

bool test_check_contains(test_context_t *t, const char *text, const char *needle, const char *file, int32_t line,
                         const char *text_name) {
    const bool ok = strstr(text, needle) != nullptr;
    if (!ok) {
        fprintf(stderr, "%s:%" PRId32 ": %s: CHECK failed: %s does not contain \"%s\". It holds:\n%s\n", file, line,
                t->name, text_name, needle, text);
    }
    return test_report(t, ok);
}

bool test_check_mem(test_context_t *t, const void *left, const void *right, uint64_t bytes, const char *file,
                    int32_t line, const char *left_text, const char *right_text) {
    const int32_t order = memcmp(left, right, bytes);
    if (order != 0) {
        uint64_t first = 0;
        while (((const uint8_t *)left)[first] == ((const uint8_t *)right)[first]) {
            first++;
        }
        fprintf(stderr, "%s:%" PRId32 ": %s: CHECK failed: %s == %s: first difference at byte %" PRIu64 "\n", file,
                line, t->name, left_text, right_text, first);
    }
    return test_report(t, order == 0);
}

static uint64_t test_monotonic_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

/* Only async-signal-safe calls here: this is the forked child. */
static void test_process_child(char *const *argv, int32_t write_fd) {
    setpgid(0, 0);
    const int32_t null_fd = open("/dev/null", O_RDONLY);
    if (null_fd < 0 || dup2(null_fd, 0) < 0 || dup2(write_fd, 1) < 0 || dup2(write_fd, 2) < 0) {
        _exit(126);
    }
    execvp(argv[0], argv);
    _exit(127);
}

void test_process_run(test_context_t *t, char *const *argv, uint32_t timeout_ms, test_process_t *out) {
    /* Take the most it may need, and give the unused tail back below. Nothing else allocates meanwhile. */
    const uint64_t mark = t->arena_used;
    char *output = test_alloc(t, TEST_OUTPUT_MAX);
    *out = (test_process_t){.output = output, .exit_code = -1};
    output[0] = '\0';

    int32_t pipe_fds[2];
    if (pipe2(pipe_fds, O_CLOEXEC) != 0) {
        perror("harness: pipe2");
        t->arena_used = mark + 16u;
        t->failure_count++;
        return;
    }
    const int32_t pid = fork();
    if (pid < 0) {
        perror("harness: fork");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        t->arena_used = mark + 16u;
        t->failure_count++;
        return;
    }
    if (pid == 0) {
        test_process_child(argv, pipe_fds[1]);
    }
    setpgid(pid, pid); /* also here, so the kill below cannot race the child's own call */
    close(pipe_fds[1]);

    const uint64_t deadline = test_monotonic_ms() + timeout_ms;
    uint64_t used = 0;
    bool open_pipe = true;
    while (open_pipe) {
        const uint64_t now = test_monotonic_ms();
        if (now >= deadline) {
            out->timed_out = 1;
            kill(-pid, SIGKILL);
            break;
        }
        struct pollfd poll_fd = {.fd = pipe_fds[0], .events = POLLIN, .revents = 0};
        const int32_t ready = poll(&poll_fd, 1, (int32_t)(deadline - now));
        if (ready < 0 && errno != EINTR) {
            perror("harness: poll");
            kill(-pid, SIGKILL);
            break;
        }
        if (ready > 0) {
            /* A child that writes more than the buffer holds keeps running; its output past that is dropped. */
            static char sink[4096];
            const bool room = used + 1u < TEST_OUTPUT_MAX;
            const int64_t got = room ? read(pipe_fds[0], output + used, TEST_OUTPUT_MAX - used - 1u)
                                     : read(pipe_fds[0], sink, sizeof(sink));
            if (got > 0 && room) {
                used += (uint64_t)got;
            } else if (got == 0 || (got < 0 && errno != EINTR)) {
                open_pipe = false;
            }
        }
    }
    close(pipe_fds[0]);

    int32_t status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (out->timed_out == 0) {
        /* The child closed its output and left, but anything it started must not outlive the test either. */
        kill(-pid, SIGKILL);
    }
    output[used] = '\0';
    out->output_length = used;
    if (WIFEXITED(status)) {
        out->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        out->exit_code = 128 + WTERMSIG(status);
    }
    out->started = out->exit_code != 127 ? 1u : 0u;
    t->arena_used = mark + ((used + 1u + 15u) & ~(uint64_t)15u);
}

int32_t test_main(int32_t argc, char **argv, const char *filter) {
    /* Unbuffered, so a crash cannot swallow what the test already reported. */
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    void *arena =
        mmap(nullptr, TEST_ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (arena == MAP_FAILED) {
        perror("harness: mmap");
        return 2;
    }
    test_context_t context = {.owned_arena = arena, .borrowed_args = argv + 1, .arg_count = argc - 1};
    const char *const env_filter = getenv("TEST_FILTER");

    uint32_t ran = 0;
    uint32_t failed = 0;
    uint64_t checks = 0;
    for (uint32_t i = 0; i < test_case_count; i++) {
        const test_case_t *test_case = &test_cases[i];
        if ((filter != nullptr && strstr(test_case->name, filter) == nullptr) ||
            (env_filter != nullptr && strstr(test_case->name, env_filter) == nullptr)) {
            continue;
        }
        context.name = test_case->name;
        context.failure_count = 0;
        context.check_count = 0;
        test_case->function(&context);
        /* Zero what was used, so the next test's test_alloc starts clean. */
        HARNESS_UNPOISON(arena, context.arena_used);
        memset(arena, 0, context.arena_used);
        context.arena_used = 0;
        ran++;
        checks += context.check_count;
        if (context.failure_count > 0) {
            failed++;
            printf("FAIL %s (%" PRIu64 " of %" PRIu64 " checks)\n", test_case->name, context.failure_count,
                   context.check_count);
        } else {
            printf("ok   %s (%" PRIu64 " checks)\n", test_case->name, context.check_count);
        }
    }
    munmap(arena, TEST_ARENA_BYTES);

    printf("harness: %" PRIu32 " tests, %" PRIu32 " failed, %" PRIu64 " checks\n", ran, failed, checks);
    if (ran == 0) {
        /* A filter that matches nothing, or a program with no tests, must not look like a pass. */
        fprintf(stderr, "harness: no test ran\n");
        return 1;
    }
    return failed > 0 ? 1 : 0;
}
