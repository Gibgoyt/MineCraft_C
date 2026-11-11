__global__ void frame_kernel(uint32_t grid, uint32_t block) {
    frame_kernel<<<grid, block>>>(grid, block);
}
