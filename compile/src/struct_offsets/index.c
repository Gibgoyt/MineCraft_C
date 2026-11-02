/*
 * C headers -> index.inc for NASM and index.h for the C compiler. NASM cannot read a C struct, and
 * plans/CODING_STANDARD.md section 7 forbids typing its offsets by hand, so this reads each `typedef struct` in the
 * headers it is given and works out the layout.
 *
 * It knows natural alignment and alignas, and it trusts neither: anything it does not fully understand is refused with a
 * file and line, and index.h repeats every number as a static_assert for the real compiler. A tool may not include run/
 * (plans/REPO_STRUCTURE.md section 11), so it has no other way to see the structs, and this way a wrong answer is a
 * failed build and not a wrong offset.
 */
#include "struct_offsets/index.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void *offsets_arena_alloc(offsets_tool_t *tool, uint64_t bytes) {
    const uint64_t start = (tool->arena_used + 63u) & ~(uint64_t)63u;
    if (start + bytes > STRUCT_OFFSETS_ARENA_BYTES) {
        fprintf(stderr, "struct_offsets: out of arena\n");
        return nullptr;
    }
    tool->arena_used = start + bytes;
    return tool->owned_arena + start;
}

[[gnu::format(printf, 2, 3)]] static bool offsets_refuse(const offsets_scanner_t *scanner, const char *format, ...) {
    fprintf(stderr, "%s:%" PRIu32 ": struct_offsets: ", scanner->borrowed_path, scanner->token_line);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    return false;
}

static bool offsets_is_word_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool offsets_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool offsets_token_is(const offsets_scanner_t *scanner, const char *text) {
    const uint64_t length = strlen(text);
    return scanner->token_kind != OFFSETS_TOKEN_END && scanner->token_length == length &&
           memcmp(scanner->borrowed_token, text, length) == 0;
}

/* Past a block comment, with the cursor on its opening slash. */
static void offsets_skip_comment(offsets_scanner_t *scanner) {
    scanner->borrowed_cursor += 2;
    while (scanner->borrowed_cursor < scanner->borrowed_end) {
        const char c = *scanner->borrowed_cursor;
        if (c == '*' && scanner->borrowed_end - scanner->borrowed_cursor >= 2 && scanner->borrowed_cursor[1] == '/') {
            scanner->borrowed_cursor += 2;
            return;
        }
        if (c == '\n') {
            scanner->line++;
        }
        scanner->borrowed_cursor++;
    }
}

/* Past a string or character literal, with the cursor on its opening quote. */
static void offsets_skip_literal(offsets_scanner_t *scanner) {
    const char quote = *scanner->borrowed_cursor;
    scanner->borrowed_cursor++;
    while (scanner->borrowed_cursor < scanner->borrowed_end) {
        const char c = *scanner->borrowed_cursor;
        scanner->borrowed_cursor++;
        if (c == quote) {
            return;
        }
        if (c == '\\' && scanner->borrowed_cursor < scanner->borrowed_end) {
            scanner->borrowed_cursor++;
        } else if (c == '\n') {
            scanner->line++; /* not valid C; the line count stays right for the error the compiler will give */
        }
    }
}

/* A preprocessor line, with its continuations. Stops on the newline that ends it. */
static void offsets_skip_directive(offsets_scanner_t *scanner) {
    bool continued = false;
    while (scanner->borrowed_cursor < scanner->borrowed_end) {
        const char c = *scanner->borrowed_cursor;
        if (c == '\n') {
            if (!continued) {
                return;
            }
            scanner->line++;
        }
        continued = c == '\\';
        scanner->borrowed_cursor++;
    }
}

