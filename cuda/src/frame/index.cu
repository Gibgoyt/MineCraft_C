/* See README.md. One frame's kernels in order on one stream, the card's clock around each, and the copy home. */
#include "frame/index.hu"

#include <time.h>

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
    return first;
}

gpu_status_t frame_host_pin(frame_gpu_t *frame, void *host, uint64_t bytes) {
    return device_memory_check(&frame->last_error, cudaHostRegister(host, bytes, cudaHostRegisterDefault),
                               "frame: pinning host memory");
}

gpu_status_t frame_host_unpin(frame_gpu_t *frame, void *host) {
    return device_memory_check(&frame->last_error, cudaHostUnregister(host), "frame: unpinning host memory");
}

/* ---- Plan 008 phase 01: the marks past the passes ---------------------------------------------------------------------- */

enum : uint32_t {
    FRAME_MARK_EVENTS = (uint32_t)SHADE_SPAN_COUNT - 1u + 2u, /* shade's, then the clock's two */
};

static void **frame_marks_event(frame_marks_t *marks, uint32_t i) {
    return i < (uint32_t)SHADE_SPAN_COUNT - 1u ? &marks->owned_shade_events[i]
                                               : &marks->owned_clock_events[i - ((uint32_t)SHADE_SPAN_COUNT - 1u)];
}

gpu_status_t frame_marks_init(frame_marks_t *marks, gpu_error_t *error) {
    *marks = frame_marks_t{};
    for (uint32_t i = 0u; i < FRAME_MARK_EVENTS; i += 1u) {
        cudaEvent_t event = nullptr;
        const gpu_status_t status = device_memory_check(error, cudaEventCreate(&event), "frame: making a mark");
        if (status != GPU_STATUS_OK) {
            const gpu_error_t created = *error;
            (void)frame_marks_deinit(marks, error);
            *error = created;
            return status;
        }
        *frame_marks_event(marks, i) = (void *)event;
    }
    marks->open = 1u;
    return GPU_STATUS_OK;
}

gpu_status_t frame_marks_deinit(frame_marks_t *marks, gpu_error_t *error) {
    gpu_status_t first = GPU_STATUS_OK;
    for (uint32_t i = 0u; i < FRAME_MARK_EVENTS; i += 1u) {
        void **const event = frame_marks_event(marks, i);
        if (*event != nullptr) {
            gpu_error_t said = {};
            const gpu_status_t status =
                device_memory_check(&said, cudaEventDestroy((cudaEvent_t)*event), "frame: giving a mark back");
            if (status != GPU_STATUS_OK && first == GPU_STATUS_OK) {
                first = status;
                *error = said;
            }
            *event = nullptr;
        }
    }
    marks->open = 0u;
    return first;
}

/* What the clock kernel saw: the SM's cycles from its start to its end. Static storage on the card, not an allocation: one word,
 * written by one thread on one stream. */
__device__ uint64_t frame_clock_seen;

/* One warp, of which one thread counts: it spins until FRAME_CLOCK_CYCLES of the SM's clock have gone. The events either side time it
 * on the card's own timer. */
__global__ void frame_clock_kernel() {
    if (threadIdx.x != 0u) {
        return;
    }
    const int64_t started = (int64_t)clock64();
    int64_t now = started;
    while ((uint64_t)(now - started) < (uint64_t)FRAME_CLOCK_CYCLES) {
        now = (int64_t)clock64();
    }
    frame_clock_seen = (uint64_t)(now - started);
}

/* Starts the clock kernel between its two events. */
static gpu_status_t frame_clock_start(frame_marks_t *marks, gpu_error_t *error) {
    gpu_status_t status = device_memory_check(error, cudaEventRecord((cudaEvent_t)marks->owned_clock_events[0]),
                                              "frame: reading the card's clock before its speed");
    if (status == GPU_STATUS_OK) {
        frame_clock_kernel<<<1, 32>>>();
        status = device_memory_check(error, cudaGetLastError(), "frame: starting the clock kernel");
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(error, cudaEventRecord((cudaEvent_t)marks->owned_clock_events[1]),
                                     "frame: reading the card's clock after its speed");
    }
    return status;
}

/* After the wait: what the clock kernel saw. The events also hold the kernel's start, a few microseconds, so this reads a little low:
 * FRAME_CLOCK_CYCLES is long enough for that to be about 1 %. */
