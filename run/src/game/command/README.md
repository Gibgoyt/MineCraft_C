# run/src/game/command

**Purpose:** plan 006 phase 01's command box, so Ahmed can tune the climate and the biomes by eye: `T` or `/` opens a one-line
box over the running game (the Atlas's pattern: the world goes on), Enter runs what was typed, Esc closes it, and Up and Down
bring back the last 16 commands. It parses `/tp X Y Z`, `/tp ~DX ~DY ~DZ` (any mix), `/tp X Y`, `/tp lat DEG [X]`, `/tp top`,
`/fly`, `/speed N` and `/speed`, `/time day N`, `/time hour H` and `/help`, shows the latitude a `/tp lat` goes to before
Enter ("y 25532 · 60.0° N"), and a red line for anything bad, which changes nothing. Teleports, fly mode and the speed factor go
to the sim through the player's mailbox (`player_command_send`); the clock is main's, and `/time` sets it here, only ever
forward, so the weekly census runs for every week passed, as sleeping does. It also makes the HUD's flying line.
**Tier:** Arena by depth; it allocates nothing. Its state, `game_command_t`, is in the World arena beside the column jobs
(`game_command_of`, in `run/src/game/index.c`), so the history lasts as long as the world is open.
**Owns:** nothing.
**Lifetime:** a world's: `game_command_start` from `game_open`.
**Threads:** main's alone. The sim never reads it: what crosses goes through `player_t`'s mailbox.

**Public functions:**

- `game_command_parse`: pure: one command's text to a `game_command_parsed_t`, or false and why. Whole numbers across are a
  block's middle (`/tp 10 20 30` puts the feet at 10.5, 20.5, 30); `~` is a step from the feet, which the sim adds to its own
  position, so `/tp ~ ~ ~300` keeps x and y to the bit. Places are held inside `PLAYER_COMMAND_PLACE_MOST` across and the
  gameplay heights.
- `game_command_lat_y`: y from a latitude: `DEG / 90 x CLIMATE_POLE_Y`, north positive.
- `game_command_preview`: the line over the box for what is being typed.
- `game_command_history_push`, `game_command_history_back`: the 16 commands, round.
- `game_command_help_lines`: `/help`'s lines.
- `game_command_start`, `game_command_input`, `game_command_up`, `game_command_run`, `game_command_draw`: the box.
- `game_command_flight_line`: "Flying · 116 b/s", "Flying · 580 b/s · speed 5x" (the speed shown is what it flies at), or
  "Speed 5x" walking, for the HUD's top right corner.

| Command | What happens |
|---|---|
| `/tp X Y Z` | `PLAYER_COMMAND_TP_EXACT`: the feet there; out of rock to the first room above |
| `/tp ~DX ~DY ~DZ` | The same, each `~` axis a step from the feet |
| `/tp X Y` | `PLAYER_COMMAND_TP_GROUND`: on the top ground at X Y once the column loads |
| `/tp lat DEG [X]` | The same at y from the latitude, x kept or given; x is not longitude |
| `/tp top` | The same where it stands |
| `/fly` | `PLAYER_COMMAND_FLY`: fly mode on or off |
| `/speed N` | `PLAYER_COMMAND_SPEED`: walking and flying N times as fast, 1 to 10; `/speed` alone says what it is |
| `/time day N`, `/time hour H` | On to day N of the year (1 to 365, as the HUD shows it), or to H o'clock |
| `/help` | The list, over the box |

A teleport is refused, with a red line, for a body that is dead or asleep, and while the last one is still in the mailbox. In a
test world, which has no body, `/tp` moves the flying eye itself (onto the estimated ground for `X Y`, `lat` and `top`), and
`/fly` and `/speed` say that the eye always flies.

**May be called by:** `run/src/game/input/` (`game_screen_input`, `game_screen_up`), `run/src/game/` (`game_open`,
`game_hud_compose`), `run/src/game/corner/` (the flying line), tests.
**Depends on:** `game`'s header, `game/atlas` (the box opens only when the Atlas is down), `game/fill` (a test world's jump asks
for its fill), `player` (the mailbox and the flight word), `ui` and `ui/command` (the look), `calendar`, `climate` (latitude),
`terrain` (the ground estimate for a flying eye), `world/column`, `input`, `log`.
