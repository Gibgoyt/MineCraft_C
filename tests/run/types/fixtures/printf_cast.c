/* Must compile: the rule in plans/CODING_STANDARD.md section 2, for both float types. */
#include "types/index.h"
#include <stdio.h>
void fixture(float32_t single, float64_t twin);
void fixture(float32_t single, float64_t twin) {
    printf("%f %f", (libc_double_t)single, (libc_double_t)twin);
}
