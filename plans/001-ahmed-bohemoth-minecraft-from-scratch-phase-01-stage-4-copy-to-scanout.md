# 001-ahmed-bohemoth-minecraft-from-scratch — phase 1, stage 4: Copy to scanout spike

**Status:** in progress: all agent work is done and re-verified; the real dumb-buffer write is UNMEASURED and awaits Ahmed   (updated 2026-11-01)

## Goal

We know the cost of readback plus the dumb-buffer write. Pass: total of at most 2 ms.

**Wave:** W1. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-01-feasibility-spikes.md`.

**Owned paths** (edit only these, plus this file):

- `/tmp/mc_c_spikes/copy_scanout/` (throwaway)
- this stage file

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Time a 1080p readback from device memory to pinned host memory in `/tmp/mc_c_spikes/copy_scanout/`
- [x] Stand-in, agent-measurable: time a streaming copy into ordinary host memory and into write-combined host memory. This does not replace the next box
- [x] Write the `--dumb-buffer` path and test its gate and logic against a fake DRM shim. No real device was opened
- [ ] Time the copy from host memory into a mapped dumb buffer (**Ahmed only**: command below)
- [ ] Record both times and the total in this file (readback is recorded; the real write and the real total wait on the box above)

## Verification

Accepted exception (plan section 6): `tools/test-or-build.sh` does not exist until Stage 02.1, so these commands are not wrapped.

    # Readback timing runs headless. The dumb-buffer part: Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    sh /tmp/mc_c_spikes/copy_scanout/build.sh
    timeout 30 /tmp/mc_c_spikes/copy_scanout/copy_scanout </dev/null    # prints VERDICT: INCOMPLETE, exit 0
    pgrep -a copy_scanout    # must print nothing

The dumb-buffer run, for Ahmed on a text console. It creates and maps a buffer only: no modeset, no CRTC, no KD or VT ioctl, so the screen does not change. If nobody holds DRM master at that moment the kernel makes the opener master, and the program drops it immediately, before any other ioctl. `/tmp` is tmpfs, so if the machine has rebooted the spike is gone. Rebuild it first, from the main checkout, using the copy of the source at the end of Results:

    mkdir -p /tmp/mc_c_spikes/copy_scanout
    for f in index.c index.h index.cu index.asm build.sh; do awk -v f="$f" '$0 == "```text " f {on=1; next} $0 == "```" {on=0} on' plans/001-ahmed-bohemoth-minecraft-from-scratch-phase-01-stage-4-copy-to-scanout.md > "/tmp/mc_c_spikes/copy_scanout/$f"; done
    sh /tmp/mc_c_spikes/copy_scanout/build.sh

The command itself:

    timeout -k 3 120 /tmp/mc_c_spikes/copy_scanout/copy_scanout --dumb-buffer /dev/dri/card1 | tee /tmp/copy_scanout_dumb.txt
    pgrep -a copy_scanout    # must print nothing

Decide from the `REAL dumb, spin-paced: TOTAL` row and the VERDICT line. Exit 0 pass, 1 miss, 2 refused with nothing opened, 3 error.

## Results

**The 2 ms target is not judged. The real scanout write is unmeasured and awaits Ahmed.** Agents cannot map a dumb buffer on this machine, so every "write" below went to host memory. A stand-in total under 2 ms is not a pass.

Measured 2026-11-01 23:21 to 23:25, four runs (the "does not wait" row comes from one of them), load average under 0.6, no other compute process on the GPU. One frame is 1920x1080 XRGB8888, 8,294,400 bytes. Runs after the first 10 of each phase are kept. Frame content is checked: the kernel writes a per-frame pattern and the host compares it, in full for every readback sampled (1 in 10) and at 4097 points for writes. Times are milliseconds and vary by about 0.03 between runs, except where a range is given.

A fifth run at 23:28 on a fresh rebuild, by the agent that took over this stage, agrees: readback to pinned 1.263 tight and 1.294 paced, stream to host WC 0.246, spin-paced stand-in total 1.831 (p99 1.979), sleep-paced 2.667 (p99 3.48), slightly above the sleep-paced median range in the table below. The real write was again UNMEASURED and the run again printed `VERDICT: INCOMPLETE`.

| Measured | median | p99 | Rate at median |
|---|---|---|---|
| Readback to pinned, paced at 120 Hz | 1.292 | 1.30 to 1.32 | 6.4 GB/s |
| Readback to pinned, tight loop | 1.263 | 1.35 to 1.44 | 6.6 GB/s |
| Readback to unpinned, tight loop | 1.327 | 1.35 to 1.57 | 6.3 GB/s |
| Async readback: submit call only | 0.002 | 0.002 to 0.004 | n/a |
| Async readback: submit plus wait | 1.263 | 1.35 to 1.50 | 6.6 GB/s |
| Upload pinned to device (DMA) | 1.283 | 1.36 to 1.44 | 6.5 GB/s |
| glibc memcpy to ordinary host memory, warm source | 0.300 | 0.39 to 0.41 | 27 GB/s |
| NASM stream to ordinary host memory, warm source | 0.225 | 0.26 | 37 GB/s |
| NASM stream to host WC memory, warm source | 0.228 to 0.255 | 0.26 to 0.29 | 33 to 37 GB/s |

The real frame order: kernel, readback, then stream the frame just read into host WC memory, so the source is cold:

| Between frames the thread... | Readback | Write | **Stand-in total** | Total p99 |
|---|---|---|---|---|
| spins to a 120 Hz deadline | 1.264 to 1.288 | 0.53 to 0.55 | **1.79 to 1.83** | 2.00 |
| sleeps to a 120 Hz deadline | 1.28 to 1.29 | 1.31 to 1.35 | **2.59 to 2.64** | 2.65 to 3.50 |
| does not wait | 1.287 | 0.562 | 1.857 | 2.658 |

What this shows:

- **Readback alone is 1.26 to 1.29 ms**, which leaves about 0.74 ms for the write within 2 ms.
- **The host WC memory really is write-combined:** a CPU read of it runs at 102 MB/s against 11 GB/s for ordinary memory. It still sits on the CPU's memory bus, so it says nothing about a write across PCIe.
- **Sleeping between frames costs 0.8 ms in the write,** and the two write modes are clearly separate. Sleep-paced ordinary memory is even bimodal, between 0.53 and 1.9 ms. The governor is `powersave` and the core idles at 800 MHz, so waking at low frequency is the likely cause, but that was not isolated. A cold source alone explains only 0.23 to 0.53 ms.
- **Prediction, not a measurement:** if the dumb buffer lives in VRAM, the write crosses the same PCIe link. Gen3 x8 carries at most 7.88 GB/s raw, so the write takes at least 1.05 ms, and about 1.28 ms at the 6.5 GB/s measured for DMA. The total would then be about 2.3 to 2.6 ms, a **miss**. Passing needs 11.3 GB/s, above the link's limit. If the driver puts the buffer in system memory, the stand-in's 1.8 ms is the better guide. nvidia-drm's source is not on this machine, so where the buffer lives was not confirmed. Ahmed's run settles it, and its `CPU read of REAL dumb buffer` row is further evidence.

**Fallbacks to weigh if Ahmed's run misses, for Ahmed to decide:** the target can be relaxed, since the copy engine runs while the CPU does other work (the async submit call costs 2 microseconds), so readback need not block the frame thread, at the price of frame latency. Getting the GPU onto x16 (Issue 1) could roughly halve both copies, if the link is the limit. Neither was tried.

### Gate check, 2026-11-01

Seven more runs from a deleted build, on a static-cudart link that matches the Stage 01.1 link list. `NEEDED` is `libc.so.6` and `ld-linux-x86-64.so.2`. Each exits 0 with `scanout write: UNMEASURED` and `VERDICT: INCOMPLETE`, and `pgrep -a copy_scanout` rc 1. `/sys/class/tty/tty0/active` reads `tty7`, and the link reads 8.0 GT/s x8. `test_shim/run.sh` gives the expected result in all 10 cases. The real binary with no shim refuses `--dumb-buffer /nonexistent/card`, exit 2, with `DISPLAY` set and with stdin not a terminal.

Six of the seven, CPU frequency 4.96 to 5.18 GHz measured beside each run, milliseconds:

| Measured | median across runs |
|---|---|
| Readback to pinned, tight loop | 1.263 to 1.264 |
| Readback to unpinned, tight loop | 1.327 to 1.360 |
| Upload pinned to device | 1.284 to 1.304 |
| memcpy to ordinary host | 0.299 to 0.378 |
| Stream to ordinary host | 0.226 to 0.245 |
| Stream to host WC | 0.226 to 0.247 |
| Stand-in WC, spin-paced: total | 1.800 to 1.843, p99 2.00 to 2.25 |
| Stand-in WC, tight: total | 1.799 to 1.825 |
| Stand-in WC, sleep-paced: total | **1.899 to 2.614**, p99 3.19 to 3.43 |
| Stand-in WB, sleep-paced: total | **2.121 to 3.038**, p99 3.24 to 3.39 |
| CPU read of host WC | 99 to 103 MB/s, against about 10000 MB/s for ordinary memory |

The seventh, the first one, was far slower on the host side. See Issue 7.

### Spike source

Copied from `/tmp/mc_c_spikes/copy_scanout/`, so that Ahmed's run survives `/tmp` being cleared. The rebuild command is under Verification.

```text index.c
/*
 * Stage 01.4 copy-to-scanout spike. Throwaway; only its results stay, in the stage file.
 * Question: can one 1080p frame go GPU -> pinned host memory -> dumb buffer in at most 2 ms?
 *
 * Without arguments it never touches /dev/dri. It measures the readback and streaming copies into
 * ordinary and write-combined HOST memory, and reports the scanout write as UNMEASURED.
 *
 * With --dumb-buffer PATH (Ahmed only, on a text console) it also creates and maps a real dumb
 * buffer and times the write. It never modesets, never takes DRM master on purpose, never issues
 * KD or VT ioctls, and never attaches the buffer to a CRTC, so the screen does not change.
 *
 * Exit codes: 0 ran and did not miss; 1 dumb-buffer total missed 2 ms; 2 refused, nothing opened;
 * 3 an error, including a frame failing its content check.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "index.h"

#define CS_NS_PER_S UINT64_C(1000000000)
#define CS_BUDGET_NS UINT64_C(2000000)
#define CS_FRAME_PERIOD_NS (CS_NS_PER_S / UINT64_C(120))
#define CS_WARMUP 10u
#define CS_TIGHT_RUNS 300u
#define CS_PACED_RUNS 240u
#define CS_OTHER_RUNS 200u
#define CS_READ_RUNS 4u
#define CS_SAMPLE_POINTS 4096u
#define CS_MAX_RUNS 512u

enum {
    CS_EXIT_OK = 0,
    CS_EXIT_MISSED = 1,
    CS_EXIT_REFUSED = 2,
    CS_EXIT_ERROR = 3,
};

typedef struct cs_stats_t {
    uint64_t first_ns;  /* 0: the first run, kept although warm-up is otherwise discarded */
    uint64_t min_ns;    /* 8 */
    uint64_t median_ns; /* 16 */
    uint64_t p99_ns;    /* 24 */
    uint64_t max_ns;    /* 32 */
} cs_stats_t;
static_assert(sizeof(cs_stats_t) == 40);

