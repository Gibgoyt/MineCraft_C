#include "types/index.h"
void palette_call(uint8_t *data, uint32_t length) {
    data[length - 1u] = 0;
}
