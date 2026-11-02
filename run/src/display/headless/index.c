/* See README.md. The Arena tier: everything it holds comes from the arena its parent gives it, and it touches neither
 * /dev/dri nor /dev/input. */
#include "display/headless/index.h"

#include "display/headless/png/index.h"
#include "file/index.h"
#include "log/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    HEADLESS_ALIGNMENT = 64u,
};

static uint64_t headless_aligned(uint64_t bytes) {
    return (bytes + HEADLESS_ALIGNMENT - 1u) & ~(uint64_t)(HEADLESS_ALIGNMENT - 1u);
}

static uint64_t headless_frame_bytes(uint32_t width, uint32_t height) {
    return (uint64_t)width * height * DISPLAY_FLIP_BYTES_PER_PIXEL;
}

uint64_t display_headless_arena_bytes(const display_headless_config_t *config) {
    const uint64_t png_bytes = display_png_bytes_needed(config->width, config->height);
    if (png_bytes == 0u) {
        return 0u;
    }
    uint64_t total = headless_aligned(headless_frame_bytes(config->width, config->height)) +
                     headless_aligned(png_bytes) + headless_aligned(FILE_PATH_BYTES);
    if (config->script_path != nullptr) {
        total += headless_aligned(DISPLAY_SCRIPT_MAX_BYTES);
        total += headless_aligned((uint64_t)DISPLAY_SCRIPT_MAX_EVENTS * sizeof(display_script_event_t));
    }
    return total;
}

static bool headless_load_script(display_headless_t *headless, arena_t *arena,
                                 const display_headless_config_t *config) {
    char *const text = arena_alloc(arena, DISPLAY_SCRIPT_MAX_BYTES, HEADLESS_ALIGNMENT);
    uint64_t length = 0;
    const file_status_t status = file_read_whole(config->script_path, text, DISPLAY_SCRIPT_MAX_BYTES, &length);
    if (status != FILE_OK) {
        LOG_ERROR("headless: cannot read the script %s: %s", config->script_path, file_status_name(status));
        return false;
    }
    display_script_event_t *const events =
        arena_alloc_array(arena, DISPLAY_SCRIPT_MAX_EVENTS, sizeof(display_script_event_t), HEADLESS_ALIGNMENT);
    display_script_error_t error;
    if (!display_script_parse(text, length, config->tick_hz, events, DISPLAY_SCRIPT_MAX_EVENTS, &headless->script,
                              &error)) {
        LOG_ERROR("headless: %s line %" PRIu32 ": %s", config->script_path, error.line, error.message);
        return false;
    }
    return true;
}

/* Works out how many ticks to run, refusing a --ticks that would quietly drop part of the script. */
static bool headless_set_tick_count(display_headless_t *headless, const display_headless_config_t *config) {
    const display_script_t *const script = &headless->script;
    if (config->script_path == nullptr) {
        if (config->tick_count == 0u) {
            LOG_ERROR("headless: with no script, a tick count has to say how long to run");
            return false;
        }
        headless->tick_count = config->tick_count;
        return true;
    }
    if (config->tick_count == 0u) {
        headless->tick_count = script->last_tick + 1u;
        return true;
    }
    if (script->has_end != 0u) {
        LOG_ERROR("headless: the script says when it ends, so a tick count cannot also say so");
        return false;
    }
    if (config->tick_count <= script->last_tick) {
        LOG_ERROR("headless: %" PRIu32 " ticks would stop before the script's last line, at tick %" PRIu32,
                  config->tick_count, script->last_tick);
        return false;
    }
    headless->tick_count = config->tick_count;
    return true;
}