static void offsets_skip_blank(offsets_scanner_t *scanner) {
    while (scanner->borrowed_cursor < scanner->borrowed_end) {
        const char c = *scanner->borrowed_cursor;
        const bool has_next = scanner->borrowed_end - scanner->borrowed_cursor >= 2;
        if (c == '\n') {
            scanner->line++;
            scanner->at_line_start = 1u;
            scanner->borrowed_cursor++;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            scanner->borrowed_cursor++;
        } else if (c == '#' && scanner->at_line_start != 0u) {
            offsets_skip_directive(scanner);
        } else if (c == '/' && has_next && scanner->borrowed_cursor[1] == '*') {
            offsets_skip_comment(scanner);
        } else if (c == '/' && has_next && scanner->borrowed_cursor[1] == '/') {
            while (scanner->borrowed_cursor < scanner->borrowed_end && *scanner->borrowed_cursor != '\n') {
                scanner->borrowed_cursor++;
            }
        } else if (c == '"' || c == '\'') {
            offsets_skip_literal(scanner);
        } else {
            return;
        }
    }
}

/* Takes a whole number in decimal or hex. Its u, l or similar suffix is dropped. */
static void offsets_scan_number(offsets_scanner_t *scanner) {
    const char *start = scanner->borrowed_cursor;
    const char *end = start;
    while (end < scanner->borrowed_end && (offsets_is_digit(*end) || offsets_is_word_start(*end))) {
        end++;
    }
    uint64_t base = 10u;
    const char *digit = start;
    if (end - start > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        base = 16u;
        digit += 2;
    }
    uint64_t value = 0;
    for (; digit < end; digit++) {
        const char c = *digit;
        uint64_t place = base;
        if (offsets_is_digit(c)) {
            place = (uint64_t)(c - '0');
        } else if (base == 16u && c >= 'a' && c <= 'f') {
            place = (uint64_t)(c - 'a') + 10u;
        } else if (base == 16u && c >= 'A' && c <= 'F') {
            place = (uint64_t)(c - 'A') + 10u;
        }
        if (place >= base) {
            break; /* the suffix */
        }
        /* Callers accept only small values, so a huge one has to stay huge and not wrap round to a small one. */
        value = value > UINT64_MAX / 32u ? UINT64_MAX : value * base + place;
    }
    scanner->token_kind = OFFSETS_TOKEN_NUMBER;
    scanner->token_number = value;
    scanner->token_length = (uint64_t)(end - start);
    scanner->borrowed_cursor = end;
}

static void offsets_next(offsets_scanner_t *scanner) {
    offsets_skip_blank(scanner);
    scanner->at_line_start = 0u;
    scanner->token_line = scanner->line;
    scanner->borrowed_token = scanner->borrowed_cursor;
    scanner->token_length = 0;
    if (scanner->borrowed_cursor >= scanner->borrowed_end) {
        scanner->token_kind = OFFSETS_TOKEN_END;
        return;
    }
    const char first = *scanner->borrowed_cursor;
    if (offsets_is_word_start(first)) {
        const char *end = scanner->borrowed_cursor;
        while (end < scanner->borrowed_end && (offsets_is_word_start(*end) || offsets_is_digit(*end))) {
            end++;
        }
        scanner->token_kind = OFFSETS_TOKEN_WORD;
        scanner->token_length = (uint64_t)(end - scanner->borrowed_cursor);
        scanner->borrowed_cursor = end;
    } else if (offsets_is_digit(first)) {
        offsets_scan_number(scanner);
    } else {
        scanner->token_kind = OFFSETS_TOKEN_MARK;
        scanner->token_length = 1u;
        scanner->borrowed_cursor++;
    }
}

/* Takes the expected punctuation or keyword, or refuses. */
static bool offsets_expect(offsets_scanner_t *scanner, const char *text) {
    if (!offsets_token_is(scanner, text)) {
        return offsets_refuse(scanner, "expected '%s', found '%.*s'", text, (int32_t)scanner->token_length,
                              scanner->borrowed_token);
    }
    offsets_next(scanner);
    return true;
}

static bool offsets_take_name(offsets_scanner_t *scanner, char *out) {
    if (scanner->token_kind != OFFSETS_TOKEN_WORD) {
        return offsets_refuse(scanner, "expected a name, found '%.*s'", (int32_t)scanner->token_length,
                              scanner->borrowed_token);
    }
    if (scanner->token_length >= STRUCT_OFFSETS_NAME_BYTES) {
        return offsets_refuse(scanner, "a name is longer than %" PRIu64 " bytes", STRUCT_OFFSETS_NAME_BYTES - 1u);
    }
    memcpy(out, scanner->borrowed_token, scanner->token_length);
    out[scanner->token_length] = '\0';
    offsets_next(scanner);
    return true;
}

