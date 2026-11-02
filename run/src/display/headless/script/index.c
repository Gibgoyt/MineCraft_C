/* See README.md. A strict little parser: anything it does not understand is an error naming the line, because a script
 * that quietly did something else would make a screenshot show the wrong thing and nobody would know why. */
#include "display/headless/script/index.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    SCRIPT_MAX_TOKENS = 5u,
    SCRIPT_MOVE_LIMIT = 1000000u,
    SCRIPT_WHEEL_LIMIT = 1000u,
    SCRIPT_MS_PER_SECOND = 1000u,
    SCRIPT_CODE_PREFIX_BYTES = 5u, /* code: */
};

typedef struct script_token_t {
    const char *start;
    uint32_t length;
    uint8_t _pad0[4];
} script_token_t;
static_assert(sizeof(script_token_t) == 16);

typedef struct script_key_t {
    char name[14];
    uint16_t code;
} script_key_t;
static_assert(sizeof(script_key_t) == 16);

static const script_key_t script_keys[] = {
    {"a", KEY_A},
    {"b", KEY_B},
    {"c", KEY_C},
    {"d", KEY_D},
    {"e", KEY_E},
    {"f", KEY_F},
    {"g", KEY_G},
    {"h", KEY_H},
    {"i", KEY_I},
    {"j", KEY_J},
    {"k", KEY_K},
    {"l", KEY_L},
    {"m", KEY_M},
    {"n", KEY_N},
    {"o", KEY_O},
    {"p", KEY_P},
    {"q", KEY_Q},
    {"r", KEY_R},
    {"s", KEY_S},
    {"t", KEY_T},
    {"u", KEY_U},
    {"v", KEY_V},
    {"w", KEY_W},
    {"x", KEY_X},
    {"y", KEY_Y},
    {"z", KEY_Z},
    {"0", KEY_0},
    {"1", KEY_1},
    {"2", KEY_2},
    {"3", KEY_3},
    {"4", KEY_4},
    {"5", KEY_5},
    {"6", KEY_6},
    {"7", KEY_7},
    {"8", KEY_8},
    {"9", KEY_9},
    {"f1", KEY_F1},
    {"f2", KEY_F2},
    {"f3", KEY_F3},
    {"f4", KEY_F4},
    {"f5", KEY_F5},
    {"f6", KEY_F6},
    {"f7", KEY_F7},
    {"f8", KEY_F8},
    {"f9", KEY_F9},
    {"f10", KEY_F10},
    {"f11", KEY_F11},
    {"f12", KEY_F12},
    {"space", KEY_SPACE},
    {"enter", KEY_ENTER},
    {"escape", KEY_ESC},
    {"tab", KEY_TAB},
    {"backspace", KEY_BACKSPACE},
    {"delete", KEY_DELETE},
    {"insert", KEY_INSERT},
    {"home", KEY_HOME},
    {"end", KEY_END},
    {"pageup", KEY_PAGEUP},
    {"pagedown", KEY_PAGEDOWN},
    {"up", KEY_UP},
    {"down", KEY_DOWN},
    {"left", KEY_LEFT},
    {"right", KEY_RIGHT},
    {"lshift", KEY_LEFTSHIFT},
    {"rshift", KEY_RIGHTSHIFT},
    {"lctrl", KEY_LEFTCTRL},
    {"rctrl", KEY_RIGHTCTRL},
    {"lalt", KEY_LEFTALT},
    {"ralt", KEY_RIGHTALT},
    {"capslock", KEY_CAPSLOCK},
    {"minus", KEY_MINUS},
    {"equal", KEY_EQUAL},
    {"comma", KEY_COMMA},
    {"dot", KEY_DOT},
    {"slash", KEY_SLASH},
    {"semicolon", KEY_SEMICOLON},
    {"apostrophe", KEY_APOSTROPHE},
    {"grave", KEY_GRAVE},
    {"leftbrace", KEY_LEFTBRACE},
    {"rightbrace", KEY_RIGHTBRACE},
    {"backslash", KEY_BACKSLASH},
};

