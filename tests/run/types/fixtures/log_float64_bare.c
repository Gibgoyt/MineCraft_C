/* Must not compile: the LOG_ macros are checked as printf is. */
#include "log/index.h"
#include "types/index.h"
void fixture(float64_t value);
void fixture(float64_t value) {
    LOG_INFO("%f", value);
}