/* The destination of one streaming write: pitch may be wider than a row on a real dumb buffer. */
typedef struct cs_target_t {
    uint8_t *base;    /* 0 */
    uint64_t pitch;   /* 8 */
    const char *name; /* 16 */
} cs_target_t;
static_assert(sizeof(cs_target_t) == 24);

typedef struct cs_dumb_t {
    uint8_t *mapping; /* 0  */
    uint64_t size;    /* 8  */
    int32_t fd;       /* 16 */
    uint32_t handle;  /* 20 */
    uint32_t pitch;   /* 24 */
    uint8_t _pad0[4]; /* 28 */
} cs_dumb_t;
static_assert(sizeof(cs_dumb_t) == 32);

/* What the frame thread does between frames in cs_run_combined. */
typedef enum cs_pace_t {
    CS_PACE_SLEEP = 0, /* clock_nanosleep to a 120 Hz deadline; the core may idle */
    CS_PACE_SPIN = 1,  /* busy-wait to the same deadline, as a default cudaDeviceSynchronize does */
    CS_PACE_TIGHT = 2, /* no wait at all */
} cs_pace_t;

static uint64_t cs_samples[CS_MAX_RUNS];
static uint64_t cs_samples_b[CS_MAX_RUNS];
static uint64_t cs_samples_c[CS_MAX_RUNS];

static uint64_t cs_now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * CS_NS_PER_S + (uint64_t)now.tv_nsec;
}

static void cs_sleep_until(uint64_t deadline_ns) {
    struct timespec until = {.tv_sec = (time_t)(deadline_ns / CS_NS_PER_S),
                             .tv_nsec = (typeof(until.tv_nsec))(deadline_ns % CS_NS_PER_S)};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, nullptr) == EINTR) {
    }
}

static int32_t cs_compare_u64(const void *a, const void *b) {
    uint64_t left = *(const uint64_t *)a;
    uint64_t right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}

