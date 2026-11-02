/* Must not compile: float32_t at a varargs call. */
#include "types/index.h"
#include <stdio.h>
void fixture(float32_t value);
void fixture(float32_t value) {
    printf("%f", value);
}
