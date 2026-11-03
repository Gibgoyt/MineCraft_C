/* See README.md. One frame's kernels in order on one stream, the card's clock around each, and the copy home. */
#include "frame/index.hu"

/* Where each pass starts among the kernels, and where the last ends. */
static const uint32_t frame_pass_first_kernel[(uint32_t)FRAME_PASS_COUNT + 1u] = {
    (uint32_t)FRAME_KERNEL_COARSE,   (uint32_t)FRAME_KERNEL_PRIMARY,  (uint32_t)FRAME_KERNEL_SHADE,
    (uint32_t)FRAME_KERNEL_GOD_RAYS, (uint32_t)FRAME_KERNEL_READBACK, (uint32_t)FRAME_KERNEL_COUNT,
};

const char *frame_kernel_name(frame_kernel_t kernel) {
    static const char *const names[(uint32_t)FRAME_KERNEL_COUNT] = {
        "coarse", "primary", "entity_rays", "lod", "shade", "god_rays", "post", "entity_overlay", "readback",
    };
    return names[(uint32_t)kernel];
}

gpu_status_t frame_gpu_init(frame_gpu_t *frame) {
    *frame = frame_gpu_t{};
    for (uint32_t i = 0u; i < (uint32_t)FRAME_KERNEL_COUNT + 1u; i += 1u) {
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
    for (uint32_t i = 0u; i < (uint32_t)FRAME_KERNEL_COUNT + 1u; i += 1u) {
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
static gpu_status_t frame_passes(frame_gpu_t *frame, const frame_args_t *args, uint32_t *out_ran) {
    const frame_targets_t *const targets = args->borrowed_targets;
    const traverse_targets_t traverse_targets = {targets->borrowed_coarse, targets->borrowed_hits};
    const traverse_camera_t *const camera = args->borrowed_camera;
    /* Every mark is made, for a kernel that will not run as well: what is between two marks is then always one kernel or nothing, and a
     * pass is always the marks from its first to the next pass's. */
    uint32_t ran = (1u << FRAME_KERNEL_COARSE) | (1u << FRAME_KERNEL_PRIMARY) | (1u << FRAME_KERNEL_SHADE) |
                   (1u << FRAME_KERNEL_POST) | (1u << FRAME_KERNEL_READBACK);
    gpu_status_t status = frame_mark(frame, FRAME_KERNEL_COARSE);
    if (status == GPU_STATUS_OK) {
        status = traverse_coarse(&frame->last_error, args->borrowed_world, camera, &traverse_targets);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_PRIMARY);
    }
    if (status == GPU_STATUS_OK) {
        status = traverse_primary(&frame->last_error, args->borrowed_world, camera, &traverse_targets, true);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_ENTITY_RAYS);
    }
    const entity_rays_scene_t *const entities = args->borrowed_entities;
    if (status == GPU_STATUS_OK && entities != nullptr) {
        /* With no boxes it returns having started nothing, and there is no kernel to name. */
        ran |= entities->box_count != 0u ? 1u << FRAME_KERNEL_ENTITY_RAYS : 0u;
        status = entity_rays_frame(&frame->last_error, entities, camera);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_LOD);
    }
    /* Under primary's clock, as it is the rest of those same rays. */
    if (status == GPU_STATUS_OK && args->borrowed_lod != nullptr) {
        ran |= 1u << FRAME_KERNEL_LOD;
        status = lod_frame(&frame->last_error, args->borrowed_lod, args->borrowed_lod_light, args->borrowed_world,
                           camera, targets->borrowed_hits);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_SHADE);
    }
    if (status == GPU_STATUS_OK) {
        status =
            shade_frame_entities(&frame->last_error, args->borrowed_scene, args->borrowed_post, args->borrowed_world,
                                 camera, targets->borrowed_hits, targets->borrowed_pixels, entities);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_GOD_RAYS);
    }
    const shade_scene_t *const scene = args->borrowed_scene;
    const bool shafts = (scene->features & SHADE_FEATURE_GOD_RAYS) != 0u;
    if (status == GPU_STATUS_OK && shafts) {
        ran |= 1u << FRAME_KERNEL_GOD_RAYS;
        status = shade_shafts_frame(&frame->last_error, scene, args->borrowed_world, camera, targets->borrowed_pixels);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_POST);
    }
    if (status == GPU_STATUS_OK) {
        status = post_frame_temporal(&frame->last_error, args->borrowed_post, scene->borrowed_temporal,
                                     (scene->features & SHADE_FEATURE_TAA) != 0u, shafts, camera,
                                     targets->borrowed_pixels, targets->borrowed_xrgb);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_OVERLAY);
    }
    if (status == GPU_STATUS_OK && entities != nullptr) {
        /* The same with nothing aimed at. */
        const entity_rays_mark_t *const mark = &entities->mark;
        ran |= mark->outline != 0u && mark->pixel_min[0] < mark->pixel_end[0] && mark->pixel_min[1] < mark->pixel_end[1]
                   ? 1u << FRAME_KERNEL_OVERLAY
                   : 0u;
        const float32_t no_jitter[2] = {0.0F, 0.0F};
        const post_temporal_t *const temporal = scene->borrowed_temporal;
        status = entity_rays_overlay(&frame->last_error, entities, args->borrowed_world, camera,
                                     temporal != nullptr ? temporal->jitter : no_jitter, scene->water_material,
                                     targets->borrowed_pixels, targets->borrowed_xrgb);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_READBACK);
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(&frame->last_error,
                                     cudaMemcpy(args->borrowed_host_xrgb, device_memory_pointer(targets->borrowed_xrgb),
                                                post_xrgb_bytes(camera->width, camera->height), cudaMemcpyDeviceToHost),
                                     "frame: reading the picture back");
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_COUNT);
    }
    *out_ran = ran;
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
    uint32_t ran = 0u;
    gpu_status_t status = frame_passes(frame, args, &ran);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    /* This is what also reports a kernel that faulted after it started. */
    status = device_memory_check(&frame->last_error, cudaDeviceSynchronize(), "frame: waiting for the passes");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    *out_timings = frame_timings_t{};
    out_timings->kernels_ran = ran;
    for (uint32_t pass = 0u; pass < (uint32_t)FRAME_PASS_COUNT; pass += 1u) {
        for (uint32_t i = frame_pass_first_kernel[pass]; i < frame_pass_first_kernel[pass + 1u]; i += 1u) {
            float32_t elapsed = 0.0F;
            status = device_memory_check(&frame->last_error,
                                         cudaEventElapsedTime(&elapsed, (cudaEvent_t)frame->owned_events[i],
                                                              (cudaEvent_t)frame->owned_events[i + 1u]),
                                         "frame: reading a kernel's time");
            if (status != GPU_STATUS_OK) {
                return status;
            }
            /* The gap for one that did not run stays in its pass, so a frame's passes still add up to the whole of it. */
            out_timings->pass_ms[pass] += elapsed;
            out_timings->total_ms += elapsed;
            out_timings->kernel_ms[i] = (ran & (1u << i)) != 0u ? elapsed : 0.0F;
        }
    }
    return GPU_STATUS_OK;
}
