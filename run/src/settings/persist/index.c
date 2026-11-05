/* See README.md. */
#include "settings/persist/index.h"

#include "assert/index.h"
#include "log/index.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* The numbers, by where each lives and how far it may go. */
typedef struct settings_number_t {
    const char *key;  /* 0 */
    uint32_t offset;  /* 8 */
    uint32_t minimum; /* 12 */
    uint32_t maximum; /* 16 */
    uint8_t _pad0[4]; /* 20 */
} settings_number_t;
static_assert(sizeof(settings_number_t) == 24);

static const settings_number_t SETTINGS_NUMBERS[] = {
    {"render_distance",
     offsetof(settings_values_t, render_distance),
     SETTINGS_RENDER_DISTANCE_MIN,
     SETTINGS_RENDER_DISTANCE_MAX,
     {0}},
    {"full_detail", offsetof(settings_values_t, full_detail), SETTINGS_FULL_DETAIL_MIN, SETTINGS_FULL_DETAIL_MAX, {0}},
    {"fov_degrees", offsetof(settings_values_t, fov_degrees), SETTINGS_FOV_MIN, SETTINGS_FOV_MAX, {0}},
    {"sensitivity_percent",
     offsetof(settings_values_t, sensitivity_percent),
     SETTINGS_SENSITIVITY_MIN,
     SETTINGS_SENSITIVITY_MAX,
     {0}},
    {"render_scale_percent",
     offsetof(settings_values_t, render_scale_percent),
     SETTINGS_RENDER_SCALE_MIN,
     SETTINGS_RENDER_SCALE_MAX,
     {0}},
    {"worker_count", offsetof(settings_values_t, worker_count), SETTINGS_WORKERS_MIN, SETTINGS_WORKERS_MAX, {0}},
};
enum : uint32_t { SETTINGS_NUMBER_COUNT = sizeof(SETTINGS_NUMBERS) / sizeof(SETTINGS_NUMBERS[0]) };

/* Two columns of eight on the screen, and one more under them. */
static const settings_effect_t SETTINGS_EFFECTS[SETTINGS_EFFECT_COUNT] = {
    {"effect_shapes", "Block shapes", SHADE_FEATURE_SHAPES, {0}},
    {"effect_alpha", "See-through", SHADE_FEATURE_ALPHA, {0}},
    {"effect_sky", "Sky and sun", SHADE_FEATURE_SKY, {0}},
    {"effect_day_cycle", "Day cycle", SHADE_FEATURE_DAY_CYCLE, {0}},
    {"effect_fog", "Fog", SHADE_FEATURE_FOG, {0}},
    {"effect_shadows", "Shadows", SHADE_FEATURE_SHADOW, {0}},
    {"effect_stored_light", "Stored light", SHADE_FEATURE_LIGHT, {0}},
    {"effect_transparency", "Transparency", SHADE_FEATURE_TRANSPARENCY, {0}},
    {"effect_water_reflection", "Reflections", SHADE_FEATURE_WATER_REFLECTION, {0}},
    {"effect_water_tint", "Water tint", SHADE_FEATURE_WATER_TINT, {0}},
    {"effect_ao", "Ambient occl.", SHADE_FEATURE_AO, {0}},
    {"effect_taa", "TAA", SHADE_FEATURE_TAA, {0}},
    {"effect_god_rays", "God rays", SHADE_FEATURE_GOD_RAYS, {0}},
    {"effect_entities", "Entities", SHADE_FEATURE_ENTITIES, {0}},
    {"effect_outline", "Block outline", SHADE_FEATURE_OUTLINE, {0}},
    {"effect_far_field", "Far field", SHADE_FEATURE_LOD, {0}},
    {"effect_under_water", "Under water", SHADE_FEATURE_UNDER_WATER, {0}},
};

const settings_effect_t *settings_persist_effect(uint32_t index) {
    ASSERT_ALWAYS(index < SETTINGS_EFFECT_COUNT);
    return &SETTINGS_EFFECTS[index];
}

