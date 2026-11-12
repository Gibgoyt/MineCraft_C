/* See README.md. The command box: plan 006 phase 01. Allocates nothing: its state is in the World arena (game_command_of). */
#include "game/command/index.h"

#include "calendar/index.h"
#include "climate/index.h"
#include "game/atlas/index.h"
#include "game/fill/index.h"
#include "game/index.h"
#include "log/index.h"
#include "terrain/index.h"
#include "ui/command/index.h"
#include "ui/index.h"
#include "world/column/index.h"

#include <inttypes.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum : uint32_t {
    COMMAND_WORDS_MAX = 5u,   /* the most words any command has, its name included */
    COMMAND_WORD_BYTES = 32u, /* one word, with its NUL: a number longer than this is not one anyone typed */
};
/* A relative step may be up to twice the place limit, from one edge to the other; the sim holds where it ends. */
#define COMMAND_STEP_MOST (2.0f64 * PLAYER_COMMAND_PLACE_MOST)
/* A flying eye (a test world) lands this far over the estimated ground. */
#define COMMAND_EYE_OVER_GROUND 3.0f64

static const char COMMAND_DOT[] = " \xC2\xB7 ";

static const char *const COMMAND_HELP[GAME_COMMAND_HELP_LINES] = {
    "/tp X Y Z        feet at block X Y, height Z",
    "/tp ~DX ~DY ~DZ  from where you are; any mix",
    "/tp X Y          stand on the ground at X Y",
    "/tp lat DEG [X]  to a latitude, -90 to 90 (x is not longitude)",
    "/tp top          up onto the top of this column",
    "/fly             fly on or off; the wheel sets its speed",
    "/speed N         walk and fly N times as fast, 1 to 10",
    "/time day N      on to day N of the year, 1 to 365",
    "/time hour H     on to H o'clock, 0 to 24",
    "/help            this list",
    "Esc closes; Up and Down bring back the last 16",
};

const char *const *game_command_help_lines(void) {
    return COMMAND_HELP;
}

/* ---- The parser: pure ------------------------------------------------------------------------------------------------------ */

typedef struct command_words_t {
    char word[COMMAND_WORDS_MAX][COMMAND_WORD_BYTES]; /* 0 */
    uint32_t count;                                   /* 160 */
    uint8_t too_many;                                 /* 164 */
    uint8_t too_long;                                 /* 165 */
    uint8_t _pad0[2];                                 /* 166 */
} command_words_t;
static_assert(sizeof(command_words_t) == 168);

static void command_split(const char *text, command_words_t *out) {
    memset(out, 0, sizeof(*out));
    const char *at = text;
    while (*at == ' ') {
        at++;
    }
    if (*at == '/') {
        at++;
    }
    while (*at != '\0') {
        while (*at == ' ') {
            at++;
        }
        if (*at == '\0') {
            break;
        }
        if (out->count == COMMAND_WORDS_MAX) {
            out->too_many = 1u;
            return;
        }
        uint32_t length = 0u;
        while (*at != '\0' && *at != ' ') {
            if (length + 1u < COMMAND_WORD_BYTES) {
                const char c = *at;
                out->word[out->count][length++] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
            } else {
                out->too_long = 1u;
            }
            at++;
        }
        out->word[out->count][length] = '\0';
        out->count++;
    }
}

/* [+-]digits[.digits] or [+-].digits, nothing else: no exponents, no inf, no nan. whole says there was no point. */
static bool command_number(const char *text, float64_t *out, bool *whole) {
    const char *at = text;
    bool negative = false;
    if (*at == '+' || *at == '-') {
        negative = *at == '-';
        at++;
    }
    float64_t value = 0.0f64;
    uint32_t digits = 0u;
    while (*at >= '0' && *at <= '9') {
        value = value * 10.0f64 + (float64_t)(*at - '0');
        digits++;
        at++;
    }
    *whole = true;
    if (*at == '.') {
        *whole = false;
        at++;
        float64_t place = 0.1f64;
        while (*at >= '0' && *at <= '9') {
            value += place * (float64_t)(*at - '0');
            place *= 0.1f64;
            digits++;
            at++;
        }
    }
    if (*at != '\0' || digits == 0u || !isfinite(value)) {
        return false;
    }
    *out = negative ? -value : value;
    return true;
}