/* Sorts in place. Runs before warmup are dropped, except for first_ns. */
static cs_stats_t cs_stats(uint64_t *samples, uint32_t runs, uint32_t warmup) {
    cs_stats_t stats = {.first_ns = samples[0]};
    uint64_t *kept = samples + warmup;
    uint32_t count = runs - warmup;
    qsort(kept, count, sizeof(kept[0]), cs_compare_u64);
    stats.min_ns = kept[0];
    stats.median_ns = kept[count / 2u];
    stats.p99_ns = kept[(count * 99u) / 100u];
    stats.max_ns = kept[count - 1u];
    return stats;
}

/* Integer arithmetic only: milliseconds with three decimals. */
static void cs_print_ms(uint64_t ns) {
    uint64_t us = (ns + UINT64_C(500)) / UINT64_C(1000);
    printf("%5" PRIu64 ".%03" PRIu64, us / UINT64_C(1000), us % UINT64_C(1000));
}

static void cs_print_stats(const char *name, const cs_stats_t *stats) {
    printf("%-38s", name);
    cs_print_ms(stats->first_ns);
    cs_print_ms(stats->min_ns);
    cs_print_ms(stats->median_ns);
    cs_print_ms(stats->p99_ns);
    cs_print_ms(stats->max_ns);
    printf(" %8" PRIu64 "\n", stats->median_ns == UINT64_C(0) ? UINT64_C(0) : CS_FRAME_BYTES * UINT64_C(1000) / stats->median_ns);
}

static void cs_print_header(void) {
    printf("%-38s%9s%9s%9s%9s%9s %8s\n", "all times in ms", "first", "min", "median", "p99", "max", "MB/s");
}

static bool cs_ok(const char *what, const char *error) {
    if (error != nullptr) {
        fprintf(stderr, "copy_scanout: %s failed: %s\n", what, error);
        return false;
    }
    return true;
}

static bool cs_check_frame(const uint32_t *frame, uint32_t seed, const char *where) {
    for (uint32_t pixel = 0u; pixel < CS_PIXELS; pixel++) {
        uint32_t expected = cs_pattern(pixel, seed);
        if (frame[pixel] != expected) {
            fprintf(stderr, "copy_scanout: %s: pixel %" PRIu32 " is 0x%08" PRIx32 ", expected 0x%08" PRIx32 "\n", where,
                    pixel, frame[pixel], expected);
            return false;
        }
    }
    return true;
}

static void cs_read_sysfs_line(const char *directory, const char *leaf, char *out, uint32_t size) {
    char path[320];
    out[0] = '\0';
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/%s", directory, leaf);
    FILE *file = fopen(path, "r");
    if (file != nullptr) {
        if (fgets(out, (int32_t)size, file) != nullptr) {
            out[strcspn(out, "\n")] = '\0';
        }
        fclose(file);
    }
}

/* The link trains down at idle, so its state next to the numbers matters. Read-only sysfs. */
static void cs_print_link(const char *when) {
    DIR *devices = opendir("/sys/bus/pci/devices");
    if (devices == nullptr) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(devices)) != nullptr) {
        char vendor[16];
        char class_code[16];
        cs_read_sysfs_line(entry->d_name, "vendor", vendor, sizeof(vendor));
        cs_read_sysfs_line(entry->d_name, "class", class_code, sizeof(class_code));
        if (strcmp(vendor, "0x10de") == 0 && strncmp(class_code, "0x0300", 6u) == 0) {
            char speed[32];
            char width[16];
            cs_read_sysfs_line(entry->d_name, "current_link_speed", speed, sizeof(speed));
            cs_read_sysfs_line(entry->d_name, "current_link_width", width, sizeof(width));
            printf("pcie link %s: %s x%s\n", when, speed, width);
            break;
        }
    }
    closedir(devices);
}

/* Readback into host_frame. paced sleeps to a 120 Hz cadence, as the real frame loop will. */
static bool cs_run_readback(const char *name, void *host_frame, uint32_t runs, bool paced, uint32_t *seed) {
    uint64_t next_frame = cs_now_ns();
    for (uint32_t run = 0u; run < runs; run++) {
        if (paced) {
            cs_sleep_until(next_frame);
            next_frame += CS_FRAME_PERIOD_NS;
        }
        *seed += 1u;
        if (!cs_ok("fill", cs_gpu_fill(*seed))) {
            return false;
        }
        uint64_t start = cs_now_ns();
        if (!cs_ok("readback", cs_gpu_read(host_frame))) {
            return false;
        }
        cs_samples[run] = cs_now_ns() - start;
        if ((run % 10u == 0u || run + 1u == runs) && !cs_check_frame(host_frame, *seed, name)) {
            return false;
        }
    }
    cs_stats_t stats = cs_stats(cs_samples, runs, CS_WARMUP);
    cs_print_stats(name, &stats);
    return true;
}

static bool cs_run_readback_async(void *host_frame, uint32_t *seed) {
    for (uint32_t run = 0u; run < CS_OTHER_RUNS; run++) {
        *seed += 1u;
        if (!cs_ok("fill", cs_gpu_fill(*seed))) {
            return false;
        }
        uint64_t start = cs_now_ns();
        if (!cs_ok("async submit", cs_gpu_read_submit(host_frame))) {
            return false;
        }
        uint64_t submitted = cs_now_ns();
        if (!cs_ok("async wait", cs_gpu_read_wait())) {
            return false;
        }
        uint64_t finished = cs_now_ns();
        cs_samples[run] = submitted - start;
        cs_samples_b[run] = finished - start;
        if (run % 10u == 0u && !cs_check_frame(host_frame, *seed, "async readback")) {
            return false;
        }
    }
    cs_stats_t submit = cs_stats(cs_samples, CS_OTHER_RUNS, CS_WARMUP);
    cs_stats_t total = cs_stats(cs_samples_b, CS_OTHER_RUNS, CS_WARMUP);
    cs_print_stats("readback async pinned: submit only", &submit);
    cs_print_stats("readback async pinned: submit+wait", &total);
    return true;
}

static bool cs_run_upload(uint32_t *host_frame, void *check_frame, uint32_t *seed) {
    *seed += 1u;
    for (uint32_t pixel = 0u; pixel < CS_PIXELS; pixel++) {
        host_frame[pixel] = cs_pattern(pixel, *seed);
    }
    for (uint32_t run = 0u; run < CS_OTHER_RUNS; run++) {
        uint64_t start = cs_now_ns();
        if (!cs_ok("upload", cs_gpu_write(host_frame))) {
            return false;
        }
        cs_samples[run] = cs_now_ns() - start;
    }
    if (!cs_ok("upload check read", cs_gpu_read(check_frame)) || !cs_check_frame(check_frame, *seed, "upload")) {
        return false;
    }
    cs_stats_t stats = cs_stats(cs_samples, CS_OTHER_RUNS, CS_WARMUP);
    cs_print_stats("upload pinned to device (DMA)", &stats);
    return true;
}

