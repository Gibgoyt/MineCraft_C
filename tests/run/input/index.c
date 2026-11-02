/* ctest run_input. See README.md. Nothing here opens a real input device: tests run on the fake kernel in fixtures/, or on
 * real calls in a mkdtemp directory holding regular files and FIFOs, and the built program is only given /dev/null. */
#include "harness/index.h"

#include "display/flip/index.h"
#include "heap/index.h"
#include "input/index.h"
#include "run/input/fixtures/fake_evdev.h"
#include "run/input/fixtures/replay.h"

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---- Helpers ----------------------------------------------------------------------------------------------------- */

static input_t *start(test_context_t *t, fake_evdev_t *fake) {
    input_t *const input = test_alloc(t, sizeof(input_t));
    test_check(t, input_init(input, FAKE_DIRECTORY, &fake_ops, fake), __FILE__, __LINE__, "input_init");
    return input;
}

/* Every descriptor it opened is closed, nothing was asked that should not have been, and the memory is back. */
static void finish(test_context_t *t, input_t *input, fake_evdev_t *fake) {
    input_deinit(input);
    for (uint32_t i = 0; i < fake->device_count; i++) {
        CHECK_EQ_U64(fake->devices[i].is_open, 0u);
    }
    CHECK_EQ_U64(fake->watch_open, 0u);
    CHECK_EQ_U64(fake->unexpected_ioctls, 0u);
    CHECK_EQ_U64(fake->outside_directory, 0u);
    /* A second deinit, as main's cleanup might, does nothing. */
    input_deinit(input);
}

static void plug(fake_evdev_t *fake, fake_device_t *device) {
    device->listed = 1u;
    fake_record(fake, IN_CREATE, device->file_name);
}

/* What a real pull looks like: reads fail from now on, and the node goes. */
static void pull(fake_evdev_t *fake, fake_device_t *device) {
    device->read_errno = ENODEV;
    device->listed = 0u;
    fake_record(fake, IN_DELETE, device->file_name);
}

/* ---- Classifying ------------------------------------------------------------------------------------------------- */

TEST(devices_are_told_apart_by_what_they_can_do) {
    input_capabilities_t caps;
    memset(&caps, 0, sizeof(caps));
    CHECK_EQ_U64(input_discovery_classify(&caps), 0u);
    fake_caps_keyboard(&caps);
    CHECK_EQ_U64(input_discovery_classify(&caps), INPUT_KIND_KEYBOARD);
    fake_caps_mouse(&caps);
    CHECK_EQ_U64(input_discovery_classify(&caps), INPUT_KIND_KEYBOARD | INPUT_KIND_MOUSE);

    memset(&caps, 0, sizeof(caps));
    fake_caps_mouse(&caps);
    CHECK_EQ_U64(input_discovery_classify(&caps), INPUT_KIND_MOUSE);
    memset(&caps, 0, sizeof(caps));
    fake_caps_power_button(&caps);
    CHECK_EQ_U64(input_discovery_classify(&caps), 0u);
    memset(&caps, 0, sizeof(caps));
    fake_caps_touchpad(&caps);
    CHECK_EQ_U64(input_discovery_classify(&caps), 0u);
}

TEST(each_thing_a_keyboard_needs_is_needed) {
    static const uint16_t needed[] = {KEY_Q, KEY_P, KEY_A, KEY_L, KEY_Z, KEY_M, KEY_SPACE, KEY_ENTER};
    for (uint32_t i = 0; i < sizeof(needed) / sizeof(needed[0]); i++) {
        input_capabilities_t caps;
        memset(&caps, 0, sizeof(caps));
        fake_caps_keyboard(&caps);
        CHECK_EQ_U64(input_discovery_classify(&caps), INPUT_KIND_KEYBOARD);
        caps.keys[needed[i] / 8u] &= (uint8_t)~(1u << (needed[i] % 8u));
        CHECK_EQ_U64(input_discovery_classify(&caps), 0u);
    }
    input_capabilities_t no_key_events;
    memset(&no_key_events, 0, sizeof(no_key_events));
    fake_caps_keyboard(&no_key_events);
    no_key_events.event_types[EV_KEY / 8u] &= (uint8_t)~(1u << (EV_KEY % 8u));
    CHECK_EQ_U64(input_discovery_classify(&no_key_events), 0u);
}

TEST(each_thing_a_mouse_needs_is_needed) {
    input_capabilities_t caps;
    memset(&caps, 0, sizeof(caps));
    fake_caps_mouse(&caps);
    caps.relative_axes[0] &= (uint8_t)~(1u << REL_X);
    CHECK_EQ_U64(input_discovery_classify(&caps), 0u);
    memset(&caps, 0, sizeof(caps));
    fake_caps_mouse(&caps);
    caps.relative_axes[0] &= (uint8_t)~(1u << REL_Y);
    CHECK_EQ_U64(input_discovery_classify(&caps), 0u);
    memset(&caps, 0, sizeof(caps));
    fake_caps_mouse(&caps);
    caps.event_types[EV_REL / 8u] &= (uint8_t)~(1u << (EV_REL % 8u));
    CHECK_EQ_U64(input_discovery_classify(&caps), 0u);
    memset(&caps, 0, sizeof(caps));
    fake_caps_mouse(&caps);
    caps.keys[BTN_LEFT / 8u] &= (uint8_t)~(1u << (BTN_LEFT % 8u));
    CHECK_EQ_U64(input_discovery_classify(&caps), 0u);
}

TEST(only_event_and_digits_can_be_a_device_name) {
    CHECK(input_discovery_name_is_event("event0"));
    CHECK(input_discovery_name_is_event("event1234"));
    CHECK(!input_discovery_name_is_event("event"));
    CHECK(!input_discovery_name_is_event("event1x"));
    CHECK(!input_discovery_name_is_event("eventx1"));
    CHECK(!input_discovery_name_is_event("mouse0"));
    CHECK(!input_discovery_name_is_event("js0"));
    CHECK(!input_discovery_name_is_event("by-id"));
    CHECK(!input_discovery_name_is_event("."));
    CHECK(!input_discovery_name_is_event(""));
    CHECK(!input_discovery_name_is_event("event12345678901234567890"));
}

/* ---- Discovery ---------------------------------------------------------------------------------------------------- */

