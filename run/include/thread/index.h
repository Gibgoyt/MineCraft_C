/* See run/src/thread/README.md. */
#ifndef MC_RUN_THREAD_INDEX_H
#define MC_RUN_THREAD_INDEX_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

/* A seq_cst fence. GCC refuses atomic_thread_fence under -fsanitize=thread, which does not model one, and the standard allows no -Wno- to say otherwise. In that
 * build alone it is a locked instruction on a local, which is what such a fence is on this machine. Nothing the sanitizer looks at leans on these: they order a store
 * against a later load, and what hands memory from thread to thread is the acquires and releases around them. */
#ifdef __SANITIZE_THREAD__
static inline void thread_fence_seq_cst(void) {
    _Atomic uint32_t here = 0u;
    (void)atomic_fetch_add_explicit(&here, 1u, memory_order_seq_cst);
}
#else
static inline void thread_fence_seq_cst(void) {
    atomic_thread_fence(memory_order_seq_cst);
}
#endif

enum : uint64_t {
    /* Virtual: only the pages a thread touches cost anything. Past our own code, which -Wframe-larger-than= keeps
     * small, this has to hold whatever libc and the CUDA runtime use on the calling thread's stack. */
    THREAD_STACK_BYTES = 2u * 1024u * 1024u,
};
enum {
    THREAD_MAX_CPUS = 256,  /* CPUs numbered past this are not used. A much larger machine than config/machine.yaml's */
    THREAD_NAME_BYTES = 16, /* what the kernel keeps, with its NUL */
};

/* Which of the threads in config/machine.yaml this is. Each role has a fixed place in the CPU order, so that the same
 * thread lands on the same CPU on every run. */
typedef enum thread_role_t : uint8_t {
    THREAD_ROLE_MAIN = 0,
    THREAD_ROLE_SIM,
    THREAD_ROLE_GPU,
    THREAD_ROLE_WORKER,
    THREAD_ROLE_AUDIO,
} thread_role_t;

/* One bit per CPU. */
typedef struct thread_cpu_mask_t {
    uint64_t bits[THREAD_MAX_CPUS / 64];
} thread_cpu_mask_t;
static_assert(sizeof(thread_cpu_mask_t) == 32);

/* The CPUs this process may use, in the order threads should be placed on them. */
typedef struct thread_topology_t {
    uint32_t cpu_count;
    uint32_t physical_count;             /* how many different physical cores those CPUs are on */
    uint16_t cpu_order[THREAD_MAX_CPUS]; /* cpu_count entries: with pin_physical_first, one per core comes first */
} thread_topology_t;
static_assert(sizeof(thread_topology_t) == 520);

typedef void (*thread_function_t)(void *borrowed_argument);

/* Must stay where it is from thread_start to thread_join: the running thread reads it. */
typedef struct thread_t {
    pthread_t handle;
    thread_function_t function;
    void *borrowed_argument;
    uint32_t cpu;
    uint8_t started; /* between thread_start and thread_join */
    uint8_t _pad0[3];
    char name[THREAD_NAME_BYTES];
} thread_t;
static_assert(sizeof(thread_t) == 48);

/* Reads the real machine: /sys for which CPUs share a core, and this thread's affinity for which it may use. Applies
 * threads.pin_physical_first from config/machine.yaml. Warns if the result disagrees with cpu.logical_threads or
 * cpu.physical_cores, or there are too few CPUs to give each thread its own. Returns false only if no CPU could be
 * found or /sys made no sense, having logged why. Call it before pinning this thread, or the answer is that one CPU. */
bool thread_topology_read(thread_topology_t *out);

/* The same from any directory laid out like /sys/devices/system/cpu, with the mask and the order given. Logs nothing
 * about config/machine.yaml. */
bool thread_topology_read_from(thread_topology_t *out, const char *sysfs_cpu_dir, const thread_cpu_mask_t *allowed,
                               bool physical_first);

/* The CPU for the index-th thread of a role, from the counts in config/machine.yaml: main, then sim, gpu, workers and audio,
 * so threads.reserved_os is what remains at the end of the order. With too few CPUs this wraps round and threads share.
 * An index past the role's count is a bug and asserts. */
uint32_t thread_cpu_for(const thread_topology_t *topology, thread_role_t role, uint32_t index);

/* Pins the calling thread to one CPU. For the main thread, which the OS started. */
[[nodiscard]] bool thread_pin_self(uint32_t cpu);

/* Starts function(borrowed_argument) on a new thread, already on its CPU from its first instruction. Named after the
 * role, as in "worker-07", which is what top and gdb show. On failure, logs why and thread_join does nothing. */
[[nodiscard]] bool thread_start(thread_t *thread, const thread_topology_t *topology, thread_role_t role, uint32_t index,
                                thread_function_t function, void *borrowed_argument);
/* Waits for the thread to end. Does nothing on one not started or already joined. */
void thread_join(thread_t *thread);

#endif