/* One frame into the target, then a read of the last pixel, which forces every posted write ahead
 * of it to complete. The time includes that read, for host-memory and real targets alike. */
static void cs_write_target(const cs_target_t *target, const uint8_t *frame, bool use_memcpy) {
    if (use_memcpy) {
        for (uint32_t row = 0u; row < CS_HEIGHT; row++) {
            memcpy(target->base + row * target->pitch, frame + (uint64_t)row * CS_PITCH, CS_PITCH);
        }
    } else if (target->pitch == CS_PITCH) {
        cs_stream_copy(target->base, frame, CS_FRAME_BYTES);
    } else {
        for (uint32_t row = 0u; row < CS_HEIGHT; row++) {
            cs_stream_copy(target->base + row * target->pitch, frame + (uint64_t)row * CS_PITCH, CS_PITCH);
        }
    }
    (void)*(volatile const uint32_t *)(const void *)(target->base + (uint64_t)(CS_HEIGHT - 1u) * target->pitch +
                                                     CS_PITCH - CS_BYTES_PER_PIXEL);
}

static uint8_t *cs_target_pixel(const cs_target_t *target, uint32_t pixel) {
    return target->base + (uint64_t)(pixel / CS_WIDTH) * target->pitch +
           (uint64_t)(pixel % CS_WIDTH) * CS_BYTES_PER_PIXEL;
}

/* Reads only evenly spaced pixels plus the last: reading all of a write-combined target would
 * take long enough to matter. */
static bool cs_check_target(const cs_target_t *target, const uint32_t *frame) {
    for (uint32_t i = 0u; i <= CS_SAMPLE_POINTS; i++) {
        uint32_t pixel = i == CS_SAMPLE_POINTS ? CS_PIXELS - 1u : i * (CS_PIXELS / CS_SAMPLE_POINTS);
        uint32_t found = *(volatile const uint32_t *)(const void *)cs_target_pixel(target, pixel);
        if (found != frame[pixel]) {
            fprintf(stderr, "copy_scanout: %s: pixel %" PRIu32 " is 0x%08" PRIx32 ", expected 0x%08" PRIx32 "\n",
                    target->name, pixel, found, frame[pixel]);
            return false;
        }
    }
    return true;
}

/* The source changes in sampled pixels each run, so a skipped write fails the check. */
static bool cs_run_write(const cs_target_t *target, uint32_t *frame, bool use_memcpy, const char *label) {
    for (uint32_t run = 0u; run < CS_OTHER_RUNS; run++) {
        for (uint32_t i = 0u; i < CS_SAMPLE_POINTS; i++) {
            frame[i * (CS_PIXELS / CS_SAMPLE_POINTS)] ^= run + 1u;
        }
        uint64_t start = cs_now_ns();
        cs_write_target(target, (const uint8_t *)frame, use_memcpy);
        cs_samples[run] = cs_now_ns() - start;
        if (run % 10u == 0u && !cs_check_target(target, frame)) {
            return false;
        }
    }
    cs_stats_t stats = cs_stats(cs_samples, CS_OTHER_RUNS, CS_WARMUP);
    cs_print_stats(label, &stats);
    return true;
}

/* CPU read of the whole target: the evidence for whether a mapping really is write-combined. */
static void cs_run_cpu_read(const cs_target_t *target, const char *label) {
    uint64_t sum = UINT64_C(0);
    for (uint32_t run = 0u; run < CS_READ_RUNS; run++) {
        uint64_t start = cs_now_ns();
        for (uint32_t pixel = 0u; pixel + 1u < CS_PIXELS; pixel += 2u) {
            sum += *(volatile const uint64_t *)(const void *)cs_target_pixel(target, pixel);
        }
        cs_samples[run] = cs_now_ns() - start;
    }
    cs_stats_t stats = cs_stats(cs_samples, CS_READ_RUNS, 1u);
    cs_print_stats(label, &stats);
    if (sum == UINT64_C(0)) {
        printf("(read sum was zero)\n");
    }
}

/* The real frame shape: a kernel, then readback, then the write. The DMA leaves the source out of
 * the CPU's caches, as it will be in the real path. pace decides what the thread does between
 * frames, because on this machine that changes the cost of the write. */
static bool cs_run_combined(const cs_target_t *target, uint32_t *frame, uint32_t *seed, const char *label,
                            cs_pace_t pace, cs_stats_t *out_total) {
    uint64_t next_frame = cs_now_ns();
    for (uint32_t run = 0u; run < CS_PACED_RUNS; run++) {
        if (pace == CS_PACE_SLEEP) {
            cs_sleep_until(next_frame);
        } else if (pace == CS_PACE_SPIN) {
            while (cs_now_ns() < next_frame) {
            }
        }
        next_frame += CS_FRAME_PERIOD_NS;
        *seed += 1u;
        if (!cs_ok("fill", cs_gpu_fill(*seed))) {
            return false;
        }
        uint64_t start = cs_now_ns();
        if (!cs_ok("readback", cs_gpu_read(frame))) {
            return false;
        }
        uint64_t read_done = cs_now_ns();
        cs_write_target(target, (const uint8_t *)frame, false);
        uint64_t write_done = cs_now_ns();
        cs_samples_c[run] = read_done - start;
        cs_samples_b[run] = write_done - read_done;
        cs_samples[run] = write_done - start;
        if (run % 10u == 0u && (!cs_check_frame(frame, *seed, label) || !cs_check_target(target, frame))) {
            return false;
        }
    }
    cs_stats_t read = cs_stats(cs_samples_c, CS_PACED_RUNS, CS_WARMUP);
    cs_stats_t write = cs_stats(cs_samples_b, CS_PACED_RUNS, CS_WARMUP);
    *out_total = cs_stats(cs_samples, CS_PACED_RUNS, CS_WARMUP);
    char line[64];
    snprintf(line, sizeof(line), "%s: readback", label);
    cs_print_stats(line, &read);
    snprintf(line, sizeof(line), "%s: write", label);
    cs_print_stats(line, &write);
    snprintf(line, sizeof(line), "%s: TOTAL", label);
    cs_print_stats(line, out_total);
    return true;
}

/* Runs before the card is opened or CUDA is started, and issues no ioctl of any kind. The dumb
 * buffer is only for Ahmed at a text console: never from inside a display server's session. */
