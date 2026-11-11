__device__ void post_entry(void);
static __device__ void step(void) {
    post_entry();
}
__device__ void frame_entry(void) {
    step();
}
