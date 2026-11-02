# run/src/input/discovery

**Purpose:** finds keyboards and mice in `/dev/input` by what they say they can do, and follows them as they are plugged and pulled.
**Tier:** Arena: takes its fixed table of 32 devices from the arena `input` gives it at init.
**Owns:** the descriptor of every open device and of the inotify watch. The memory is `input`'s.
**Lifetime:** Process.
**Threads:** main. Never logs, since it runs while the console is dark: what happens goes in `input_discovery_stats_t`.
**Public functions:**

- `input_discovery_name_is_event`: whether a directory entry can be an evdev node.
- `input_discovery_classify`: keyboard, mouse, both or neither, from `EVIOCGBIT`'s bitmaps.
- `input_discovery_init`, `input_discovery_deinit`: a pair. `_init` starts the watch, then lists the directory.
- `input_discovery_poll`: takes in what the directory says has changed.
- `input_discovery_lost`: closes a device that a read said is gone.

**May be called by:** `input`.
**Depends on:** `input/ops`, `input/events` (its sibling, for each device's stream and for letting go of a vanished device's keys),
`arena`.

## Hot-plug

| What happens | How it is seen | What is done |
|---|---|---|
| A node appears | `IN_CREATE`, `IN_MOVED_TO` | Open it, ask what it is, keep or close it |
| udev sets its permissions, after the node appears | `EACCES` on `IN_CREATE`, then `IN_ATTRIB` | Try again |
| A device is pulled | A read says `ENODEV`, usually before `IN_DELETE` | Close it, let go of its keys, and list the directory within 120 polls, in case its name came back |
| A node goes | `IN_DELETE`, `IN_MOVED_FROM` | The same, if a read has not done it already |
| The inotify queue overflows | `IN_Q_OVERFLOW` | List the directory now |
| The directory goes: devtmpfs removes `/dev/input` with its last node | `IN_DELETE_SELF`, `IN_IGNORED`, or it was never there | Try to watch it again every 120 polls, listing it when that works |

The watch starts before the first listing, so that nothing that appears between the two is missed; what comes up twice is opened once.
