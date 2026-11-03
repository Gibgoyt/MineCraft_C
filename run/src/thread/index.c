/* See README.md. */
#include "thread/index.h"

#include "assert/index.h"
#include "file/index.h"
#include "log/index.h"
#include "machine/index.h"

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>

/* ---- CPU masks --------------------------------------------------------------------------------------------------- */

static bool thread_mask_has(const thread_cpu_mask_t *mask, uint32_t cpu) {
    return (mask->bits[cpu / 64u] >> (cpu % 64u) & 1u) != 0;
}

static void thread_mask_add(thread_cpu_mask_t *mask, uint32_t cpu) {
    mask->bits[cpu / 64u] |= 1ull << (cpu % 64u);
}

/* Parses the kernel's list format, such as "0,10" or "0-3,8", with the newline it ends in. Numbers past what a mask
 * holds are read and left out. A range is walked only up to there, so a huge one in bad data costs nothing. */
static bool thread_mask_parse_list(const char *text, thread_cpu_mask_t *out) {
    enum : uint32_t { LARGEST_ACCEPTED = 1000000u };
    *out = (thread_cpu_mask_t){.bits = {0}};
    const char *cursor = text;
    while (true) {
        uint32_t range[2] = {0, 0};
        for (uint32_t side = 0; side < 2u; side++) {
            if (*cursor < '0' || *cursor > '9') {
                return false;
            }
            while (*cursor >= '0' && *cursor <= '9') {
                range[side] = range[side] * 10u + (uint32_t)(*cursor - '0');
                if (range[side] > LARGEST_ACCEPTED) {
                    return false;
                }
                cursor++;
            }
            if (side == 0u) {
                if (*cursor != '-') {
                    range[1] = range[0];
                    break;
                }
                cursor++;
            }
        }
        if (range[1] < range[0]) {
            return false;
        }
        for (uint32_t cpu = range[0]; cpu <= range[1] && cpu < THREAD_MAX_CPUS; cpu++) {
            thread_mask_add(out, cpu);
        }
        if (*cursor != ',') {
            break;
        }
        cursor++;
    }
    return *cursor == '\0' || (cursor[0] == '\n' && cursor[1] == '\0');
}

/* ---- Topology ---------------------------------------------------------------------------------------------------- */