static float64_t command_clamp(float64_t value, float64_t low, float64_t high) {
    return value < low ? low : (value > high ? high : value);
}

/* One axis of a teleport: ~ alone, ~step, or a place. A whole place across is the block's middle; a height is where the feet go. */
static bool command_axis(const char *word, uint32_t axis, game_command_parsed_t *out) {
    static const uint32_t relative[3] = {PLAYER_COMMAND_RELATIVE_X, PLAYER_COMMAND_RELATIVE_Y,
                                         PLAYER_COMMAND_RELATIVE_Z};
    float64_t value = 0.0f64;
    bool whole = true;
    if (word[0] == '~') {
        if (word[1] != '\0' && !command_number(word + 1, &value, &whole)) {
            return false;
        }
        out->relative |= relative[axis];
        out->target[axis] = command_clamp(value, -COMMAND_STEP_MOST, COMMAND_STEP_MOST);
        return true;
    }
    if (!command_number(word, &value, &whole)) {
        return false;
    }
    if (axis < 2u) {
        value += whole ? 0.5f64 : 0.0f64;
        out->target[axis] = command_clamp(value, -PLAYER_COMMAND_PLACE_MOST, PLAYER_COMMAND_PLACE_MOST);
    } else {
        out->target[axis] = command_clamp(value, (float64_t)COLUMN_Z_GAMEPLAY_MIN, (float64_t)COLUMN_Z_GAMEPLAY_MAX);
    }
    return true;
}

static bool command_bad(game_command_parsed_t *out, const char *why) {
    const char *const keep = why;
    memset(out, 0, sizeof(*out));
    (void)snprintf(out->error, sizeof(out->error), "%s", keep);
    return false;
}

float64_t game_command_lat_y(float64_t degrees) {
    return degrees / GAME_COMMAND_LAT_MOST * CLIMATE_POLE_Y;
}

static bool command_tp(const command_words_t *words, game_command_parsed_t *out) {
    static const char usage[] = "Usage: /tp X Y Z, /tp X Y, /tp lat DEG [X] or /tp top";
    const uint32_t args = words->count - 1u;
    out->kind = GAME_COMMAND_TP;
    out->tp = PLAYER_COMMAND_TP_GROUND;
    if (args == 1u && strcmp(words->word[1], "top") == 0) {
        out->relative = PLAYER_COMMAND_RELATIVE_X | PLAYER_COMMAND_RELATIVE_Y | PLAYER_COMMAND_RELATIVE_Z;
        return true;
    }
    if (args >= 1u && strcmp(words->word[1], "lat") == 0) {
        float64_t degrees = 0.0f64;
        bool whole = true;
        if (args < 2u || args > 3u || !command_number(words->word[2], &degrees, &whole)) {
            return command_bad(out, "Usage: /tp lat DEG [X], DEG from -90 (south) to 90 (north)");
        }
        if (degrees < -GAME_COMMAND_LAT_MOST || degrees > GAME_COMMAND_LAT_MOST) {
            return command_bad(out, "A latitude is from -90 (south) to 90 (north)");
        }
        out->latitude = 1u;
        out->target[1] = game_command_lat_y(degrees);
        out->relative = PLAYER_COMMAND_RELATIVE_Z;
        if (args == 3u) {
            if (!command_axis(words->word[3], 0u, out)) {
                return command_bad(out, "Usage: /tp lat DEG [X], X a number or ~");
            }
        } else {
            out->relative |= PLAYER_COMMAND_RELATIVE_X;
        }
        return true;
    }
    if (args != 2u && args != 3u) {
        return command_bad(out, usage);
    }
    for (uint32_t axis = 0u; axis < args; axis++) {
        if (!command_axis(words->word[1u + axis], axis, out)) {
            char why[GAME_COMMAND_TEXT_BYTES];
            (void)snprintf(why, sizeof(why), "Not a number or ~: %s", words->word[1u + axis]);
            return command_bad(out, why);
        }
    }
    if (args == 2u) {
        out->relative |= PLAYER_COMMAND_RELATIVE_Z; /* the height is the ground's */
    } else {
        out->tp = PLAYER_COMMAND_TP_EXACT;
    }
    return true;
}

