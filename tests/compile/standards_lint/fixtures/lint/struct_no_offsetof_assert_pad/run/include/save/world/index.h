#include <stdint.h>

typedef struct save_header_t {
    uint32_t version;
    uint8_t flags;
    uint8_t _pad0[3];
} save_header_t;
static_assert(sizeof(save_header_t) == 8);
static_assert(alignof(save_header_t) == 4);
static_assert(offsetof(save_header_t, version) == 0);
static_assert(offsetof(save_header_t, flags) == 4);
