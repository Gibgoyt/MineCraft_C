#include <stdint.h>

typedef struct frame_view_t {
    uint32_t width;
    uint32_t height;
} frame_view_t;
static_assert(sizeof(frame_view_t) == 8);
static_assert(offsetof(frame_view_t, width) == 0);
static_assert(offsetof(frame_view_t, height) == 4);