static bool command_time(const command_words_t *words, game_command_parsed_t *out) {
    float64_t value = 0.0f64;
    bool whole = true;
    if (words->count != 3u || !command_number(words->word[2], &value, &whole)) {
        return command_bad(out, "Usage: /time day N (1 to 365) or /time hour H (0 to 24)");
    }
    if (strcmp(words->word[1], "day") == 0) {
        if (!whole || value < 1.0f64 || value > (float64_t)CALENDAR_DAYS_PER_YEAR) {
            return command_bad(out, "A day of the year is a whole number from 1 to 365");
        }
        out->kind = GAME_COMMAND_TIME_DAY;
    } else if (strcmp(words->word[1], "hour") == 0) {
        if (value < 0.0f64 || value > 24.0f64) {
            return command_bad(out, "An hour is from 0 to 24");
        }
        out->kind = GAME_COMMAND_TIME_HOUR;
    } else {
        return command_bad(out, "Usage: /time day N (1 to 365) or /time hour H (0 to 24)");
    }
    out->value = value;
    return true;
}

bool game_command_parse(const char *text, game_command_parsed_t *out) {
    memset(out, 0, sizeof(*out));
    command_words_t words;
    command_split(text, &words);
    if (words.count == 0u) {
        return command_bad(out, "Type a command: /help lists them");
    }
    if (words.too_many != 0u || words.too_long != 0u) {
        return command_bad(out, "Too much there: /help lists the commands");
    }
    const char *const name = words.word[0];
    if (strcmp(name, "tp") == 0) {
        return command_tp(&words, out);
    }
    if (strcmp(name, "fly") == 0) {
        out->kind = GAME_COMMAND_FLY;
        return words.count == 1u || command_bad(out, "Usage: /fly");
    }
    if (strcmp(name, "speed") == 0) {
        if (words.count == 1u) {
            out->kind = GAME_COMMAND_SPEED_SHOW;
            return true;
        }
        float64_t value = 0.0f64;
        bool whole = true;
        if (words.count != 2u || !command_number(words.word[1], &value, &whole) || !whole ||
            value < (float64_t)PLAYER_MOVEMENT_FACTOR_MIN || value > (float64_t)PLAYER_MOVEMENT_FACTOR_MAX) {
            return command_bad(out, "Usage: /speed N, N a whole number from 1 to 10");
        }
        out->kind = GAME_COMMAND_SPEED;
        out->value = value;
        return true;
    }
    if (strcmp(name, "time") == 0) {
        return command_time(&words, out);
    }
    if (strcmp(name, "help") == 0) {
        out->kind = GAME_COMMAND_HELP;
        return true;
    }
    char why[GAME_COMMAND_TEXT_BYTES];
    (void)snprintf(why, sizeof(why), "Unknown command /%s: /help lists them", name);
    return command_bad(out, why);
}

void game_command_preview(const char *text, char *out, uint32_t bytes) {
    if (bytes == 0u) {
        return;
    }
    out[0] = '\0';
    game_command_parsed_t parsed;
    if (!game_command_parse(text, &parsed) || parsed.kind != GAME_COMMAND_TP || parsed.latitude == 0u) {
        return;
    }
    const float64_t lat = climate_latitude_deg(parsed.target[1]);
    (void)snprintf(out, bytes, "y %.0f%s%.1f\xC2\xB0 %c", (libc_double_t)parsed.target[1], COMMAND_DOT,
                   (libc_double_t)fabs(lat), lat < 0.0f64 ? 'S' : 'N');
}

