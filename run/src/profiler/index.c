/* See README.md. Scopes write only their own lane, with no atomics and no heap. Everything that reads comes after those threads are done. */
#include "profiler/index.h"

#include "assert/index.h"
#include "log/index.h"
#include "time/index.h"
#include "types/index.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum : uint64_t {
    PROFILER_LINE_BYTES = 1024u,          /* the longest line it writes, escaped names and all */
    PROFILER_TEXT_BYTES = 256u * 1024u,   /* what a file gets at a time */
    PROFILER_RECORDS_DEFAULT = 1u << 17u, /* 4 MiB a lane: minutes, at a thread's usual few hundred scopes a second */
    PROFILER_RECORDS_MIN = 16u,
    /* The table's rows, then the room for text. */
    PROFILER_ROWS_BYTES = (uint64_t)PROFILER_SUMMARY_ROWS_MAX * sizeof(profiler_summary_row_t),
    PROFILER_WRITE_BYTES = PROFILER_ROWS_BYTES + PROFILER_TEXT_BYTES,
};
enum : uint32_t {
    /* A name is cut here in what is written. Each byte can become six escaped. */
    PROFILER_NAME_WRITTEN_MAX = 64u,
    PROFILER_NAME_ESCAPED_BYTES = PROFILER_NAME_WRITTEN_MAX * 6u + 1u,
    /* The game's main, sim and gpu threads and the card, besides the workers. */
    PROFILER_LANES_GAME = 4u,
};
static_assert(MC_THREADS_WORKERS + PROFILER_LANES_GAME <= PROFILER_LANES_MAX);
static_assert(PROFILER_NAME_ESCAPED_BYTES * 2u < PROFILER_LINE_BYTES);

profiler_limits_t profiler_limits_default(void) {
    return (profiler_limits_t){.records_per_lane = PROFILER_RECORDS_DEFAULT,
                               .lane_count = (uint32_t)MC_THREADS_WORKERS + PROFILER_LANES_GAME,
                               ._pad0 = {0}};
}

bool profiler_init(profiler_t *profiler, profiler_limits_t limits) {
    memset(profiler, 0, sizeof(*profiler));
    const uint64_t records = limits.records_per_lane;
    if (records < PROFILER_RECORDS_MIN || (records & (records - 1u)) != 0u || limits.lane_count == 0u ||
        limits.lane_count > PROFILER_LANES_MAX) {
        LOG_ERROR("profiler: %" PRIu64 " records a lane on %" PRIu32 " lanes will not do", records, limits.lane_count);
        return false;
    }
    const uint64_t lanes_bytes = (uint64_t)limits.lane_count * sizeof(profiler_lane_t);
    const uint64_t records_bytes = (uint64_t)limits.lane_count * records * sizeof(profiler_record_t);
    if (!heap_reserve(lanes_bytes + records_bytes + PROFILER_WRITE_BYTES, &profiler->owned_region)) {
        return false; /* it has said why */
    }
    uint8_t *const base = profiler->owned_region.owned_base;
    profiler->borrowed_lanes = (profiler_lane_t *)(void *)base;
    profiler->borrowed_write_bytes = base + lanes_bytes + records_bytes;
    profiler_record_t *const all_records = (profiler_record_t *)(void *)(base + lanes_bytes);
    for (uint32_t i = 0u; i < limits.lane_count; i++) {
        profiler->borrowed_lanes[i].borrowed_records = all_records + (uint64_t)i * records;
        profiler->borrowed_lanes[i].capacity = records;
    }
    profiler->limits = limits;
    profiler->started_ns = time_now_ns();
    return true;
}

void profiler_deinit(profiler_t *profiler) {
    heap_release(&profiler->owned_region);
    memset(profiler, 0, sizeof(*profiler));
}

/* ---- Lanes ------------------------------------------------------------------------------------------------------------ */

