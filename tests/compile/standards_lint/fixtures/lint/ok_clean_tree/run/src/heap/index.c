#include <sys/mman.h>
void *heap_reserve(uint64_t bytes) {
    return mmap(0, bytes, 0, 0, -1, 0);
}
void heap_release(void *block) {
    munmap(block, 1);
}
