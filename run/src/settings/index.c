/* See README.md. The Owner: it holds the memory its part borrows. */
#include "settings/index.h"

#include "log/index.h"

#include <inttypes.h>
#include <string.h>

enum : uint64_t {
    SETTINGS_ARENA_BYTES = (uint64_t)FILE_PATH_BYTES + SETTINGS_TEXT_BYTES + 2u * 64u,
};

bool settings_init(settings_t *settings) {
    memset(settings, 0, sizeof(*settings));
    settings_persist_defaults(&settings->values);
    if (!heap_reserve(SETTINGS_ARENA_BYTES, &settings->owned_region)) {
        return false; /* it has said why */
    }
    arena_init(&settings->arena, "settings", settings->owned_region.owned_base, SETTINGS_ARENA_BYTES);
    settings->borrowed_path = arena_alloc_zeroed(&settings->arena, (uint64_t)FILE_PATH_BYTES + SETTINGS_TEXT_BYTES, 1u);
    return true;
}

/* The file's worth of text, after the path in the same block. */
static uint8_t *settings_text(const settings_t *settings) {
    return (uint8_t *)settings->borrowed_path + FILE_PATH_BYTES;
}

void settings_deinit(settings_t *settings) {
    if (settings->owned_region.owned_base != nullptr) {
        arena_deinit(&settings->arena);
        heap_release(&settings->owned_region);
    }
    memset(settings, 0, sizeof(*settings));
}

void settings_load(settings_t *settings, const char *path) {
    const uint64_t path_bytes = strlen(path) + 1u;
    if (path_bytes > FILE_PATH_BYTES) {
        LOG_WARN("settings: a path of %" PRIu64 " bytes is too long, so nothing will be kept", path_bytes);
        settings->borrowed_path[0] = '\0';
        return;
    }
    memcpy(settings->borrowed_path, path, path_bytes);
    uint64_t bytes = 0u;
    const file_status_t status = file_read_whole(path, settings_text(settings), SETTINGS_TEXT_BYTES, &bytes);
    if (status == FILE_ERROR_NOT_FOUND) {
        return;
    }
    if (status != FILE_OK) {
        LOG_WARN("settings: could not read %s: %s, so the defaults are used", path, file_status_name(status));
        return;
    }
    settings_persist_report_t report;
    settings_persist_parse(settings_text(settings), bytes, &settings->values, &report);
    LOG_INFO("settings: %s set %" PRIu32 " things, with %" PRIu32 " problems: render distance %" PRIu32 ", %" PRIu32
             " workers",
             path, report.applied, report.problems, settings->values.render_distance, settings->values.worker_count);
}

file_status_t settings_save(settings_t *settings) {
    if (settings->borrowed_path[0] == '\0') {
        return FILE_ERROR_NOT_FOUND;
    }
    uint64_t bytes = 0u;
    if (!settings_persist_write(&settings->values, settings_text(settings), SETTINGS_TEXT_BYTES, &bytes)) {
        return FILE_ERROR_TOO_LARGE;
    }
    const file_status_t status = file_write_atomic(settings->borrowed_path, settings_text(settings), bytes);
    if (status == FILE_OK) {
        LOG_INFO("settings: kept in %s: render distance %" PRIu32, settings->borrowed_path,
                 settings->values.render_distance);
    }
    return status;
}
