#include <cuda_runtime.h>
#include "gpu_bridge/frame/index.h"
void device_get(void) {
    cudaMalloc(0, 1);
    cudaFree(0);
}
