#include <stdint.h>

typedef struct copy_span_t {
    uint32_t width;
    uint32_t height;
} copy_span_t;
static_assert(sizeof(copy_span_t) == 8);
static_assert(alignof(copy_span_t) == 4);
static_assert(offsetof(copy_span_t, width) == 0);
