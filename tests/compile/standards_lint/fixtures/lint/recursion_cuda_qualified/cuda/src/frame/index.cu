__device__ void frame_b(void);
__device__ void frame_a(void) noexcept {
    frame_b();
}
__device__ void frame_b(void) __attribute__((noinline)) {
    frame_a();
}