bool display_headless_init(display_headless_t *headless, arena_t *arena, const display_headless_config_t *config) {
    memset(headless, 0, sizeof(*headless));
    const uint64_t png_bytes = display_png_bytes_needed(config->width, config->height);
    if (png_bytes == 0u) {
        LOG_ERROR("headless: a frame of %" PRIu32 "x%" PRIu32 " is not possible: each side is 1 to %" PRIu32,
                  config->width, config->height, (uint32_t)DISPLAY_PNG_MAX_DIMENSION);
        return false;
    }
    if (config->script_path != nullptr && !headless_load_script(headless, arena, config)) {
        return false;
    }
    if (!headless_set_tick_count(headless, config)) {
        return false;
    }
    if (headless->script.screenshot_count != 0u) {
        if (config->screenshot_dir == nullptr) {
            LOG_ERROR("headless: the script takes screenshots, so it needs a directory to put them in");
            return false;
        }
        const file_status_t status = file_make_dirs(config->screenshot_dir);
        if (status != FILE_OK) {
            LOG_ERROR("headless: cannot make %s: %s", config->screenshot_dir, file_status_name(status));
            return false;
        }
    }
    headless->borrowed_screenshot_dir = config->screenshot_dir;
    headless->borrowed_png = arena_alloc(arena, png_bytes, HEADLESS_ALIGNMENT);
    headless->png_capacity = png_bytes;
    headless->borrowed_path = arena_alloc(arena, FILE_PATH_BYTES, HEADLESS_ALIGNMENT);
    headless->frame = (display_buffer_t){
        .owned_map = arena_alloc_zeroed(arena, headless_frame_bytes(config->width, config->height), HEADLESS_ALIGNMENT),
        .map_bytes = headless_frame_bytes(config->width, config->height),
        .pitch = config->width * DISPLAY_FLIP_BYTES_PER_PIXEL,
        .width = config->width,
        .height = config->height,
    };
    return true;
}

void display_headless_deinit(display_headless_t *headless) {
    memset(headless, 0, sizeof(*headless));
}

bool display_headless_screenshot(display_headless_t *headless, const char *path) {
    const display_buffer_t *const frame = &headless->frame;
    uint64_t bytes = 0;
    if (!display_png_encode(frame->owned_map, frame->pitch, frame->width, frame->height, headless->borrowed_png,
                            headless->png_capacity, &bytes)) {
        LOG_ERROR("headless: could not encode a frame of %" PRIu32 "x%" PRIu32, frame->width, frame->height);
        return false;
    }
    const file_status_t status = file_write_atomic(path, headless->borrowed_png, bytes);
    if (status != FILE_OK) {
        LOG_ERROR("headless: cannot write %s: %s", path, file_status_name(status));
        return false;
    }
    headless->screenshots_written++;
    return true;
}

static bool headless_take_screenshots(display_headless_t *headless, const display_headless_tick_t *tick) {
    char name[DISPLAY_SCRIPT_NAME_BYTES];
    for (uint32_t i = 0; i < tick->event_count; i++) {
        const display_script_event_t *const event = &tick->events[i];
        if (event->kind != DISPLAY_SCRIPT_SCREENSHOT) {
            continue;
        }
        display_script_screenshot_name(&headless->script, event, name);
        const int32_t written =
            snprintf(headless->borrowed_path, FILE_PATH_BYTES, "%s/%s", headless->borrowed_screenshot_dir, name);
        if (written < 0 || written >= FILE_PATH_BYTES) {
            LOG_ERROR("headless: the path of screenshot %s is too long", name);
            return false;
        }
        if (!display_headless_screenshot(headless, headless->borrowed_path)) {
            return false;
        }
        LOG_INFO("headless: tick %" PRIu32 ": wrote %s", tick->tick, headless->borrowed_path);
    }
    return true;
}

bool display_headless_run(display_headless_t *headless, const display_headless_scene_t *scene) {
    const display_script_t *const script = &headless->script;
    uint32_t next_event = 0;
    for (uint32_t tick = 0; tick < headless->tick_count; tick++) {
        display_script_state_begin_tick(&headless->state, tick);
        const uint32_t first_event = next_event;
        while (next_event < script->event_count && script->borrowed_events[next_event].tick == tick) {
            display_script_state_apply(&headless->state, &script->borrowed_events[next_event]);
            next_event++;
        }
        const display_headless_tick_t info = {.events = next_event > first_event ? script->borrowed_events + first_event
                                                                                 : nullptr,
                                              .state = &headless->state,
                                              .event_count = next_event - first_event,
                                              .tick = tick};
        if (scene->tick != nullptr && !scene->tick(scene->borrowed_context, &info)) {
            return false;
        }
        if (scene->draw != nullptr && !scene->draw(scene->borrowed_context, &info, &headless->frame)) {
            return false;
        }
        headless->frames_presented++;
        if (!headless_take_screenshots(headless, &info)) {
            return false;
        }
    }
    return true;
}