static bool cs_dumb_gate_passes(const char *card_path) {
    const char *display = getenv("DISPLAY");
    const char *wayland = getenv("WAYLAND_DISPLAY");
    if ((display != nullptr && display[0] != '\0') || (wayland != nullptr && wayland[0] != '\0')) {
        fprintf(stderr, "copy_scanout: REFUSED: DISPLAY or WAYLAND_DISPLAY is set; a display server owns this session.\n"
                        "Run this on a text console (Ctrl+Alt+F3, log in). %s was not opened.\n", card_path);
        return false;
    }
    const char *tty = ttyname(STDIN_FILENO);
    if (tty == nullptr || strncmp(tty, "/dev/tty", 8u) != 0 || tty[8] < '1' || tty[8] > '9') {
        fprintf(stderr, "copy_scanout: REFUSED: stdin is not a Linux virtual console (it is %s).\n"
                        "Run this on a text console (Ctrl+Alt+F3, log in). %s was not opened.\n",
                tty == nullptr ? "not a terminal" : tty, card_path);
        return false;
    }
    char active[32] = "";
    FILE *file = fopen("/sys/class/tty/tty0/active", "r");
    if (file == nullptr || fgets(active, (int32_t)sizeof(active), file) == nullptr) {
        fprintf(stderr, "copy_scanout: REFUSED: cannot read the active console. %s was not opened.\n", card_path);
        if (file != nullptr) {
            fclose(file);
        }
        return false;
    }
    fclose(file);
    active[strcspn(active, "\n")] = '\0';
    if (strcmp(active, tty + 5) != 0) {
        fprintf(stderr, "copy_scanout: REFUSED: this is %s but the visible console is %s. %s was not opened.\n", tty + 5,
                active, card_path);
        return false;
    }
    return true;
}

static void cs_dumb_close(cs_dumb_t *dumb) {
    if (dumb->mapping != nullptr) {
        munmap(dumb->mapping, dumb->size);
        dumb->mapping = nullptr;
    }
    if (dumb->handle != 0u) {
        struct drm_mode_destroy_dumb destroy = {.handle = dumb->handle};
        if (ioctl(dumb->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy) != 0) {
            fprintf(stderr, "copy_scanout: DESTROY_DUMB: %s\n", strerror(errno));
        }
        dumb->handle = 0u;
    }
    if (dumb->fd >= 0) {
        close(dumb->fd);
        dumb->fd = -1;
    }
}

static bool cs_dumb_open(cs_dumb_t *dumb, const char *card_path) {
    dumb->fd = open(card_path, O_RDWR | O_CLOEXEC);
    if (dumb->fd < 0) {
        fprintf(stderr, "copy_scanout: open %s: %s\n", card_path, strerror(errno));
        return false;
    }
    /* The kernel makes the first opener master when nobody holds it. Dumb buffers do not need
     * master, so give it straight back: a display server returning to its VT must find it free.
     * This fails harmlessly when we were never master. */
    (void)ioctl(dumb->fd, DRM_IOCTL_DROP_MASTER, nullptr);

    struct drm_get_cap cap = {.capability = DRM_CAP_DUMB_BUFFER};
    if (ioctl(dumb->fd, DRM_IOCTL_GET_CAP, &cap) != 0 || cap.value == UINT64_C(0)) {
        fprintf(stderr, "copy_scanout: the driver offers no dumb buffers: %s\n", strerror(errno));
        return false;
    }
    cap = (struct drm_get_cap){.capability = DRM_CAP_DUMB_PREFER_SHADOW};
    bool prefer_shadow = ioctl(dumb->fd, DRM_IOCTL_GET_CAP, &cap) == 0 && cap.value != UINT64_C(0);

    struct drm_mode_create_dumb create = {.height = CS_HEIGHT, .width = CS_WIDTH, .bpp = 32u};
    if (ioctl(dumb->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
        fprintf(stderr, "copy_scanout: CREATE_DUMB: %s\n", strerror(errno));
        return false;
    }
    dumb->handle = create.handle;
    dumb->pitch = create.pitch;
    dumb->size = create.size;
    struct drm_mode_map_dumb map = {.handle = create.handle};
    if (ioctl(dumb->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
        fprintf(stderr, "copy_scanout: MAP_DUMB: %s\n", strerror(errno));
        return false;
    }
    void *mapping = mmap(nullptr, dumb->size, PROT_READ | PROT_WRITE, MAP_SHARED, dumb->fd, (off_t)map.offset);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "copy_scanout: mmap dumb buffer: %s\n", strerror(errno));
        return false;
    }
    dumb->mapping = mapping;
    if (dumb->pitch % 32u != 0u || dumb->pitch < CS_PITCH || dumb->size < (uint64_t)dumb->pitch * CS_HEIGHT ||
        (uintptr_t)mapping % 32u != 0u) {
        fprintf(stderr, "copy_scanout: unusable dumb buffer: pitch %" PRIu32 ", size %" PRIu64 "\n", dumb->pitch,
                dumb->size);
        return false;
    }
    printf("dumb buffer on %s: pitch %" PRIu32 ", size %" PRIu64 ", prefer_shadow %" PRIu32 "\n", card_path, dumb->pitch,
           dumb->size, (uint32_t)prefer_shadow);
    return true;
}

static bool cs_run_real_dumb(const char *card_path, uint32_t *frame, uint32_t *seed, int32_t *out_exit) {
    cs_dumb_t dumb = {.fd = -1};
    cs_stats_t real = {0};
    bool ok = cs_dumb_open(&dumb, card_path);
    if (ok) {
        const cs_target_t target = {.base = dumb.mapping, .pitch = dumb.pitch, .name = "dumb buffer"};
        ok = cs_run_write(&target, frame, false, "stream to REAL dumb buffer");
        if (ok) {
            cs_run_cpu_read(&target, "CPU read of REAL dumb buffer");
            cs_stats_t real_sleep;
            ok = cs_run_combined(&target, frame, seed, "REAL dumb, sleep-paced", CS_PACE_SLEEP, &real_sleep) &&
                 cs_run_combined(&target, frame, seed, "REAL dumb, spin-paced", CS_PACE_SPIN, &real);
        }
    }
    cs_dumb_close(&dumb);
    if (!ok) {
        return false;
    }
    bool passed = real.median_ns <= CS_BUDGET_NS;
    printf("scanout write: MEASURED on a real dumb buffer.\n"
           "VERDICT: %s. Spin-paced median total against the 2 ms target; p99 and sleep-paced are in the table.\n", passed ? "PASS" : "MISS");
    *out_exit = passed ? CS_EXIT_OK : CS_EXIT_MISSED;
    return true;
}