static const script_key_t script_buttons[] = {
    {"left", BTN_LEFT}, {"right", BTN_RIGHT}, {"middle", BTN_MIDDLE}, {"side", BTN_SIDE}, {"extra", BTN_EXTRA},
};

typedef struct script_parser_t {
    display_script_t *out;
    display_script_event_t *events;
    display_script_error_t *error;
    uint32_t capacity;
    uint32_t tick_hz;
    uint32_t line;
    uint32_t previous_tick;
} script_parser_t;
static_assert(sizeof(script_parser_t) == 40);

[[gnu::format(printf, 2, 3)]] static bool script_fail(script_parser_t *parser, const char *format, ...) {
    parser->error->line = parser->line;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(parser->error->message, sizeof(parser->error->message), format, arguments);
    va_end(arguments);
    return false;
}

static bool script_token_is(const script_token_t *token, const char *word) {
    const uint64_t length = strlen(word);
    return length == token->length && memcmp(token->start, word, length) == 0;
}

static char script_lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

/* Digits only, up to limit: value stays below limit * 10 + 9, so with the limits used here no sum can overflow. */
static bool script_digits(const char *start, uint32_t length, uint64_t limit, uint64_t *out) {
    if (length == 0u) {
        return false;
    }
    uint64_t value = 0;
    for (uint32_t i = 0; i < length; i++) {
        if (start[i] < '0' || start[i] > '9') {
            return false;
        }
        value = value * 10u + (uint64_t)(start[i] - '0');
        if (value > limit) {
            return false;
        }
    }
    *out = value;
    return true;
}

static bool script_signed(const script_token_t *token, uint32_t limit, int32_t *out) {
    const bool negative = token->length > 0u && token->start[0] == '-';
    const uint32_t skip = token->length > 0u && (token->start[0] == '-' || token->start[0] == '+') ? 1u : 0u;
    uint64_t value = 0;
    if (!script_digits(token->start + skip, token->length - skip, limit, &value)) {
        return false;
    }
    *out = negative ? -(int32_t)value : (int32_t)value;
    return true;
}

/* N, +N, Nms or +Nms. */
static bool script_parse_when(script_parser_t *parser, const script_token_t *token, uint32_t *out_tick) {
    const bool relative = token->start[0] == '+';
    const char *const digits = token->start + (relative ? 1u : 0u);
    uint32_t digit_count = token->length - (relative ? 1u : 0u);
    const bool in_ms = digit_count > 2u && memcmp(digits + digit_count - 2u, "ms", 2u) == 0;
    digit_count -= in_ms ? 2u : 0u;

    uint64_t value = 0;
    if (!script_digits(digits, digit_count, 0xffffffffu, &value)) {
        return script_fail(parser, "'%.*s' is not a time: write 40, +5, 1500ms or +250ms", (int32_t)token->length,
                           token->start);
    }
    const uint64_t ticks = in_ms ? value * parser->tick_hz / SCRIPT_MS_PER_SECOND : value;
    const uint64_t tick = relative ? parser->previous_tick + ticks : ticks;
    if (tick > DISPLAY_SCRIPT_MAX_TICK) {
        return script_fail(parser, "tick %" PRIu64 " is past the last one allowed, %" PRIu32, tick,
                           (uint32_t)DISPLAY_SCRIPT_MAX_TICK);
    }
    if (tick < parser->previous_tick) {
        return script_fail(parser, "tick %" PRIu64 " goes back in time: an earlier line is at tick %" PRIu32, tick,
                           parser->previous_tick);
    }
    *out_tick = (uint32_t)tick;
    return true;
}