profiler_lane_t *profiler_lanes_claim(profiler_t *profiler, const char *prefix, uint32_t count) {
    if (profiler == nullptr || profiler->owned_region.owned_base == nullptr) {
        return nullptr;
    }
    /* Relaxed: this hands nothing over. Whoever is given a lane is the one thread looking at it, and the reading comes after a join. */
    uint32_t first = atomic_load_explicit(&profiler->lanes_claimed, memory_order_relaxed);
    do {
        if (count == 0u || count > profiler->limits.lane_count - first) {
            LOG_WARN("profiler: no room for %" PRIu32 " lanes of %s: it will not be timed", count, prefix);
            return nullptr;
        }
    } while (!atomic_compare_exchange_weak_explicit(&profiler->lanes_claimed, &first, first + count,
                                                    memory_order_relaxed, memory_order_relaxed));
    profiler_lane_t *const lanes = profiler->borrowed_lanes + first;
    for (uint32_t i = 0u; i < count; i++) {
        /* A byte, so that it can be seen to fit: PROFILER_LANES_MAX of them at most. */
        snprintf(lanes[i].name, sizeof(lanes[i].name), "%.11s %" PRIu8, prefix, (uint8_t)i);
    }
    return lanes;
}

profiler_lane_t *profiler_lane_claim(profiler_t *profiler, const char *name) {
    profiler_lane_t *const lane = profiler_lanes_claim(profiler, name, 1u);
    if (lane != nullptr) {
        snprintf(lane->name, sizeof(lane->name), "%.15s", name);
    }
    return lane;
}

/* ---- Scopes ----------------------------------------------------------------------------------------------------------- */

static void profiler_record_add(profiler_lane_t *lane, const char *name, int64_t start_ns, int64_t duration_ns,
                                int64_t self_ns) {
    lane->borrowed_records[lane->written & (lane->capacity - 1u)] =
        (profiler_record_t){.name = name, .start_ns = start_ns, .duration_ns = duration_ns, .self_ns = self_ns};
    lane->written++;
}

void profiler_begin_at(profiler_lane_t *lane, const char *name, int64_t now_ns) {
    if (lane == nullptr) {
        return;
    }
    if (lane->depth < PROFILER_DEPTH_MAX) {
        lane->stack[lane->depth] = (profiler_open_t){.name = name, .start_ns = now_ns, .child_ns = 0};
    } else {
        lane->too_deep++;
    }
    lane->depth++;
}

void profiler_end_at(profiler_lane_t *lane, int64_t now_ns) {
    if (lane == nullptr) {
        return;
    }
    if (lane->depth == 0u) {
        lane->unmatched_ends++;
        return;
    }
    lane->depth--;
    if (lane->depth >= PROFILER_DEPTH_MAX) {
        return; /* it was counted when it began, and what it took stays its parent's own */
    }
    const profiler_open_t *const open = &lane->stack[lane->depth];
    const int64_t duration_ns = now_ns > open->start_ns ? now_ns - open->start_ns : 0;
    const int64_t self_ns = duration_ns > open->child_ns ? duration_ns - open->child_ns : 0;
    profiler_record_add(lane, open->name, open->start_ns, duration_ns, self_ns);
    if (lane->depth > 0u) {
        lane->stack[lane->depth - 1u].child_ns += duration_ns;
    }
}

/* The clock is read last on the way in and first on the way out, so that a record is as little as can be of what it measures. */
void profiler_begin(profiler_lane_t *lane, const char *name) {
    if (lane != nullptr) {
        profiler_begin_at(lane, name, time_now_ns());
    }
}

void profiler_end(profiler_lane_t *lane) {
    if (lane != nullptr) {
        profiler_end_at(lane, time_now_ns());
    }
}

void profiler_span_add(profiler_lane_t *lane, const char *name, int64_t start_ns, int64_t duration_ns) {
    if (lane != nullptr) {
        const int64_t kept_ns = duration_ns > 0 ? duration_ns : 0;
        profiler_record_add(lane, name, start_ns, kept_ns, kept_ns);
    }
}

/* ---- Reading ---------------------------------------------------------------------------------------------------------- */

