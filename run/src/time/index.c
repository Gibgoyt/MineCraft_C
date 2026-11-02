/* See README.md. CLOCK_MONOTONIC and not CLOCK_MONOTONIC_RAW: only the former can be slept on with clock_nanosleep, and
 * a clock read and slept on must be the same clock. */
#include "time/index.h"

#include "assert/index.h"

#include <errno.h>
#include <time.h>

int64_t time_now_ns(void) {
    struct timespec now;
    /* Fails only on a bad clock id or pointer, and both are constants here. */
    const int32_t result = clock_gettime(CLOCK_MONOTONIC, &now);
    ASSERT_ALWAYS(result == 0);
    return (int64_t)now.tv_sec * TIME_NS_PER_SECOND + (int64_t)now.tv_nsec;
}

void time_sleep_until_ns(int64_t deadline_ns) {
    if (deadline_ns < 0) {
        return; /* before boot, and timespec cannot hold it */
    }
    const struct timespec deadline = {.tv_sec = deadline_ns / TIME_NS_PER_SECOND,
                                      .tv_nsec = deadline_ns % TIME_NS_PER_SECOND};
    /* The deadline is absolute, so going round again after a signal loses nothing. clock_nanosleep returns the error
     * and does not set errno. */
    int32_t result = 0;
    do {
        result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
    } while (result == EINTR);
    ASSERT_ALWAYS(result == 0);
}

void time_sleep_ns(int64_t duration_ns) {
    if (duration_ns > 0) {
        time_sleep_until_ns(time_now_ns() + duration_ns);
    }
}

float64_t time_ns_to_seconds(int64_t duration_ns) {
    return (float64_t)duration_ns / 1.0e9f64;
}

float64_t time_ns_to_ms(int64_t duration_ns) {
    return (float64_t)duration_ns / 1.0e6f64;
}