void game_command_history_push(game_command_t *command, const char *text) {
    (void)snprintf(command->history[command->history_next], GAME_COMMAND_TEXT_BYTES, "%s", text);
    command->history_next = (command->history_next + 1u) % GAME_COMMAND_HISTORY;
    command->history_count += command->history_count < GAME_COMMAND_HISTORY ? 1u : 0u;
}

const char *game_command_history_back(const game_command_t *command, uint32_t n) {
    if (n == 0u || n > command->history_count) {
        return nullptr;
    }
    return command->history[(command->history_next + GAME_COMMAND_HISTORY - n) % GAME_COMMAND_HISTORY];
}

/* ---- The box --------------------------------------------------------------------------------------------------------------- */

void game_command_start(game_command_t *command) {
    memset(command, 0, sizeof(*command));
}

bool game_command_up(const game_t *game) {
    const game_command_t *const command = game_command_of(game);
    return command != nullptr && command->up != 0u;
}

static void command_close(game_command_t *command) {
    command->up = 0u;
    command->help = 0u;
    command->browse = 0u;
    command->text[0] = '\0';
    command->line[0] = '\0';
    command->error[0] = '\0';
}

static bool command_fail(game_command_t *command, const char *why) {
    (void)snprintf(command->error, sizeof(command->error), "%s", why);
    return false;
}

/* A flying eye, with no body: main moves it itself. A height of the ground is the estimate's, as nothing need be loaded. */
static void command_eye_jump(game_t *game, const game_command_parsed_t *parsed) {
    float64_t *const eye = game->camera.position;
    float64_t to[3];
    for (uint32_t a = 0u; a < 3u; a++) {
        const uint32_t bit = PLAYER_COMMAND_RELATIVE_X << a;
        to[a] = (parsed->relative & bit) != 0u ? eye[a] + parsed->target[a] : parsed->target[a];
    }
    to[0] = command_clamp(to[0], -PLAYER_COMMAND_PLACE_MOST, PLAYER_COMMAND_PLACE_MOST);
    to[1] = command_clamp(to[1], -PLAYER_COMMAND_PLACE_MOST, PLAYER_COMMAND_PLACE_MOST);
    if (parsed->tp == PLAYER_COMMAND_TP_GROUND) {
        const float64_t ground =
            (float64_t)terrain_height_estimate(game->borrowed_terrain, (int32_t)floor(to[0]), (int32_t)floor(to[1]));
        to[2] = ground + COMMAND_EYE_OVER_GROUND + PLAYER_MOVEMENT_EYE_STANDING;
    }
    to[2] = command_clamp(to[2], (float64_t)COLUMN_Z_GAMEPLAY_MIN, (float64_t)COLUMN_Z_GAMEPLAY_MAX);
    memcpy(eye, to, sizeof(to));
    game_fill_t *const fill = game_fill_of(game);
    if (fill != nullptr) {
        game_fill_jump_ask(fill, column_chunk_of((int32_t)floor(to[0])), column_chunk_of((int32_t)floor(to[1])));
    }
}

static bool command_teleport(game_t *game, game_command_t *command, const game_command_parsed_t *parsed) {
    player_t *const player = game->borrowed_player;
    if (player == nullptr) {
        command_eye_jump(game, parsed);
        return true;
    }
    if (player_vitals_read(player).state != (uint8_t)PLAYER_SURVIVAL_ALIVE) {
        return command_fail(command, "Not while dead or asleep");
    }
    if (!player_command_send(player, (player_command_kind_t)parsed->tp, parsed->target, parsed->relative, 0u)) {
        return command_fail(command, "Still on the way from the last one: try again");
    }
    LOG_INFO("command: teleport, %s %.1f %.1f %.1f (relative %#" PRIx32 ")",
             parsed->tp == PLAYER_COMMAND_TP_GROUND ? "on the ground at" : "feet at", (libc_double_t)parsed->target[0],
             (libc_double_t)parsed->target[1], (libc_double_t)parsed->target[2], parsed->relative);
    return true;
}