static bool cs_run_all(const char *card_path, int32_t *out_exit) {
    uint32_t seed = 0u;
    void *pinned = nullptr;
    void *pinned_check = nullptr;
    void *write_combined = nullptr;
    void *ordinary_read = mmap(nullptr, CS_FRAME_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void *ordinary_write = mmap(nullptr, CS_FRAME_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ordinary_read == MAP_FAILED || ordinary_write == MAP_FAILED) {
        fprintf(stderr, "copy_scanout: mmap: %s\n", strerror(errno));
        return false;
    }
    /* Page-fault cost is not what is being measured. */
    memset(ordinary_read, 0, CS_FRAME_BYTES);
    memset(ordinary_write, 0, CS_FRAME_BYTES);
    if (!cs_ok("init", cs_gpu_init()) || !cs_ok("pinned alloc", cs_gpu_alloc_pinned(&pinned)) ||
        !cs_ok("pinned alloc", cs_gpu_alloc_pinned(&pinned_check)) ||
        !cs_ok("write-combined alloc", cs_gpu_alloc_write_combined(&write_combined))) {
        return false;
    }
    memset(write_combined, 0, CS_FRAME_BYTES);
    uint32_t *frame = pinned;

    cs_print_header();
    cs_print_link("before");
    if (!cs_run_readback("readback pinned, paced 120 Hz", pinned, CS_PACED_RUNS, true, &seed) ||
        !cs_run_readback("readback pinned, tight loop", pinned, CS_TIGHT_RUNS, false, &seed) ||
        !cs_run_readback("readback unpinned, tight loop", ordinary_read, CS_OTHER_RUNS, false, &seed) ||
        !cs_run_readback_async(pinned, &seed) || !cs_run_upload(frame, pinned_check, &seed)) {
        return false;
    }
    cs_print_link("after GPU copies");

    const cs_target_t ordinary = {.base = ordinary_write, .pitch = CS_PITCH, .name = "ordinary host memory"};
    const cs_target_t combined = {.base = write_combined, .pitch = CS_PITCH, .name = "host write-combined memory"};
    if (!cs_run_write(&ordinary, frame, true, "memcpy to ordinary host (WB)") ||
        !cs_run_write(&ordinary, frame, false, "stream to ordinary host (WB)") ||
        !cs_run_write(&combined, frame, false, "stream to host WC")) {
        return false;
    }
    cs_run_cpu_read(&ordinary, "CPU read of ordinary host (WB)");
    cs_run_cpu_read(&combined, "CPU read of host WC");

    cs_stats_t unused;
    if (!cs_run_combined(&ordinary, frame, &seed, "stand-in WB, sleep-paced", CS_PACE_SLEEP, &unused) ||
        !cs_run_combined(&combined, frame, &seed, "stand-in WC, sleep-paced", CS_PACE_SLEEP, &unused) ||
        !cs_run_combined(&combined, frame, &seed, "stand-in WC, spin-paced", CS_PACE_SPIN, &unused) ||
        !cs_run_combined(&combined, frame, &seed, "stand-in WC, tight", CS_PACE_TIGHT, &unused)) {
        return false;
    }
    cs_print_link("after frame runs");

    if (card_path == nullptr) {
        printf("scanout write: UNMEASURED. No dumb buffer was opened; every write above went to host memory.\n"
               "VERDICT: INCOMPLETE. The 2 ms target cannot be judged until the real write is timed by:\n"
               "  /tmp/mc_c_spikes/copy_scanout/copy_scanout --dumb-buffer /dev/dri/card1   (on a text console)\n");
        *out_exit = CS_EXIT_OK;
    } else if (!cs_run_real_dumb(card_path, frame, &seed, out_exit)) {
        return false;
    }

    cs_ok("free", cs_gpu_free_host(pinned));
    cs_ok("free", cs_gpu_free_host(pinned_check));
    cs_ok("free", cs_gpu_free_host(write_combined));
    munmap(ordinary_read, CS_FRAME_BYTES);
    munmap(ordinary_write, CS_FRAME_BYTES);
    cs_gpu_shutdown();
    return true;
}

int32_t main(int32_t argc, char **argv) {
    const char *card_path = nullptr;
    if (argc == 3 && strcmp(argv[1], "--dumb-buffer") == 0) {
        card_path = argv[2];
        if (!cs_dumb_gate_passes(card_path)) {
            return CS_EXIT_REFUSED;
        }
    } else if (argc != 1) {
        fprintf(stderr, "usage: copy_scanout [--dumb-buffer /dev/dri/cardN]\n");
        return CS_EXIT_ERROR;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0u);

    /* One CPU, so migrations are not part of the numbers. */
    int32_t cpu = sched_getcpu();
    cpu_set_t one_cpu;
    CPU_ZERO(&one_cpu);
    if (cpu >= 0) {
        CPU_SET((uint32_t)cpu, &one_cpu);
    }
    if (cpu < 0 || sched_setaffinity(0, sizeof(one_cpu), &one_cpu) != 0) {
        fprintf(stderr, "copy_scanout: could not pin to one CPU; continuing unpinned\n");
    } else {
        printf("pinned to cpu %" PRId32 "\n", cpu);
    }

    int32_t exit_code = CS_EXIT_ERROR;
    if (!cs_run_all(card_path, &exit_code)) {
        cs_gpu_shutdown();
        return CS_EXIT_ERROR;
    }
    return exit_code;
}
```

```text index.h
/* Stage 01.4 copy-to-scanout spike: the boundary shared by index.c (GCC C23), index.cu (nvcc)
 * and index.asm (NASM). Throwaway. */
#pragma once
#include <stdint.h>
#ifdef __CUDACC__
#define CS_HD __host__ __device__
#else
#define CS_HD
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* One XRGB8888 1080p frame. */
#define CS_WIDTH 1920u
#define CS_HEIGHT 1080u
#define CS_BYTES_PER_PIXEL 4u
#define CS_PITCH (CS_WIDTH * CS_BYTES_PER_PIXEL)
#define CS_PIXELS (CS_WIDTH * CS_HEIGHT)
#define CS_FRAME_BYTES ((uint64_t)CS_PITCH * (uint64_t)CS_HEIGHT)

/* The frame the kernel writes and the host checks. Differs per pixel and per frame, so a copy
 * that did not happen, or that delivered a stale frame, cannot pass the check. */
static inline CS_HD uint32_t cs_pattern(uint32_t pixel, uint32_t seed) {
    uint32_t mixed = (pixel + 1u) * 2654435761u;
    return (mixed ^ (mixed >> 15u)) ^ (seed * 2246822519u);
}

/* All return nullptr on success, or a static error string from cudart. */
const char *cs_gpu_init(void);
const char *cs_gpu_alloc_pinned(void **out_memory);
const char *cs_gpu_alloc_write_combined(void **out_memory);
const char *cs_gpu_free_host(void *memory);
const char *cs_gpu_fill(uint32_t seed);
const char *cs_gpu_read(void *host_frame);
const char *cs_gpu_read_submit(void *host_frame);
const char *cs_gpu_read_wait(void);
const char *cs_gpu_write(const void *host_frame);
void cs_gpu_shutdown(void);

/* index.asm. dst must be 32-byte aligned and bytes a multiple of 32. AVX2 non-temporal stores
 * ending in sfence: one-way, so a write-combined target is never read. */
void cs_stream_copy(void *dst, const void *src, uint64_t bytes);

#ifdef __cplusplus
}
#endif
```

```text index.cu
// Stage 01.4 spike: device side and the cudart calls. Throwaway.
#include <cuda_runtime.h>
#include "index.h"

