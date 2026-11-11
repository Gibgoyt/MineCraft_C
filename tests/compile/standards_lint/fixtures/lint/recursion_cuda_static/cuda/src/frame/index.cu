static __device__ void frame_step(uint32_t n);
static __device__ void frame_step(uint32_t n) {
    frame_step(n);
}
__device__ void frame_entry(uint32_t n) {
    frame_step(n);
}
