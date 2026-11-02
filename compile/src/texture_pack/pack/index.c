/* See README.md. The pack is untrusted: nothing read from it reaches a file or a message but decoded pixels. */
#include "texture_pack/pack/index.h"

#include "png/index.h"
#include "texture_pack/mctex/index.h"

#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define TP_PACK_TEXT_MAX_BYTES (1ull << 20)
#define TP_PACK_REFS_MAX_BYTES (1ull << 24)
#define TP_PACK_PNG_MAX_BYTES (1ull << 24)
/* Enough for the raw image and IDAT of anything within MCTEX_MAX_PIXEL_BYTES, even at 16 bits a channel. */
#define TP_PACK_WORK_MAX_BYTES (1ull << 28)
#define TP_PACK_MAX_DIRECTORIES 4096u
#define TP_COMMIT_HEX_DIGITS 40u
#define TP_REF_PREFIX "ref: "
#define TP_REF_PREFIX_BYTES 5u

static uint64_t tp_line_length(const uint8_t *bytes, uint64_t length) {
    uint64_t line = 0;
    while (line < length && bytes[line] != '\n' && bytes[line] != '\r') {
        line++;
    }
    return line;
}

static bool tp_is_commit_hex(const uint8_t *bytes, uint64_t length) {
    if (length != TP_COMMIT_HEX_DIGITS) {
        return false;
    }
    for (uint64_t i = 0; i < length; i++) {
        const uint8_t c = bytes[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

/* The commit a ref holds, from its loose file or else from packed-refs. commit gets 40 bytes. */
static bool tp_pack_resolve_ref(tp_arena_t *arena, const char *pack, const char *ref, uint8_t *commit) {
    const char *loose = tp_arena_format(arena, ".git/%s", ref);
    uint8_t *bytes = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_contained(arena, pack, loose, TP_PACK_TEXT_MAX_BYTES, &bytes, &length);
    if (status == TP_READ_REFUSED) {
        return false;
    }
    if (status == TP_READ_OK) {
        const uint64_t line = tp_line_length(bytes, length);
        if (!tp_is_commit_hex(bytes, line)) {
            return tp_fail("%s: the ref %s does not hold a commit hash", pack, ref);
        }
        memcpy(commit, bytes, TP_COMMIT_HEX_DIGITS);
        return true;
    }
    const tp_read_status_t packed =
        tp_read_contained(arena, pack, ".git/packed-refs", TP_PACK_REFS_MAX_BYTES, &bytes, &length);
    if (packed == TP_READ_REFUSED) {
        return false;
    }
    if (packed == TP_READ_OK) {
        const uint64_t ref_length = strlen(ref);
        uint64_t offset = 0;
        while (offset < length) {
            const uint64_t line = tp_line_length(bytes + offset, length - offset);
            if (line == TP_COMMIT_HEX_DIGITS + 1u + ref_length &&
                tp_is_commit_hex(bytes + offset, TP_COMMIT_HEX_DIGITS) && bytes[offset + TP_COMMIT_HEX_DIGITS] == ' ' &&
                memcmp(bytes + offset + TP_COMMIT_HEX_DIGITS + 1u, ref, ref_length) == 0) {
                memcpy(commit, bytes + offset, TP_COMMIT_HEX_DIGITS);
                return true;
            }
            offset += line + 1u;
        }
    }
    return tp_fail("%s: the ref %s is in neither its own file nor packed-refs", pack, ref);
}

bool tp_pack_check_commit(tp_arena_t *arena, const char *pack, const tp_manifest_t *manifest) {
    const uint64_t mark = arena->used;
    uint8_t *head = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_contained(arena, pack, ".git/HEAD", TP_PACK_TEXT_MAX_BYTES, &head, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: no .git/HEAD, so its commit cannot be checked against the pin", pack);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    uint8_t commit[TP_COMMIT_HEX_DIGITS];
    const uint64_t line = tp_line_length(head, length);
    if (tp_is_commit_hex(head, line)) {
        memcpy(commit, head, TP_COMMIT_HEX_DIGITS);
    } else {
        if (line <= TP_REF_PREFIX_BYTES || memcmp(head, TP_REF_PREFIX, TP_REF_PREFIX_BYTES) != 0) {
            return tp_fail("%s: .git/HEAD holds neither a commit nor a ref", pack);
        }
        const char *ref = tp_arena_text(arena, head + TP_REF_PREFIX_BYTES, line - TP_REF_PREFIX_BYTES);
        if (!tp_path_is_safe(ref)) {
            return tp_fail("%s: .git/HEAD names a ref that is not a safe path", pack);
        }
        if (!tp_pack_resolve_ref(arena, pack, ref, commit)) {
            return false;
        }
    }
    if (memcmp(commit, manifest->commit, TP_COMMIT_HEX_DIGITS) != 0) {
        return tp_fail("%s: it is at commit %.40s, and %s pins %s", pack, (const char *)commit, manifest->path,
                       manifest->commit);
    }
    arena->used = mark;
    return true;
}

/* The value of `license = x` in a Minetest texture_pack.conf, or nullptr. Spaces around it do not count. */
static const char *tp_conf_licence(tp_arena_t *arena, const uint8_t *bytes, uint64_t length) {
    uint64_t offset = 0;
    while (offset < length) {
        const uint64_t line = tp_line_length(bytes + offset, length - offset);
        const char *text = (const char *)(bytes + offset);
        uint64_t start = 0;
        while (start < line && text[start] == ' ') {
            start++;
        }
        if (line - start >= 7u && memcmp(text + start, "license", 7) == 0) {
            uint64_t at = start + 7u;
            while (at < line && text[at] == ' ') {
                at++;
            }
            if (at < line && text[at] == '=') {
                at++;
                while (at < line && text[at] == ' ') {
                    at++;
                }
                uint64_t end = line;
                while (end > at && text[end - 1u] == ' ') {
                    end--;
                }
                return tp_arena_text(arena, text + at, end - at);
            }
        }
        /* After a CR, the LF reads as one empty line, which is fine. */
        offset += line + 1u;
    }
    return nullptr;
}

bool tp_pack_check_licence(tp_arena_t *arena, const char *pack, const tp_manifest_t *manifest) {
    const uint64_t mark = arena->used;
    for (uint32_t i = 0; i < manifest->evidence_count; i++) {
        const tp_evidence_t *evidence = &manifest->evidence[i];
        uint8_t *bytes = nullptr;
        uint64_t length = 0;
        const tp_read_status_t status =
            tp_read_contained(arena, pack, evidence->file, TP_PACK_TEXT_MAX_BYTES, &bytes, &length);
        if (status == TP_READ_MISSING) {
            return tp_fail("licence check: the pack has no %s, where it stated its licence when reviewed",
                           evidence->file);
        }
        if (status != TP_READ_OK) {
            return false;
        }
        if (!tp_bytes_contain(bytes, length, evidence->contains)) {
            return tp_fail("licence check: %s no longer says \"%s\"", evidence->file, evidence->contains);
        }
    }
    uint8_t *conf = nullptr;
    uint64_t conf_length = 0;
    const tp_read_status_t status =
        tp_read_contained(arena, pack, "texture_pack.conf", TP_PACK_TEXT_MAX_BYTES, &conf, &conf_length);
    if (status == TP_READ_REFUSED) {
        return false;
    }
    if (status == TP_READ_OK) {
        const char *stated = tp_conf_licence(arena, conf, conf_length);
        if (stated != nullptr && strcmp(stated, manifest->licence) != 0) {
            /* The pack's own bytes, so they are not repeated: only that they differ. */
            return tp_fail("licence check: texture_pack.conf states a licence other than %s", manifest->licence);
        }
    }
    arena->used = mark;
    return true;
}

/* The name of a file that could change what we may do with the pack, wherever it is. */
static bool tp_name_looks_like_licence(const char *name) {
    static const char *const prefixes[] = {"licen", "copying", "copyright", "notice", "unlicen", "credits", "authors"};
    for (uint32_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        if (strncasecmp(name, prefixes[i], strlen(prefixes[i])) == 0) {
            return true;
        }
    }
    return false;
}

/* Which manifest rows the walk has found so far. */
typedef struct tp_seen_t {
    uint8_t *entries; /* 0: one per manifest entry */
    uint8_t *overs;   /* 8: one per manifest entry, for its `over` file */
    uint8_t *skips;   /* 16: one per manifest skip */
} tp_seen_t;
static_assert(sizeof(tp_seen_t) == 24);

static bool tp_coverage_account(const tp_manifest_t *manifest, const char *file, const char *name, tp_seen_t *seen,
                                tp_coverage_t *out) {
    out->files++;
    bool accounted = false;
    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        if (strcmp(manifest->entries[i].from, file) == 0) {
            seen->entries[i] = 1;
            out->imported++;
            accounted = true;
        }
        if (manifest->entries[i].over != nullptr && strcmp(manifest->entries[i].over, file) == 0) {
            seen->overs[i] = 1;
            accounted = true;
        }
    }
    for (uint32_t i = 0; i < manifest->skip_count; i++) {
        if (strcmp(manifest->skips[i].file, file) == 0) {
            seen->skips[i] = 1;
            out->skipped++;
            accounted = true;
        }
    }
    bool is_evidence = false;
    for (uint32_t i = 0; i < manifest->evidence_count; i++) {
        if (strcmp(manifest->evidence[i].file, file) == 0) {
            out->evidence++;
            accounted = true;
            is_evidence = true;
        }
    }
    if (!is_evidence && tp_name_looks_like_licence(name)) {
        return tp_fail("coverage: %s looks like a licence or credits file, and no one has reviewed it: read it, then "
                       "add it to pack.evidence in %s",
                       file, manifest->path);
    }
    if (!accounted) {
        return tp_fail("coverage: %s is in the pack, and %s neither imports nor skips it", file, manifest->path);
    }
    return true;
}

/* One directory of the walk: files are accounted for, and directories go on the stack. */
static bool tp_coverage_directory(tp_arena_t *arena, const char *pack, const tp_manifest_t *manifest,
                                  const char *directory, const char **stack, uint32_t *depth, tp_seen_t *seen,
                                  tp_coverage_t *out) {
    const int32_t descriptor = tp_open_contained_directory(arena, pack, directory);
    if (descriptor < 0) {
        return false;
    }
    DIR *listing = fdopendir(descriptor);
    if (listing == nullptr) {
        close(descriptor);
        return tp_fail("%s: cannot list '%s'", pack, directory);
    }
    bool good = true;
    struct dirent *item = readdir(listing);
    while (good && item != nullptr) {
        const char *name = item->d_name;
        const bool is_dots = strcmp(name, ".") == 0 || strcmp(name, "..") == 0;
        /* Git's own, at the top of the clone only. Anything else hidden has to pass the checks below, and cannot. */
        const bool is_git = directory[0] == '\0' && (strcmp(name, ".git") == 0 || strcmp(name, ".gitignore") == 0);
        if (!is_dots && !is_git) {
            if (!tp_name_is_safe(name, strlen(name))) {
                good = tp_fail("coverage: a name in '%s' has bytes this tool will not repeat or import", directory);
            } else {
                const char *path = directory[0] == '\0' ? tp_arena_text(arena, name, strlen(name))
                                                        : tp_arena_format(arena, "%s/%s", directory, name);
                struct stat info;
                if (fstatat(dirfd(listing), name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
                    good = tp_fail("coverage: cannot look at %s", path);
                } else if (!S_ISDIR(info.st_mode)) {
                    good = tp_coverage_account(manifest, path, name, seen, out);
                } else if (*depth >= TP_PACK_MAX_DIRECTORIES) {
                    good = tp_fail("coverage: more than %u directories waiting", TP_PACK_MAX_DIRECTORIES);
                } else {
                    stack[*depth] = path;
                    *depth += 1u;
                }
            }
        }
        item = readdir(listing);
    }
    closedir(listing);
    return good;
}

bool tp_pack_check_coverage(tp_arena_t *arena, const char *pack, const tp_manifest_t *manifest, tp_coverage_t *out) {
    const uint64_t mark = arena->used;
    memset(out, 0, sizeof(*out));
    tp_seen_t seen = {.entries = tp_arena_alloc(arena, (uint64_t)manifest->entry_count + 1u),
                      .overs = tp_arena_alloc(arena, (uint64_t)manifest->entry_count + 1u),
                      .skips = tp_arena_alloc(arena, (uint64_t)manifest->skip_count + 1u)};
    /* Recursion is banned: an explicit stack of directories still to read. */
    const char **stack = tp_arena_alloc(arena, sizeof(stack[0]) * TP_PACK_MAX_DIRECTORIES);
    uint32_t depth = 1;
    stack[0] = "";
    while (depth > 0) {
        depth--;
        if (!tp_coverage_directory(arena, pack, manifest, stack[depth], stack, &depth, &seen, out)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < manifest->entry_count; i++) {
        if (seen.entries[i] == 0) {
            return tp_fail("coverage: %s:%u wants %s, which the pack does not have", manifest->path,
                           manifest->entries[i].line, manifest->entries[i].from);
        }
        if (manifest->entries[i].over != nullptr && seen.overs[i] == 0) {
            return tp_fail("coverage: %s:%u composites over %s, which the pack does not have", manifest->path,
                           manifest->entries[i].line, manifest->entries[i].over);
        }
    }
    for (uint32_t i = 0; i < manifest->skip_count; i++) {
        if (seen.skips[i] == 0) {
            return tp_fail("coverage: %s skips %s, which the pack does not have", manifest->path,
                           manifest->skips[i].file);
        }
    }
    arena->used = mark;
    return true;
}

/* One PNG from the pack, to RGBA8. Every limit is decided before any big allocation. */
static bool tp_pack_decode(tp_arena_t *arena, const char *pack, const char *file, uint8_t **out_pixels,
                           png_info_t *info) {
    uint8_t *bytes = nullptr;
    uint64_t length = 0;
    const tp_read_status_t status = tp_read_contained(arena, pack, file, TP_PACK_PNG_MAX_BYTES, &bytes, &length);
    if (status == TP_READ_MISSING) {
        return tp_fail("%s: not in the pack", file);
    }
    if (status != TP_READ_OK) {
        return false;
    }
    png_error_t error;
    if (png_probe(bytes, length, info, &error) != PNG_OK) {
        return tp_fail("%s: refused at byte %" PRIu64 ": %s", file, error.offset, error.message);
    }
    if (info->pixel_bytes > MCTEX_MAX_PIXEL_BYTES || info->work_bytes > TP_PACK_WORK_MAX_BYTES) {
        return tp_fail("%s: %ux%u is over this tool's budget", file, info->width, info->height);
    }
    uint8_t *pixels = tp_arena_alloc(arena, info->pixel_bytes);
    uint8_t *work = tp_arena_alloc(arena, info->work_bytes);
    if (png_decode(bytes, length, pixels, info->pixel_bytes, work, info->work_bytes, nullptr, &error) != PNG_OK) {
        return tp_fail("%s: refused at byte %" PRIu64 ": %s", file, error.offset, error.message);
    }
    *out_pixels = pixels;
    return true;
}

/* top over a fully opaque base. Rounds to nearest, in integers, so that it is the same on any machine. */
static void tp_composite_over_opaque(uint8_t *top, const uint8_t *base, uint64_t pixel_bytes) {
    for (uint64_t i = 0; i < pixel_bytes; i += 4u) {
        const uint32_t alpha = top[i + 3u];
        for (uint32_t c = 0; c < 3u; c++) {
            const uint32_t mixed =
                ((uint32_t)top[i + c] * alpha + (uint32_t)base[i + c] * (255u - alpha) + 127u) / 255u;
            top[i + c] = (uint8_t)mixed;
        }
        top[i + 3u] = 255u;
    }
}

bool tp_pack_load_entry(tp_arena_t *arena, const char *pack, const tp_entry_t *entry, tp_image_t *out) {
    memset(out, 0, sizeof(*out));
    png_info_t info;
    uint8_t *pixels = nullptr;
    if (!tp_pack_decode(arena, pack, entry->from, &pixels, &info)) {
        return false;
    }
    if (info.height % entry->frames != 0) {
        return tp_fail("%s: a height of %u does not divide into %u frames", entry->from, info.height, entry->frames);
    }
    const uint32_t frame_height = info.height / entry->frames;
    uint64_t expected = 0;
    if (!mctex_pixel_bytes(info.width, frame_height, entry->frames, &expected) || expected != info.pixel_bytes) {
        return tp_fail("%s: %ux%u in %u frames is over the .mctex limits", entry->from, info.width, frame_height,
                       entry->frames);
    }
    if (entry->over != nullptr) {
        png_info_t base_info;
        uint8_t *base = nullptr;
        if (!tp_pack_decode(arena, pack, entry->over, &base, &base_info)) {
            return false;
        }
        if (base_info.width != info.width || base_info.height != info.height) {
            return tp_fail("%s is %ux%u, and %s under it is %ux%u", entry->from, info.width, info.height, entry->over,
                           base_info.width, base_info.height);
        }
        for (uint64_t i = 3u; i < base_info.pixel_bytes; i += 4u) {
            if (base[i] != 255u) {
                return tp_fail("%s is not fully opaque, so it cannot be the base of %s", entry->over, entry->from);
            }
        }
        tp_composite_over_opaque(pixels, base, info.pixel_bytes);
    }
    *out = (tp_image_t){.borrowed_pixels = pixels,
                        .pixel_bytes = info.pixel_bytes,
                        .width = info.width,
                        .height = frame_height,
                        .frames = entry->frames,
                        ._pad0 = {0}};
    return true;
}