static uint32_t profiler_lanes_in_use(const profiler_t *profiler) {
    return atomic_load_explicit(&profiler->lanes_claimed, memory_order_relaxed);
}

static uint64_t profiler_lane_kept(const profiler_lane_t *lane) {
    return lane->written < lane->capacity ? lane->written : lane->capacity;
}

/* The i'th oldest of what a lane has kept. */
static const profiler_record_t *profiler_lane_record(const profiler_lane_t *lane, uint64_t i) {
    return &lane->borrowed_records[(lane->written - profiler_lane_kept(lane) + i) & (lane->capacity - 1u)];
}

/* The row with this name, or a new one, or nullptr if there is no room. Nearly every record is found by its pointer, as a name is one
 * literal in one place, and the text is looked at only for one that is not. */
static profiler_summary_row_t *profiler_summary_row(profiler_summary_row_t *rows, uint32_t capacity, uint32_t *count,
                                                    const char *name) {
    for (uint32_t i = 0u; i < *count; i++) {
        if (rows[i].name == name) {
            return &rows[i];
        }
    }
    for (uint32_t i = 0u; i < *count; i++) {
        if (strcmp(rows[i].name, name) == 0) {
            return &rows[i];
        }
    }
    if (*count == capacity) {
        return nullptr;
    }
    profiler_summary_row_t *const row = &rows[*count];
    *count += 1u;
    memset(row, 0, sizeof(*row));
    row->name = name;
    return row;
}

uint32_t profiler_summary_read(const profiler_t *profiler, profiler_summary_row_t *out, uint32_t capacity,
                               uint64_t *out_unlisted) {
    uint32_t count = 0u;
    uint64_t unlisted = 0u;
    const uint32_t lanes = profiler_lanes_in_use(profiler);
    for (uint32_t l = 0u; l < lanes; l++) {
        const profiler_lane_t *const lane = &profiler->borrowed_lanes[l];
        const uint64_t kept = profiler_lane_kept(lane);
        for (uint64_t i = 0u; i < kept; i++) {
            const profiler_record_t *const record = profiler_lane_record(lane, i);
            profiler_summary_row_t *const row = profiler_summary_row(out, capacity, &count, record->name);
            if (row == nullptr) {
                unlisted++;
                continue;
            }
            row->count++;
            row->total_ns += record->duration_ns;
            row->self_ns += record->self_ns;
            row->max_ns = record->duration_ns > row->max_ns ? record->duration_ns : row->max_ns;
            row->lane_mask |= 1u << l;
        }
    }
    /* Few rows, and this runs once, at the end. */
    for (uint32_t i = 1u; i < count; i++) {
        const profiler_summary_row_t moving = out[i];
        uint32_t j = i;
        while (j > 0u && out[j - 1u].self_ns < moving.self_ns) {
            out[j] = out[j - 1u];
            j--;
        }
        out[j] = moving;
    }
    if (out_unlisted != nullptr) {
        *out_unlisted = unlisted;
    }
    return count;
}

/* ---- Writing ---------------------------------------------------------------------------------------------------------- */

typedef struct profiler_writer_t {
    char *borrowed_text;         /* 0: PROFILER_TEXT_BYTES */
    const file_t *borrowed_file; /* 8 */
    uint64_t used;               /* 16 */
    file_status_t status;        /* 24: the first failure, after which nothing more goes out */
    uint8_t _pad0[7];            /* 25 */
} profiler_writer_t;
static_assert(sizeof(profiler_writer_t) == 32);

static void profiler_writer_flush(profiler_writer_t *writer) {
    if (writer->status == FILE_OK && writer->used != 0u) {
        writer->status = file_write_all(writer->borrowed_file, writer->borrowed_text, writer->used);
    }
    writer->used = 0u;
}

[[gnu::format(printf, 2, 3)]]
static void profiler_writer_line(profiler_writer_t *writer, const char *format, ...) {
    if (PROFILER_TEXT_BYTES - writer->used < PROFILER_LINE_BYTES) {
        profiler_writer_flush(writer);
    }
    va_list arguments;
    va_start(arguments, format);
    const int32_t length = vsnprintf(writer->borrowed_text + writer->used, PROFILER_LINE_BYTES, format, arguments);
    va_end(arguments);
    ASSERT_ALWAYS(length >= 0 && (uint64_t)length < PROFILER_LINE_BYTES);
    writer->used += (uint64_t)length;
}

