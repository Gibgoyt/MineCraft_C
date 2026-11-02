# run/src/display/headless/script

**Purpose:** the input script language: text to a list of timed key, mouse and screenshot events, and the held-keys state those events add up to.
**Tier:** Borrow. Allocates nothing: the caller gives it the text and the event array.
**Owns:** nothing.
**Lifetime:** The caller's. Events point into the text, so the text must outlast them.
**Threads:** one at a time per script.
**Public functions:**

- `display_script_parse`: text to events. Every failure names the line and says what it wanted.
- `display_script_screenshot_name`: the file name a screenshot event writes.
- `display_script_state_begin_tick`, `display_script_state_apply`, `display_script_state_is_down`: the keyboard and mouse so far.

**May be called by:** `display/headless`, `display/headless/demo`, and scenes reading the state they are handed.
**Depends on:** libc, `linux/input-event-codes.h` for the `KEY_*` and `BTN_*` numbers.

The language itself is documented for its users in `run/src/display/headless/README.md`.