static gpu_status_t frame_clock_finish(frame_marks_t *marks, gpu_error_t *error) {
    uint64_t seen = 0u;
    gpu_status_t status = device_memory_check(error, cudaMemcpyFromSymbol(&seen, frame_clock_seen, sizeof(seen)),
                                              "frame: reading what the clock kernel saw");
    float32_t elapsed = 0.0F;
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(error,
                                     cudaEventElapsedTime(&elapsed, (cudaEvent_t)marks->owned_clock_events[0],
                                                          (cudaEvent_t)marks->owned_clock_events[1]),
                                     "frame: reading the clock kernel's time");
    }
    if (status != GPU_STATUS_OK) {
        return status;
    }
    marks->clock_cycles = seen;
    marks->clock_ms = elapsed;
    marks->card_mhz = elapsed > 0.0F ? (float32_t)((float64_t)seen / ((float64_t)elapsed * 1000.0)) : 0.0F;
    return GPU_STATUS_OK;
}

gpu_status_t frame_gpu_clock_mhz(frame_marks_t *marks, gpu_error_t *error) {
    if (marks->open == 0u) {
        *error = gpu_error_t{"frame: the marks are not started", nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    gpu_status_t status = frame_clock_start(marks, error);
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(error, cudaDeviceSynchronize(), "frame: waiting for the clock kernel");
    }
    return status == GPU_STATUS_OK ? frame_clock_finish(marks, error) : status;
}

gpu_status_t frame_gpu_clock_rated(float32_t *out_mhz, gpu_error_t *error) {
    *out_mhz = 0.0F;
    int32_t device = 0;
    int32_t khz = 0;
    gpu_status_t status = device_memory_check(error, cudaGetDevice(&device), "frame: asking which card this is");
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(error, cudaDeviceGetAttribute(&khz, cudaDevAttrClockRate, device),
                                     "frame: asking the card's peak clock");
    }
    if (status == GPU_STATUS_OK) {
        *out_mhz = (float32_t)khz / 1000.0F;
    }
    return status;
}

static int64_t frame_now_ns() {
    struct timespec now = {};
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000 + (int64_t)now.tv_nsec;
}

static gpu_status_t frame_mark(frame_gpu_t *frame, uint32_t event) {
    return device_memory_check(&frame->last_error, cudaEventRecord((cudaEvent_t)frame->owned_events[event]),
                               "frame: reading the card's clock");
}