TEST(the_first_look_keeps_keyboards_and_mice_and_closes_the_rest) {
    fake_evdev_t *const fake = fake_new(t);
    /* Numbered against what they are, so that nothing can pass by going on the number. */
    fake_device_t *const power = fake_add(fake, "event0", "Power Button", fake_caps_power_button, true);
    fake_device_t *const mouse = fake_add(fake, "event1", "A Mouse", fake_caps_mouse, true);
    fake_device_t *const touchpad = fake_add(fake, "event2", "A Touchpad", fake_caps_touchpad, true);
    fake_device_t *const keyboard = fake_add(fake, "event3", "A Keyboard", fake_caps_keyboard, true);
    /* Keyboard capabilities behind a name that cannot be an evdev node: not even opened. */
    fake_device_t *const not_event = fake_add(fake, "mouse0", "Legacy", fake_caps_keyboard, true);
    fake->extra_names[0] = "by-id";
    fake->extra_names[1] = "by-path";
    fake->extra_count = 2u;

    input_t *const input = start(t, fake);
    CHECK_EQ_U64(input->discovery.device_count, 2u);
    CHECK(fake->watched_before_first_list != 0u);
    CHECK_EQ_U64(mouse->is_open, 1u);
    CHECK_EQ_U64(keyboard->is_open, 1u);
    CHECK_EQ_U64(power->is_open, 0u);
    CHECK_EQ_U64(power->close_count, 1u);
    CHECK_EQ_U64(touchpad->is_open, 0u);
    CHECK_EQ_U64(touchpad->close_count, 1u);
    CHECK_EQ_U64(not_event->open_count, 0u);
    CHECK_EQ_U64(input->discovery.stats.rejected, 2u);

    bool saw_keyboard = false;
    for (uint32_t i = 0; i < INPUT_DISCOVERY_MAX_DEVICES; i++) {
        const input_device_t *const device = &input->discovery.borrowed_devices[i];
        if (device->in_use != 0u && strcmp(device->file_name, "event3") == 0) {
            saw_keyboard = true;
            CHECK_STR_EQ(device->label, "A Keyboard");
            CHECK_EQ_U64(device->kinds, INPUT_KIND_KEYBOARD);
        }
    }
    CHECK(saw_keyboard);
    finish(t, input, fake);
    CHECK_EQ_U64(keyboard->close_count, 1u);
    CHECK_EQ_U64(fake->watch_closes, 1u);
}

TEST(what_cannot_be_opened_or_asked_is_left_out_and_counted) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const denied = fake_add(fake, "event0", "Denied", fake_caps_keyboard, true);
    denied->open_errno = EACCES;
    fake_device_t *const no_caps = fake_add(fake, "event1", "Not evdev", fake_caps_keyboard, true);
    no_caps->caps_errno = ENOTTY;
    fake_device_t *const no_name = fake_add(fake, "event2", "", fake_caps_mouse, true);
    no_name->name_errno = EIO;

    input_t *const input = start(t, fake);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK_EQ_U64(input->discovery.stats.open_failures, 1u);
    CHECK_EQ_I64(input->discovery.stats.last_errno, EACCES);
    CHECK_EQ_U64(input->discovery.stats.rejected, 1u);
    CHECK_EQ_U64(no_caps->close_count, 1u);
    /* A nameless device still works. */
    CHECK_EQ_U64(no_name->is_open, 1u);
    CHECK_STR_EQ(input->discovery.borrowed_devices[0].label, "unnamed");
    finish(t, input, fake);
}

TEST(a_long_device_name_is_cut_and_stays_terminated) {
    fake_evdev_t *const fake = fake_new(t);
    const char *const long_label = "A Very Long Device Name That Goes On And On And On And On And On And On And On";
    fake_add(fake, "event0", long_label, fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    const char *const label = input->discovery.borrowed_devices[0].label;
    CHECK_EQ_U64(strlen(label), INPUT_DISCOVERY_LABEL_BYTES - 1u);
    CHECK_EQ_I64(strncmp(label, long_label, INPUT_DISCOVERY_LABEL_BYTES - 1u), 0);
    finish(t, input, fake);
}

TEST(no_devices_and_no_directory_are_not_failures) {
    fake_evdev_t *const fake = fake_new(t);
    fake->watch_errno = ENOENT;
    fake->list_errno = ENOENT;
    input_t *const input = start(t, fake);
    CHECK_EQ_U64(input->running, 1u);
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    CHECK_EQ_I64(input->discovery.watch_fd, -1);
    const input_frame_t *const frame = input_poll(input);
    CHECK(!input_keys_any(&frame->down));
    finish(t, input, fake);
    CHECK_EQ_U64(fake->watch_closes, 0u);
}

TEST(more_devices_than_slots_are_counted_and_a_freed_slot_is_used) {
    fake_evdev_t *const fake = fake_new(t);
    for (uint32_t i = 0; i < INPUT_DISCOVERY_MAX_DEVICES + 3u; i++) {
        char name[32];
        snprintf(name, sizeof(name), "event%u", i);
        fake_add(fake, name, "Keyboard", fake_caps_keyboard, true);
    }
    input_t *const input = start(t, fake);
    CHECK_EQ_U64(input->discovery.device_count, INPUT_DISCOVERY_MAX_DEVICES);
    CHECK_EQ_U64(input->discovery.stats.table_full, 3u);
    fake_device_t *const first = &fake->devices[0];
    pull(fake, first);
    plug(fake, &fake->devices[INPUT_DISCOVERY_MAX_DEVICES]); /* the record names one of those left out */
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, INPUT_DISCOVERY_MAX_DEVICES);
    CHECK_EQ_U64(first->is_open, 0u);
    CHECK_EQ_U64(fake->devices[INPUT_DISCOVERY_MAX_DEVICES].is_open, 1u);
    finish(t, input, fake);
}

/* ---- Replaying events --------------------------------------------------------------------------------------------- */

TEST(a_key_held_and_let_go_has_one_press_and_one_release) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);

    const input_frame_t *frame = input_poll(input);
    CHECK(!input_keys_any(&frame->down));

    fake_replay(keyboard, replay_w_held, REPLAY_COUNT(replay_w_held));
    frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_W));
    CHECK(input_keys_has(&frame->pressed, KEY_W));
    CHECK(input_keys_has(&frame->repeated, KEY_W));
    CHECK(!input_keys_has(&frame->released, KEY_W));
    CHECK(!input_keys_has(&frame->down, KEY_A));

    /* Next frame: still held, and the edges have gone. */
    frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_W));
    CHECK(!input_keys_any(&frame->pressed));
    CHECK(!input_keys_any(&frame->repeated));

    fake_replay(keyboard, replay_w_up, REPLAY_COUNT(replay_w_up));
    frame = input_poll(input);
    CHECK(!input_keys_any(&frame->down));
    CHECK(input_keys_has(&frame->released, KEY_W));
    CHECK(!input_keys_any(&frame->pressed));

    frame = input_poll(input);
    CHECK(!input_keys_any(&frame->released));
    CHECK_EQ_U64(input->events.stats.ignored, 0u);
    CHECK_EQ_U64(input->events.stats.dropped, 0u);
    finish(t, input, fake);
}

TEST(a_tap_inside_one_frame_is_still_seen) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    fake_replay(keyboard, replay_space_tap, REPLAY_COUNT(replay_space_tap));
    const input_frame_t *const frame = input_poll(input);
    CHECK(input_keys_has(&frame->pressed, KEY_SPACE));
    CHECK(input_keys_has(&frame->released, KEY_SPACE));
    CHECK(!input_keys_has(&frame->down, KEY_SPACE));
    finish(t, input, fake);
}

