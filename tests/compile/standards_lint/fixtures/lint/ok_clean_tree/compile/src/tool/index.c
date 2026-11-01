#include "types/index.h"
#include "tool/part/index.h"
int32_t main(int32_t argc, char **argv) {
    void *region = mmap(0, 1, 0, 0, -1, 0);
    munmap(region, 1);
    return 0;
}