/* Whichever failed says so through last_error, so the first failure stops the frame with its own words. */
static gpu_status_t frame_passes(frame_gpu_t *frame, const frame_args_t *args, uint32_t *out_ran,
                                 const uint32_t **out_fresh_count) {
    const uint32_t *frame_fresh_count = nullptr;
    const frame_targets_t *const targets = args->borrowed_targets;
    const traverse_targets_t traverse_targets = {targets->borrowed_coarse, targets->borrowed_hits};
    const traverse_camera_t *const camera = args->borrowed_camera;
    /* Every mark is made, for a kernel that will not run as well: what is between two marks is then always one kernel or nothing, and a
     * pass is always the marks from its first to the next pass's. */
    uint32_t ran = (1u << FRAME_KERNEL_COARSE) | (1u << FRAME_KERNEL_PRIMARY) | (1u << FRAME_KERNEL_SHADE) |
                   (1u << FRAME_KERNEL_POST) | (1u << FRAME_KERNEL_READBACK);
    frame_marks_t *const marks = frame->borrowed_marks;
    gpu_status_t status = GPU_STATUS_OK;
    /* Before the first pass's mark, so the frame's own times are as they would be without it. */
    if (marks != nullptr && marks->clock_due != 0u) {
        status = frame_clock_start(marks, &frame->last_error);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_COARSE);
    }
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
        shade_marks_t shade_marks = {};
        if (marks != nullptr) {
            for (uint32_t i = 0u; i + 1u < (uint32_t)SHADE_SPAN_COUNT; i += 1u) {
                shade_marks.borrowed_events[i] = marks->owned_shade_events[i];
            }
        }
        status = shade_frame_marked(&frame->last_error, args->borrowed_scene, args->borrowed_post, args->borrowed_world,
                                    camera, targets->borrowed_hits, targets->borrowed_pixels, entities,
                                    marks != nullptr ? &shade_marks : nullptr);
        frame_fresh_count = shade_marks.fresh_count;
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
        status =
            post_frame_temporal(&frame->last_error, args->borrowed_post, scene->borrowed_temporal,
                                (scene->features & SHADE_FEATURE_TAA) != 0u, shafts, camera, targets->borrowed_pixels,
                                targets->borrowed_xrgb, args->borrowed_weather, args->borrowed_weather_map);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_OVERLAY);
    }
    if (status == GPU_STATUS_OK && entities != nullptr) {
        /* The same with nothing aimed at and nothing to place. */
        const entity_rays_mark_t *const mark = &entities->mark;
        const entity_rays_preview_t *const preview = &entities->preview;
        const bool lines =
            mark->outline != 0u && mark->pixel_min[0] < mark->pixel_end[0] && mark->pixel_min[1] < mark->pixel_end[1];
        const bool ghost = preview->count != 0u && preview->pixel_min[0] < preview->pixel_end[0] &&
                           preview->pixel_min[1] < preview->pixel_end[1];
        ran |= lines || ghost ? 1u << FRAME_KERNEL_OVERLAY : 0u;
        const float32_t no_jitter[2] = {0.0F, 0.0F};
        const post_temporal_t *const temporal = scene->borrowed_temporal;
        status = entity_rays_overlay(&frame->last_error, entities, args->borrowed_world, camera,
                                     temporal != nullptr ? temporal->jitter : no_jitter, scene->water_material,
                                     targets->borrowed_pixels, targets->borrowed_xrgb);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, FRAME_KERNEL_READBACK);
    }
    if (marks != nullptr) {
        /* The copy home waits for every kernel before it, so from here the thread waits for the card. */
        marks->queued_ns = frame_now_ns();
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
    *out_fresh_count = frame_fresh_count;
    return status;
}

/* After the wait: shade's spans, the fresh list's length, and the clock if this frame read it. The length is four bytes copied after
 * the wait, as a copy home in the middle of the stream would make the thread wait there for shade, with the card idle while it then
 * starts post. */
static gpu_status_t frame_marks_read(frame_marks_t *marks, frame_gpu_t *frame, const uint32_t *fresh_count) {
    cudaEvent_t from = (cudaEvent_t)frame->owned_events[FRAME_KERNEL_SHADE];
    for (uint32_t i = 0u; i < (uint32_t)SHADE_SPAN_COUNT; i += 1u) {
        const cudaEvent_t to = i + 1u < (uint32_t)SHADE_SPAN_COUNT
                                   ? (cudaEvent_t)marks->owned_shade_events[i]
                                   : (cudaEvent_t)frame->owned_events[FRAME_KERNEL_GOD_RAYS];
        float32_t elapsed = 0.0F;
        const gpu_status_t status = device_memory_check(&frame->last_error, cudaEventElapsedTime(&elapsed, from, to),
                                                        "frame: reading a shade kernel's time");
        if (status != GPU_STATUS_OK) {
            return status;
        }
        marks->shade_ms[i] = elapsed;
        from = to;
    }
    marks->fresh_count = 0u;
    if (fresh_count != nullptr) {
        const gpu_status_t status = device_memory_check(
            &frame->last_error, cudaMemcpy(&marks->fresh_count, fresh_count, sizeof(uint32_t), cudaMemcpyDeviceToHost),
            "frame: reading the fresh list's length");
        if (status != GPU_STATUS_OK) {
            return status;
        }
    }
    marks->clock_read = marks->clock_due;
    if (marks->clock_due != 0u) {
        marks->clock_due = 0u;
        return frame_clock_finish(marks, &frame->last_error);
    }
    return GPU_STATUS_OK;
}

gpu_status_t frame_gpu_run(frame_gpu_t *frame, const frame_args_t *args, frame_timings_t *out_timings) {
    if (frame->owned_events[0] == nullptr) {
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
    const uint32_t *fresh_count = nullptr;
    gpu_status_t status = frame_passes(frame, args, &ran, &fresh_count);
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
    frame_marks_t *const marks = frame->borrowed_marks;
    return marks != nullptr ? frame_marks_read(marks, frame, fresh_count) : GPU_STATUS_OK;
}
