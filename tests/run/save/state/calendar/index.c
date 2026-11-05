/* ctest run_save_state_calendar. See README.md. */
#include "harness/index.h"

#include "run/physics/fixtures/test_world.h"

#include "calendar/index.h"
#include "file/index.h"
#include "log/index.h"
#include "save/index.h"
#include "tables/blocks/index.h"
#include "tables/items/index.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum : uint64_t {
    SC_PATH_BYTES = 512u,
};
static const uint64_t SC_SEED = 9393u;

/* ---- Small things ------------------------------------------------------------------------------------------------------ */

/* A separate FNV-1a, so that a change to the one under test is not also made here. */
static uint64_t sc_fnv(const void *data, uint64_t bytes) {
    const uint8_t *const cursor = data;
    uint64_t hash = 14695981039346656037ull;
    for (uint64_t i = 0u; i < bytes; i++) {
        hash = (hash ^ cursor[i]) * 1099511628211ull;
    }
    return hash;
}

static char *sc_state_path(test_context_t *t, const char *root, uint64_t generation) {
    char *const path = test_alloc(t, SC_PATH_BYTES);
    snprintf(path, SC_PATH_BYTES, "%s/state.%" PRIu64, root, generation);
    return path;
}

static char *sc_temp_directory(test_context_t *t) {
    char *const directory = test_alloc(t, SC_PATH_BYTES);
    snprintf(directory, SC_PATH_BYTES, "/tmp/mc_save_state_calendar_XXXXXX");
    return mkdtemp(directory);
}

static void sc_temp_remove(test_context_t *t, const char *directory) {
    if (directory == nullptr || strncmp(directory, "/tmp/mc_save_state_calendar_", 28u) != 0) {
        return;
    }
    char *const argv[] = {(char *)"/usr/bin/rm", (char *)"-rf", (char *)directory, nullptr};
    test_process_t process;
    test_process_run(t, argv, 30000u, &process);
    CHECK_EQ_I64(process.exit_code, 0);
}

static uint8_t *sc_slurp(test_context_t *t, const char *path, uint64_t *out_bytes) {
    uint8_t *const bytes = test_alloc(t, SAVE_STATE_IMAGE_BYTES);
    if (!CHECK_EQ_U64(file_read_whole(path, bytes, SAVE_STATE_IMAGE_BYTES, out_bytes), FILE_OK)) {
        *out_bytes = 0u;
    }
    return bytes;
}

/* Errors a test sets out to cause, off the screen. Returns what to give back. */
static int32_t sc_quiet_begin(void) {
    return log_set_fd(open("/dev/null", O_WRONLY | O_CLOEXEC));
}
static void sc_quiet_end(int32_t previous) {
    const int32_t quiet = log_set_fd(previous);
    (void)close(quiet);
}

/* The hashes of a file made right again, as someone forging a number would. */
static void sc_rehash(uint8_t *file, uint64_t bytes) {
    save_state_header_t header;
    memcpy(&header, file, sizeof(header));
    header.sections_hash = sc_fnv(file + sizeof(header), bytes - sizeof(header));
    header.checksum = sc_fnv(&header, offsetof(save_state_header_t, checksum));
    memcpy(file, &header, sizeof(header));
}

/* ---- A world's worth of state ---------------------------------------------------------------------------------------- */

typedef struct sc_rig_t {
    test_world_t w;           /* 0 */
    player_t *player;         /* 24 */
    item_t *item;             /* 32 */
    entity_t *entity;         /* 40 */
    mob_t *mob;               /* 48 */
    mob_spawn_t *spawn;       /* 56 */
    item_stack_t *held;       /* 64 */
    save_t *save;             /* 72 */
    const char *root;         /* 80 */
    save_state_parts_t parts; /* 88 */
} sc_rig_t;
static_assert(sizeof(sc_rig_t) == 184);