TEST(mouse_movement_adds_up_in_a_frame_and_starts_again_in_the_next) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    input_t *const input = start(t, fake);
    fake_replay(mouse, replay_mouse_moves, REPLAY_COUNT(replay_mouse_moves));
    const input_frame_t *frame = input_poll(input);
    CHECK_EQ_I64(frame->mouse_dx, 12);
    CHECK_EQ_I64(frame->mouse_dy, 8);
    CHECK_EQ_I64(frame->wheel, 1);
    CHECK_EQ_I64(frame->wheel_horizontal, -2);
    frame = input_poll(input);
    CHECK_EQ_I64(frame->mouse_dx, 0);
    CHECK_EQ_I64(frame->mouse_dy, 0);
    CHECK_EQ_I64(frame->wheel, 0);
    CHECK_EQ_I64(frame->wheel_horizontal, 0);
    finish(t, input, fake);
}

TEST(mouse_buttons_are_keys) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    input_t *const input = start(t, fake);
    fake_replay(mouse, replay_left_down, REPLAY_COUNT(replay_left_down));
    const input_frame_t *frame = input_poll(input);
    CHECK(input_keys_has(&frame->pressed, BTN_LEFT));
    CHECK(input_keys_has(&frame->down, BTN_LEFT));
    fake_replay(mouse, replay_left_up, REPLAY_COUNT(replay_left_up));
    frame = input_poll(input);
    CHECK(input_keys_has(&frame->released, BTN_LEFT));
    CHECK(!input_keys_has(&frame->down, BTN_LEFT));
    finish(t, input, fake);
}

TEST(a_key_held_on_two_keyboards_is_down_until_both_let_go) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const first = fake_add(fake, "event0", "First", fake_caps_keyboard, true);
    fake_device_t *const second = fake_add(fake, "event1", "Second", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);

    fake_replay(first, replay_w_held, REPLAY_COUNT(replay_w_held));
    const input_frame_t *frame = input_poll(input);
    CHECK(input_keys_has(&frame->pressed, KEY_W));

    fake_replay(second, replay_w_held, REPLAY_COUNT(replay_w_held));
    frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_W));
    CHECK(!input_keys_has(&frame->pressed, KEY_W));

    fake_replay(first, replay_w_up, REPLAY_COUNT(replay_w_up));
    frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_W));
    CHECK(!input_keys_has(&frame->released, KEY_W));

    fake_replay(second, replay_w_up, REPLAY_COUNT(replay_w_up));
    frame = input_poll(input);
    CHECK(!input_keys_has(&frame->down, KEY_W));
    CHECK(input_keys_has(&frame->released, KEY_W));
    finish(t, input, fake);
}

TEST(events_that_mean_nothing_change_nothing_and_are_counted) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    fake_replay(keyboard, replay_garbage, REPLAY_COUNT(replay_garbage));
    const input_frame_t *const frame = input_poll(input);
    CHECK(!input_keys_any(&frame->down));
    CHECK(!input_keys_any(&frame->pressed));
    CHECK(!input_keys_any(&frame->released)); /* KEY_B let go, though never pressed */
    CHECK(!input_keys_any(&frame->repeated)); /* KEY_C repeating, though never pressed */
    CHECK_EQ_I64(frame->mouse_dx, 0);
    CHECK_EQ_I64(frame->mouse_dy, 0);
    CHECK_EQ_U64(input->events.stats.ignored, 4u); /* two bad codes, two bad values */
    CHECK_EQ_U64(input->events.stats.events, REPLAY_COUNT(replay_garbage));
    for (uint32_t code = 0; code < INPUT_KEY_CODES; code++) {
        CHECK_EQ_U64(input->events.borrowed_holders[code], 0u);
    }
    finish(t, input, fake);
}

TEST(pressing_what_is_held_does_not_count_twice) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    static const struct input_event twice[] = {REPLAY_EVENT(EV_KEY, KEY_A, 1), REPLAY_EVENT(EV_KEY, KEY_A, 1)};
    fake_replay(keyboard, twice, REPLAY_COUNT(twice));
    input_poll(input);
    CHECK_EQ_U64(input->events.borrowed_holders[KEY_A], 1u);
    static const struct input_event once_up[] = {REPLAY_EVENT(EV_KEY, KEY_A, 0)};
    fake_replay(keyboard, once_up, REPLAY_COUNT(once_up));
    const input_frame_t *const frame = input_poll(input);
    CHECK(!input_keys_has(&frame->down, KEY_A));
    CHECK_EQ_U64(input->events.borrowed_holders[KEY_A], 0u);
    finish(t, input, fake);
}

TEST(a_read_that_ends_inside_an_event_uses_what_is_whole) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    static const struct input_event two[] = {REPLAY_EVENT(EV_KEY, KEY_A, 1), REPLAY_EVENT(EV_KEY, KEY_B, 1)};
    fake_replay(keyboard, two, REPLAY_COUNT(two));
    keyboard->torn_bytes = 1u;
    const input_frame_t *const frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_A));
    CHECK(!input_keys_has(&frame->down, KEY_B));
    CHECK_EQ_U64(input->events.stats.torn_reads, 1u);
    finish(t, input, fake);
}

TEST(a_long_queue_is_read_across_many_reads_and_a_flood_cannot_hold_a_frame) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    input_t *const input = start(t, fake);
    static const struct input_event one[] = {REPLAY_EVENT(EV_REL, REL_X, 1)};
    /* 64 events a read: a little over one read's worth takes two. */
    for (uint32_t i = 0; i < 100u; i++) {
        fake_replay(mouse, one, 1u);
    }
    const uint32_t reads_before = mouse->read_calls;
    const input_frame_t *frame = input_poll(input);
    CHECK_EQ_I64(frame->mouse_dx, 100);
    CHECK_EQ_U64(mouse->read_calls - reads_before, 2u);

    /* Exactly one read's worth must not lose the read that finds it empty, or stop early: the next poll sees nothing new. */
    for (uint32_t i = 0; i < 64u; i++) {
        fake_replay(mouse, one, 1u);
    }
    frame = input_poll(input);
    CHECK_EQ_I64(frame->mouse_dx, 64);

    /* More than a drain can take: the rest waits for the next frame, and none of it is lost. */
    for (uint32_t i = 0; i < 16u * 64u + 10u; i++) {
        fake_replay(mouse, one, 1u);
    }
    frame = input_poll(input);
    CHECK_EQ_I64(frame->mouse_dx, 16 * 64);
    frame = input_poll(input);
    CHECK_EQ_I64(frame->mouse_dx, 10);
    finish(t, input, fake);
}

