/* See README.md. */
#include "gpu_bridge/status/index.h"

#include "log/index.h"

#include <inttypes.h>

const char *gpu_status_name(gpu_status_t status) {
    switch (status) {
    case GPU_STATUS_OK:
        return "no error";
    case GPU_STATUS_NO_DEVICE:
        return "no CUDA device";
    case GPU_STATUS_DRIVER:
        return "the driver will not run this program";
    case GPU_STATUS_NO_KERNEL_IMAGE:
        return "the card cannot run the compiled kernels (check gpu.architecture in config/machine.yaml)";
    case GPU_STATUS_OUT_OF_MEMORY:
        return "out of memory";
    case GPU_STATUS_OVER_BUDGET:
        return "over this pool's budget (gpu.budget in config/machine.yaml)";
    case GPU_STATUS_TABLE_FULL:
        return "too many live device blocks";
    case GPU_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case GPU_STATUS_BAD_HANDLE:
        return "a device block that is not live";
    case GPU_STATUS_DEVICE_LOST:
        return "the device was lost: a kernel faulted, or the context died";
    case GPU_STATUS_SELF_CHECK:
        return "a kernel ran and gave the wrong answer";
    case GPU_STATUS_CUDA_OTHER:
        return "an unexpected CUDA error";
    }
    return "not a gpu_status_t";
}

void gpu_error_log(const gpu_error_t *error) {
    const char *const what = error->borrowed_what != nullptr ? error->borrowed_what : "gpu call";
    if (error->borrowed_cuda_name != nullptr) {
        LOG_ERROR("gpu: %s: %s (CUDA %" PRId32 ", %s: %s)", what, gpu_status_name(error->status), error->cuda_code,
                  error->borrowed_cuda_name, error->borrowed_cuda_text != nullptr ? error->borrowed_cuda_text : "");
    } else {
        LOG_ERROR("gpu: %s: %s", what, gpu_status_name(error->status));
    }
}