void settings_persist_defaults(settings_values_t *out) {
    *out = (settings_values_t){.render_distance = MC_GAME_RENDER_DISTANCE_DEFAULT,
                               .full_detail = RENDER_LOD_FULL_DETAIL_DEFAULT,
                               .fov_degrees = SETTINGS_FOV_DEFAULT,
                               .sensitivity_percent = SETTINGS_SENSITIVITY_DEFAULT,
                               .render_scale_percent = SETTINGS_RENDER_SCALE_DEFAULT,
                               .worker_count = SETTINGS_WORKERS_MAX,
                               .effects = SHADE_FEATURES_PLAY,
                               .vsync = 1u,
                               .show_coordinates = 1u,
                               .sound_volume_percent = SETTINGS_SOUND_VOLUME_DEFAULT,
                               .show_date = 1u};
}

static uint32_t *settings_number_at(settings_values_t *values, const settings_number_t *number) {
    return (uint32_t *)(void *)((uint8_t *)values + number->offset);
}

void settings_persist_clamp(settings_values_t *values) {
    for (uint32_t i = 0u; i < SETTINGS_NUMBER_COUNT; i++) {
        uint32_t *const value = settings_number_at(values, &SETTINGS_NUMBERS[i]);
        *value = *value < SETTINGS_NUMBERS[i].minimum ? SETTINGS_NUMBERS[i].minimum : *value;
        *value = *value > SETTINGS_NUMBERS[i].maximum ? SETTINGS_NUMBERS[i].maximum : *value;
    }
    values->effects &= SHADE_FEATURES_EVERY;
    values->vsync = values->vsync != 0u ? 1u : 0u;
    values->show_coordinates = values->show_coordinates != 0u ? 1u : 0u;
    values->show_date = values->show_date != 0u ? 1u : 0u;
    values->sound_volume_percent = values->sound_volume_percent > SETTINGS_SOUND_VOLUME_MAX
                                       ? (uint8_t)SETTINGS_SOUND_VOLUME_MAX
                                       : values->sound_volume_percent;
}

/* ---- Reading ---------------------------------------------------------------------------------------------------------- */

static bool settings_is_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r';
}