TEST(an_empty_poll_reads_each_device_once) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    input_t *const input = start(t, fake);
    input_poll(input);
    CHECK_EQ_U64(mouse->read_calls, 1u);
    CHECK_EQ_U64(fake->watch_reads, 1u);
    finish(t, input, fake);
}

/* ---- The queue overflowing ---------------------------------------------------------------------------------------- */

TEST(after_a_drop_nothing_counts_until_the_report_and_keys_come_from_the_kernel) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);

    /* A went down normally. In the lost events it came up and D went down, which only EVIOCGKEY can say. */
    keyboard->key_state.words[KEY_D / 64u] |= 1ull << (KEY_D % 64u);
    fake_replay(keyboard, replay_drop_at_start, REPLAY_COUNT(replay_drop_at_start));
    const input_frame_t *frame = input_poll(input);
    CHECK_EQ_U64(input->events.stats.dropped, 1u);
    CHECK(input_keys_has(&frame->down, KEY_D));
    CHECK(!input_keys_has(&frame->down, KEY_A));
    CHECK(!input_keys_has(&frame->down, KEY_Z)); /* it sits after the drop, and is not to be believed */
    CHECK_EQ_I64(frame->mouse_dx, 0);
    CHECK_EQ_U64(input->discovery.borrowed_devices[0].stream.discarding, 1u);

    /* The report ends it, and asks the kernel again: by now D has come up too. */
    keyboard->key_state.words[KEY_D / 64u] = 0u;
    fake_replay(keyboard, replay_drop_at_end, REPLAY_COUNT(replay_drop_at_end));
    frame = input_poll(input);
    CHECK_EQ_U64(input->discovery.borrowed_devices[0].stream.discarding, 0u);
    CHECK(!input_keys_any(&frame->down));
    CHECK(input_keys_has(&frame->released, KEY_D));
    CHECK_EQ_I64(frame->mouse_dy, 0);
    CHECK_EQ_I64(frame->mouse_dx, 4); /* after the report, normal again */
    finish(t, input, fake);
}

TEST(a_resync_the_kernel_refuses_keeps_what_was_known_and_is_counted) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    keyboard->key_errno = EIO;
    fake_replay(keyboard, replay_drop_at_start, REPLAY_COUNT(replay_drop_at_start));
    const input_frame_t *const frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_A));
    CHECK_EQ_U64(input->events.stats.resync_failures, 1u);
    finish(t, input, fake);
}

/* ---- Hot-plug ----------------------------------------------------------------------------------------------------- */

TEST(a_device_plugged_in_starts_working_in_that_frame) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event7", "Late Keyboard", fake_caps_keyboard, false);
    input_t *const input = start(t, fake);
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    CHECK_EQ_U64(keyboard->open_count, 0u);

    plug(fake, keyboard);
    fake_replay(keyboard, replay_w_held, REPLAY_COUNT(replay_w_held));
    const input_frame_t *const frame = input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK(input_keys_has(&frame->pressed, KEY_W));
    finish(t, input, fake);
}

TEST(something_else_plugged_in_is_looked_at_and_closed) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const power = fake_add(fake, "event7", "Power", fake_caps_power_button, false);
    input_t *const input = start(t, fake);
    plug(fake, power);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    CHECK_EQ_U64(power->open_count, 1u);
    CHECK_EQ_U64(power->close_count, 1u);
    finish(t, input, fake);
}

TEST(permissions_that_arrive_after_the_node_are_waited_for) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event7", "Keyboard", fake_caps_keyboard, false);
    input_t *const input = start(t, fake);
    keyboard->open_errno = EACCES;
    plug(fake, keyboard);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    CHECK_EQ_U64(input->discovery.stats.open_failures, 1u);

    /* udev has set the group now. */
    keyboard->open_errno = 0;
    fake_record(fake, IN_ATTRIB, keyboard->file_name);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK_EQ_U64(keyboard->is_open, 1u);

    /* Another IN_ATTRIB on what is open does nothing: not a second descriptor. */
    fake_record(fake, IN_ATTRIB, keyboard->file_name);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK_EQ_U64(keyboard->open_count, 2u);
    finish(t, input, fake);
}

TEST(a_device_seen_by_both_the_listing_and_the_watch_is_opened_once) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    /* It appeared between the watch starting and the listing, so the record comes after it is already open. */
    fake_record(fake, IN_CREATE, keyboard->file_name);
    input_t *const input = start(t, fake);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK_EQ_U64(keyboard->open_count, 1u);
    finish(t, input, fake);
}

TEST(a_pulled_device_closes_lets_go_of_its_keys_and_leaves_the_others) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const first = fake_add(fake, "event0", "First", fake_caps_keyboard, true);
    fake_device_t *const second = fake_add(fake, "event1", "Second", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    static const struct input_event a_down[] = {REPLAY_EVENT(EV_KEY, KEY_A, 1)};
    static const struct input_event b_down[] = {REPLAY_EVENT(EV_KEY, KEY_B, 1)};
    fake_replay(first, a_down, 1u);
    fake_replay(first, b_down, 1u);
    fake_replay(second, b_down, 1u);
    const input_frame_t *frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_A));

    /* With a key stuck down, a pulled keyboard would walk the player off a cliff. */
    pull(fake, first);
    frame = input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK_EQ_U64(first->is_open, 0u);
    CHECK_EQ_U64(input->discovery.stats.removed, 1u);
    CHECK(!input_keys_has(&frame->down, KEY_A));
    CHECK(input_keys_has(&frame->released, KEY_A));
    /* The other keyboard still holds this one. */
    CHECK(input_keys_has(&frame->down, KEY_B));
    CHECK(!input_keys_has(&frame->released, KEY_B));
    finish(t, input, fake);
    CHECK_EQ_U64(second->close_count, 1u);
}

TEST(a_failed_read_alone_is_enough_to_lose_a_device_and_a_late_delete_is_harmless) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    fake_replay(keyboard, replay_w_held, REPLAY_COUNT(replay_w_held));
    input_poll(input);

    keyboard->read_errno = ENODEV;
    const input_frame_t *frame = input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    CHECK_EQ_U64(input->discovery.stats.lost, 1u);
    CHECK(input_keys_has(&frame->released, KEY_W));
    CHECK(!input_keys_has(&frame->down, KEY_W));

    keyboard->listed = 0u;
    fake_record(fake, IN_DELETE, keyboard->file_name);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.stats.removed, 0u);
    CHECK_EQ_U64(keyboard->close_count, 1u);
    finish(t, input, fake);
}