static bool sc_rig_begin(test_context_t *t, sc_rig_t *rig, const char *root) {
    memset(rig, 0, sizeof(*rig));
    if (!test_world_begin(t, &rig->w)) {
        return false;
    }
    rig->root = root;
    rig->player = test_world_aligned(t, sizeof(player_t));
    rig->item = test_world_aligned(t, sizeof(item_t));
    rig->entity = test_world_aligned(t, sizeof(entity_t));
    rig->mob = test_world_aligned(t, sizeof(mob_t));
    rig->spawn = test_alloc(t, sizeof(mob_spawn_t));
    rig->held = test_alloc(t, sizeof(item_stack_t));
    memset(rig->held, 0, sizeof(item_stack_t));
    rig->save = test_world_aligned(t, sizeof(save_t));
    test_world_fill(t, &rig->w, -16, -16, 0, 31, 31, 0, BLOCK_ID_STONE);
    player_init(rig->player, rig->w.physics);
    if (!CHECK(item_init(rig->item)) || !CHECK(entity_init(rig->entity, rig->w.physics)) ||
        !CHECK(mob_init(rig->mob, nullptr)) || !CHECK(save_init(rig->save))) {
        return false;
    }
    if (!CHECK_EQ_U64(save_open(rig->save, root, SC_SEED), SAVE_WORLD_OK)) {
        return false;
    }
    player_spawn(rig->player, 3.5f64, 4.5f64, 1.0f64);
    item_open(rig->item);
    item_world_begin(rig->item, &rig->w.world->edit);
    entity_open(rig->entity, 41u);
    mob_spawn_start(rig->spawn, &rig->entity->arena_world, 43u);
    mob_open(rig->mob, rig->w.world, 47u);
    rig->parts = (save_state_parts_t){
        .borrowed_player = rig->player,
        .borrowed_item = rig->item,
        .borrowed_entity = rig->entity,
        .borrowed_mob = rig->mob,
        .borrowed_spawn = rig->spawn,
        .borrowed_held = rig->held,
    };
    return true;
}

static void sc_rig_end(sc_rig_t *rig) {
    mob_close(rig->mob);
    mob_deinit(rig->mob);
    entity_close(rig->entity);
    entity_deinit(rig->entity);
    item_close(rig->item);
    item_deinit(rig->item);
    player_deinit(rig->player);
    save_deinit(rig->save);
    test_world_end(&rig->w);
}

/* Something in the time, the calendar and the bag, to see each come back or not. */
static void sc_fill(sc_rig_t *rig) {
    (void)inventory_add(&rig->item->inventory, ITEM_ID_DIRT, 40u);
    rig->parts.time = (save_state_time_t){
        .world_ticks = 1234u, .day_units = 0x12345678u, .yaw = 1.25f32, .pitch = -0.5f32, ._pad0 = {0}};
    /* The plan's world 1000 days old, whose census has run for week 154. */
    rig->parts.calendar = (save_state_calendar_t){
        .clock_days = 1000u, .last_census_week = 154u, .day_zero = CALENDAR_DAY_ZERO, ._pad0 = {0}};
}

/* ---- The calendar: plan 004 phase 01 ---------------------------------------------------------------------------------- */

/* Where the calendar section's head is in a file sc_fill's rig wrote: right after the time's. */
enum : uint64_t {
    SC_CALENDAR_AT = sizeof(save_state_header_t) + sizeof(save_state_section_t) + sizeof(save_state_time_t),
    SC_CALENDAR_SPAN = sizeof(save_state_section_t) + sizeof(save_state_calendar_t),
};

/* A file of sc_fill's rig, written and read back. */
static const uint8_t *sc_calendar_file(test_context_t *t, const char *root, uint64_t *out_bytes) {
    sc_rig_t *const rig = test_alloc(t, sizeof(sc_rig_t));
    if (!sc_rig_begin(t, rig, root)) {
        return nullptr;
    }
    sc_fill(rig);
    save_state_capture(&rig->save->state, &rig->parts);
    const bool written = save_state_write(&rig->save->state, 0u) == SAVE_WORLD_OK;
    sc_rig_end(rig);
    if (!written) {
        return nullptr;
    }
    return sc_slurp(t, sc_state_path(t, root, 0u), out_bytes);
}

/* The same file with its calendar section taken out, and the header saying version. What version 3 wrote, as it had none. */
static uint64_t sc_calendar_strip(uint8_t *file, uint64_t bytes, uint32_t version) {
    memmove(file + SC_CALENDAR_AT, file + SC_CALENDAR_AT + SC_CALENDAR_SPAN, bytes - SC_CALENDAR_AT - SC_CALENDAR_SPAN);
    const uint64_t stripped = bytes - SC_CALENDAR_SPAN;
    save_state_header_t header;
    memcpy(&header, file, sizeof(header));
    header.version = version;
    header.section_count--;
    header.sections_bytes = stripped - sizeof(header);
    memcpy(file, &header, sizeof(header));
    sc_rehash(file, stripped);
    return stripped;
}

