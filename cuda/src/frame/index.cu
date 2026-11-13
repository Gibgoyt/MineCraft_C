/* See README.md. One frame's kernels in order on one stream, the card's clock around each, and the copy home on a second stream, with up
 * to two frames on their way at once. */
#include "frame/index.hu"

#include <string.h>
#include <time.h>

/* Where each pass starts among the kernels, and where the last ends. */
static const uint32_t frame_pass_first_kernel[(uint32_t)FRAME_PASS_COUNT + 1u] = {
    (uint32_t)FRAME_KERNEL_COARSE,   (uint32_t)FRAME_KERNEL_PRIMARY,  (uint32_t)FRAME_KERNEL_SHADE,
    (uint32_t)FRAME_KERNEL_GOD_RAYS, (uint32_t)FRAME_KERNEL_READBACK, (uint32_t)FRAME_KERNEL_COUNT,
};

enum : uint32_t {
    /* A slot's events: one before each kernel and one after the copy home, shade's, the clock's two, and the end. */
    FRAME_SLOT_EVENTS = (uint32_t)FRAME_KERNEL_COUNT + 1u + (uint32_t)SHADE_SPAN_COUNT - 1u + 2u + 1u,
};

const char *frame_kernel_name(frame_kernel_t kernel) {
    static const char *const names[(uint32_t)FRAME_KERNEL_COUNT] = {
        "coarse", "primary", "entity_rays", "lod", "shade", "god_rays", "post", "entity_overlay", "readback",
    };
    return names[(uint32_t)kernel];
}

uint64_t frame_flight_bytes(void) {
    return ((uint64_t)sizeof(frame_flight_t) + FRAME_FLIGHT_ALIGNMENT - 1u) / FRAME_FLIGHT_ALIGNMENT *
           FRAME_FLIGHT_ALIGNMENT;
}

static void **frame_slot_event(frame_slot_t *slot, uint32_t i) {
    const uint32_t kernels = (uint32_t)FRAME_KERNEL_COUNT + 1u;
    const uint32_t shade = (uint32_t)SHADE_SPAN_COUNT - 1u;
    if (i < kernels) {
        return &slot->owned_events[i];
    }
    if (i < kernels + shade) {
        return &slot->owned_shade_events[i - kernels];
    }
    if (i < kernels + shade + 2u) {
        return &slot->owned_clock_events[i - kernels - shade];
    }
    return &slot->owned_end;
}

gpu_status_t frame_gpu_init(frame_gpu_t *frame, void *flight, uint8_t *staging, uint64_t staging_bytes) {
    *frame = frame_gpu_t{};
    memset(flight, 0, frame_flight_bytes());
    gpu_status_t status =
        device_memory_check(&frame->last_error, cudaHostRegister(flight, frame_flight_bytes(), cudaHostRegisterDefault),
                            "frame: pinning the frames on their way");
    if (status != GPU_STATUS_OK) {
        return status;
    }
    frame_flight_t *const on_way = (frame_flight_t *)flight;
    frame->borrowed_flight = on_way;
    for (uint32_t s = 0u; s < FRAME_FLIGHT_SLOTS && status == GPU_STATUS_OK; s += 1u) {
        for (uint32_t i = 0u; i < FRAME_SLOT_EVENTS && status == GPU_STATUS_OK; i += 1u) {
            cudaEvent_t event = nullptr;
            status = device_memory_check(&frame->last_error, cudaEventCreate(&event), "frame: making a clock event");
            *frame_slot_event(&on_way->slots[s], i) = (void *)event;
        }
    }
    if (status == GPU_STATUS_OK) {
        cudaStream_t stream = nullptr;
        status = device_memory_check(&frame->last_error, cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking),
                                     "frame: making the stream the pictures go home on");
        frame->owned_copy_stream = (void *)stream;
    }
    if (status == GPU_STATUS_OK && staging != nullptr && staging_bytes != 0u) {
        status = device_staging_init(&frame->staging, staging, staging_bytes, &frame->last_error);
    }
    if (status != GPU_STATUS_OK) {
        /* The creating failure is the one to report, so it is kept over whatever giving back the rest says. */
        const gpu_error_t created = frame->last_error;
        (void)frame_gpu_deinit(frame);
        frame->last_error = created;
    }
    return status;
}

