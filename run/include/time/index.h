/* See run/src/time/README.md. */
#ifndef MC_RUN_TIME_INDEX_H
#define MC_RUN_TIME_INDEX_H

#include "types/index.h"

#include <stdint.h>

/* Signed on purpose: the difference of two readings is a plain subtraction, and a negative one is visible. It takes 292
 * years of uptime to overflow. */
enum : int64_t {
    TIME_NS_PER_MS = 1000000,
    TIME_NS_PER_SECOND = 1000000000,
};

/* Nanoseconds since an arbitrary point at boot, the same point for every thread. Never goes backwards, and is not moved
 * when somebody sets the wall clock. Not a syscall on this platform: the kernel maps it into the process (vDSO). */
int64_t time_now_ns(void);

/* Sleeps for at least this long. A signal does not cut it short. Zero or less returns at once. */
void time_sleep_ns(int64_t duration_ns);

/* Sleeps until time_now_ns() has reached deadline_ns. A deadline already past returns at once. A fixed-rate loop should
 * use this and add its period to the last deadline, so that lateness in one step does not push back all the others. */
void time_sleep_until_ns(int64_t deadline_ns);

float64_t time_ns_to_seconds(int64_t duration_ns);
float64_t time_ns_to_ms(int64_t duration_ns);

#endif
