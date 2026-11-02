/* Must not compile: -Wformat rejects a bare float64_t for %f. */
#include "types/index.h"
#include <stdio.h>
void fixture(float64_t value);
void fixture(float64_t value) {
    printf("%f", value);
}