bool thread_topology_read_from(thread_topology_t *out, const char *sysfs_cpu_dir, const thread_cpu_mask_t *allowed,
                               bool physical_first) {
    /* Long enough for every CPU of a mask written singly, which no real sibling list comes near. */
    char text[1024];
    char path[FILE_PATH_BYTES];
    thread_cpu_mask_t present = {.bits = {0}};
    thread_cpu_mask_t first_of_core = {.bits = {0}};
    uint16_t first_of[THREAD_MAX_CPUS] = {0};
    *out = (thread_topology_t){.cpu_count = 0, .physical_count = 0, .cpu_order = {0}};

    for (uint32_t cpu = 0; cpu < THREAD_MAX_CPUS; cpu++) {
        if (!thread_mask_has(allowed, cpu)) {
            continue;
        }
        const int32_t path_length =
            snprintf(path, sizeof(path), "%s/cpu%u/topology/thread_siblings_list", sysfs_cpu_dir, cpu);
        if (path_length < 0 || (uint64_t)path_length >= sizeof(path)) {
            LOG_ERROR("%s: path too long", sysfs_cpu_dir);
            return false;
        }
        uint64_t text_bytes = 0;
        const file_status_t status = file_read_whole(path, text, sizeof(text) - 1u, &text_bytes);
        if (status == FILE_ERROR_NOT_FOUND) {
            continue; /* an offline CPU keeps its number and has no topology directory */
        }
        if (status != FILE_OK) {
            LOG_ERROR("%s: %s", path, file_status_name(status));
            return false;
        }
        text[text_bytes] = '\0';

        thread_cpu_mask_t siblings;
        if (!thread_mask_parse_list(text, &siblings) || !thread_mask_has(&siblings, cpu)) {
            LOG_ERROR("%s: cannot make sense of '%s' for cpu %u", path, text, cpu);
            return false;
        }
        /* The core's first CPU among those we may use, so a core whose lowest CPU is forbidden still counts as a core.
         * CPUs are visited in rising order, and cpu is in siblings, so this loop ends at cpu at the latest. */
        uint32_t first = 0;
        while (!thread_mask_has(&siblings, first) || !thread_mask_has(allowed, first)) {
            first++;
        }
        first_of[cpu] = (uint16_t)first;
        if (first == cpu) {
            thread_mask_add(&first_of_core, cpu);
            out->physical_count++;
        }
        thread_mask_add(&present, cpu);
    }

    if (physical_first) {
        for (uint32_t cpu = 0; cpu < THREAD_MAX_CPUS; cpu++) {
            if (thread_mask_has(&first_of_core, cpu)) {
                out->cpu_order[out->cpu_count++] = (uint16_t)cpu;
            }
        }
    }
    /* With a core each for main and the sim, whatever shares the sim's core goes last, so that what threads.reserved_os leaves free is
     * that: a worker beside the sim took its tick's p99 from 3.6 to 5.3 ms, turn about in the same minutes. See the phase 17 stage 3 file. */
    const uint32_t sim_core =
        physical_first && out->cpu_count > MC_THREADS_MAIN ? out->cpu_order[MC_THREADS_MAIN] : UINT32_MAX;
    for (uint32_t cpu = 0; cpu < THREAD_MAX_CPUS; cpu++) {
        if (thread_mask_has(&present, cpu) && !(physical_first && thread_mask_has(&first_of_core, cpu)) &&
            first_of[cpu] != sim_core) {
            out->cpu_order[out->cpu_count++] = (uint16_t)cpu;
        }
    }
    for (uint32_t cpu = 0; cpu < THREAD_MAX_CPUS; cpu++) {
        if (thread_mask_has(&present, cpu) && !(physical_first && thread_mask_has(&first_of_core, cpu)) &&
            first_of[cpu] == sim_core) {
            out->cpu_order[out->cpu_count++] = (uint16_t)cpu;
        }
    }
    if (out->cpu_count == 0) {
        LOG_ERROR("%s: no usable CPU found", sysfs_cpu_dir);
        return false;
    }
    return true;
}

bool thread_topology_read(thread_topology_t *out) {
    cpu_set_t affinity;
    CPU_ZERO(&affinity);
    if (sched_getaffinity(0, sizeof(affinity), &affinity) != 0) {
        LOG_ERROR("sched_getaffinity: %s", strerror(errno));
        return false;
    }
    thread_cpu_mask_t allowed = {.bits = {0}};
    for (uint32_t cpu = 0; cpu < THREAD_MAX_CPUS; cpu++) {
        if (CPU_ISSET(cpu, &affinity)) {
            thread_mask_add(&allowed, cpu);
        }
    }
    if (!thread_topology_read_from(out, "/sys/devices/system/cpu", &allowed, MC_THREADS_PIN_PHYSICAL_FIRST != 0)) {
        return false;
    }
    const uint32_t threads_wanted = MC_THREADS_MAIN + MC_THREADS_SIM + MC_THREADS_GPU + MC_THREADS_WORKERS;
    if (out->cpu_count != MC_CPU_LOGICAL_THREADS || out->physical_count != MC_CPU_PHYSICAL_CORES ||
        out->cpu_count < threads_wanted + MC_THREADS_RESERVED_OS) {
        LOG_WARN("config/machine.yaml has %u logical CPUs on %u cores and places %u threads, leaving %u spare; this "
                 "process has %u logical CPUs on %u cores",
                 (uint32_t)MC_CPU_LOGICAL_THREADS, (uint32_t)MC_CPU_PHYSICAL_CORES, threads_wanted,
                 (uint32_t)MC_THREADS_RESERVED_OS, out->cpu_count, out->physical_count);
    }
    return true;
}

