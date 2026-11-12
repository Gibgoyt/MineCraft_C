# run/src/ui/command

**Purpose:** the look of plan 006 phase 01's command box, and the panel the F3 tuning lines are drawn on. The box is a
`ui_widget_text_field` across the bottom of the picture, always focused while up, with a red line over it for a bad command, a
line for what the text means or what a command said, and `/help`'s lines over those. `ui_command_keys` takes what
`ui_widget_input_feed` put in the widget's input (printable ASCII, so `- . / ~` and digits, Backspace and Enter) into the box's
text, and clears it, so nothing else sees it.
**Tier:** Arena by depth; a Borrower in fact: it allocates nothing, draws on the canvas and widget it is handed and holds no state; what to show comes in a
`ui_command_view_t`.
**Owns:** nothing.
**Lifetime:** none.
**Threads:** main's, between `ui_frame_begin*` and `ui_frame_end`, and `ui_command_keys` after `ui_widget_input_feed`.

**Public functions:** `ui_command_keys`, `ui_command_draw`, `ui_command_lines_draw` (lines on a dark panel at a place, at the F3
overlay's scale, as wide as the longest).

**May be called by:** `run/src/game/command/` (the box), `run/src/game/` (`game_hud_compose`, the tuning lines under F3), tests.
**Depends on:** `ui/widget` (the text field and its input), `ui/compose` (fills and rects), `ui/font` (text).
