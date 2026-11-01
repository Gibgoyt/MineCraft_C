#include <stdint.h>
typedef struct cc_t {
    uint64_t b;
    uint8_t a;
    uint8_t _pad0[7];
} cc_t;
static_assert(sizeof(cc_t) == 16);
extern cc_t cc_value;