/* As a JSON string's inside, whatever a name holds, and cut so that a line always fits. */
static void profiler_name_escape(const char *name, char *out) {
    static const char hex[] = "0123456789abcdef";
    uint32_t used = 0u;
    for (uint32_t i = 0u; i < PROFILER_NAME_WRITTEN_MAX && name[i] != '\0'; i++) {
        const uint8_t byte = (uint8_t)name[i];
        if (byte == '"' || byte == '\\') {
            out[used++] = '\\';
            out[used++] = (char)byte;
        } else if (byte < 0x20u || byte >= 0x7fu) {
            out[used++] = '\\';
            out[used++] = 'u';
            out[used++] = '0';
            out[used++] = '0';
            out[used++] = hex[byte >> 4u];
            out[used++] = hex[byte & 15u];
        } else {
            out[used++] = (char)byte;
        }
    }
    out[used] = '\0';
}

/* Nanoseconds as microseconds, exactly, with no float in the way. A span from before the profiler started shows at its start. */
static void profiler_micros(int64_t ns, int64_t *out_whole, int32_t *out_thousandths) {
    const int64_t kept = ns > 0 ? ns : 0;
    *out_whole = kept / 1000;
    *out_thousandths = (int32_t)(kept % 1000);
}

file_status_t profiler_trace_write(const profiler_t *profiler, const file_t *file) {
    profiler_writer_t writer = {.borrowed_text = (char *)profiler->borrowed_write_bytes + PROFILER_ROWS_BYTES,
                                .borrowed_file = file,
                                .used = 0u,
                                .status = FILE_OK,
                                ._pad0 = {0}};
    profiler_writer_line(&writer, "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n");
    profiler_writer_line(
        &writer, "{\"ph\":\"M\",\"pid\":1,\"tid\":0,\"name\":\"process_name\",\"args\":{\"name\":\"minecraft_c\"}}");
    char escaped[PROFILER_NAME_ESCAPED_BYTES];
    const uint32_t lanes = profiler_lanes_in_use(profiler);
    for (uint32_t l = 0u; l < lanes; l++) {
        const profiler_lane_t *const lane = &profiler->borrowed_lanes[l];
        const uint32_t tid = l + 1u;
        const uint64_t kept = profiler_lane_kept(lane);
        profiler_name_escape(lane->name, escaped);
        profiler_writer_line(&writer,
                             ",\n{\"ph\":\"M\",\"pid\":1,\"tid\":%" PRIu32
                             ",\"name\":\"thread_name\",\"args\":{\"name\":\"%s\"}}",
                             tid, escaped);
        profiler_writer_line(&writer,
                             ",\n{\"ph\":\"M\",\"pid\":1,\"tid\":%" PRIu32
                             ",\"name\":\"thread_sort_index\",\"args\":{\"sort_index\":%" PRIu32 "}}",
                             tid, tid);
        profiler_writer_line(&writer,
                             ",\n{\"ph\":\"M\",\"pid\":1,\"tid\":%" PRIu32
                             ",\"name\":\"mc_lane\",\"args\":{\"written\":%" PRIu64 ",\"lost\":%" PRIu64
                             ",\"unmatched_ends\":%" PRIu64 ",\"too_deep\":%" PRIu64 ",\"still_open\":%" PRIu32 "}}",
                             tid, lane->written, lane->written - kept, lane->unmatched_ends, lane->too_deep,
                             lane->depth);
        for (uint64_t i = 0u; i < kept; i++) {
            const profiler_record_t *const record = profiler_lane_record(lane, i);
            profiler_name_escape(record->name, escaped);
            int64_t start_us = 0, duration_us = 0;
            int32_t start_rest = 0, duration_rest = 0;
            profiler_micros(record->start_ns - profiler->started_ns, &start_us, &start_rest);
            profiler_micros(record->duration_ns, &duration_us, &duration_rest);
            profiler_writer_line(&writer,
                                 ",\n{\"ph\":\"X\",\"pid\":1,\"tid\":%" PRIu32 ",\"name\":\"%s\",\"ts\":%" PRId64
                                 ".%03" PRId32 ",\"dur\":%" PRId64 ".%03" PRId32 "}",
                                 tid, escaped, start_us, start_rest, duration_us, duration_rest);
        }
    }
    profiler_writer_line(&writer, "\n]}\n");
    profiler_writer_flush(&writer);
    return writer.status;
}