static uint32_t *cs_device_frame = nullptr;
static cudaStream_t cs_stream = nullptr;

static const char *cs_result(cudaError_t error) {
    return error == cudaSuccess ? nullptr : cudaGetErrorString(error);
}

__global__ void cs_fill_kernel(uint32_t *frame, uint32_t pixels, uint32_t seed) {
    uint32_t pixel = blockIdx.x * blockDim.x + threadIdx.x;
    if (pixel < pixels) {
        frame[pixel] = cs_pattern(pixel, seed);
    }
}

extern "C" const char *cs_gpu_init(void) {
    cudaError_t error = cudaMalloc(&cs_device_frame, CS_FRAME_BYTES);
    if (error == cudaSuccess) {
        error = cudaStreamCreate(&cs_stream);
    }
    return cs_result(error);
}

extern "C" const char *cs_gpu_alloc_pinned(void **out_memory) {
    return cs_result(cudaHostAlloc(out_memory, CS_FRAME_BYTES, cudaHostAllocDefault));
}

extern "C" const char *cs_gpu_alloc_write_combined(void **out_memory) {
    return cs_result(cudaHostAlloc(out_memory, CS_FRAME_BYTES, cudaHostAllocWriteCombined));
}

extern "C" const char *cs_gpu_free_host(void *memory) {
    return cs_result(cudaFreeHost(memory));
}

extern "C" const char *cs_gpu_fill(uint32_t seed) {
    const uint32_t block = 256u;
    cs_fill_kernel<<<(CS_PIXELS + block - 1u) / block, block>>>(cs_device_frame, CS_PIXELS, seed);
    cudaError_t error = cudaGetLastError();
    if (error == cudaSuccess) {
        error = cudaDeviceSynchronize();
    }
    return cs_result(error);
}

extern "C" const char *cs_gpu_read(void *host_frame) {
    return cs_result(cudaMemcpy(host_frame, cs_device_frame, CS_FRAME_BYTES, cudaMemcpyDeviceToHost));
}

extern "C" const char *cs_gpu_read_submit(void *host_frame) {
    return cs_result(cudaMemcpyAsync(host_frame, cs_device_frame, CS_FRAME_BYTES, cudaMemcpyDeviceToHost, cs_stream));
}

extern "C" const char *cs_gpu_read_wait(void) {
    return cs_result(cudaStreamSynchronize(cs_stream));
}

extern "C" const char *cs_gpu_write(const void *host_frame) {
    return cs_result(cudaMemcpy(cs_device_frame, host_frame, CS_FRAME_BYTES, cudaMemcpyHostToDevice));
}

extern "C" void cs_gpu_shutdown(void) {
    if (cs_stream != nullptr) {
        cudaStreamDestroy(cs_stream);
        cs_stream = nullptr;
    }
    if (cs_device_frame != nullptr) {
        cudaFree(cs_device_frame);
        cs_device_frame = nullptr;
    }
}
```

```text index.asm
; Stage 01.4 spike: one-way streaming copy. Throwaway.
; void cs_stream_copy(void *dst, const void *src, uint64_t bytes)
; rdi = dst (32-byte aligned), rsi = src, rdx = bytes (multiple of 32). index.c checks both.
default rel
section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global cs_stream_copy
align 32
cs_stream_copy:
    mov rcx, rdx
    shr rcx, 7                  ; 128-byte blocks
    jz .tail
.block:
    vmovdqu ymm0, [rsi]
    vmovdqu ymm1, [rsi + 32]
    vmovdqu ymm2, [rsi + 64]
    vmovdqu ymm3, [rsi + 96]
    vmovntdq [rdi], ymm0
    vmovntdq [rdi + 32], ymm1
    vmovntdq [rdi + 64], ymm2
    vmovntdq [rdi + 96], ymm3
    add rsi, 128
    add rdi, 128
    dec rcx
    jnz .block
.tail:
    and rdx, 127
    jz .done
.tail_loop:
    vmovdqu ymm0, [rsi]
    vmovntdq [rdi], ymm0
    add rsi, 32
    add rdi, 32
    sub rdx, 32
    jnz .tail_loop
.done:
    sfence
    vzeroupper
    ret
```

```text build.sh
#!/bin/sh
# Stage 01.4 spike build. The output is /tmp/mc_c_spikes/copy_scanout/copy_scanout.
set -eu
here=/tmp/mc_c_spikes/copy_scanout
out="$here/build"
mkdir -p "$out"
nasm -f elf64 -o "$out/asm.o" "$here/index.asm"
gcc -std=c23 -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wdouble-promotion -Wshadow -Wvla \
    -Wpadded -Werror -O2 -march=skylake -mtune=skylake -fno-tree-vectorize -fno-tree-slp-vectorize \
    -c -o "$out/c.o" "$here/index.c"
nvcc -std=c++20 -arch=sm_75 -O3 -Xcompiler=-fno-exceptions,-fno-rtti,-fno-threadsafe-statics \
    -c -o "$out/cuda.o" "$here/index.cu"
gcc -o "$here/copy_scanout" "$out/c.o" "$out/asm.o" "$out/cuda.o" \
    -L/opt/cuda/lib64 -lcudart_static
