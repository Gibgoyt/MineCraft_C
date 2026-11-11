#include <stdint.h>
int32_t cg_entry(int32_t x);

static int32_t cg_double(int32_t x) {
    return x * 2;
}

static int32_t cg_apply(int32_t (*function)(int32_t), int32_t x) {
    return function(x);
}

int32_t cg_entry(int32_t x) {
    return cg_apply(cg_double, x);
}
