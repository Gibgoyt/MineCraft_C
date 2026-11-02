# run/src/display/mode

**Purpose:** decides which connector, mode and CRTC to use, and remembers what the CRTC held so it can go back.
**Tier:** Arena: allocates its connector list and a scratch mode list from the arena given to `display_mode_init`.
**Owns:** nothing; those allocations last as long as the arena's owner, `display`, keeps it.
**Lifetime:** Process.
**Threads:** main.
**Public functions:**

- `display_mode_refresh_mhz`, `display_mode_is_usable`, `display_mode_tier`, `display_mode_is_better`, `display_mode_pick`,
  `display_mode_connector_name`, `display_mode_pick_connector`: pure, tested on fixtures.
- `display_mode_init`, `display_mode_deinit`: a pair. Asks the card and picks. Changes nothing on the card.

**May be called by:** `display`, `display/flip`.
**Depends on:** `display/device`, `arena`, `log`.

## How a mode is picked

Refresh comes from the timings, since `vrefresh` is rounded. Interlaced and stereo modes are never used.

| Tier | The wanted size | The wanted refresh, or within 1 Hz below it |
|---|---|---|
| 4 | Yes | Yes |
| 3 | No | Yes |
| 2 | Yes | No |
| 1 | No | No |

The tier order is the KMS spike's (Stage 01.2), so that its run and this one choose alike. Within a tier that meets the
refresh, the nearest refresh wins, which is where this differs from the spike: a 240 Hz panel runs at the 120 the yaml asks
for. Within a tier that does not, the fastest wins. Then the mode the monitor prefers, then the larger area.

`connector: auto` compares each connected connector's best mode by the same rule, the earliest winning a tie. Any other
value must be a connected connector's name, as in `/sys/class/drm`: `HDMI-A-1`, `eDP-1`. If it is not there the program
stops; it does not fall back to another monitor.

## How a CRTC is picked

The one already driving the connector, so the restore puts back exactly what was there. Otherwise a free one the connector's
encoders allow, so another monitor keeps its picture. Otherwise the first one they allow. Every connected connector on that
CRTC goes back on it at the restore, not only the one we drive.