/* A whole number, and nothing else. Only digits, with no sign and no base, so that no text can mean something by accident. */
static bool settings_number_parse(const uint8_t *text, uint64_t length, uint32_t *out) {
    if (length == 0u || length > 10u) {
        return false;
    }
    uint64_t value = 0u;
    for (uint64_t i = 0u; i < length; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10u + (uint64_t)(text[i] - '0');
    }
    if (value > UINT32_MAX) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool settings_bool_parse(const uint8_t *text, uint64_t length, bool *out) {
    if (length == 4u && memcmp(text, "true", 4u) == 0) {
        *out = true;
        return true;
    }
    if (length == 5u && memcmp(text, "false", 5u) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static bool settings_key_is(const uint8_t *key, uint64_t length, const char *name) {
    return strlen(name) == length && memcmp(key, name, length) == 0;
}

typedef enum settings_line_result_t : uint8_t {
    SETTINGS_LINE_SET = 0,
    SETTINGS_LINE_HELD = 1, /* set, to the nearest it can be */
    SETTINGS_LINE_BAD = 2,
} settings_line_result_t;

static settings_line_result_t settings_line_number(const settings_number_t *number, const uint8_t *value,
                                                   uint64_t value_length, settings_values_t *values) {
    uint32_t parsed = 0u;
    if (!settings_number_parse(value, value_length, &parsed)) {
        return SETTINGS_LINE_BAD;
    }
    const uint32_t held =
        parsed < number->minimum ? number->minimum : (parsed > number->maximum ? number->maximum : parsed);
    *settings_number_at(values, number) = held;
    return held == parsed ? SETTINGS_LINE_SET : SETTINGS_LINE_HELD;
}

/* One line that has both a key and a value. */
static settings_line_result_t settings_line_apply(const uint8_t *key, uint64_t key_length, const uint8_t *value,
                                                  uint64_t value_length, settings_values_t *values) {
    for (uint32_t i = 0u; i < SETTINGS_NUMBER_COUNT; i++) {
        if (settings_key_is(key, key_length, SETTINGS_NUMBERS[i].key)) {
            return settings_line_number(&SETTINGS_NUMBERS[i], value, value_length, values);
        }
    }
    /* A byte, with no room in the table's words. */
    if (settings_key_is(key, key_length, "sound_volume_percent")) {
        uint32_t parsed = 0u;
        if (!settings_number_parse(value, value_length, &parsed)) {
            return SETTINGS_LINE_BAD;
        }
        const uint32_t held = parsed > SETTINGS_SOUND_VOLUME_MAX ? SETTINGS_SOUND_VOLUME_MAX : parsed;
        values->sound_volume_percent = (uint8_t)held;
        return held == parsed ? SETTINGS_LINE_SET : SETTINGS_LINE_HELD;
    }
    bool on = false;
    uint8_t *flag = nullptr;
    if (settings_key_is(key, key_length, "vsync")) {
        flag = &values->vsync;
    } else if (settings_key_is(key, key_length, "show_coordinates")) {
        flag = &values->show_coordinates;
    } else if (settings_key_is(key, key_length, "show_date")) {
        flag = &values->show_date;
    }
    if (flag != nullptr) {
        if (!settings_bool_parse(value, value_length, &on)) {
            return SETTINGS_LINE_BAD;
        }
        *flag = on ? 1u : 0u;
        return SETTINGS_LINE_SET;
    }
    for (uint32_t i = 0u; i < SETTINGS_EFFECT_COUNT; i++) {
        if (settings_key_is(key, key_length, SETTINGS_EFFECTS[i].key)) {
            if (!settings_bool_parse(value, value_length, &on)) {
                return SETTINGS_LINE_BAD;
            }
            values->effects =
                on ? values->effects | SETTINGS_EFFECTS[i].bit : values->effects & ~SETTINGS_EFFECTS[i].bit;
            return SETTINGS_LINE_SET;
        }
    }
    return SETTINGS_LINE_BAD;
}

/* Takes a line apart, to a key and a value with nothing round them. False if it has no colon, or nothing on one side of it. An empty line is
 * neither: out_empty says so. */
static bool settings_line_split(const uint8_t *line, uint64_t length, const uint8_t **out_key, uint64_t *out_key_length,
                                const uint8_t **out_value, uint64_t *out_value_length, bool *out_empty) {
    const uint8_t *const hash = memchr(line, '#', length);
    uint64_t end = hash != nullptr ? (uint64_t)(hash - line) : length;
    while (end > 0u && settings_is_space(line[end - 1u])) {
        end--;
    }
    uint64_t start = 0u;
    while (start < end && settings_is_space(line[start])) {
        start++;
    }
    *out_empty = start == end;
    if (*out_empty) {
        return false;
    }
    const uint8_t *const colon = memchr(line + start, ':', end - start);
    if (colon == nullptr) {
        return false;
    }
    uint64_t key_end = (uint64_t)(colon - line);
    while (key_end > start && settings_is_space(line[key_end - 1u])) {
        key_end--;
    }
    uint64_t value_start = (uint64_t)(colon - line) + 1u;
    while (value_start < end && settings_is_space(line[value_start])) {
        value_start++;
    }
    *out_key = line + start;
    *out_key_length = key_end - start;
    *out_value = line + value_start;
    *out_value_length = end - value_start;
    return *out_key_length != 0u && *out_value_length != 0u;
}

/* Whether this file is one this program can read: its first line that says anything must say which version it is. */
static bool settings_version_good(const uint8_t *text, uint64_t length) {
    uint64_t at = 0u;
    uint32_t line_number = 1u;
    while (at < length) {
        const uint8_t *const newline = memchr(text + at, '\n', length - at);
        const uint64_t line_length = newline != nullptr ? (uint64_t)(newline - (text + at)) : length - at;
        const uint8_t *key = nullptr;
        const uint8_t *value = nullptr;
        uint64_t key_length = 0u;
        uint64_t value_length = 0u;
        bool empty = false;
        const bool split =
            settings_line_split(text + at, line_length, &key, &key_length, &value, &value_length, &empty);
        if (!empty) {
            uint32_t version = 0u;
            if (!split || !settings_key_is(key, key_length, "version") ||
                !settings_number_parse(value, value_length, &version) || version != SETTINGS_VERSION) {
                LOG_WARN("settings: line %" PRIu32 " should say this is version %" PRIu32
                         " of the file, so nothing in it is used",
                         line_number, (uint32_t)SETTINGS_VERSION);
                return false;
            }
            return true;
        }
        at += line_length + 1u;
        line_number++;
    }
    return true; /* nothing in it, which sets nothing either way */
}

void settings_persist_parse(const uint8_t *text, uint64_t length, settings_values_t *values,
                            settings_persist_report_t *out_report) {
    *out_report = (settings_persist_report_t){.applied = 0u, .problems = 0u};
    if (!settings_version_good(text, length)) {
        out_report->problems = 1u;
        return;
    }
    uint64_t at = 0u;
    uint32_t line_number = 1u;
    while (at < length) {
        const uint8_t *const newline = memchr(text + at, '\n', length - at);
        const uint64_t line_length = newline != nullptr ? (uint64_t)(newline - (text + at)) : length - at;
        const uint8_t *key = nullptr;
        const uint8_t *value = nullptr;
        uint64_t key_length = 0u;
        uint64_t value_length = 0u;
        bool empty = false;
        const bool split =
            line_length <= SETTINGS_LINE_BYTES && memchr(text + at, '\0', line_length) == nullptr &&
            settings_line_split(text + at, line_length, &key, &key_length, &value, &value_length, &empty);
        if (split && settings_key_is(key, key_length, "version")) {
            /* settings_version_good has read the one that counts. */
        } else if (split) {
            const settings_line_result_t result = settings_line_apply(key, key_length, value, value_length, values);
            if (result == SETTINGS_LINE_BAD) {
                out_report->problems++;
                LOG_WARN("settings: line %" PRIu32 " is not a setting with a value it can have, so it is ignored",
                         line_number);
            } else {
                out_report->applied++;
                if (result == SETTINGS_LINE_HELD) {
                    out_report->problems++;
                    LOG_WARN("settings: line %" PRIu32 " is out of range, so the nearest that can be is used",
                             line_number);
                }
            }
        } else if (!empty || line_length > SETTINGS_LINE_BYTES) {
            out_report->problems++;
            LOG_WARN("settings: line %" PRIu32 " is not \"key: value\", so it is ignored", line_number);
        }
        at += line_length + 1u;
        line_number++;
    }
}

/* ---- Writing ---------------------------------------------------------------------------------------------------------- */

static bool settings_append(uint8_t *out, uint64_t capacity, uint64_t *at, const char *key, const char *value) {
    const int32_t wrote = snprintf((char *)out + *at, capacity - *at, "%s: %s\n", key, value);
    if (wrote < 0 || (uint64_t)wrote >= capacity - *at) {
        return false;
    }
    *at += (uint64_t)wrote;
    return true;
}

bool settings_persist_write(const settings_values_t *values, uint8_t *out, uint64_t capacity, uint64_t *out_bytes) {
    static const char header[] =
        "version: 1\n# minecraft_c's settings: what the Settings screen wrote. A line that is wrong is\n"
        "# told about at the start and ignored.\n";
    static_assert(SETTINGS_VERSION == 1u);
    if (capacity < sizeof(header)) {
        return false;
    }
    memcpy(out, header, sizeof(header) - 1u);
    uint64_t at = sizeof(header) - 1u;
    settings_values_t own = *values;
    for (uint32_t i = 0u; i < SETTINGS_NUMBER_COUNT; i++) {
        char number[16];
        (void)snprintf(number, sizeof(number), "%" PRIu32, *settings_number_at(&own, &SETTINGS_NUMBERS[i]));
        if (!settings_append(out, capacity, &at, SETTINGS_NUMBERS[i].key, number)) {
            return false;
        }
    }
    char volume[16];
    (void)snprintf(volume, sizeof(volume), "%" PRIu32, (uint32_t)values->sound_volume_percent);
    if (!settings_append(out, capacity, &at, "sound_volume_percent", volume)) {
        return false;
    }
    if (!settings_append(out, capacity, &at, "vsync", values->vsync != 0u ? "true" : "false")) {
        return false;
    }
    if (!settings_append(out, capacity, &at, "show_coordinates", values->show_coordinates != 0u ? "true" : "false")) {
        return false;
    }
    if (!settings_append(out, capacity, &at, "show_date", values->show_date != 0u ? "true" : "false")) {
        return false;
    }
    for (uint32_t i = 0u; i < SETTINGS_EFFECT_COUNT; i++) {
        const bool on = (values->effects & SETTINGS_EFFECTS[i].bit) != 0u;
        if (!settings_append(out, capacity, &at, SETTINGS_EFFECTS[i].key, on ? "true" : "false")) {
            return false;
        }
    }
    *out_bytes = at;
    return true;
}