static bool script_table_code(const script_key_t *table, uint32_t count, const script_token_t *token, uint16_t *out) {
    char lowered[sizeof(table[0].name)];
    if (token->length >= sizeof(lowered)) {
        return false;
    }
    for (uint32_t i = 0; i < token->length; i++) {
        lowered[i] = script_lower(token->start[i]);
    }
    lowered[token->length] = '\0';
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(table[i].name, lowered) == 0) {
            *out = table[i].code;
            return true;
        }
    }
    return false;
}

static bool script_key_code(const script_token_t *token, uint16_t *out) {
    if (token->length > SCRIPT_CODE_PREFIX_BYTES && memcmp(token->start, "code:", SCRIPT_CODE_PREFIX_BYTES) == 0) {
        uint64_t code = 0;
        if (!script_digits(token->start + SCRIPT_CODE_PREFIX_BYTES, token->length - SCRIPT_CODE_PREFIX_BYTES,
                           DISPLAY_SCRIPT_CODE_COUNT - 1u, &code)) {
            return false;
        }
        *out = (uint16_t)code;
        return true;
    }
    return script_table_code(script_keys, sizeof(script_keys) / sizeof(script_keys[0]), token, out);
}

static bool script_parse_press(script_parser_t *parser, const script_token_t *token, uint8_t *out) {
    if (script_token_is(token, "down")) {
        *out = 1u;
        return true;
    }
    if (script_token_is(token, "up")) {
        *out = 0u;
        return true;
    }
    return script_fail(parser, "'%.*s' should be down or up", (int32_t)token->length, token->start);
}

void display_script_screenshot_name(const display_script_t *script, const display_script_event_t *event, char *out) {
    if (event->name_length != 0u) {
        memcpy(out, script->borrowed_text + event->name_offset, event->name_length);
        out[event->name_length] = '\0';
        return;
    }
    snprintf(out, DISPLAY_SCRIPT_NAME_BYTES, "tick_%08" PRIu32 ".png", event->tick);
}

/* A name becomes a file in the directory given on the command line, so it may not lead anywhere else. */
static bool script_name_is_clean(const script_token_t *token) {
    if (token->length < 5u || token->length >= DISPLAY_SCRIPT_NAME_BYTES || token->start[0] == '.' ||
        memcmp(token->start + token->length - 4u, ".png", 4u) != 0) {
        return false;
    }
    for (uint32_t i = 0; i < token->length; i++) {
        const char c = token->start[i];
        const bool fine = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                          c == '-' || c == '.';
        if (!fine) {
            return false;
        }
    }
    return true;
}

/* A second shot of one name would replace the first, and the script's author would never see it. */
static bool script_screenshot_is_new(const script_parser_t *parser, const display_script_event_t *event) {
    char name[DISPLAY_SCRIPT_NAME_BYTES];
    char earlier[DISPLAY_SCRIPT_NAME_BYTES];
    display_script_screenshot_name(parser->out, event, name);
    for (uint32_t i = 0; i < parser->out->event_count; i++) {
        if (parser->events[i].kind == DISPLAY_SCRIPT_SCREENSHOT) {
            display_script_screenshot_name(parser->out, &parser->events[i], earlier);
            if (strcmp(name, earlier) == 0) {
                return false;
            }
        }
    }
    return true;
}

static bool script_parse_screenshot(script_parser_t *parser, const script_token_t *tokens, uint32_t count,
                                    display_script_event_t *event) {
    if (count > 3u) {
        return script_fail(parser, "screenshot takes at most a file name");
    }
    if (parser->out->screenshot_count == DISPLAY_SCRIPT_MAX_SCREENSHOTS) {
        return script_fail(parser, "more than %" PRIu32 " screenshots", (uint32_t)DISPLAY_SCRIPT_MAX_SCREENSHOTS);
    }
    event->kind = DISPLAY_SCRIPT_SCREENSHOT;
    if (count == 3u) {
        if (!script_name_is_clean(&tokens[2])) {
            return script_fail(parser, "'%.*s' is not a name to use: letters, digits, _ - . ending in .png",
                               (int32_t)tokens[2].length, tokens[2].start);
        }
        event->name_offset = (uint32_t)(tokens[2].start - parser->out->borrowed_text);
        event->name_length = (uint16_t)tokens[2].length;
    }
    if (!script_screenshot_is_new(parser, event)) {
        return script_fail(parser, "this screenshot's file name is already used by an earlier line");
    }
    parser->out->screenshot_count++;
    return true;
}