gpu_status_t frame_gpu_deinit(frame_gpu_t *frame) {
    gpu_status_t first = GPU_STATUS_OK;
    gpu_error_t error = {};
    frame_flight_t *const flight = frame->borrowed_flight;
    if (flight != nullptr) {
        bool on_way = false;
        for (uint32_t s = 0u; s < FRAME_FLIGHT_SLOTS; s += 1u) {
            on_way = on_way || flight->slots[s].state != (uint8_t)FRAME_SLOT_FREE;
        }
        /* Nothing may still read the staging or write the pictures once they go. */
        if (on_way) {
            first = device_memory_check(&error, cudaDeviceSynchronize(), "frame: waiting for the frames on their way");
            frame->last_error = first != GPU_STATUS_OK ? error : frame->last_error;
        }
    }
    {
        const gpu_status_t status = device_staging_deinit(&frame->staging, &error);
        if (status != GPU_STATUS_OK && first == GPU_STATUS_OK) {
            first = status;
            frame->last_error = error;
        }
    }
    if (frame->owned_copy_stream != nullptr) {
        const gpu_status_t status =
            device_memory_check(&error, cudaStreamDestroy((cudaStream_t)frame->owned_copy_stream),
                                "frame: giving the stream the pictures go home on back");
        if (status != GPU_STATUS_OK && first == GPU_STATUS_OK) {
            first = status;
            frame->last_error = error;
        }
        frame->owned_copy_stream = nullptr;
    }
    if (flight != nullptr) {
        for (uint32_t s = 0u; s < FRAME_FLIGHT_SLOTS; s += 1u) {
            for (uint32_t i = 0u; i < FRAME_SLOT_EVENTS; i += 1u) {
                void **const event = frame_slot_event(&flight->slots[s], i);
                if (*event == nullptr) {
                    continue;
                }
                const gpu_status_t status = device_memory_check(&error, cudaEventDestroy((cudaEvent_t)*event),
                                                                "frame: giving a clock event back");
                if (status != GPU_STATUS_OK && first == GPU_STATUS_OK) {
                    first = status;
                    frame->last_error = error;
                }
                *event = nullptr;
            }
            flight->slots[s].state = (uint8_t)FRAME_SLOT_FREE;
        }
        const gpu_status_t status =
            device_memory_check(&error, cudaHostUnregister(flight), "frame: unpinning the frames on their way");
        if (status != GPU_STATUS_OK && first == GPU_STATUS_OK) {
            first = status;
            frame->last_error = error;
        }
        frame->borrowed_flight = nullptr;
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

gpu_status_t frame_marks_init(frame_marks_t *marks, gpu_error_t *error) {
    *marks = frame_marks_t{};
    for (uint32_t i = 0u; i < 2u; i += 1u) {
        cudaEvent_t event = nullptr;
        const gpu_status_t status = device_memory_check(error, cudaEventCreate(&event), "frame: making a mark");
        if (status != GPU_STATUS_OK) {
            const gpu_error_t created = *error;
            (void)frame_marks_deinit(marks, error);
            *error = created;
            return status;
        }
        marks->owned_clock_events[i] = (void *)event;
    }
    marks->open = 1u;
    return GPU_STATUS_OK;
}

gpu_status_t frame_marks_deinit(frame_marks_t *marks, gpu_error_t *error) {
    gpu_status_t first = GPU_STATUS_OK;
    for (uint32_t i = 0u; i < 2u; i += 1u) {
        void **const event = &marks->owned_clock_events[i];
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
 * written by one thread on one stream, and copied home behind it on the same stream. */
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

/* Starts the clock kernel between two events. */
static gpu_status_t frame_clock_start(void *const events[2], gpu_error_t *error) {
    gpu_status_t status = device_memory_check(error, cudaEventRecord((cudaEvent_t)events[0]),
                                              "frame: reading the card's clock before its speed");
    if (status == GPU_STATUS_OK) {
        frame_clock_kernel<<<1, 32>>>();
        status = device_memory_check(error, cudaGetLastError(), "frame: starting the clock kernel");
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(error, cudaEventRecord((cudaEvent_t)events[1]),
                                     "frame: reading the card's clock after its speed");
    }
    return status;
}

/* After the wait: what the clock kernel saw, in cycles, over the time between its events. The events also hold the kernel's start, a few
 * microseconds, so this reads a little low: FRAME_CLOCK_CYCLES is long enough for that to be about 1 %. */
static gpu_status_t frame_clock_note(frame_marks_t *marks, void *const events[2], uint64_t seen, gpu_error_t *error) {
    float32_t elapsed = 0.0F;
    const gpu_status_t status =
        device_memory_check(error, cudaEventElapsedTime(&elapsed, (cudaEvent_t)events[0], (cudaEvent_t)events[1]),
                            "frame: reading the clock kernel's time");
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
    gpu_status_t status = frame_clock_start(marks->owned_clock_events, error);
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(error, cudaDeviceSynchronize(), "frame: waiting for the clock kernel");
    }
    uint64_t seen = 0u;
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(error, cudaMemcpyFromSymbol(&seen, frame_clock_seen, sizeof(seen)),
                                     "frame: reading what the clock kernel saw");
    }
    return status == GPU_STATUS_OK ? frame_clock_note(marks, marks->owned_clock_events, seen, error) : status;
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

static gpu_status_t frame_mark(frame_gpu_t *frame, frame_slot_t *slot, uint32_t event) {
    return device_memory_check(&frame->last_error, cudaEventRecord((cudaEvent_t)slot->owned_events[event]),
                               "frame: reading the card's clock");
}

/* Whichever failed says so through last_error, so the first failure stops the frame with its own words. Everything here is on the frame's
 * stream, the legacy one, as every copy to the card is: so a copy queued between two frames lands after the one and before the other. */
static gpu_status_t frame_passes(frame_gpu_t *frame, frame_slot_t *slot, const frame_args_t *args) {
    const frame_targets_t *const targets = args->borrowed_targets;
    const traverse_targets_t traverse_targets = {targets->borrowed_coarse, targets->borrowed_hits};
    const traverse_camera_t *const camera = args->borrowed_camera;
    /* Every mark is made, for a kernel that will not run as well: what is between two marks is then always one kernel or nothing, and a
     * pass is always the marks from its first to the next pass's. */
    uint32_t ran = (1u << FRAME_KERNEL_COARSE) | (1u << FRAME_KERNEL_PRIMARY) | (1u << FRAME_KERNEL_SHADE) |
                   (1u << FRAME_KERNEL_POST) | (1u << FRAME_KERNEL_READBACK);
    gpu_status_t status = GPU_STATUS_OK;
    /* Plan 009 phase 01: a bench's counts, held to the picture's size, so the planes stay inside the block. */
    const frame_marks_t *const marks = slot->marked != 0u ? frame->borrowed_marks : nullptr;
    const device_block_t *const counts = marks != nullptr ? marks->borrowed_counts : nullptr;
    if (counts != nullptr && counts->bytes < traverse_counts_bytes(camera->width * camera->height)) {
        frame->last_error = gpu_error_t{"frame: the counts block is too small for the picture", nullptr, nullptr, 0,
                                        GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    /* Before the first pass's mark, so the frame's own times are as they would be without it. */
    if (slot->clock != 0u) {
        status = frame_clock_start(slot->owned_clock_events, &frame->last_error);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_COARSE);
    }
    if (status == GPU_STATUS_OK) {
        status = traverse_coarse(&frame->last_error, args->borrowed_world, camera, &traverse_targets);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_PRIMARY);
    }
    if (status == GPU_STATUS_OK) {
        status =
            traverse_primary_counted(&frame->last_error, args->borrowed_world, camera, &traverse_targets, true, counts);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_ENTITY_RAYS);
    }
    const entity_rays_scene_t *const entities = args->borrowed_entities;
    if (status == GPU_STATUS_OK && entities != nullptr) {
        /* With no boxes it returns having started nothing, and there is no kernel to name. */
        ran |= entities->box_count != 0u ? 1u << FRAME_KERNEL_ENTITY_RAYS : 0u;
        status = entity_rays_frame(&frame->last_error, entities, camera);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_LOD);
    }
    /* Under primary's clock, as it is the rest of those same rays. */
    if (status == GPU_STATUS_OK && args->borrowed_lod != nullptr) {
        ran |= 1u << FRAME_KERNEL_LOD;
        status = lod_frame_counted(&frame->last_error, args->borrowed_lod, args->borrowed_lod_light,
                                   args->borrowed_world, camera, targets->borrowed_hits, counts);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_SHADE);
    }
    if (status == GPU_STATUS_OK) {
        shade_marks_t shade_marks = {};
        for (uint32_t i = 0u; i + 1u < (uint32_t)SHADE_SPAN_COUNT; i += 1u) {
            shade_marks.borrowed_events[i] = slot->owned_shade_events[i];
        }
        shade_marks.borrowed_counts = counts;
        status = shade_frame_marked(&frame->last_error, args->borrowed_scene, args->borrowed_post, args->borrowed_world,
                                    camera, targets->borrowed_hits, targets->borrowed_pixels, entities,
                                    slot->marked != 0u ? &shade_marks : nullptr);
        slot->borrowed_fresh_count = shade_marks.fresh_count;
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_GOD_RAYS);
    }
    const shade_scene_t *const scene = args->borrowed_scene;
    const bool shafts = (scene->features & SHADE_FEATURE_GOD_RAYS) != 0u;
    if (status == GPU_STATUS_OK && shafts) {
        ran |= 1u << FRAME_KERNEL_GOD_RAYS;
        status = shade_shafts_frame(&frame->last_error, scene, args->borrowed_world, camera, targets->borrowed_pixels);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_POST);
    }
    /* Plan 008 phase 05: drawn smaller, post's picture goes into the hits block, which nothing reads once shade is done, and is brought up
     * into the target before the overlays: under the overlay's mark, so post's own time is as at the full size. */
    const traverse_camera_t *const output = args->borrowed_output_camera;
    const device_block_t *const drawn = output != nullptr ? targets->borrowed_hits : targets->borrowed_xrgb;
    if (status == GPU_STATUS_OK) {
        status =
            post_frame_temporal(&frame->last_error, args->borrowed_post, scene->borrowed_temporal,
                                (scene->features & SHADE_FEATURE_TAA) != 0u, shafts, camera, targets->borrowed_pixels,
                                drawn, args->borrowed_weather, args->borrowed_weather_map);
    }
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_OVERLAY);
    }
    if (status == GPU_STATUS_OK && output != nullptr) {
        ran |= 1u << FRAME_KERNEL_OVERLAY;
        status = post_upscale(&frame->last_error, drawn, camera->width, camera->height, targets->borrowed_xrgb,
                              output->width, output->height);
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
        /* Brought up, the picture is the output camera's, which has no jitter. */
        float32_t jitter[2] = {0.0F, 0.0F};
        if (scene->borrowed_temporal != nullptr && output == nullptr) {
            post_temporal_jitter(scene->borrowed_temporal, jitter);
        }
        status = entity_rays_overlay_scaled(&frame->last_error, entities, args->borrowed_world,
                                            output != nullptr ? output : camera, camera->width, camera->height, jitter,
                                            scene->water_material, targets->borrowed_pixels, targets->borrowed_xrgb);
    }
    /* The picture is whole: the copy stream waits for this. */
    if (status == GPU_STATUS_OK) {
        status = frame_mark(frame, slot, FRAME_KERNEL_READBACK);
    }
    /* The few words the marks want, into pinned memory: queued, they cost the thread no wait, and the card nothing a kernel would notice. */
    if (status == GPU_STATUS_OK && slot->borrowed_fresh_count != nullptr) {
        status = device_memory_check(
            &frame->last_error,
            cudaMemcpyAsync(&slot->fresh_seen, slot->borrowed_fresh_count, sizeof(uint32_t), cudaMemcpyDeviceToHost, 0),
            "frame: reading the fresh list's length");
    }
    if (status == GPU_STATUS_OK && slot->clock != 0u) {
        status = device_memory_check(&frame->last_error,
                                     cudaMemcpyFromSymbolAsync(&slot->clock_seen, frame_clock_seen, sizeof(uint64_t),
                                                               0u, cudaMemcpyDeviceToHost, 0),
                                     "frame: reading what the clock kernel saw");
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(&frame->last_error, cudaEventRecord((cudaEvent_t)slot->owned_end),
                                     "frame: marking the frame's end");
    }
    slot->ran = ran;
    slot->queued_ns = frame_now_ns();
    return status;
}

