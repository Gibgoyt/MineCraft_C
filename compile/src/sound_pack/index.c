/*
 * The Owner of the tool: maps its one arena, and runs one command. The build runs blob; a person runs verify and import.
 * See README.md.
 */
#include "sound_pack/index.h"

#include "sound_pack/blob/index.h"
#include "sound_pack/import/index.h"
#include "sound_pack/tree/index.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* Reserved, not used: only touched pages cost anything. */
#define SP_ARENA_BYTES (1ull << 32)

typedef enum sp_command_t : uint8_t {
    SP_COMMAND_BLOB = 0,
    SP_COMMAND_VERIFY = 1,
    SP_COMMAND_IMPORT = 2,
} sp_command_t;

typedef struct sp_arguments_t {
    sound_import_t import; /* 0: sounds is here for every command */
    const char *out;       /* 72 */
    uint8_t command;       /* 80: a sp_command_t */
    uint8_t _pad0[7];      /* 81 */
} sp_arguments_t;
static_assert(sizeof(sp_arguments_t) == 88);

static bool sp_arguments_parse(int32_t argc, char **argv, sp_arguments_t *out) {
    memset(out, 0, sizeof(*out));
    if (argc < 2) {
        return false;
    }
    if (strcmp(argv[1], "blob") == 0) {
        out->command = SP_COMMAND_BLOB;
    } else if (strcmp(argv[1], "verify") == 0) {
        out->command = SP_COMMAND_VERIFY;
    } else if (strcmp(argv[1], "import") == 0) {
        out->command = SP_COMMAND_IMPORT;
    } else {
        return false;
    }
    for (int32_t i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return false;
        }
        const char *const key = argv[i];
        const char *const value = argv[i + 1];
        const bool importing = out->command == SP_COMMAND_IMPORT;
        if (strcmp(key, "--sounds") == 0) {
            out->import.sounds = value;
        } else if (strcmp(key, "--out") == 0 && out->command == SP_COMMAND_BLOB) {
            out->out = value;
        } else if (strcmp(key, "--raw") == 0 && importing) {
            out->import.raw = value;
        } else if (strcmp(key, "--family") == 0 && importing) {
            out->import.family = value;
        } else if (strcmp(key, "--name") == 0 && importing) {
            out->import.name = value;
        } else if (strcmp(key, "--origin") == 0 && importing) {
            out->import.origin = value;
        } else if (strcmp(key, "--file") == 0 && importing) {
            out->import.file = value;
        } else if (strcmp(key, "--url") == 0 && importing) {
            out->import.url = value;
        } else if (strcmp(key, "--licence") == 0 && importing) {
            out->import.licence = value;
        } else if (strcmp(key, "--changes") == 0 && importing) {
            out->import.changes = value;
        } else {
            return false;
        }
    }
    if (out->import.sounds == nullptr) {
        return false;
    }
    switch (out->command) {
    case SP_COMMAND_BLOB:
        return out->out != nullptr;
    case SP_COMMAND_VERIFY:
        return true;
    default:
        return out->import.raw != nullptr && out->import.family != nullptr && out->import.name != nullptr &&
               out->import.origin != nullptr && out->import.file != nullptr && out->import.url != nullptr &&
               out->import.licence != nullptr && out->import.changes != nullptr;
    }
}

static bool sp_run(tp_arena_t *arena, const sp_arguments_t *arguments) {
    if (arguments->command == SP_COMMAND_IMPORT) {
        return sound_pack_import(arena, &arguments->import);
    }
    sound_tree_t tree;
    if (!sound_tree_load(arena, arguments->import.sounds, &tree)) {
        return false;
    }
    if (arguments->command == SP_COMMAND_BLOB) {
        return sound_pack_blob_write(arena, &tree, arguments->out);
    }
    printf("sound_pack: verified %s: %u sounds, %u recorded, %u made from recipes\n", arguments->import.sounds,
           tree.sound_count, tree.recorded_count, tree.sound_count - tree.recorded_count);
    return true;
}

int32_t main(int32_t argc, char **argv) {
    sp_arguments_t arguments;
    if (!sp_arguments_parse(argc, argv, &arguments)) {
        fprintf(stderr,
                "usage: sound_pack blob --sounds <directory> --out <file>\n"
                "       sound_pack verify --sounds <directory>\n"
                "       sound_pack import --sounds <directory> --raw <s16le mono 48000 Hz file> --family <family>\n"
                "                         --name <hit_1> --origin <text> --file <text> --url <text> --licence CC0-1.0\n"
                "                         --changes <text>\n");
        return SOUND_PACK_EXIT_USAGE;
    }
    void *owned_region =
        mmap(nullptr, SP_ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (owned_region == MAP_FAILED) {
        perror("sound_pack: mmap");
        return SOUND_PACK_EXIT_USAGE;
    }
    tp_arena_t arena = {.borrowed_base = owned_region, .capacity = SP_ARENA_BYTES, .used = 0};
    const bool good = sp_run(&arena, &arguments);
    munmap(owned_region, SP_ARENA_BYTES);
    return good ? SOUND_PACK_EXIT_OK : SOUND_PACK_EXIT_REFUSED;
}