static bool script_parse_press_event(script_parser_t *parser, const script_token_t *tokens, uint32_t count,
                                     display_script_event_t *event) {
    const bool is_key = tokens[1].start[0] == 'k';
    if (count != 4u) {
        return script_fail(parser, "%s takes a name, then down or up", is_key ? "key" : "button");
    }
    const bool known = is_key ? script_key_code(&tokens[2], &event->code)
                              : script_table_code(script_buttons, sizeof(script_buttons) / sizeof(script_buttons[0]),
                                                  &tokens[2], &event->code);
    if (!known) {
        return script_fail(parser, "no %s called '%.*s'", is_key ? "key" : "button", (int32_t)tokens[2].length,
                           tokens[2].start);
    }
    event->kind = DISPLAY_SCRIPT_KEY;
    return script_parse_press(parser, &tokens[3], &event->pressed);
}

static bool script_parse_event(script_parser_t *parser, const script_token_t *tokens, uint32_t count,
                               display_script_event_t *event) {
    const script_token_t *const verb = &tokens[1];
    if (script_token_is(verb, "key") || script_token_is(verb, "button")) {
        return script_parse_press_event(parser, tokens, count, event);
    }
    if (script_token_is(verb, "move")) {
        event->kind = DISPLAY_SCRIPT_MOVE;
        if (count != 4u || !script_signed(&tokens[2], SCRIPT_MOVE_LIMIT, &event->dx) ||
            !script_signed(&tokens[3], SCRIPT_MOVE_LIMIT, &event->dy)) {
            return script_fail(parser, "move takes dx and dy, whole numbers up to %" PRIu32 " either way",
                               (uint32_t)SCRIPT_MOVE_LIMIT);
        }
        return true;
    }
    if (script_token_is(verb, "wheel")) {
        event->kind = DISPLAY_SCRIPT_WHEEL;
        if (count != 3u || !script_signed(&tokens[2], SCRIPT_WHEEL_LIMIT, &event->dx)) {
            return script_fail(parser, "wheel takes steps, a whole number up to %" PRIu32 " either way",
                               (uint32_t)SCRIPT_WHEEL_LIMIT);
        }
        return true;
    }
    if (script_token_is(verb, "screenshot")) {
        return script_parse_screenshot(parser, tokens, count, event);
    }
    return script_fail(parser, "'%.*s' is not one of key, button, move, wheel, screenshot, end", (int32_t)verb->length,
                       verb->start);
}

static bool script_parse_tokens(script_parser_t *parser, const script_token_t *tokens, uint32_t count) {
    if (parser->out->has_end != 0u) {
        return script_fail(parser, "nothing may follow end");
    }
    if (count < 2u) {
        return script_fail(parser, "a line is a time and then what happens");
    }
    uint32_t tick = 0;
    if (!script_parse_when(parser, &tokens[0], &tick)) {
        return false;
    }
    parser->previous_tick = tick;
    parser->out->last_tick = tick;
    if (script_token_is(&tokens[1], "end")) {
        if (count != 2u) {
            return script_fail(parser, "end takes nothing");
        }
        parser->out->has_end = 1u;
        return true;
    }
    if (parser->out->event_count == parser->capacity) {
        return script_fail(parser, "more than %" PRIu32 " events", parser->capacity);
    }
    display_script_event_t *const event = &parser->events[parser->out->event_count];
    memset(event, 0, sizeof(*event));
    event->tick = tick;
    event->line = parser->line;
    if (!script_parse_event(parser, tokens, count, event)) {
        return false;
    }
    parser->out->event_count++;
    return true;
}