file_status_t profiler_trace_dump(const profiler_t *profiler, const char *path) {
    file_t file;
    file_status_t status = file_open(&file, path, FILE_MODE_WRITE);
    if (status == FILE_OK) {
        status = profiler_trace_write(profiler, &file);
        const file_status_t closed = file_close(&file);
        status = status == FILE_OK ? closed : status;
    }
    if (status != FILE_OK) {
        LOG_ERROR("profiler: could not write the trace to %s: %s", path, file_status_name(status));
    }
    return status;
}

file_status_t profiler_summary_write(const profiler_t *profiler, const file_t *file) {
    profiler_summary_row_t *const rows = (profiler_summary_row_t *)(void *)profiler->borrowed_write_bytes;
    uint64_t unlisted = 0u;
    const uint32_t count = profiler_summary_read(profiler, rows, PROFILER_SUMMARY_ROWS_MAX, &unlisted);
    profiler_writer_t writer = {.borrowed_text = (char *)profiler->borrowed_write_bytes + PROFILER_ROWS_BYTES,
                                .borrowed_file = file,
                                .used = 0u,
                                .status = FILE_OK,
                                ._pad0 = {0}};
    profiler_writer_line(&writer, "profiler: %-16s %12s %12s %10s %10s %6s\n", "lane", "scopes", "lost", "unmatched",
                         "too deep", "open");
    const uint32_t lanes = profiler_lanes_in_use(profiler);
    for (uint32_t l = 0u; l < lanes; l++) {
        const profiler_lane_t *const lane = &profiler->borrowed_lanes[l];
        if (lane->written != 0u || lane->unmatched_ends != 0u || lane->depth != 0u) {
            profiler_writer_line(
                &writer, "profiler: %-16s %12" PRIu64 " %12" PRIu64 " %10" PRIu64 " %10" PRIu64 " %6" PRIu32 "\n",
                lane->name, lane->written, lane->written - profiler_lane_kept(lane), lane->unmatched_ends,
                lane->too_deep, lane->depth);
        }
    }
    profiler_writer_line(&writer, "profiler: %-24s %10s %12s %12s %10s %10s %5s\n", "scope", "count", "total ms",
                         "self ms", "mean ms", "max ms", "lanes");
    for (uint32_t i = 0u; i < count; i++) {
        const profiler_summary_row_t *const row = &rows[i];
        uint32_t lane_total = 0u;
        for (uint32_t mask = row->lane_mask; mask != 0u; mask &= mask - 1u) {
            lane_total++;
        }
        profiler_writer_line(&writer, "profiler: %-24.64s %10" PRIu64 " %12.3f %12.3f %10.4f %10.4f %5" PRIu32 "\n",
                             row->name, row->count, (libc_double_t)time_ns_to_ms(row->total_ns),
                             (libc_double_t)time_ns_to_ms(row->self_ns),
                             (libc_double_t)(time_ns_to_ms(row->total_ns) / (float64_t)row->count),
                             (libc_double_t)time_ns_to_ms(row->max_ns), lane_total);
    }
    if (unlisted != 0u) {
        profiler_writer_line(&writer, "profiler: and %" PRIu64 " records of names past the first %" PRIu32 "\n",
                             unlisted, (uint32_t)PROFILER_SUMMARY_ROWS_MAX);
    }
    profiler_writer_flush(&writer);
    return writer.status;
}
