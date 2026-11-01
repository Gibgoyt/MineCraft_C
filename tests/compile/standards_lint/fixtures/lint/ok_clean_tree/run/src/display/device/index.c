void device_map(void) {
    mmap(0, 1, 0, 0, -1, 0);
    munmap(0, 1);
}