gpu_status_t frame_gpu_queue(frame_gpu_t *frame, const frame_args_t *args, uint32_t *out_slot) {
    frame_flight_t *const flight = frame->borrowed_flight;
    if (flight == nullptr) {
        frame->last_error = gpu_error_t{"frame: not started", nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    const traverse_camera_t *const camera = args->borrowed_camera;
    /* What goes home is the output's size: the camera's own, or what it is brought up to (plan 008 phase 05). */
    const traverse_camera_t *const output =
        args->borrowed_output_camera != nullptr ? args->borrowed_output_camera : camera;
    if (args->borrowed_targets->borrowed_xrgb->bytes < post_xrgb_bytes(output->width, output->height)) {
        frame->last_error = gpu_error_t{"frame: the picture is too large for its target", nullptr, nullptr, 0,
                                        GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    if (output != camera &&
        (args->borrowed_targets->borrowed_hits == nullptr ||
         args->borrowed_targets->borrowed_hits->bytes < post_xrgb_bytes(camera->width, camera->height))) {
        frame->last_error = gpu_error_t{"frame: no room in the hits block for the picture drawn smaller", nullptr,
                                        nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    const uint32_t index = flight->next;
    frame_slot_t *const slot = &flight->slots[index];
    if (slot->state != (uint8_t)FRAME_SLOT_FREE) {
        frame->last_error =
            gpu_error_t{"frame: two frames are already on their way", nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    frame_marks_t *const marks = frame->borrowed_marks;
    slot->marked = marks != nullptr ? 1u : 0u;
    slot->clock = marks != nullptr && marks->clock_due != 0u ? 1u : 0u;
    slot->borrowed_fresh_count = nullptr;
    slot->borrowed_xrgb = args->borrowed_targets->borrowed_xrgb;
    slot->width = output->width;
    slot->height = output->height;
    if (slot->clock != 0u) {
        marks->clock_due = 0u;
    }
    const gpu_status_t status = frame_passes(frame, slot, args);
    if (status != GPU_STATUS_OK) {
        return status;
    }
    slot->state = (uint8_t)FRAME_SLOT_QUEUED;
    flight->next = (index + 1u) % FRAME_FLIGHT_SLOTS;
    *out_slot = index;
    /* Whatever is copied to the card from here is the next frame's. */
    return device_staging_turn(&frame->staging, &frame->last_error);
}

gpu_status_t frame_gpu_home(frame_gpu_t *frame, uint32_t slot_index, uint8_t *host_xrgb) {
    frame_flight_t *const flight = frame->borrowed_flight;
    if (flight == nullptr || slot_index >= FRAME_FLIGHT_SLOTS ||
        flight->slots[slot_index].state != (uint8_t)FRAME_SLOT_QUEUED) {
        frame->last_error =
            gpu_error_t{"frame: sending home a frame not queued", nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    frame_slot_t *const slot = &flight->slots[slot_index];
    const cudaStream_t copy = (cudaStream_t)frame->owned_copy_stream;
    gpu_status_t status = device_memory_check(
        &frame->last_error, cudaStreamWaitEvent(copy, (cudaEvent_t)slot->owned_events[FRAME_KERNEL_READBACK], 0),
        "frame: waiting for the picture to be whole");
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(&frame->last_error,
                                     cudaMemcpyAsync(host_xrgb, device_memory_pointer(slot->borrowed_xrgb),
                                                     post_xrgb_bytes(slot->width, slot->height), cudaMemcpyDeviceToHost,
                                                     copy),
                                     "frame: reading the picture back");
    }
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(&frame->last_error,
                                     cudaEventRecord((cudaEvent_t)slot->owned_events[FRAME_KERNEL_COUNT], copy),
                                     "frame: reading the card's clock");
    }
    if (status == GPU_STATUS_OK) {
        slot->state = (uint8_t)FRAME_SLOT_HOME;
    }
    return status;
}

/* After the wait: shade's spans, the fresh list's length, and the clock if this frame read it. */
static gpu_status_t frame_marks_read(frame_marks_t *marks, frame_gpu_t *frame, frame_slot_t *slot) {
    cudaEvent_t from = (cudaEvent_t)slot->owned_events[FRAME_KERNEL_SHADE];
    for (uint32_t i = 0u; i < (uint32_t)SHADE_SPAN_COUNT; i += 1u) {
        const cudaEvent_t to = i + 1u < (uint32_t)SHADE_SPAN_COUNT
                                   ? (cudaEvent_t)slot->owned_shade_events[i]
                                   : (cudaEvent_t)slot->owned_events[FRAME_KERNEL_GOD_RAYS];
        float32_t elapsed = 0.0F;
        const gpu_status_t status = device_memory_check(&frame->last_error, cudaEventElapsedTime(&elapsed, from, to),
                                                        "frame: reading a shade kernel's time");
        if (status != GPU_STATUS_OK) {
            return status;
        }
        marks->shade_ms[i] = elapsed;
        from = to;
    }
    marks->fresh_count = slot->borrowed_fresh_count != nullptr ? slot->fresh_seen : 0u;
    marks->queued_ns = slot->queued_ns;
    marks->clock_read = slot->clock;
    return slot->clock != 0u ? frame_clock_note(marks, slot->owned_clock_events, slot->clock_seen, &frame->last_error)
                             : GPU_STATUS_OK;
}

gpu_status_t frame_gpu_finish(frame_gpu_t *frame, uint32_t slot_index, frame_timings_t *out_timings) {
    frame_flight_t *const flight = frame->borrowed_flight;
    if (flight == nullptr || slot_index >= FRAME_FLIGHT_SLOTS ||
        flight->slots[slot_index].state != (uint8_t)FRAME_SLOT_HOME) {
        frame->last_error =
            gpu_error_t{"frame: finishing a frame not sent home", nullptr, nullptr, 0, GPU_STATUS_INVALID_ARGUMENT};
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    frame_slot_t *const slot = &flight->slots[slot_index];
    slot->state = (uint8_t)FRAME_SLOT_FREE;
    /* This is what also reports a kernel that faulted after it started, in this frame or the one before. */
    gpu_status_t status = device_memory_check(&frame->last_error,
                                              cudaEventSynchronize((cudaEvent_t)slot->owned_events[FRAME_KERNEL_COUNT]),
                                              "frame: waiting for the passes");
    if (status == GPU_STATUS_OK) {
        status = device_memory_check(&frame->last_error, cudaEventSynchronize((cudaEvent_t)slot->owned_end),
                                     "frame: waiting for the passes");
    }
    if (status != GPU_STATUS_OK) {
        return status;
    }
    *out_timings = frame_timings_t{};
    out_timings->kernels_ran = slot->ran;
    for (uint32_t pass = 0u; pass < (uint32_t)FRAME_PASS_COUNT; pass += 1u) {
        for (uint32_t i = frame_pass_first_kernel[pass]; i < frame_pass_first_kernel[pass + 1u]; i += 1u) {
            float32_t elapsed = 0.0F;
            status = device_memory_check(&frame->last_error,
                                         cudaEventElapsedTime(&elapsed, (cudaEvent_t)slot->owned_events[i],
                                                              (cudaEvent_t)slot->owned_events[i + 1u]),
                                         "frame: reading a kernel's time");
            if (status != GPU_STATUS_OK) {
                return status;
            }
            /* The gap for one that did not run stays in its pass, so a frame's passes still add up to the whole of it. */
            out_timings->pass_ms[pass] += elapsed;
            out_timings->total_ms += elapsed;
            out_timings->kernel_ms[i] = (slot->ran & (1u << i)) != 0u ? elapsed : 0.0F;
        }
    }
    frame_marks_t *const marks = frame->borrowed_marks;
    return marks != nullptr && slot->marked != 0u ? frame_marks_read(marks, frame, slot) : GPU_STATUS_OK;
}

bool frame_gpu_done(frame_gpu_t *frame, uint32_t slot_index) {
    frame_flight_t *const flight = frame->borrowed_flight;
    if (flight == nullptr || slot_index >= FRAME_FLIGHT_SLOTS ||
        flight->slots[slot_index].state != (uint8_t)FRAME_SLOT_HOME) {
        return false;
    }
    const frame_slot_t *const slot = &flight->slots[slot_index];
    const cudaError_t home = cudaEventQuery((cudaEvent_t)slot->owned_events[FRAME_KERNEL_COUNT]);
    const cudaError_t end = home == cudaSuccess ? cudaEventQuery((cudaEvent_t)slot->owned_end) : home;
    return end != cudaErrorNotReady;
}

bool frame_gpu_whole(frame_gpu_t *frame, uint32_t slot_index) {
    frame_flight_t *const flight = frame->borrowed_flight;
    if (flight == nullptr || slot_index >= FRAME_FLIGHT_SLOTS ||
        flight->slots[slot_index].state == (uint8_t)FRAME_SLOT_FREE) {
        return false;
    }
    return cudaEventQuery((cudaEvent_t)flight->slots[slot_index].owned_events[FRAME_KERNEL_READBACK]) !=
           cudaErrorNotReady;
}

gpu_status_t frame_gpu_run(frame_gpu_t *frame, const frame_args_t *args, frame_timings_t *out_timings) {
    uint32_t slot = 0u;
    gpu_status_t status = frame_gpu_queue(frame, args, &slot);
    if (status == GPU_STATUS_OK) {
        status = frame_gpu_home(frame, slot, args->borrowed_host_xrgb);
    }
    return status == GPU_STATUS_OK ? frame_gpu_finish(frame, slot, out_timings) : status;
}