TEST(a_device_lost_by_reading_whose_name_comes_back_is_found_by_a_later_listing) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    const uint32_t lists_before = fake->list_calls;
    keyboard->read_errno = ENODEV;
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 0u);

    /* The records that would have said so are gone, and the node is back: only a listing can find it. */
    keyboard->read_errno = 0;
    for (uint32_t i = 0; i < INPUT_DISCOVERY_RETRY_POLLS; i++) {
        input_poll(input);
    }
    CHECK_EQ_U64(fake->list_calls - lists_before, 1u);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    /* And no more listings once it has been found. */
    for (uint32_t i = 0; i < INPUT_DISCOVERY_RETRY_POLLS * 2u; i++) {
        input_poll(input);
    }
    CHECK_EQ_U64(fake->list_calls - lists_before, 1u);
    finish(t, input, fake);
}

TEST(plugged_pulled_and_plugged_again_uses_the_same_name_cleanly) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event3", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    for (uint32_t round = 0; round < 100u; round++) {
        fake_replay(keyboard, replay_w_held, REPLAY_COUNT(replay_w_held));
        CHECK(input_keys_has(&input_poll(input)->down, KEY_W));
        pull(fake, keyboard);
        CHECK(!input_keys_has(&input_poll(input)->down, KEY_W));
        CHECK_EQ_U64(input->discovery.device_count, 0u);
        keyboard->read_errno = 0;
        plug(fake, keyboard);
        input_poll(input);
        CHECK_EQ_U64(input->discovery.device_count, 1u);
    }
    CHECK_EQ_U64(keyboard->open_count, 101u);
    CHECK_EQ_U64(keyboard->close_count, 100u);
    finish(t, input, fake);
}

TEST(records_for_other_names_do_nothing) {
    fake_evdev_t *const fake = fake_new(t);
    input_t *const input = start(t, fake);
    fake_record(fake, IN_CREATE, "mouse0");
    fake_record(fake, IN_DELETE, "event99");
    fake_record(fake, IN_CREATE, "by-id");
    input_poll(input);
    CHECK_EQ_U64(input->discovery.stats.open_failures, 0u);
    CHECK_EQ_U64(input->discovery.stats.removed, 0u);
    finish(t, input, fake);
}

TEST(an_inotify_overflow_lists_the_directory_again) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event3", "Keyboard", fake_caps_keyboard, false);
    input_t *const input = start(t, fake);
    keyboard->listed = 1u;
    fake_record(fake, IN_Q_OVERFLOW, nullptr);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK_EQ_U64(fake->list_calls, 2u);
    finish(t, input, fake);
}

TEST(records_that_do_not_fit_are_not_believed) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event3", "Keyboard", fake_caps_keyboard, false);
    input_t *const input = start(t, fake);
    keyboard->listed = 1u;

    /* A length past the end of what was read. */
    const struct inotify_event overlong = {.wd = 1, .mask = IN_CREATE, .cookie = 0u, .len = 4000u};
    memcpy(fake->records, &overlong, sizeof(overlong));
    fake->record_bytes = sizeof(overlong);
    input_poll(input);
    CHECK_EQ_U64(keyboard->open_count, 0u);

    /* A name with no terminator inside its length. */
    const struct inotify_event open_ended = {.wd = 1, .mask = IN_CREATE, .cookie = 0u, .len = 6u};
    memcpy(fake->records, &open_ended, sizeof(open_ended));
    memcpy(fake->records + sizeof(open_ended), "event3", 6u);
    fake->record_bytes = sizeof(open_ended) + 6u;
    input_poll(input);
    CHECK_EQ_U64(keyboard->open_count, 0u);

    /* Half a header. */
    memcpy(fake->records, &open_ended, 8u);
    fake->record_bytes = 8u;
    input_poll(input);
    CHECK_EQ_U64(keyboard->open_count, 0u);

    /* A name too long for the table: it cannot be one of ours, and must not overrun anything. */
    fake_record(fake, IN_CREATE, "event123456789012345678901234567890");
    input_poll(input);
    CHECK_EQ_U64(input->discovery.stats.open_failures, 0u);

    /* After all that, a good record still works. */
    fake_record(fake, IN_CREATE, keyboard->file_name);
    input_poll(input);
    CHECK_EQ_U64(keyboard->open_count, 1u);
    finish(t, input, fake);
}

TEST(a_directory_that_goes_away_and_comes_back_is_watched_again) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event3", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);

    /* Its last node goes, and devtmpfs takes the directory with it. */
    pull(fake, keyboard);
    fake_record(fake, IN_DELETE_SELF, nullptr);
    fake_record(fake, IN_IGNORED, nullptr);
    fake->watch_errno = ENOENT;
    fake->list_errno = ENOENT;
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    CHECK_EQ_I64(input->discovery.watch_fd, -1);
    CHECK_EQ_U64(input->discovery.stats.watch_losses, 1u);
    CHECK_EQ_U64(fake->watch_open, 0u);
    /* The second record went with the watch, and it may not be read from a closed descriptor. */
    const uint32_t reads = fake->watch_reads;
    input_poll(input);
    CHECK_EQ_U64(fake->watch_reads, reads);

    /* Tries, and fails, at the fixed interval. */
    const uint32_t tries_before = fake->watch_calls;
    while (input->discovery.polls % INPUT_DISCOVERY_RETRY_POLLS != 0u) {
        input_poll(input);
    }
    CHECK_EQ_U64(fake->watch_calls, tries_before + 1u);
    CHECK_EQ_I64(input->discovery.watch_fd, -1);

    /* Now it is back, with a device already in it that no record will ever announce. */
    fake->watch_errno = 0;
    fake->list_errno = 0;
    keyboard->read_errno = 0;
    keyboard->listed = 1u;
    fake->record_bytes = 0u;
    for (uint32_t i = 0; i < INPUT_DISCOVERY_RETRY_POLLS; i++) {
        input_poll(input);
    }
    CHECK(input->discovery.watch_fd >= 0);
    CHECK_EQ_U64(input->discovery.stats.watch_starts, 2u);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    finish(t, input, fake);
    CHECK_EQ_U64(fake->watch_closes, 2u);
}

TEST(a_watch_that_cannot_be_read_is_given_up_and_started_again) {
    fake_evdev_t *const fake = fake_new(t);
    input_t *const input = start(t, fake);
    fake->watch_read_errno = EIO;
    input_poll(input);
    CHECK_EQ_I64(input->discovery.watch_fd, -1);
    CHECK_EQ_I64(input->discovery.stats.last_errno, EIO);
    fake->watch_read_errno = 0;
    while (input->discovery.polls % INPUT_DISCOVERY_RETRY_POLLS != 0u) {
        input_poll(input);
    }
    CHECK(input->discovery.watch_fd >= 0);
    finish(t, input, fake);
}

/* ---- Memory ------------------------------------------------------------------------------------------------------- */

