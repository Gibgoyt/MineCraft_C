#include <stdint.h>
int32_t cg_a(int32_t n);
int32_t cg_b(int32_t n);
int32_t cg_a(int32_t n) {
    return n > 0 ? cg_b(n - 1) : 0;
}
