// Adds an array on the GPU. Compute only: a few KiB of VRAM, and nothing here touches the display.
#include <stdint.h>
#include <stdio.h>

static bool hello_check(cudaError_t rc, const char *what) {
    if (rc != cudaSuccess) {
        fprintf(stderr, "minecraft_c hello: %s: %s\n", what, cudaGetErrorString(rc));
        return false;
    }
    return true;
}

__global__ void hello_sum_kernel(const uint32_t *values, uint32_t *sum) {
    const uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    atomicAdd(sum, values[i]);
}

static bool hello_run(const uint32_t *values, uint32_t count, uint32_t *device_values, uint32_t *device_sum,
                      uint32_t *out_sum) {
    const uint32_t zero = 0;
    if (!hello_check(cudaMemcpy(device_values, values, count * sizeof(uint32_t), cudaMemcpyHostToDevice), "upload") ||
        !hello_check(cudaMemcpy(device_sum, &zero, sizeof(uint32_t), cudaMemcpyHostToDevice), "clear")) {
        return false;
    }
    hello_sum_kernel<<<1, count>>>(device_values, device_sum);
    if (!hello_check(cudaGetLastError(), "launch") || !hello_check(cudaDeviceSynchronize(), "kernel")) {
        return false;
    }
    return hello_check(cudaMemcpy(out_sum, device_sum, sizeof(uint32_t), cudaMemcpyDeviceToHost), "download");
}

extern "C" bool mc_hello_gpu_sum(const uint32_t *values, uint32_t count, uint32_t *out_sum) {
    uint32_t *device_values = nullptr;
    uint32_t *device_sum = nullptr;
    bool ok = hello_check(cudaMalloc(&device_values, count * sizeof(uint32_t)), "malloc values") &&
              hello_check(cudaMalloc(&device_sum, sizeof(uint32_t)), "malloc sum") &&
              hello_run(values, count, device_values, device_sum, out_sum);
    ok = hello_check(cudaFree(device_sum), "free sum") && ok;
    ok = hello_check(cudaFree(device_values), "free values") && ok;
    return ok;
}