/* Days, then hours, only ever forward, so the weekly census runs for each week passed, as sleeping or --climate-jump does. */
static void command_time_set(game_t *game, const game_command_parsed_t *parsed) {
    if (parsed->kind == GAME_COMMAND_TIME_DAY) {
        const calendar_date_t date = game_calendar_date(game);
        const uint32_t target = (uint32_t)parsed->value - 1u;
        const uint32_t forward =
            (target + CALENDAR_DAYS_PER_YEAR - (uint32_t)date.day_of_year) % CALENDAR_DAYS_PER_YEAR;
        game_clock_set(game, game->clock_days + forward, game->day_fraction);
        return;
    }
    const float64_t fraction = parsed->value / 24.0f64;
    game_clock_set(game, game->clock_days + (fraction < game->day_fraction ? 1u : 0u), fraction);
}

bool game_command_run(game_t *game, const char *text) {
    game_command_t *const command = game_command_of(game);
    if (command == nullptr) {
        return false;
    }
    command->error[0] = '\0';
    command->line[0] = '\0';
    command->help = 0u;
    game_command_parsed_t parsed;
    if (!game_command_parse(text, &parsed)) {
        return command_fail(command, parsed.error);
    }
    player_t *const player = game->borrowed_player;
    switch ((game_command_kind_t)parsed.kind) {
    case GAME_COMMAND_TP:
        return command_teleport(game, command, &parsed);
    case GAME_COMMAND_FLY:
    case GAME_COMMAND_SPEED:
        if (player == nullptr) {
            return command_fail(command, "A test world's eye always flies, at the wheel's speed");
        }
        if (!player_command_send(player, parsed.kind == GAME_COMMAND_FLY ? PLAYER_COMMAND_FLY : PLAYER_COMMAND_SPEED,
                                 nullptr, 0u, (uint32_t)parsed.value)) {
            return command_fail(command, "Still on the way from the last one: try again");
        }
        return true;
    case GAME_COMMAND_SPEED_SHOW: {
        if (player == nullptr) {
            return command_fail(command, "A test world's eye always flies, at the wheel's speed");
        }
        bool flying = false;
        uint32_t speed = 0u, factor = 0u;
        player_flight_read(player, &flying, &speed, &factor);
        (void)snprintf(command->line, sizeof(command->line), "Speed %" PRIu32 "x (/speed N, 1 to 10, to change it)",
                       factor);
        return true;
    }
    case GAME_COMMAND_TIME_DAY:
    case GAME_COMMAND_TIME_HOUR: {
        command_time_set(game, &parsed);
        const calendar_date_t date = game_calendar_date(game);
        LOG_INFO("command: the clock is now day %" PRIu32 " at %02" PRIu32 ":%02" PRIu32,
                 (uint32_t)date.day_of_year + 1u, (uint32_t)date.hour, (uint32_t)date.minute);
        return true;
    }
    case GAME_COMMAND_HELP:
        command->help = 1u;
        return true;
    case GAME_COMMAND_BAD:
    default:
        return command_fail(command, "Unknown command: /help lists them");
    }
}

/* Up and Down: the history, and back to an empty line past the newest. */
static void command_browse(game_command_t *command, bool older) {
    const uint32_t next = older ? command->browse + 1u : (command->browse == 0u ? 0u : command->browse - 1u);
    const char *const found = game_command_history_back(command, next);
    if (older && found == nullptr) {
        return; /* past the oldest: it stays */
    }
    command->browse = next;
    (void)snprintf(command->text, sizeof(command->text), "%s", found != nullptr ? found : "");
    command->error[0] = '\0';
    game_command_preview(command->text, command->line, sizeof(command->line));
}