typedef struct offsets_builtin_t {
    const char *name;
    uint64_t bytes;
} offsets_builtin_t;
static_assert(sizeof(offsets_builtin_t) == 16);

/* The types plans/CODING_STANDARD.md section 2 allows in a struct. Each is aligned to its own size. */
static const offsets_builtin_t offsets_builtins[] = {
    {"uint8_t", 1u}, {"uint16_t", 2u}, {"uint32_t", 4u},  {"uint64_t", 8u},  {"int8_t", 1u},    {"int16_t", 2u},
    {"int32_t", 4u}, {"int64_t", 8u},  {"float32_t", 4u}, {"float64_t", 8u}, {"uintptr_t", 8u}, {"char", 1u},
};
enum { OFFSETS_BUILTIN_COUNT = sizeof(offsets_builtins) / sizeof(offsets_builtins[0]) };

static const offsets_struct_t *offsets_find_struct(const offsets_tool_t *tool, const char *name) {
    for (uint64_t i = 0; i < tool->struct_count; i++) {
        if (strcmp(tool->borrowed_structs[i].name, name) == 0) {
            return &tool->borrowed_structs[i];
        }
    }
    return nullptr;
}

/* Gives the size and alignment of a named type, or false if nothing here knows it. */
static bool offsets_type_layout(const offsets_tool_t *tool, const char *name, uint64_t *size, uint64_t *align) {
    for (uint32_t i = 0; i < OFFSETS_BUILTIN_COUNT; i++) {
        if (strcmp(offsets_builtins[i].name, name) == 0) {
            *size = offsets_builtins[i].bytes;
            *align = offsets_builtins[i].bytes;
            return true;
        }
    }
    const offsets_struct_t *known = offsets_find_struct(tool, name);
    if (known == nullptr) {
        return false;
    }
    *size = known->size;
    *align = known->align;
    return true;
}

/* A dimension or alignas argument: a plain number, since this tool does not evaluate constants. */
static bool offsets_take_number(offsets_scanner_t *scanner, const char *what, uint64_t maximum, uint64_t *out) {
    if (scanner->token_kind != OFFSETS_TOKEN_NUMBER) {
        return offsets_refuse(scanner, "%s must be a plain number, found '%.*s'", what, (int32_t)scanner->token_length,
                              scanner->borrowed_token);
    }
    if (scanner->token_number == 0u || scanner->token_number > maximum) {
        return offsets_refuse(scanner, "%s must be from 1 to %" PRIu64, what, maximum);
    }
    *out = scanner->token_number;
    offsets_next(scanner);
    return true;
}

static bool offsets_take_declaration_start(const offsets_tool_t *tool, offsets_scanner_t *scanner,
                                           offsets_declaration_t *out) {
    memset(out, 0, sizeof(*out));
    out->alignas_value = 1u;
    if (offsets_token_is(scanner, "alignas")) {
        offsets_next(scanner);
        if (!offsets_expect(scanner, "(") ||
            !offsets_take_number(scanner, "alignas", STRUCT_OFFSETS_MAX_ALIGN, &out->alignas_value) ||
            !offsets_expect(scanner, ")")) {
            return false;
        }
        if ((out->alignas_value & (out->alignas_value - 1u)) != 0u) {
            return offsets_refuse(scanner, "alignas(%" PRIu64 ") is not a power of two", out->alignas_value);
        }
    }
    if (offsets_token_is(scanner, "const")) {
        offsets_next(scanner);
    }
    if (offsets_token_is(scanner, "struct") || offsets_token_is(scanner, "union") ||
        offsets_token_is(scanner, "enum")) {
        return offsets_refuse(scanner, "'%.*s' in a field: use a typedef's name", (int32_t)scanner->token_length,
                              scanner->borrowed_token);
    }
    if (!offsets_take_name(scanner, out->type_name)) {
        return false;
    }
    out->type_known = offsets_type_layout(tool, out->type_name, &out->type_size, &out->type_align) ? 1u : 0u;
    return true;
}