TEST(the_calendar_reloads_exactly) {
    char *const root = sc_temp_directory(t);
    REQUIRE(root != nullptr);
    uint64_t bytes = 0u;
    const uint8_t *const good = sc_calendar_file(t, root, &bytes);
    REQUIRE(good != nullptr);
    CHECK_EQ_U64(save_state_check(good, bytes, SC_SEED, 0u), SAVE_WORLD_OK);
    sc_rig_t *const rig = test_alloc(t, sizeof(sc_rig_t));
    REQUIRE(sc_rig_begin(t, rig, root));
    CHECK_EQ_U64(rig->save->state.kept, 1u);
    REQUIRE(save_state_apply(&rig->save->state, &rig->parts));
    CHECK_EQ_U64(rig->parts.calendar.clock_days, 1000u);
    CHECK_EQ_U64(rig->parts.calendar.last_census_week, 154u);
    CHECK_EQ_U64(rig->parts.calendar.day_zero, CALENDAR_DAY_ZERO);
    sc_rig_end(rig);
    sc_temp_remove(t, root);
}

TEST(a_version_3_file_still_opens_with_a_new_calendar) {
    char *const root = sc_temp_directory(t);
    REQUIRE(root != nullptr);
    uint64_t bytes = 0u;
    const uint8_t *const good = sc_calendar_file(t, root, &bytes);
    REQUIRE(good != nullptr);
    /* The section is where this test thinks it is. */
    save_state_section_t head;
    memcpy(&head, good + SC_CALENDAR_AT, sizeof(head));
    REQUIRE(head.id == SAVE_STATE_SECTION_CALENDAR && head.bytes == sizeof(save_state_calendar_t));
    uint8_t *const old = test_alloc(t, bytes);
    memcpy(old, good, bytes);
    const uint64_t old_bytes = sc_calendar_strip(old, bytes, SAVE_STATE_VERSION_READ_MIN);
    CHECK_EQ_U64(SAVE_STATE_VERSION_READ_MIN, 3u);
    CHECK_EQ_U64(save_state_check(old, old_bytes, SC_SEED, 0u), SAVE_WORLD_OK);
    REQUIRE(file_write_atomic(sc_state_path(t, root, 0u), old, old_bytes) == FILE_OK);

    /* Believed, and the caller's calendar, a new world's, is left as it was; everything else comes back. */
    sc_rig_t *const rig = test_alloc(t, sizeof(sc_rig_t));
    REQUIRE(sc_rig_begin(t, rig, root));
    CHECK_EQ_U64(rig->save->state.kept, 1u);
    const save_state_calendar_t fresh = {.clock_days = 0u, .last_census_week = 11u, .day_zero = CALENDAR_DAY_ZERO};
    rig->parts.calendar = fresh;
    REQUIRE(save_state_apply(&rig->save->state, &rig->parts));
    CHECK_MEM_EQ(&rig->parts.calendar, &fresh, sizeof(fresh));
    CHECK_EQ_U64(rig->parts.time.world_ticks, 1234u);
    CHECK_EQ_U64(inventory_count(&rig->item->inventory, ITEM_ID_DIRT), 40u);
    /* Its next save is version 4, with the calendar it went on with. */
    save_state_capture(&rig->save->state, &rig->parts);
    REQUIRE(save_state_write(&rig->save->state, 0u) == SAVE_WORLD_OK);
    uint64_t new_bytes = 0u;
    const uint8_t *const now = sc_slurp(t, sc_state_path(t, root, 0u), &new_bytes);
    save_state_header_t header;
    memcpy(&header, now, sizeof(header));
    CHECK_EQ_U64(header.version, SAVE_STATE_VERSION);
    uint64_t calendar_bytes = 0u;
    const uint8_t *const calendar =
        save_state_section_find(now, new_bytes, SAVE_STATE_SECTION_CALENDAR, &calendar_bytes);
    REQUIRE(calendar != nullptr && calendar_bytes == sizeof(fresh));
    CHECK_MEM_EQ(calendar, &fresh, sizeof(fresh));
    sc_rig_end(rig);
    sc_temp_remove(t, root);
}

