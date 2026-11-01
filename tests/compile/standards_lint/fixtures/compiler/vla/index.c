#include <stdint.h>
void cc_use(uint8_t *data);
void cc_call(uint32_t n);
void cc_call(uint32_t n) {
    uint8_t data[n];
    cc_use(data);
}
