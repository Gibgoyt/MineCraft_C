# run/src/input/events

**Purpose:** reads raw evdev events from one open device, and adds them to the frame all devices share.
**Tier:** Arena: takes `input_frame_t` and the per-code holder counts from the arena `input` gives it at init.
**Owns:** nothing. The memory is `input`'s, and each descriptor is `input/discovery`'s.
**Lifetime:** Process.
**Threads:** main. Never logs, since it runs while the console is dark: failures go in `input_events_stats_t`.
**Public functions:**

- `input_events_init`, `input_events_deinit`: a pair.
- `input_events_begin_frame`: clears edges and movement, keeps what is held.
- `input_events_stream_start`: one device, holding nothing yet.
- `input_events_drain`: reads what a device has into the frame; a negative errno means the device is gone.
- `input_events_stream_release`: lets go of everything a vanished device held.
- `input_keys_has`, `input_keys_any`: read a key set.

**May be called by:** `input`, `input/discovery`, and whoever reads a frame.
**Depends on:** `input/ops`, `arena`.

## What it decides

- Two keyboards can hold the same key. A code is down while any device holds it, `pressed` means the first one took it, and
  `released` means the last one let go.
- A press and release between two polls shows in `pressed` and `released`, and not in `down`.
- Auto-repeat (`value == 2`) goes in `repeated` and changes nothing else. Pressing what is held, and releasing what is not, do
  nothing, so no sequence of events can unbalance the holder counts.
- `SYN_DROPPED`: everything up to the next `SYN_REPORT` is thrown away, and key state comes from `EVIOCGKEY`, asked once the read
  holding the drop or the report has been taken, because its answer is newer than anything already read. Mouse movement lost in a drop stays lost.
- Movement is raw counts, with no acceleration and no scaling. That is for whoever reads the frame to decide.
- Absolute axes are ignored, so a touchpad does not work: `input/discovery` does not even keep one open.
