/* See run/src/ui/menu/catalog/README.md. */
#ifndef MC_RUN_UI_MENU_CATALOG_INDEX_H
#define MC_RUN_UI_MENU_CATALOG_INDEX_H

#include "file/index.h"

#include <stdint.h>

enum : uint32_t {
    UI_MENU_NAME_BYTES = 32u,      /* a world's name, with its NUL: it fits a list row at any scale */
    UI_MENU_DIRECTORY_BYTES = 48u, /* a world's directory under the saves directory, with its NUL */
    UI_MENU_META_BYTES = 256u,     /* the most a world.meta can be: more than the three lines ever need */
    UI_MENU_META_VERSION = 1u,
};

/* One world, as far as the menus care. */
typedef struct ui_menu_world_t {
    uint64_t seed;                           /* 0 */
    char name[UI_MENU_NAME_BYTES];           /* 8 */
    char directory[UI_MENU_DIRECTORY_BYTES]; /* 40 */
} ui_menu_world_t;
static_assert(sizeof(ui_menu_world_t) == 88);

/* The seed a person's text means: blank is if_blank, a whole number that fits 64 bits is that number, a negative one being its
 * two's complement, and any other text a hash of itself, so that a word gives one world everywhere. Spaces round it count for
 * nothing. */
uint64_t ui_menu_catalog_seed_from_text(const char *text, uint64_t if_blank);

/* A name with the spaces round it gone, cut to what fits. False, with out empty, if nothing is left. */
bool ui_menu_catalog_name_clean(const char *name, char out[UI_MENU_NAME_BYTES]);

/* The directory a name asks for. Only letters, digits, - and _ survive, and each other byte becomes _, so no name can leave the
 * saves directory or be hidden in it. */
void ui_menu_catalog_directory_name(const char *name, char out[UI_MENU_NAME_BYTES]);

/* What a world.meta says, if it is exactly what ui_menu_catalog_create writes. Anything else, down to one wrong byte, is false.
 * directory in out is left alone. */
bool ui_menu_catalog_meta_parse(const char *bytes, uint64_t length, ui_menu_world_t *out);

/* Makes a new world's directory, a free name next to its wanted one, and its world.meta, which arrives whole or not at all.
 * The name must have something in it after ui_menu_catalog_name_clean. */
[[nodiscard]] file_status_t ui_menu_catalog_create(const char *saves, const char *name, uint64_t seed,
                                                   ui_menu_world_t *out);

/* Every world under saves, up to capacity, by name whatever its case, then by directory. No saves directory yet is no worlds.
 * What has no world.meta is no world and is ignored. What has one this cannot read, or that does not fit, is counted in
 * out_skipped. Past that, a failure is the directory's own, with no worlds. */
[[nodiscard]] file_status_t ui_menu_catalog_read(const char *saves, ui_menu_world_t *worlds, uint32_t capacity,
                                                 uint32_t *out_count, uint32_t *out_skipped);

#endif
