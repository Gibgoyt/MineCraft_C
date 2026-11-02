/* Must compile. */
#include "log/index.h"
#include "types/index.h"
void fixture(float64_t value);
void fixture(float64_t value) {
    LOG_INFO("%.2f", (libc_double_t)value);
}
