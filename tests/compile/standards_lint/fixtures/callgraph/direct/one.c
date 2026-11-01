#include <stdint.h>
int32_t cg_self(int32_t n);
int32_t cg_self(int32_t n) {
    return n > 0 ? cg_self(n - 1) : 0;
}
