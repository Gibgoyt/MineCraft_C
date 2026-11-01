#include <stdint.h>

typedef struct frame_inner_t {
    uint32_t a;
} frame_inner_t;
static_assert(sizeof(frame_inner_t) == 4);
static_assert(alignof(frame_inner_t) == 4);
static_assert(offsetof(frame_inner_t, a) == 0);

typedef struct frame_view_t {
    void (*on_done)(uint32_t code, uint32_t other);
    const uint8_t *pixels;
    uint32_t first, second;
    struct {
        uint32_t x;
        uint32_t y;
    } named;
    union {
        uint32_t u;
        uint32_t v;
    };
    alignas(4) uint32_t aligned;
    frame_inner_t inner;
    uint8_t bytes[4];
    uint8_t _pad0[4];
} frame_view_t;
static_assert(sizeof(frame_view_t) == 48);
static_assert(alignof(frame_view_t) == 8);
static_assert(offsetof(frame_view_t, on_done) == 0);
static_assert(offsetof(frame_view_t, pixels) == 8);
static_assert(offsetof(frame_view_t, first) == 16);
static_assert(offsetof(frame_view_t, second) == 20);
static_assert(offsetof(frame_view_t, named) == 24);
static_assert(offsetof(frame_view_t, u) == 32);
static_assert(offsetof(frame_view_t, v) == 32);
static_assert(offsetof(frame_view_t, aligned) == 36);
static_assert(offsetof(frame_view_t, inner) == 40);
static_assert(offsetof(frame_view_t, bytes) == 44);
static_assert(offsetof(frame_view_t, _pad0) == 48);