TEST(polling_never_touches_the_heap_and_everything_goes_back) {
    heap_counters_t before;
    heap_counters_read(&before);
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const keyboard = fake_add(fake, "event0", "Keyboard", fake_caps_keyboard, true);
    fake_device_t *const mouse = fake_add(fake, "event1", "Mouse", fake_caps_mouse, false);
    input_t *const input = start(t, fake);

    heap_counters_t started;
    heap_counters_read(&started);
    CHECK_EQ_U64(started.live_regions, before.live_regions + 1u);
    for (uint32_t i = 0; i < 300u; i++) {
        fake_replay(keyboard, replay_space_tap, REPLAY_COUNT(replay_space_tap));
        plug(fake, mouse);
        fake_replay(mouse, replay_mouse_moves, REPLAY_COUNT(replay_mouse_moves));
        input_poll(input);
        pull(fake, mouse);
        input_poll(input);
        mouse->read_errno = 0;
    }
    heap_counters_t polled;
    heap_counters_read(&polled);
    CHECK_EQ_U64(polled.reserve_calls, started.reserve_calls);
    CHECK_EQ_U64(polled.release_calls, started.release_calls);
    const uint64_t allocations = input->arena_setup.alloc_count;
    input_poll(input);
    CHECK_EQ_U64(input->arena_setup.alloc_count, allocations);

    finish(t, input, fake);
    heap_counters_t ended;
    heap_counters_read(&ended);
    CHECK_EQ_U64(ended.live_bytes, before.live_bytes);
    CHECK_EQ_U64(ended.live_regions, before.live_regions);
}

TEST(deinit_on_something_never_started_does_nothing) {
    input_t *const input = test_alloc(t, sizeof(input_t));
    input_deinit(input);
    input_report(input);
    CHECK_EQ_U64(input->running, 0u);
}

/* ---- The cursor --------------------------------------------------------------------------------------------------- */

TEST(the_cursor_starts_in_the_middle_follows_the_mouse_and_stops_at_the_edges) {
    input_cursor_t cursor;
    memset(&cursor, 0, sizeof(cursor));
    input_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    input_cursor_move(&cursor, &frame, 1000u, 500u);
    CHECK_EQ_I64(cursor.x, 500);
    CHECK_EQ_I64(cursor.y, 250);

    frame.mouse_dx = 12;
    frame.mouse_dy = -30;
    input_cursor_move(&cursor, &frame, 1000u, 500u);
    CHECK_EQ_I64(cursor.x, 512);
    CHECK_EQ_I64(cursor.y, 220);

    frame.mouse_dx = -100000;
    frame.mouse_dy = -100000;
    input_cursor_move(&cursor, &frame, 1000u, 500u);
    CHECK_EQ_I64(cursor.x, 0);
    CHECK_EQ_I64(cursor.y, 0);

    frame.mouse_dx = 100000;
    frame.mouse_dy = 100000;
    input_cursor_move(&cursor, &frame, 1000u, 500u);
    CHECK_EQ_I64(cursor.x, 1000);
    CHECK_EQ_I64(cursor.y, 500);
}

TEST(the_test_patterns_cursor_moves_with_a_replayed_mouse_and_esc_ends_it) {
    fake_evdev_t *const fake = fake_new(t);
    fake_device_t *const mouse = fake_add(fake, "event0", "Mouse", fake_caps_mouse, true);
    fake_device_t *const keyboard = fake_add(fake, "event1", "Keyboard", fake_caps_keyboard, true);
    input_t *const input = start(t, fake);
    uint32_t x = 0;
    uint32_t y = 0;
    bool active = true;

    CHECK(input_test_pattern_cursor(input, 1000u, 600u, &x, &y, &active));
    CHECK_EQ_U64(x, 500u);
    CHECK_EQ_U64(y, 300u);
    CHECK(!active);

    fake_replay(mouse, replay_mouse_moves, REPLAY_COUNT(replay_mouse_moves));
    CHECK(input_test_pattern_cursor(input, 1000u, 600u, &x, &y, &active));
    CHECK_EQ_U64(x, 512u);
    CHECK_EQ_U64(y, 308u);
    CHECK(!active);

    fake_replay(mouse, replay_left_down, REPLAY_COUNT(replay_left_down));
    CHECK(input_test_pattern_cursor(input, 1000u, 600u, &x, &y, &active));
    CHECK(active);
    fake_replay(mouse, replay_left_up, REPLAY_COUNT(replay_left_up));
    CHECK(input_test_pattern_cursor(input, 1000u, 600u, &x, &y, &active));
    CHECK(!active);

    fake_replay(keyboard, replay_w_held, REPLAY_COUNT(replay_w_held));
    CHECK(input_test_pattern_cursor(input, 1000u, 600u, &x, &y, &active));
    CHECK(active);

    static const struct input_event esc[] = {REPLAY_EVENT(EV_KEY, KEY_ESC, 1), REPLAY_SYN};
    fake_replay(keyboard, esc, REPLAY_COUNT(esc));
    CHECK(!input_test_pattern_cursor(input, 1000u, 600u, &x, &y, &active));
    finish(t, input, fake);
}

/* ---- The square on the test pattern ------------------------------------------------------------------------------- */

enum : uint32_t {
    CURSOR_TEST_WIDTH = 320u,
    CURSOR_TEST_HEIGHT = 200u,
    CURSOR_TEST_PITCH = CURSOR_TEST_WIDTH * 4u + 32u,
    CURSOR_TEST_GAP_FILL = 0xa5a5a5a5u,
    CURSOR_TEST_SQUARE_SIDE = 50u, /* the moving square: a quarter of the shorter side */
    CURSOR_TEST_SQUARE_COLOUR = 0x00ff8000u,
};

static display_buffer_t cursor_buffer(test_context_t *t) {
    display_buffer_t buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.width = CURSOR_TEST_WIDTH;
    buffer.height = CURSOR_TEST_HEIGHT;
    buffer.pitch = CURSOR_TEST_PITCH;
    buffer.map_bytes = (uint64_t)CURSOR_TEST_PITCH * CURSOR_TEST_HEIGHT;
    buffer.owned_map = test_alloc(t, buffer.map_bytes);
    memset(buffer.owned_map, 0xa5, buffer.map_bytes);
    display_flip_pattern_fill(&buffer);
    return buffer;
}

static uint32_t cursor_read(const display_buffer_t *buffer, uint32_t x, uint32_t y) {
    uint32_t value;
    memcpy(&value, buffer->owned_map + (uint64_t)y * buffer->pitch + (uint64_t)x * 4u, sizeof(value));
    return value;
}

/* How many pixels differ from the still picture, outside one rectangle. */
static uint64_t cursor_differences_outside(const display_buffer_t *buffer, uint32_t left, uint32_t top, uint32_t side) {
    uint64_t wrong = 0;
    for (uint32_t y = 0; y < buffer->height; y++) {
        for (uint32_t x = 0; x < buffer->width; x++) {
            const bool inside = x >= left && x < left + side && y >= top && y < top + side;
            if (!inside &&
                cursor_read(buffer, x, y) != display_flip_pattern_pixel(x, y, buffer->width, buffer->height)) {
                wrong++;
            }
        }
    }
    return wrong;
}