static bool script_parse_line(script_parser_t *parser, const char *line, uint64_t length) {
    script_token_t tokens[SCRIPT_MAX_TOKENS];
    uint32_t count = 0;
    bool in_token = false;
    for (uint64_t i = 0; i < length; i++) {
        const char c = line[i];
        if (c == '#') {
            break;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            in_token = false;
            continue;
        }
        if ((uint8_t)c < 0x20u || (uint8_t)c >= 0x7fu) {
            return script_fail(parser, "byte 0x%02" PRIx32 " is not text a script can hold", (uint32_t)(uint8_t)c);
        }
        if (!in_token) {
            if (count == SCRIPT_MAX_TOKENS) {
                return script_fail(parser, "too many words on this line");
            }
            tokens[count] = (script_token_t){.start = line + i, .length = 0u, ._pad0 = {0}};
            count++;
            in_token = true;
        }
        tokens[count - 1u].length++;
    }
    return count == 0u || script_parse_tokens(parser, tokens, count);
}

bool display_script_parse(const char *text, uint64_t length, uint32_t tick_hz, display_script_event_t *events,
                          uint32_t capacity, display_script_t *out, display_script_error_t *error) {
    memset(out, 0, sizeof(*out));
    memset(error, 0, sizeof(*error));
    out->borrowed_text = text;
    out->borrowed_events = events;
    script_parser_t parser = {.out = out,
                              .events = events,
                              .error = error,
                              .capacity = capacity,
                              .tick_hz = tick_hz,
                              .line = 0u,
                              .previous_tick = 0u};
    bool ok = true;
    if (length > DISPLAY_SCRIPT_MAX_BYTES) {
        ok = script_fail(&parser, "over %" PRIu32 " bytes", (uint32_t)DISPLAY_SCRIPT_MAX_BYTES);
    }
    uint64_t at = 0;
    while (ok && at < length) {
        const char *const newline = memchr(text + at, '\n', length - at);
        const uint64_t end = newline != nullptr ? (uint64_t)(newline - text) : length;
        parser.line++;
        ok = script_parse_line(&parser, text + at, end - at);
        at = end + 1u;
    }
    if (!ok) {
        out->event_count = 0u;
        out->screenshot_count = 0u;
        out->last_tick = 0u;
        out->has_end = 0u;
    }
    return ok;
}

void display_script_state_begin_tick(display_script_state_t *state, uint32_t tick) {
    state->tick = tick;
    state->tick_dx = 0;
    state->tick_dy = 0;
    state->tick_wheel = 0;
}

void display_script_state_apply(display_script_state_t *state, const display_script_event_t *event) {
    switch ((display_script_kind_t)event->kind) {
    case DISPLAY_SCRIPT_KEY: {
        const uint64_t bit = 1ull << (event->code % 64u);
        if (event->pressed != 0u) {
            state->down[event->code / 64u] |= bit;
        } else {
            state->down[event->code / 64u] &= ~bit;
        }
        break;
    }
    case DISPLAY_SCRIPT_MOVE:
        state->mouse_x += event->dx;
        state->mouse_y += event->dy;
        state->tick_dx += event->dx;
        state->tick_dy += event->dy;
        break;
    case DISPLAY_SCRIPT_WHEEL:
        state->wheel += event->dx;
        state->tick_wheel += event->dx;
        break;
    case DISPLAY_SCRIPT_SCREENSHOT:
        break;
    }
}

bool display_script_state_is_down(const display_script_state_t *state, uint32_t code) {
    if (code >= DISPLAY_SCRIPT_CODE_COUNT) {
        return false;
    }
    return (state->down[code / 64u] >> (code % 64u) & 1u) != 0u;
}
