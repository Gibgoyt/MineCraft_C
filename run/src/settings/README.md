# run/src/settings

**Purpose:** every setting a person can change, and keeping them between runs: render distance from 2 to 512 chunks, how far of that is drawn block by block, field of view, mouse sensitivity, vsync or uncapped, whether coordinates show, each of the seventeen graphics effects, render scale and worker count.
**Tier:** Owner.
**Owns:** `owned_region`, with `arena` over it, which holds the path and one file's worth of text. Both live as long as the `settings_t`.
**Lifetime:** Process.
**Threads:** `main`.
**Public functions** (`run/include/settings/index.h`):

- `settings_init`, `settings_deinit`: a pair. Deinit is safe on a zeroed `settings_t`.
- `settings_load`: reads the file over the defaults, and remembers where it is.
- `settings_save`: writes it, whole or not at all.

**May be called by:** `run/src/main/`, which loads, and `run/src/game/session/`, which saves when a person leaves the screen. `run/src/ui/screen_settings/` changes the values and calls nothing here.
**Depends on:** `heap`, `arena`, `file`, `log`, and its part `persist`.

**Parts:** `persist`: the values, their ranges, and the file.

## Who does what with a setting

`settings_t.values` is all this holds: `run/src/ui/screen_settings/` changes them, `run/src/game/session/` turns them into a `game_config_t` and hands that to `game_reconfigure`, and this keeps them.

| Setting | Takes effect |
|---|---|
| Render distance | At once, with no restart and in the world that is open: nearer than full detail by the map's view, and past it by a far field made anew, which fills in over about 0.4 s of a worker for each coarse column in a release build. See `run/src/game/README.md` |
| Full detail | At once, up to what the open world's table was sized for, which is what full detail was when it opened. Further is on the screen as taking a new world: the table and the slab pool on the card are sized once, and what is in them is sized for the world as it opened |
| Field of view, sensitivity | At once |
| Each effect | At once |
| Render scale | At once: new pictures on the card, brought up to the display's size. See `run/src/game/README.md` |
| Vsync or uncapped | At once. Uncapped is a flip that does not wait for a vblank, if the driver allows it, and otherwise one that does; the gpu thread stops waiting for a picture to be taken |
| Coordinates | At once: nothing on the card is touched, and `main` draws them or not on the next frame |
| Worker count | At the next start, and the screen says so. Workers start before anything else that uses them, and everything that uses them keeps jobs on them |

## The file

`settings.yaml` beside `saves`, unless `--settings` says otherwise: see `run/src/settings/persist/README.md` for what is in it. It is read before `job_init`, for the worker count, and only for `--menu`: `--test-world` and the others run on their flags, so that no file can change what a test sees. Flags given with `--menu` go over what the file says, and are kept if a person then changes anything on the screen.
