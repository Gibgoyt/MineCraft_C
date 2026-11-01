#include "harness/index.h"
#include "frame/index.h"
void harness_map(void) {
    mmap(0, 1, 0, 0, -1, 0);
}