uint32_t thread_cpu_for(const thread_topology_t *topology, thread_role_t role, uint32_t index) {
    uint32_t first_slot = 0;
    uint32_t count = 0;
    switch (role) {
    case THREAD_ROLE_MAIN:
        first_slot = 0;
        count = MC_THREADS_MAIN;
        break;
    case THREAD_ROLE_SIM:
        first_slot = MC_THREADS_MAIN;
        count = MC_THREADS_SIM;
        break;
    case THREAD_ROLE_GPU:
        first_slot = MC_THREADS_MAIN + MC_THREADS_SIM;
        count = MC_THREADS_GPU;
        break;
    case THREAD_ROLE_WORKER:
        first_slot = MC_THREADS_MAIN + MC_THREADS_SIM + MC_THREADS_GPU;
        count = MC_THREADS_WORKERS;
        break;
    }
    ASSERT_ALWAYS(index < count);
    ASSERT_ALWAYS(topology->cpu_count != 0);
    return topology->cpu_order[(first_slot + index) % topology->cpu_count];
}

/* ---- Threads ----------------------------------------------------------------------------------------------------- */

bool thread_pin_self(uint32_t cpu) {
    cpu_set_t only;
    CPU_ZERO(&only);
    CPU_SET(cpu, &only);
    const int32_t result = pthread_setaffinity_np(pthread_self(), sizeof(only), &only);
    if (result != 0) {
        LOG_ERROR("cannot pin to cpu %u: %s", cpu, strerror(result));
        return false;
    }
    return true;
}

static void thread_make_name(char *out, thread_role_t role, uint32_t index) {
    const char *role_name = "worker";
    switch (role) {
    case THREAD_ROLE_MAIN:
        role_name = "main";
        break;
    case THREAD_ROLE_SIM:
        role_name = "sim";
        break;
    case THREAD_ROLE_GPU:
        role_name = "gpu";
        break;
    case THREAD_ROLE_WORKER:
        break;
    }
    /* Too many workers to fit would only cut the name, which is fine. */
    const int32_t length = role == THREAD_ROLE_WORKER ? snprintf(out, THREAD_NAME_BYTES, "%s-%02u", role_name, index)
                                                      : snprintf(out, THREAD_NAME_BYTES, "%s", role_name);
    ASSERT_ALWAYS(length > 0);
}

static void *thread_entry(void *raw) {
    thread_t *thread = raw;
    /* Only for people reading top and gdb, so a failure is not worth reporting. */
    (void)pthread_setname_np(pthread_self(), thread->name);
    thread->function(thread->borrowed_argument);
    return nullptr;
}

bool thread_start(thread_t *thread, const thread_topology_t *topology, thread_role_t role, uint32_t index,
                  thread_function_t function, void *borrowed_argument) {
    memset(thread, 0, sizeof(*thread));
    thread->function = function;
    thread->borrowed_argument = borrowed_argument;
    thread->cpu = thread_cpu_for(topology, role, index);
    thread_make_name(thread->name, role, index);

    cpu_set_t only;
    CPU_ZERO(&only);
    CPU_SET(thread->cpu, &only);

    pthread_attr_t attributes;
    int32_t result = pthread_attr_init(&attributes);
    if (result != 0) {
        LOG_ERROR("%s: pthread_attr_init: %s", thread->name, strerror(result));
        return false;
    }
    /* Set on the attributes and not by the thread itself, so that no instruction of it, nor any page of its stack, is
     * ever touched on the wrong CPU. */
    result = pthread_attr_setaffinity_np(&attributes, sizeof(only), &only);
    if (result == 0) {
        result = pthread_attr_setstacksize(&attributes, THREAD_STACK_BYTES);
    }
    if (result == 0) {
        result = pthread_create(&thread->handle, &attributes, thread_entry, thread);
    }
    /* pthread_attr_destroy cannot fail on Linux, and there is nothing to do if it did. */
    (void)pthread_attr_destroy(&attributes);
    if (result != 0) {
        LOG_ERROR("cannot start %s on cpu %u: %s", thread->name, thread->cpu, strerror(result));
        return false;
    }
    thread->started = 1;
    return true;
}

void thread_join(thread_t *thread) {
    if (thread->started == 0) {
        return;
    }
    thread->started = 0;
    /* Fails only on a bad or self handle, which would be a bug here and not a state to carry on from. */
    const int32_t result = pthread_join(thread->handle, nullptr);
    ASSERT_ALWAYS(result == 0);
}
