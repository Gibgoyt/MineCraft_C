#include <stdint.h>
void cc_use(uint8_t *data);
void cc_call(void);
void cc_call(void) {
    uint8_t data[4096];
    cc_use(data);
}