/* One name on a declaration line, with its pointer stars and dimensions. */
static bool offsets_take_field(offsets_scanner_t *scanner, const offsets_declaration_t *declaration,
                               offsets_struct_t *record) {
    uint64_t size = declaration->type_size;
    uint64_t align = declaration->type_align;
    bool is_pointer = false;
    while (offsets_token_is(scanner, "*") || offsets_token_is(scanner, "const")) {
        is_pointer = is_pointer || offsets_token_is(scanner, "*");
        offsets_next(scanner);
    }
    if (offsets_token_is(scanner, "(")) {
        return offsets_refuse(scanner, "a function pointer field: this tool does not read that declarator");
    }
    if (is_pointer) {
        size = STRUCT_OFFSETS_POINTER_BYTES;
        align = STRUCT_OFFSETS_POINTER_BYTES;
    } else if (declaration->type_known == 0u) {
        return offsets_refuse(scanner, "unknown type '%s': list its header first, or use a sized type",
                              declaration->type_name);
    }
    if (record->field_count == STRUCT_OFFSETS_MAX_FIELDS) {
        return offsets_refuse(scanner, "more than %" PRIu64 " fields", STRUCT_OFFSETS_MAX_FIELDS);
    }
    offsets_field_t *field = &record->borrowed_fields[record->field_count];
    if (!offsets_take_name(scanner, field->name)) {
        return false;
    }
    while (offsets_token_is(scanner, "[")) {
        offsets_next(scanner);
        uint64_t count = 0;
        if (!offsets_take_number(scanner, "an array dimension", STRUCT_OFFSETS_MAX_ARRAY, &count) ||
            !offsets_expect(scanner, "]")) {
            return false;
        }
        if (size > UINT32_MAX) {
            return offsets_refuse(scanner, "'%s' is too large", field->name);
        }
        size *= count;
    }
    if (offsets_token_is(scanner, ":")) {
        return offsets_refuse(scanner, "'%s' is a bitfield, whose layout is up to the compiler", field->name);
    }
    if (offsets_token_is(scanner, "(")) {
        return offsets_refuse(scanner, "'%s' is a function: this tool does not read that declarator", field->name);
    }
    if (declaration->alignas_value > align) {
        align = declaration->alignas_value;
    }
    /* -Wpadded refuses this in the C build as well. A struct with a hole would have offsets only the compiler knows. */
    if (record->size % align != 0u) {
        return offsets_refuse(scanner, "'%s' at offset %" PRIu64 " needs compiler padding: write it out", field->name,
                              record->size);
    }
    field->offset = record->size;
    record->size += size;
    record->field_count++;
    if (align > record->align) {
        record->align = align;
    }
    return true;
}

/* With the scanner on the first token after the opening brace. Ends past the closing one. */
static bool offsets_take_fields(const offsets_tool_t *tool, offsets_scanner_t *scanner, offsets_struct_t *record) {
    while (!offsets_token_is(scanner, "}")) {
        if (scanner->token_kind == OFFSETS_TOKEN_END) {
            return offsets_refuse(scanner, "the file ends inside a struct");
        }
        offsets_declaration_t declaration;
        if (!offsets_take_declaration_start(tool, scanner, &declaration)) {
            return false;
        }
        while (true) {
            if (!offsets_take_field(scanner, &declaration, record)) {
                return false;
            }
            if (!offsets_token_is(scanner, ",")) {
                break;
            }
            offsets_next(scanner);
        }
        if (!offsets_expect(scanner, ";")) {
            return false;
        }
    }
    offsets_next(scanner);
    if (record->field_count == 0u) {
        return offsets_refuse(scanner, "a struct with no fields");
    }
    if (record->size % record->align != 0u) {
        return offsets_refuse(scanner,
                              "%" PRIu64 " bytes is not a multiple of the struct's alignment, %" PRIu64
                              ": write out the trailing padding",
                              record->size, record->align);
    }
    return true;
}