gcc -std=c23 -Wall -Wextra -Werror -O1 -shared -fPIC -o "$out/test_shim.so" "$here/test_shim/index.c"
```

## Commits

Appended as they land:

Nothing from this stage is on `main`, so there is no landed sha yet. The commits are on branch `worktree-wf_a4b549de-085-9`, which holds all four Phase 01 stages rebased on `5bb5c57`, and the gate check. Landing needs `git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only worktree-wf_a4b549de-085-9` from outside the agent isolation; see the phase file. Record the landed shas here after that.


## Issues found

**1. The GPU link is x8, not the x16 the plan assumes.**
Expected: plan section "machine" and `research/003-speed-research.md` section 5 say gen3 x16 and "roughly 12 GB/s theoretical readback".
Actual: 8.0 GT/s x8 during the copies, and 6.5 GB/s measured.
Evidence: `/sys/bus/pci/devices/0000:01:00.0/max_link_width` is 16 (the GPU's own limit), but `current_link_width` is 8, and the root port's `/sys/bus/pci/devices/0000:00:01.0/max_link_width` and `current_link_width` are both 8. The sibling port `00:01.1` is active with nothing behind it. The nvidia-smi figure the plan used, `pcie.link.width.max`, reports only the GPU's limit. `lspci` also names the GPU `TU104BM [GeForce RTX 2080 SUPER Mobile / Max-Q]`.
Done: recorded, and the master plan left verbatim. Whether firmware can give it x16 is unknown and is for Ahmed. It decides this stage: x8 cannot carry 16.6 MB per frame in 2 ms if the dumb buffer is in VRAM.

**2. Two todos cannot be finished by an agent.**
Expected: this stage times the dumb-buffer write. Actual: the hard safety rules forbid agents opening `/dev/dri/card*`, and the desktop is live.
Evidence: the stage's own Verification note, and `/sys/class/tty/tty0/active` reading `tty7`.
Done: boxes left unticked, Status set to blocked, and the run left ready for Ahmed. The default run printed `scanout write: UNMEASURED` and `VERDICT: INCOMPLETE`, and exited 0.

**3. The `--dumb-buffer` path is tested only against a fake, so it may still fail on nvidia-drm.**
Expected: the gate refuses anywhere but the visible text console, before opening anything.
Actual, under an `LD_PRELOAD` shim that stops every `/dev/dri/` open from reaching the kernel (`test_shim/run.sh`): `DISPLAY` set, `WAYLAND_DISPLAY` set, stdin not a terminal, a pty, and a console other than the visible one all exit 2 with no open attempted. With the gate passed, a denied open exits with an error; a fake dumb buffer completes at pitch 7680 and 8192; pitch 7684 is rejected and the buffer destroyed. A default run under the shim logged no `/dev/dri` open at all, from us or from libcuda.
Limit: the fake is a memfd, so those runs printed `VERDICT: PASS` and `MEASURED on a real dumb buffer`, and both lines mean nothing there. The strings are only true outside the shim. The ioctls against the real driver have never run.
Done: left as is; Ahmed's run is the first real test. Rerun in full on the 23:28 rebuild with the same results; the only source change was the shim's banned `unsigned long long` log cast, now `PRIx64`. A word scan of the spike sources for the banned keywords now finds only the word "long" in one comment in `index.c`.

**4. Which number decides the pass was chosen here, and it matters.**
Expected: "total of at most 2 ms" has one meaning. Actual: spin-paced and sleep-paced stand-ins differ by 0.8 ms, either side of the target.
Evidence: the second Results table.
Done: VERDICT uses the spin-paced median, because a default `cudaDeviceSynchronize` busy-waits, so the real frame thread is awake when the copy starts. That reasoning is not proven by this spike, and the p99 sits right at the target. The program prints all rows so Ahmed can overrule it.

**5. This stage's commit could not be landed on main by the agent.**
Expected: `flock /tmp/mc_c_land.lock sh -c 'git rebase main && git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only worktree-wf_a4b549de-085-7'` fast-forwards main.
Actual: the session's worktree isolation guard refused it, both in that form and as a plain `git -C /home/ahmed/Projects/Games/MineCraft_C merge --ff-only worktree-wf_a4b549de-085-7`: "this command redirects git to the shared checkout via -C. Refusing to run it".
Done: the guard was not worked around, since a wrapper script, `git update-ref` or a `cd` into the main checkout would all bypass the same boundary. The branch is rebased on main and sits one fast-forward away. Whoever can write to the main checkout lands it with the command above.
Second attempt, by the agent that took over this stage on branch `worktree-wf_a4b549de-085-8`: the instructed `flock` command, with this branch's name, was refused the same way: "this command hands sh text naming git in a plain command, which cannot be shown to stay inside the worktree". It was again not worked around. `main` was still at `5bb5c57` afterwards. To land from the main checkout, `git merge --ff-only worktree-wf_a4b549de-085-8`, or if main has moved, `git cherry-pick 5bb5c57..worktree-wf_a4b549de-085-8`; these commits touch only this file.

**6. Gate check: Ahmed's command would not have survived a reboot.**
Expected: the stage's remaining measurement can be run whenever Ahmed gets to it, as he tests at the end.
Actual: the stage said that after a reboot "this stage's measurement must be rewritten first". Only `/tmp` held the source, on tmpfs. Stage 01.2 kept its source for this reason; this one did not.
Done: fixed. The source is under "Spike source" with a rebuild command, and the five files extracted by that command were compared with `cmp` against `/tmp/mc_c_spikes/copy_scanout/`. `build.sh` also now links `-lcudart_static` like the Stage 01.1 link list, not shared `-lcudart`; the numbers did not move. `test_shim/run.sh` now passes `/dev/dri/fake_card`, which does not exist, instead of the real `/dev/dri/card1`, so a shim that failed to load cannot reach the card. This is extra margin: without the shim, the gate would also refuse, because stdin is `/dev/null`.

**7. Gate check: the Results understated how much the numbers vary.**
Expected: the Results table's sleep-paced total "2.59 to 2.64", and its spin-paced p99 "2.00", describe what a rerun gives.
Actual: sleep-paced medians ranged 1.899 to 3.219 over these seven runs, and the spin-paced p99 reached 2.25 in the six normal ones. The first run was an outlier on every host-side row: stream to ordinary host 1.370 (usual 0.23), memcpy 1.637 (usual 0.30), stream to WC 1.380 (usual 0.23), CPU read of ordinary memory 1913 MB/s (usual about 10000), unpinned readback 2.107 (usual 1.33), and spin-paced p99 3.307. It ran pinned to cpu 12 at load average 1.0. The pinned DMA rows did not move, so the GPU and PCIe link were not the cause.
Done: recorded, with the tables above left as measured. **The cause of the outlier is not known:** the host rows are about six times slower, which would fit a core held near its 800 MHz floor by the `powersave` governor, but frequency was not being recorded on that run, and it did not recur in six more that did record it. When Ahmed reads his own run, one bad run is not decisive. Run it twice or three times. The verdict rests on a median, and a run like this one would still have read 1.811 on that row, so a passing median can hide a p99 well above 2 ms.