TEST(the_cursor_is_drawn_where_it_is_told_and_repaints_what_it_left) {
    display_buffer_t buffer = cursor_buffer(t);
    const uint32_t side = display_flip_pattern_cursor_side(&buffer);
    CHECK_EQ_U64(side, 24u);

    display_flip_pattern_cursor_draw(&buffer, 100u, 60u, false);
    CHECK_EQ_U64(cursor_differences_outside(&buffer, 100u, 60u, side), 0u);
    CHECK_EQ_U64(cursor_read(&buffer, 100u, 60u), 0x00000000u);             /* its edge */
    CHECK_EQ_U64(cursor_read(&buffer, 100u + side - 1u, 60u), 0x00000000u); /* and the far corner */
    CHECK_EQ_U64(cursor_read(&buffer, 100u + side / 2u, 60u + side / 2u), 0x00ffffffu);
    display_flip_pattern_cursor_erase(&buffer);
    CHECK_EQ_U64(cursor_differences_outside(&buffer, 0u, 0u, 0u), 0u);

    /* A held key or button shows, in the colour of nothing else on screen. */
    display_flip_pattern_cursor_draw(&buffer, 10u, 20u, true);
    CHECK_EQ_U64(cursor_read(&buffer, 10u + side / 2u, 20u + side / 2u), 0x00ff0060u);
    display_flip_pattern_cursor_erase(&buffer);
    CHECK_EQ_U64(buffer.has_cursor, 0u);
    CHECK_EQ_U64(cursor_differences_outside(&buffer, 0u, 0u, 0u), 0u);
}

TEST(the_cursor_cannot_be_drawn_off_the_buffer) {
    display_buffer_t buffer = cursor_buffer(t);
    const uint32_t side = display_flip_pattern_cursor_side(&buffer);
    display_flip_pattern_cursor_draw(&buffer, 4000000000u, 4000000000u, false);
    CHECK_EQ_U64(buffer.cursor_x, CURSOR_TEST_WIDTH - side);
    CHECK_EQ_U64(buffer.cursor_y, CURSOR_TEST_HEIGHT - side);
    CHECK_EQ_U64(cursor_read(&buffer, CURSOR_TEST_WIDTH - 1u, CURSOR_TEST_HEIGHT - 1u), 0x00000000u);
    /* Past the end of the row is the gap, which fill left alone, and so must this. */
    for (uint32_t y = 0; y < buffer.height; y++) {
        CHECK_EQ_U64(cursor_read(&buffer, buffer.width, y), CURSOR_TEST_GAP_FILL);
    }
    /* A filled buffer has no cursor to erase. */
    display_flip_pattern_fill(&buffer);
    CHECK_EQ_U64(buffer.has_cursor, 0u);
}

TEST(the_cursor_stays_on_top_of_the_moving_square_and_leaves_no_trail_on_it) {
    display_buffer_t buffer = cursor_buffer(t);
    const uint32_t side = display_flip_pattern_cursor_side(&buffer);
    /* In the order the loop does it, with the cursor parked across the square's path. */
    const uint32_t cursor_y = (CURSOR_TEST_HEIGHT - CURSOR_TEST_SQUARE_SIDE) / 2u;
    for (uint64_t frame = 0; frame < 40u; frame++) {
        display_flip_pattern_cursor_erase(&buffer);
        display_flip_pattern_frame(&buffer, frame);
        display_flip_pattern_cursor_draw(&buffer, 60u, cursor_y, false);
        CHECK_EQ_U64(cursor_read(&buffer, 60u + side / 2u, cursor_y + side / 2u), 0x00ffffffu);
    }
    /* The square has gone right through it: what is left is the square where it is and the cursor where it is. */
    display_flip_pattern_cursor_erase(&buffer);
    CHECK(buffer.square_x > 60u + side);
    CHECK_EQ_U64(cursor_read(&buffer, buffer.square_x, buffer.square_y), CURSOR_TEST_SQUARE_COLOUR);
    CHECK_EQ_U64(cursor_differences_outside(&buffer, buffer.square_x, buffer.square_y, CURSOR_TEST_SQUARE_SIDE), 0u);
}

/* ---- The real calls, on nothing that is a device ------------------------------------------------------------------ */

typedef struct real_directory_t {
    char path[256];
} real_directory_t;
static_assert(sizeof(real_directory_t) == 256);

static bool real_directory_make(real_directory_t *directory) {
    const char *const base = getenv("TMPDIR");
    snprintf(directory->path, sizeof(directory->path), "%s/run_input_XXXXXX", base != nullptr ? base : "/tmp");
    return mkdtemp(directory->path) != nullptr;
}

static void real_path(const real_directory_t *directory, const char *name, char *out, uint64_t room) {
    snprintf(out, room, "%s/%s", directory->path, name);
}

static void real_remove(const real_directory_t *directory, const char *name) {
    char path[320];
    real_path(directory, name, path, sizeof(path));
    unlink(path);
}

/* The ioctl of a keyboard and mouse for a FIFO, since a FIFO has no evdev to ask. Everything else stays real, so this drives
 * the real inotify, directory listing, open, read and close. A regular file still gets the real ioctl, which says ENOTTY. */
static int32_t hybrid_ioctl_call(void *context, int32_t fd, uint64_t request, void *argument) {
    struct stat status;
    if (fstat(fd, &status) != 0 || !S_ISFIFO(status.st_mode)) {
        return input_ops_real()->ioctl_call(context, fd, request, argument);
    }
    const uint64_t size_mask = (uint64_t)_IOC_SIZEMASK << _IOC_SIZESHIFT;
    const uint64_t without_size = request & ~size_mask;
    const uint64_t room = _IOC_SIZE(request);
    input_capabilities_t caps;
    memset(&caps, 0, sizeof(caps));
    fake_caps_keyboard(&caps);
    fake_caps_mouse(&caps);
    if (without_size == (EVIOCGBIT(0, 0) & ~size_mask)) {
        return fake_copy(argument, room, caps.event_types, sizeof(caps.event_types));
    }
    if (without_size == (uint64_t)EVIOCGBIT(EV_KEY, 0)) {
        return fake_copy(argument, room, caps.keys, sizeof(caps.keys));
    }
    if (without_size == (uint64_t)EVIOCGBIT(EV_REL, 0)) {
        return fake_copy(argument, room, caps.relative_axes, sizeof(caps.relative_axes));
    }
    if (without_size == (uint64_t)EVIOCGNAME(0)) {
        return fake_copy(argument, room, "FIFO", sizeof("FIFO"));
    }
    return -EINVAL;
}

/* Makes a FIFO named name in the directory and returns the writing end, held open so that the reader sees no end of file. */
static int32_t real_fifo(const real_directory_t *directory, const char *name) {
    char path[320];
    real_path(directory, name, path, sizeof(path));
    if (mkfifo(path, 0600) != 0) {
        return -1;
    }
    return open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
}

