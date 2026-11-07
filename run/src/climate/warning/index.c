/* See README.md. The HUD's storm warning from the outlook's chances. Allocates only from the arena it is given. */
#include "climate/warning/index.h"

#include "calendar/index.h"
#include "climate/outlook/index.h"
#include "climate/systems/index.h"
#include "climate/weather/week/index.h"

#include <math.h>
#include <string.h>

climate_warning_t climate_warning_decide(const climate_warning_input_t *input) {
    climate_warning_t out;
    memset(&out, 0, sizeof(out));
    /* 1: the known hurricane most likely to come, ties to the lower id. */
    uint32_t best = UINT32_MAX;
    for (uint32_t i = 0u; i < input->hurricane_count; i++) {
        if (best == UINT32_MAX || input->hurricane_chances[i] > input->hurricane_chances[best] ||
            (input->hurricane_chances[i] == input->hurricane_chances[best] &&
             input->hurricane_ids[i] < input->hurricane_ids[best])) {
            best = i;
        }
    }
    if (best != UINT32_MAX && input->hurricane_chances[best] > CLIMATE_WARNING_THRESHOLD) {
        out.kind = CLIMATE_WARNING_NAMED;
        out.system_id = input->hurricane_ids[best];
        out.chance = input->hurricane_chances[best];
        return out;
    }
    /* 2 and 3: at least one of them, the outlook's own combination. */
    const float64_t hurricane =
        climate_outlook_at_least_one(input->hurricane_chances, input->hurricane_count,
                                     input->hurricane_unborn > 0.0f64 ? input->hurricane_unborn : 0.0f64);
    if (hurricane > CLIMATE_WARNING_THRESHOLD) {
        out.kind = CLIMATE_WARNING_HURRICANE_LIKELY;
        out.chance = hurricane;
        return out;
    }
    const float64_t storm = climate_outlook_at_least_one(input->storm_chances, input->storm_count,
                                                         input->storm_unborn > 0.0f64 ? input->storm_unborn : 0.0f64);
    if (storm > CLIMATE_WARNING_THRESHOLD) {
        out.kind = CLIMATE_WARNING_STORM;
        out.chance = storm;
        return out;
    }
    const float64_t named = best != UINT32_MAX ? input->hurricane_chances[best] : 0.0f64;
    out.chance = fmax(named, fmax(hurricane, storm));
    return out;
}

void climate_warning_steps(const climate_system_t *system, uint64_t seed, uint64_t week, float64_t census_t,
                           float64_t t0, float64_t x, float64_t y, bool error_off,
                           float64_t out_chances[CLIMATE_WARNING_STEPS], bool out_deep[CLIMATE_WARNING_STEPS]) {
    for (uint32_t k = 0u; k < CLIMATE_WARNING_STEPS; k++) {
        const float64_t t = t0 + CLIMATE_WARNING_STEP_DAYS * (float64_t)k;
        out_chances[k] =
            climate_outlook_system_chance(system, seed, week, census_t, t, x, y, error_off, CLIMATE_OUTLOOK_FOOTPRINT);
        climate_system_state_t state;
        climate_system_state_at(system, census_t + t, &state);
        out_deep[k] = state.alive != 0u && state.pressure_hpa <= -CLIMATE_STORM_DEPTH_HPA;
    }
}

float64_t climate_warning_census_t(float64_t t) {
    const float64_t day = floor(t);
    return (float64_t)CALENDAR_DAYS_PER_WEEK * floor(day / (float64_t)CALENDAR_DAYS_PER_WEEK);
}

