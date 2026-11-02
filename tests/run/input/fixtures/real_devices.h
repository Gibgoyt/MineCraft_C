/* What this machine's own input devices say they can do, for ctest run_input: the EV, KEY and REL lines of
 * /proc/bus/input/devices, copied on 2026-11-02 under Linux 7.2.7. That is a text file anyone can read: no device was opened
 * for it. It prints the same bitmaps EVIOCGBIT answers, as hex words with the highest first and leading zero words left out.
 * Included once, by ../index.c. */
#ifndef MC_TESTS_RUN_INPUT_FIXTURES_REAL_DEVICES_H
#define MC_TESTS_RUN_INPUT_FIXTURES_REAL_DEVICES_H

#include "input/index.h"

#include <stdint.h>

typedef struct real_device_t {
    const char *file_name;
    const char *label;
    const char *event_types;
    const char *keys;
    const char *relative_axes;
    uint8_t expected_kinds;
    uint8_t _pad0[7];
} real_device_t;
static_assert(sizeof(real_device_t) == 48);

#define REAL_NOT_ONE 0u
#define REAL_G710_KEYS "1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe"
#define REAL_POWER_KEYS "8000 10000000000000 0"
#define REAL_VIDEO_KEYS "3e000b00000000 0 0 0"

static const real_device_t real_devices[] = {
    {"event0", "Lid Switch", "21", "0", "0", REAL_NOT_ONE, {0}},
    {"event1", "Power Button", "3", REAL_POWER_KEYS, "0", REAL_NOT_ONE, {0}},
    {"event2", "Power Button", "3", REAL_POWER_KEYS, "0", REAL_NOT_ONE, {0}},
    {"event3",
     "AT Translated Set 2 keyboard",
     "120013",
     "20000000001f0000 0 40000 0 0 0 0 402000007 f780307af830d001 feffffdfffcfffff fffffffffffffffe",
     "0",
     INPUT_KIND_KEYBOARD,
     {0}},
    {"event4", "Logitech Logitech G710 Keyboard", "120013", REAL_G710_KEYS, "0", INPUT_KIND_KEYBOARD, {0}},
    {"event5",
     "Logitech Logitech G710 Keyboard",
     "100013",
     "1000000000007 ff980078000007ff febeffdfffefffff fffffffffffffffe",
     "0",
     INPUT_KIND_KEYBOARD,
     {0}},
    {"event6", "Razer Razer Basilisk V2", "17", "1f0000 0 0 0 0", "903", INPUT_KIND_MOUSE, {0}},
    {"event7",
     "Razer Razer Basilisk V2 Keyboard",
     "10001f",
     "733eff 0 0 483ffff17aff32d bfd4444600000000 1 130c730b17c007 ffbf7bfad941dfff febeffdfffefffff fffffffffffffffe",
     "1040",
     INPUT_KIND_KEYBOARD,
     {0}},
    {"event8", "Razer Razer Basilisk V2", "9", "0", "0", REAL_NOT_ONE, {0}},
    {"event9", "Razer Razer Basilisk V2", "120013", REAL_G710_KEYS, "0", INPUT_KIND_KEYBOARD, {0}},
    {"event10",
     "DELL Technologies Keyboard",
     "12001f",
     "733eff 0 0 483ffff17aff32d bfd4444600000000 1 130c730b17c007 ffbf7bfad9415fff febeffdfffefffff fffffffffffffffe",
     "1040",
     INPUT_KIND_KEYBOARD,
     {0}},
    {"event11", "Intel HID events", "13", "81000300000000 5000004000 1e294000000020 0", "0", REAL_NOT_ONE, {0}},
    {"event12", "Video Bus", "3", REAL_VIDEO_KEYS, "0", REAL_NOT_ONE, {0}},
    {"event13", "Video Bus", "3", REAL_VIDEO_KEYS, "0", REAL_NOT_ONE, {0}},
    {"event14", "PC Speaker", "40001", "0", "0", REAL_NOT_ONE, {0}},
    {"event15",
     "Dell WMI hotkeys",
     "13",
     "c0000 0 0 800000000000 0 0 101500b00000c00 200300000 2000000000000 0",
     "0",
     REAL_NOT_ONE,
     {0}},
    {"event16", "Synaptics TM3394-001", "b", "e520 30000 0 0 0 0", "0", REAL_NOT_ONE, {0}},
    {"event17", "HDA NVidia HDMI/DP,pcm=3", "21", "0", "0", REAL_NOT_ONE, {0}},
    {"event18", "HDA NVidia HDMI/DP,pcm=7", "21", "0", "0", REAL_NOT_ONE, {0}},
    {"event19", "HDA NVidia HDMI/DP,pcm=8", "21", "0", "0", REAL_NOT_ONE, {0}},
    {"event20", "HDA NVidia HDMI/DP,pcm=9", "21", "0", "0", REAL_NOT_ONE, {0}},
};

#endif