TEST(the_real_calls_reject_what_is_not_a_device_and_leave_it_closed) {
    real_directory_t directory;
    REQUIRE(real_directory_make(&directory));
    char file[320];
    real_path(&directory, "event1", file, sizeof(file));
    const int32_t made = open(file, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    REQUIRE(made >= 0);
    close(made);

    input_t *const input = test_alloc(t, sizeof(input_t));
    REQUIRE(input_init(input, directory.path, input_ops_real(), nullptr));
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    CHECK_EQ_U64(input->discovery.stats.rejected, 1u); /* ENOTTY: a regular file is not an evdev device */
    CHECK(input->discovery.watch_fd >= 0);

    /* One made later, with the real inotify saying so. */
    real_path(&directory, "event2", file, sizeof(file));
    const int32_t later = open(file, O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    REQUIRE(later >= 0);
    close(later);
    input_poll(input);
    CHECK(input->discovery.stats.rejected >= 2u);
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    input_deinit(input);

    real_remove(&directory, "event1");
    real_remove(&directory, "event2");
    rmdir(directory.path);
}

TEST(the_real_calls_refuse_a_missing_directory_without_failing) {
    input_t *const input = test_alloc(t, sizeof(input_t));
    REQUIRE(input_init(input, "/nonexistent/run_input", input_ops_real(), nullptr));
    CHECK_EQ_I64(input->discovery.watch_fd, -1);
    CHECK_EQ_I64(input->discovery.stats.last_errno, ENOENT);
    CHECK_EQ_I64(input_ops_real()->open_device(nullptr, "/nonexistent/run_input/event0"), -ENOENT);
    input_deinit(input);
}

TEST(replay_through_real_inotify_and_real_reads_covers_plugging_and_pulling) {
    real_directory_t directory;
    REQUIRE(real_directory_make(&directory));
    input_ops_t ops = *input_ops_real();
    ops.ioctl_call = hybrid_ioctl_call;

    /* One device from the start, found by the real listing. */
    const int32_t first = real_fifo(&directory, "event0");
    REQUIRE(first >= 0);
    input_t *const input = test_alloc(t, sizeof(input_t));
    REQUIRE(input_init(input, directory.path, &ops, nullptr));
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK_STR_EQ(input->discovery.borrowed_devices[0].label, "FIFO");

    /* Bytes through a real pipe, exactly as a read of evdev gets them. */
    CHECK_EQ_I64(write(first, replay_w_held, sizeof(replay_w_held)), (int64_t)sizeof(replay_w_held));
    CHECK_EQ_I64(write(first, replay_mouse_moves, sizeof(replay_mouse_moves)), (int64_t)sizeof(replay_mouse_moves));
    const input_frame_t *frame = input_poll(input);
    CHECK(input_keys_has(&frame->down, KEY_W));
    CHECK_EQ_I64(frame->mouse_dx, 12);

    /* Nothing there: a non-blocking read comes back empty and the frame does not wait. */
    frame = input_poll(input);
    CHECK_EQ_I64(frame->mouse_dx, 0);
    CHECK(input_keys_has(&frame->down, KEY_W));

    /* Plugged in later, seen by the real inotify. */
    const int32_t second = real_fifo(&directory, "event1");
    REQUIRE(second >= 0);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 2u);
    CHECK_EQ_I64(write(second, replay_left_down, sizeof(replay_left_down)), (int64_t)sizeof(replay_left_down));
    frame = input_poll(input);
    CHECK(input_keys_has(&frame->pressed, BTN_LEFT));

    /* Pulled: the real inotify says the node went, and its keys are let go. */
    real_remove(&directory, "event0");
    frame = input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    CHECK(input_keys_has(&frame->released, KEY_W));

    /* A move into the directory counts as appearing, and out of it as going. */
    char outside[320];
    char inside[320];
    real_path(&directory, "event5", inside, sizeof(inside));
    snprintf(outside, sizeof(outside), "%s.moved", directory.path);
    CHECK_EQ_I64(mkfifo(outside, 0600), 0);
    CHECK_EQ_I64(rename(outside, inside), 0);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 2u);
    CHECK_EQ_I64(rename(inside, outside), 0);
    input_poll(input);
    CHECK_EQ_U64(input->discovery.device_count, 1u);
    unlink(outside);

    /* The directory goes and comes back: the real watch ends, and starts again. Only once nothing is open: an open file pins
     * its dentry and so its parent, and the kernel says IN_DELETE_SELF when the last of those goes, not at the rmdir. */
    close(first);
    close(second);
    real_remove(&directory, "event1");
    CHECK_EQ_I64(rmdir(directory.path), 0);
    input_poll(input);
    CHECK_EQ_I64(input->discovery.watch_fd, -1);
    CHECK_EQ_U64(input->discovery.device_count, 0u);
    CHECK_EQ_I64(mkdir(directory.path, 0700), 0);
    const int32_t third = real_fifo(&directory, "event9");
    REQUIRE(third >= 0);
    for (uint32_t i = 0; i < INPUT_DISCOVERY_RETRY_POLLS; i++) {
        input_poll(input);
    }
    CHECK(input->discovery.watch_fd >= 0);
    CHECK_EQ_U64(input->discovery.device_count, 1u);

    input_deinit(input);
    close(third);
    real_remove(&directory, "event9");
    rmdir(directory.path);
}

/* ---- The built program -------------------------------------------------------------------------------------------- */

/* stdin is /dev/null, so display_device_check_console refuses, and input_init must not have been reached: input_init logs
 * "input:" for what it finds, even if it finds nothing. */
TEST(a_refused_test_pattern_never_reaches_the_input_system) {
    REQUIRE(t->arg_count == 1);
    char *const argv[] = {t->borrowed_args[0], (char *)"--test-pattern", (char *)"--card",
                          (char *)"/nonexistent/run_input/card", nullptr};
    test_process_t result;
    test_process_run(t, argv, 30000u, &result);
    REQUIRE(result.started != 0u);
    CHECK_EQ_I64(result.exit_code, 6);
    CHECK(strstr(result.output, "input:") == nullptr);
}

TEST(the_other_modes_of_the_program_never_reach_the_input_system) {
    REQUIRE(t->arg_count == 1);
    char *const report[] = {t->borrowed_args[0], (char *)"--report", nullptr};
    test_process_t result;
    test_process_run(t, report, 30000u, &result);
    REQUIRE(result.started != 0u);
    CHECK_EQ_I64(result.exit_code, 0);
    CHECK(strstr(result.output, "input:") == nullptr);

    char *const plain[] = {t->borrowed_args[0], nullptr};
    test_process_run(t, plain, 60000u, &result);
    REQUIRE(result.started != 0u);
    CHECK_EQ_I64(result.exit_code, 0);
    CHECK(strstr(result.output, "input:") == nullptr);
}

TEST_MAIN()