/* With the scanner just past "typedef struct". */
static bool offsets_take_struct(offsets_tool_t *tool, offsets_scanner_t *scanner) {
    if (scanner->token_kind == OFFSETS_TOKEN_WORD) {
        offsets_next(scanner); /* the tag; the typedef's name is what counts */
    }
    if (!offsets_token_is(scanner, "{")) {
        return true; /* typedef struct x_t x_t; declares no layout */
    }
    offsets_next(scanner);
    if (tool->struct_count == STRUCT_OFFSETS_MAX_STRUCTS) {
        return offsets_refuse(scanner, "more than %" PRIu64 " structs", STRUCT_OFFSETS_MAX_STRUCTS);
    }
    offsets_struct_t *record = &tool->borrowed_structs[tool->struct_count];
    memset(record, 0, sizeof(*record));
    record->align = 1u;
    record->borrowed_fields = offsets_arena_alloc(tool, STRUCT_OFFSETS_MAX_FIELDS * sizeof(offsets_field_t));
    if (record->borrowed_fields == nullptr || !offsets_take_fields(tool, scanner, record) ||
        !offsets_take_name(scanner, record->name)) {
        return false;
    }
    if (offsets_find_struct(tool, record->name) != nullptr) {
        return offsets_refuse(scanner, "'%s' was already read", record->name);
    }
    tool->struct_count++;
    return offsets_expect(scanner, ";");
}

static bool offsets_read_file(offsets_tool_t *tool, const char *path, char **out_text, uint64_t *out_length) {
    const int32_t fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "struct_offsets: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    char *text = offsets_arena_alloc(tool, STRUCT_OFFSETS_MAX_INPUT_BYTES);
    uint64_t length = 0;
    bool ok = text != nullptr;
    while (ok) {
        const int64_t got = read(fd, text + length, STRUCT_OFFSETS_MAX_INPUT_BYTES - length);
        if (got < 0) {
            fprintf(stderr, "struct_offsets: cannot read %s: %s\n", path, strerror(errno));
            ok = false;
        } else if (got == 0) {
            break;
        } else {
            length += (uint64_t)got;
            if (length == STRUCT_OFFSETS_MAX_INPUT_BYTES) {
                fprintf(stderr, "struct_offsets: %s is too large\n", path);
                ok = false;
            }
        }
    }
    (void)close(fd);
    *out_text = text;
    *out_length = length;
    return ok;
}

static bool offsets_read_header(offsets_tool_t *tool, const char *path) {
    char *text = nullptr;
    uint64_t length = 0;
    if (!offsets_read_file(tool, path, &text, &length)) {
        return false;
    }
    offsets_scanner_t scanner;
    memset(&scanner, 0, sizeof(scanner));
    scanner.borrowed_path = path;
    scanner.borrowed_cursor = text;
    scanner.borrowed_end = text + length;
    scanner.line = 1u;
    scanner.at_line_start = 1u;
    offsets_next(&scanner);
    while (scanner.token_kind != OFFSETS_TOKEN_END) {
        if (!offsets_token_is(&scanner, "typedef")) {
            offsets_next(&scanner);
            continue;
        }
        offsets_next(&scanner);
        if (!offsets_token_is(&scanner, "struct")) {
            continue;
        }
        offsets_next(&scanner);
        if (!offsets_take_struct(tool, &scanner)) {
            return false;
        }
    }
    return true;
}

[[gnu::format(printf, 2, 3)]] static void offsets_print(offsets_output_t *output, const char *format, ...) {
    const uint64_t room = STRUCT_OFFSETS_OUTPUT_BYTES - output->length;
    va_list args;
    va_start(args, format);
    const int32_t wrote = vsnprintf(output->borrowed_text + output->length, room, format, args);
    va_end(args);
    if (wrote < 0 || (uint64_t)wrote >= room) {
        output->overflowed = 1u;
        return;
    }
    output->length += (uint64_t)wrote;
}