TEST(a_calendar_that_cannot_be_true_is_refused) {
    char *const root = sc_temp_directory(t);
    REQUIRE(root != nullptr);
    uint64_t bytes = 0u;
    const uint8_t *const good = sc_calendar_file(t, root, &bytes);
    REQUIRE(good != nullptr);
    CHECK_EQ_U64(save_state_check(good, bytes, SC_SEED, 0u), SAVE_WORLD_OK);
    uint8_t *const copy = test_alloc(t, bytes);
    const uint64_t data_at = SC_CALENDAR_AT + sizeof(save_state_section_t);
    const int32_t quiet = sc_quiet_begin();

    /* Each with good hashes, so only what it says is wrong. clock_days 1000 and D0 79 reach week (79 + 1000 + 1) / 7 = 154. */
    typedef struct sc_calendar_case_t {
        save_state_calendar_t calendar; /* 0 */
        uint64_t believed;              /* 24 */
    } sc_calendar_case_t;
    static_assert(sizeof(sc_calendar_case_t) == 32);
    const sc_calendar_case_t cases[] = {
        {{.clock_days = 1000u, .last_census_week = 154u, .day_zero = 79u}, 1u},
        {{.clock_days = 1ull << 32, .last_census_week = 0u, .day_zero = 79u}, 0u},
        {{.clock_days = (1ull << 32) - 1u, .last_census_week = 0u, .day_zero = 79u}, 1u},
        {{.clock_days = 1000u, .last_census_week = 0u, .day_zero = 365u}, 0u},
        {{.clock_days = 1000u, .last_census_week = 0u, .day_zero = 364u}, 1u},
        {{.clock_days = 1000u, .last_census_week = 200u, .day_zero = 79u}, 0u},
        {{.clock_days = 1000u, .last_census_week = 155u, .day_zero = 79u}, 0u},
        {{.clock_days = 1000u, .last_census_week = 154u, .day_zero = 79u, ._pad0 = {0, 0, 1, 0}}, 0u},
    };
    for (uint32_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        memcpy(copy, good, bytes);
        memcpy(copy + data_at, &cases[i].calendar, sizeof(cases[i].calendar));
        sc_rehash(copy, bytes);
        CHECK_EQ_U64((uint64_t)i * 100u + (save_state_check(copy, bytes, SC_SEED, 0u) == SAVE_WORLD_OK ? 1u : 0u),
                     (uint64_t)i * 100u + cases[i].believed);
    }
    /* Too short a section, with the rest moved up to suit. */
    memcpy(copy, good, bytes);
    save_state_section_t head;
    memcpy(&head, copy + SC_CALENDAR_AT, sizeof(head));
    head.bytes = 16u;
    memcpy(copy + SC_CALENDAR_AT, &head, sizeof(head));
    memmove(copy + data_at + 16u, copy + data_at + 24u, bytes - data_at - 24u);
    save_state_header_t header;
    memcpy(&header, copy, sizeof(header));
    header.sections_bytes -= 8u;
    memcpy(copy, &header, sizeof(header));
    sc_rehash(copy, bytes - 8u);
    CHECK_EQ_U64(save_state_check(copy, bytes - 8u, SC_SEED, 0u), SAVE_WORLD_ERROR_DAMAGED);
    /* A version 4 file with no calendar, and a version 3 file with one. */
    memcpy(copy, good, bytes);
    const uint64_t stripped = sc_calendar_strip(copy, bytes, SAVE_STATE_VERSION);
    CHECK_EQ_U64(save_state_check(copy, stripped, SC_SEED, 0u), SAVE_WORLD_ERROR_DAMAGED);
    memcpy(copy, good, bytes);
    memcpy(&header, copy, sizeof(header));
    header.version = SAVE_STATE_VERSION_READ_MIN;
    memcpy(copy, &header, sizeof(header));
    sc_rehash(copy, bytes);
    CHECK_EQ_U64(save_state_check(copy, bytes, SC_SEED, 0u), SAVE_WORLD_ERROR_DAMAGED);
    /* Older than any version still read. */
    header.version = SAVE_STATE_VERSION_READ_MIN - 1u;
    memcpy(copy, &header, sizeof(header));
    sc_rehash(copy, bytes);
    CHECK_EQ_U64(save_state_check(copy, bytes, SC_SEED, 0u), SAVE_WORLD_ERROR_VERSION);
    sc_quiet_end(quiet);
    sc_temp_remove(t, root);
}

TEST_MAIN()
