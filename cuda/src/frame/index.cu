/* See README.md. One frame's kernels in order on one stream, the card's clock around each, and the copy home. */
#include "frame/index.hu"

gpu_status_t frame_gpu_init(frame_gpu_t *frame) {
    *frame = frame_gpu_t{};
    for (uint32_t i = 0u; i < (uint32_t)FRAME_PASS_COUNT + 1u; i += 1u) {
        cudaEvent_t event = nullptr;
        const gpu_status_t status =
            device_memory_check(&frame->last_error, cudaEventCreate(&event), "frame: making a clock event");
        if (status != GPU_STATUS_OK) {
            /* The creating failure is the one to report, so it is kept over whatever giving back the rest says. */
            const gpu_error_t created = frame->last_error;
            (void)frame_gpu_deinit(frame);
            frame->last_error = created;
            return status;
        }
        frame->owned_events[i] = (void *)event;
    }
    frame->open = 1u;
    return GPU_STATUS_OK;
}

gpu_status_t frame_gpu_deinit(frame_gpu_t *frame) {
    gpu_status_t first = GPU_STATUS_OK;
    for (uint32_t i = 0u; i < (uint32_t)FRAME_PASS_COUNT + 1u; i += 1u) {
        if (frame->owned_events[i] != nullptr) {
            gpu_error_t error = {};
            const gpu_status_t status = device_memory_check(
                &error, cudaEventDestroy((cudaEvent_t)frame->owned_events[i]), "frame: giving a clock event back");
            if (status != GPU_STATUS_OK && first == GPU_STATUS_OK) {
                first = status;
                frame->last_error = error;
            }
            frame->owned_events[i] = nullptr;
        }
    }
    frame->open = 0u;
    return first;
}

gpu_status_t frame_host_pin(frame_gpu_t *frame, void *host, uint64_t bytes) {
    return device_memory_check(&frame->last_error, cudaHostRegister(host, bytes, cudaHostRegisterDefault),
                               "frame: pinning host memory");
}

gpu_status_t frame_host_unpin(frame_gpu_t *frame, void *host) {
    return device_memory_check(&frame->last_error, cudaHostUnregister(host), "frame: unpinning host memory");
}

static gpu_status_t frame_mark(frame_gpu_t *frame, uint32_t event) {
    return device_memory_check(&frame->last_error, cudaEventRecord((cudaEvent_t)frame->owned_events[event]),
                               "frame: reading the card's clock");
}

/* Whichever failed says so through last_error, so the first failure stops the frame with its own words. */
static gpu_status_t frame_passes(frame_gpu_t *frame, const frame_args_t *args) {
    const frame_targets_t *const targets = args->borrowed_targets;
    const traverse_targets_t traverse_targets = {targets->borrowed_coarse, targets->borrowed_hits};
    const traverse_camera_t *const camera = args->borrowed_camera;
    gpu_status_t status = frame_mark(frame, FRAME_PASS_COARSE);
    if (status == GPU_STATUS_OK) {
        status = traverse_coarse(&frame->last_error, args->borrowed_world, camera, &traverse_targets);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_PASS_PRIMARY);
    }
    if (status == GPU_STATUS_OK) {
        status = traverse_primary(&frame->last_error, args->borrowed_world, camera, &traverse_targets, true);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_PASS_SHADE);
    }
    if (status == GPU_STATUS_OK) {
        status = shade_frame(&frame->last_error, args->borrowed_scene, args->borrowed_post, args->borrowed_world,
                             camera, targets->borrowed_hits, targets->borrowed_pixels);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_PASS_POST);
    }
    const shade_scene_t *const scene = args->borrowed_scene;
    const bool shafts = (scene->features & SHADE_FEATURE_GOD_RAYS) != 0u;
    if (status == GPU_STATUS_OK && shafts) {
        status = shade_shafts_frame(&frame->last_error, scene, args->borrowed_world, camera, targets->borrowed_pixels);
    }
    if (status == GPU_STATUS_OK) {
        status = post_frame_temporal(&frame->last_error, args->borrowed_post, scene->borrowed_temporal,
                                     (scene->features & SHADE_FEATURE_TAA) != 0u, shafts, camera,
                                     targets->borrowed_pixels, targets->borrowed_xrgb);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_PASS_READBACK);
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(&frame->last_error,
                                     cudaMemcpy(args->borrowed_host_xrgb, device_memory_pointer(targets->borrowed_xrgb),
                                                post_xrgb_bytes(camera->width, camera->height), cudaMemcpyDeviceToHost),
                                     "frame: reading the picture back");
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_PASS_COUNT);
    }
    return status;
}

gpu_status_t frame_gpu_run(frame_gpu_t *frame, const frame_args_t *args, frame_timings_t *out_timings) {
    if (frame->open == 0u) {
        frame->last_error = gpu_error_t{"frame: not started", nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    if (args->borrowed_targets->borrowed_xrgb->bytes <
        post_xrgb_bytes(args->borrowed_camera->width, args->borrowed_camera->height)) {
        frame->last_error = gpu_error_t{"frame: the picture is too large for its target", nullptr, nullptr, 0,
                                        GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    gpu_status_t status = frame_passes(frame, args);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    /* This is what also reports a kernel that faulted after it started. */
    status = device_memory_check(&frame->last_error, cudaDeviceSynchronize(), "frame: waiting for the passes");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    float32_t total = 0.0F;
    for (uint32_t i = 0u; i < (uint32_t)FRAME_PASS_COUNT; i += 1u) {
        status = device_memory_check(&frame->last_error,
                                     cudaEventElapsedTime(&out_timings->pass_ms[i], (cudaEvent_t)frame->owned_events[i],
                                                          (cudaEvent_t)frame->owned_events[i + 1u]),
                                     "frame: reading a pass's time");
        if (status != GPU_STATUS_OK) {
            return status;
        }
        total += out_timings->pass_ms[i];
    }
    out_timings->total_ms = total;
    return GPU_STATUS_OK;
}