/* The upper-case form of a name: NASM's defines follow the standard's rule for constants. */
static void offsets_upper(const char *name, char *out) {
    uint64_t i = 0;
    for (; name[i] != '\0'; i++) {
        out[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 'a' + 'A') : name[i];
    }
    out[i] = '\0';
}

/* After the last /include/, which is how the build's include roots spell the header. */
static const char *offsets_include_path(const char *path) {
    static const char marker[] = "/include/";
    const char *found = nullptr;
    for (const char *at = strstr(path, marker); at != nullptr; at = strstr(at + 1, marker)) {
        found = at;
    }
    return found != nullptr ? found + strlen(marker) : nullptr;
}

static void offsets_render_inc(const offsets_tool_t *tool, char **headers, uint32_t header_count,
                               offsets_output_t *output) {
    offsets_print(output, "; Generated by compile/src/struct_offsets. Do not edit. Read from:\n");
    for (uint32_t i = 0; i < header_count; i++) {
        offsets_print(output, ";   %s\n", offsets_include_path(headers[i]));
    }
    offsets_print(output, "%%ifndef MC_GENERATED_STRUCT_OFFSETS_INDEX_INC\n");
    offsets_print(output, "%%define MC_GENERATED_STRUCT_OFFSETS_INDEX_INC\n");
    for (uint64_t i = 0; i < tool->struct_count; i++) {
        const offsets_struct_t *record = &tool->borrowed_structs[i];
        char upper[STRUCT_OFFSETS_NAME_BYTES];
        offsets_upper(record->name, upper);
        offsets_print(output, "\n%%define %s_SIZEOF %" PRIu64 "\n", upper, record->size);
        offsets_print(output, "%%define %s_ALIGNOF %" PRIu64 "\n", upper, record->align);
        for (uint64_t j = 0; j < record->field_count; j++) {
            const offsets_field_t *field = &record->borrowed_fields[j];
            if (strncmp(field->name, "_pad", 4u) == 0) {
                continue; /* nothing has any business reaching for padding */
            }
            char field_upper[STRUCT_OFFSETS_NAME_BYTES];
            offsets_upper(field->name, field_upper);
            offsets_print(output, "%%define %s_%s %" PRIu64 "\n", upper, field_upper, field->offset);
        }
    }
    offsets_print(output, "\n%%endif\n");
}

static void offsets_render_h(const offsets_tool_t *tool, char **headers, uint32_t header_count,
                             offsets_output_t *output) {
    offsets_print(output, "/* Generated by compile/src/struct_offsets. Do not edit. Whatever includes this has the\n"
                          " * compiler itself confirm every number in the index.inc beside it. */\n");
    offsets_print(output, "#ifndef MC_GENERATED_STRUCT_OFFSETS_INDEX_H\n");
    offsets_print(output, "#define MC_GENERATED_STRUCT_OFFSETS_INDEX_H\n\n");
    offsets_print(output, "#include <stddef.h>\n");
    for (uint32_t i = 0; i < header_count; i++) {
        offsets_print(output, "#include \"%s\"\n", offsets_include_path(headers[i]));
    }
    for (uint64_t i = 0; i < tool->struct_count; i++) {
        const offsets_struct_t *record = &tool->borrowed_structs[i];
        offsets_print(output, "\nstatic_assert(sizeof(%s) == %" PRIu64 ");\n", record->name, record->size);
        offsets_print(output, "static_assert(alignof(%s) == %" PRIu64 ");\n", record->name, record->align);
        for (uint64_t j = 0; j < record->field_count; j++) {
            offsets_print(output, "static_assert(offsetof(%s, %s) == %" PRIu64 ");\n", record->name,
                          record->borrowed_fields[j].name, record->borrowed_fields[j].offset);
        }
    }
    offsets_print(output, "\n#endif\n");
}