bool game_command_input(game_t *game, const input_frame_t *frame) {
    game_command_t *const command = game_command_of(game);
    ui_t *const ui = game->borrowed_ui;
    if (command == nullptr || ui == nullptr) {
        return false;
    }
    if (command->up == 0u) {
        if (game_atlas_up(game) || (game_has_screens(game) && ui_screen_up(ui))) {
            return false;
        }
        const bool slash = input_keys_has(&frame->pressed, KEY_SLASH);
        if (!slash && !input_keys_has(&frame->pressed, KEY_T)) {
            return false;
        }
        command_close(command);
        command->up = 1u;
        /* The key that opened it is not typed: this poll goes no further. */
        (void)snprintf(command->text, sizeof(command->text), "%s", slash ? "/" : "");
        return true;
    }
    ui_widget_input_feed(&ui->widget, frame);
    if (input_keys_has(&frame->pressed, KEY_ESC)) {
        (void)ui_command_keys(&ui->widget, command->text, sizeof(command->text)); /* what came with it is dropped */
        command_close(command);
        return true;
    }
    if (input_keys_has(&frame->pressed, KEY_UP) || input_keys_has(&frame->pressed, KEY_DOWN)) {
        command_browse(command, input_keys_has(&frame->pressed, KEY_UP));
    }
    const ui_field_event_t event = ui_command_keys(&ui->widget, command->text, sizeof(command->text));
    if (event == UI_FIELD_CHANGED) {
        command->error[0] = '\0';
        command->help = 0u;
        game_command_preview(command->text, command->line, sizeof(command->line));
    } else if (event == UI_FIELD_SUBMITTED) {
        const bool empty = command->text[0] == '\0' || strcmp(command->text, "/") == 0;
        if (empty) {
            command_close(command);
            return true;
        }
        char ran[GAME_COMMAND_TEXT_BYTES];
        (void)snprintf(ran, sizeof(ran), "%s", command->text);
        game_command_history_push(command, ran);
        command->browse = 0u;
        const bool ok = game_command_run(game, ran);
        game_command_parsed_t parsed;
        const bool stays = ok && game_command_parse(ran, &parsed) &&
                           (parsed.kind == GAME_COMMAND_HELP || parsed.kind == GAME_COMMAND_SPEED_SHOW);
        if (ok && !stays) {
            command_close(command);
        } else if (stays) {
            (void)snprintf(command->text, sizeof(command->text), "/");
        }
    }
    return true;
}

void game_command_draw(game_t *game) {
    game_command_t *const command = game_command_of(game);
    ui_t *const ui = game->borrowed_ui;
    if (command == nullptr || ui == nullptr || command->up == 0u) {
        return;
    }
    const ui_command_view_t view = {
        .borrowed_text = command->text,
        .borrowed_error = command->error,
        .borrowed_line = command->line,
        .borrowed_lines = command->help != 0u ? COMMAND_HELP : nullptr,
        .line_count = command->help != 0u ? GAME_COMMAND_HELP_LINES : 0u,
        ._pad0 = {0},
    };
    ui_command_draw(&ui->widget, &ui->canvas, &ui->font, &view);
}

void game_command_flight_line(const game_t *game, char *out, uint32_t bytes) {
    if (bytes == 0u) {
        return;
    }
    out[0] = '\0';
    const player_t *const player = game->borrowed_player;
    if (player == nullptr) {
        return;
    }
    bool flying = false;
    uint32_t speed = 0u, factor = 0u;
    player_flight_read(player, &flying, &speed, &factor);
    if (flying && factor > 1u) {
        (void)snprintf(out, bytes, "Flying%s%" PRIu32 " b/s%sspeed %" PRIu32 "x", COMMAND_DOT, speed, COMMAND_DOT,
                       factor);
    } else if (flying) {
        (void)snprintf(out, bytes, "Flying%s%" PRIu32 " b/s", COMMAND_DOT, speed);
    } else if (factor > 1u) {
        (void)snprintf(out, bytes, "Speed %" PRIu32 "x", factor);
    }
}