bool climate_warning_at(const climate_t *climate, arena_t *scratch, float64_t x, float64_t y, float64_t t,
                        climate_warning_t *out) {
    memset(out, 0, sizeof(*out));
    const float64_t census_t = climate_warning_census_t(t);
    const uint64_t week = (uint64_t)(census_t / (float64_t)CALENDAR_DAYS_PER_WEEK);
    const float64_t t0 = t - census_t;
    climate_systems_day_t list;
    uint32_t *order = nullptr;
    uint32_t alive = 0u;
    climate_cell_t cell;
    if (!climate_cell_get(climate, (int32_t)floor(x), (int32_t)floor(y), &cell) ||
        !climate_outlook_known_list(climate, x, y, census_t, scratch, &list, &order, &alive)) {
        return false;
    }
    const uint64_t seed = climate_outlook_seed(climate);
    float64_t *const hurricane_chances = arena_alloc_array(scratch, alive + 1u, sizeof(float64_t), alignof(float64_t));
    uint64_t *const hurricane_ids = arena_alloc_array(scratch, alive + 1u, sizeof(uint64_t), alignof(uint64_t));
    float64_t *const storm_chances = arena_alloc_array(scratch, alive + 1u, sizeof(float64_t), alignof(float64_t));
    uint32_t hurricanes = 0u, storms = 0u;
    for (uint32_t i = 0u; i < alive; i++) {
        const climate_system_t *const system = &list.borrowed_systems[order[i]];
        if (system->kind != CLIMATE_SYSTEM_HURRICANE && system->kind != CLIMATE_SYSTEM_LOW) {
            continue; /* highs never warn */
        }
        float64_t chances[CLIMATE_WARNING_STEPS];
        bool deep[CLIMATE_WARNING_STEPS];
        climate_warning_steps(system, seed, week, census_t, t0, x, y, false, chances, deep);
        float64_t q = 0.0f64, q_deep = 0.0f64;
        bool any_deep = false;
        for (uint32_t k = 0u; k < CLIMATE_WARNING_STEPS; k++) {
            q = chances[k] > q ? chances[k] : q;
            if (deep[k]) {
                any_deep = true;
                q_deep = chances[k] > q_deep ? chances[k] : q_deep;
            }
        }
        if (system->kind == CLIMATE_SYSTEM_HURRICANE) {
            hurricane_chances[hurricanes] = q;
            hurricane_ids[hurricanes] = system->id;
            hurricanes++;
        } else if (any_deep) {
            storm_chances[storms++] = q_deep;
        }
    }
    /* Not born at the census: the outlook's rate sums now and a day on. A storm is a low past 20 hPa: the outlook's share of
     * lows that deep (CLIMATE_OUTLOOK_DEEP_LOW_SHARE), as its own day's storm chance takes it. */
    climate_systems_mask_t mask;
    climate_systems_mask_start(&mask, climate, scratch, nullptr);
    const float64_t after[2] = {t0, t0 + 1.0f64};
    float64_t hurricane_lambda[2], low_lambda[2];
    climate_outlook_unborn(climate, scratch, &mask, CLIMATE_SYSTEM_HURRICANE, x, y, census_t, after, 2u,
                           hurricane_lambda);
    climate_outlook_unborn(climate, scratch, &mask, CLIMATE_SYSTEM_LOW, x, y, census_t, after, 2u, low_lambda);
    const float64_t hurricane_unborn = hurricane_lambda[1] - hurricane_lambda[0];
    const float64_t low_unborn = (low_lambda[1] - low_lambda[0]) * (float64_t)CLIMATE_OUTLOOK_DEEP_LOW_SHARE;
    const climate_warning_input_t input = {
        .hurricane_chances = hurricane_chances,
        .hurricane_ids = hurricane_ids,
        .storm_chances = storm_chances,
        .hurricane_unborn = hurricane_unborn > 0.0f64 ? hurricane_unborn : 0.0f64,
        .storm_unborn = low_unborn > 0.0f64 ? low_unborn : 0.0f64,
        .hurricane_count = hurricanes,
        .storm_count = storms,
    };
    *out = climate_warning_decide(&input);
    if (out->kind == CLIMATE_WARNING_NAMED) {
        for (uint32_t i = 0u; i < alive; i++) {
            const climate_system_t *const system = &list.borrowed_systems[order[i]];
            if (system->id == out->system_id && system->kind == CLIMATE_SYSTEM_HURRICANE) {
                climate_system_state_t state;
                climate_system_state_at(system, t, &state);
                out->name_seed = system->name_seed;
                out->hurricane = state.wind_max >= CLIMATE_HURRICANE_STORM_VMAX ? 1u : 0u;
                break;
            }
        }
    }
    return true;
}