static bool offsets_write_file(const char *directory, const char *name, const offsets_output_t *output) {
    char path[4096];
    const int32_t path_length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    if (path_length < 0 || (uint64_t)path_length >= sizeof(path)) {
        fprintf(stderr, "struct_offsets: the output path is too long\n");
        return false;
    }
    const int32_t fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        fprintf(stderr, "struct_offsets: cannot create %s: %s\n", path, strerror(errno));
        return false;
    }
    uint64_t written = 0;
    bool ok = true;
    while (ok && written < output->length) {
        const int64_t wrote = write(fd, output->borrowed_text + written, output->length - written);
        if (wrote <= 0) {
            fprintf(stderr, "struct_offsets: cannot write %s: %s\n", path, strerror(errno));
            ok = false;
        } else {
            written += (uint64_t)wrote;
        }
    }
    if (close(fd) != 0) {
        fprintf(stderr, "struct_offsets: cannot finish %s: %s\n", path, strerror(errno));
        ok = false;
    }
    return ok;
}

static int32_t offsets_run(offsets_tool_t *tool, const char *out_dir, char **headers, uint32_t header_count) {
    for (uint32_t i = 0; i < header_count; i++) {
        if (offsets_include_path(headers[i]) == nullptr) {
            fprintf(stderr, "struct_offsets: %s is not under an include/ directory\n", headers[i]);
            return STRUCT_OFFSETS_EXIT_REFUSED;
        }
        if (!offsets_read_header(tool, headers[i])) {
            return STRUCT_OFFSETS_EXIT_REFUSED;
        }
    }
    offsets_output_t inc = {.borrowed_text = offsets_arena_alloc(tool, STRUCT_OFFSETS_OUTPUT_BYTES)};
    offsets_output_t h = {.borrowed_text = offsets_arena_alloc(tool, STRUCT_OFFSETS_OUTPUT_BYTES)};
    if (inc.borrowed_text == nullptr || h.borrowed_text == nullptr) {
        return STRUCT_OFFSETS_EXIT_CANNOT_RUN;
    }
    offsets_render_inc(tool, headers, header_count, &inc);
    offsets_render_h(tool, headers, header_count, &h);
    if (inc.overflowed != 0u || h.overflowed != 0u) {
        fprintf(stderr, "struct_offsets: the output is larger than %" PRIu64 " bytes\n", STRUCT_OFFSETS_OUTPUT_BYTES);
        return STRUCT_OFFSETS_EXIT_CANNOT_RUN;
    }
    if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "struct_offsets: cannot create %s: %s\n", out_dir, strerror(errno));
        return STRUCT_OFFSETS_EXIT_CANNOT_RUN;
    }
    if (!offsets_write_file(out_dir, "index.inc", &inc) || !offsets_write_file(out_dir, "index.h", &h)) {
        return STRUCT_OFFSETS_EXIT_CANNOT_RUN;
    }
    return STRUCT_OFFSETS_EXIT_OK;
}

static bool offsets_tool_init(offsets_tool_t *tool) {
    memset(tool, 0, sizeof(*tool));
    void *region = mmap(nullptr, STRUCT_OFFSETS_ARENA_BYTES, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (region == MAP_FAILED) {
        fprintf(stderr, "struct_offsets: cannot map the arena: %s\n", strerror(errno));
        return false;
    }
    tool->owned_arena = region;
    tool->borrowed_structs = offsets_arena_alloc(tool, STRUCT_OFFSETS_MAX_STRUCTS * sizeof(offsets_struct_t));
    return true;
}

static void offsets_tool_deinit(offsets_tool_t *tool) {
    if (tool->owned_arena != nullptr) {
        (void)munmap(tool->owned_arena, STRUCT_OFFSETS_ARENA_BYTES);
        tool->owned_arena = nullptr;
    }
}

int32_t main(int32_t argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: struct_offsets <output directory> <header>...\n");
        return STRUCT_OFFSETS_EXIT_CANNOT_RUN;
    }
    offsets_tool_t tool;
    int32_t status = STRUCT_OFFSETS_EXIT_CANNOT_RUN;
    if (offsets_tool_init(&tool)) {
        status = offsets_run(&tool, argv[1], argv + 2, (uint32_t)argc - 2u);
    }
    offsets_tool_deinit(&tool);
    return status;
}
