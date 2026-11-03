# run/src/ui/menu/catalog

**Purpose:** worlds on disk, as the menus see them: what is there, how to make another, and what a typed name and seed turn into.
**Tier:** Borrow: it fills what the caller gives it, and uses stack locals.
**Owns:** nothing.
**Lifetime:** of each call.
**Threads:** whichever calls it; it keeps no state. `ui_menu_t` calls it on `main`.
**Public functions** (`run/include/ui/menu/catalog/index.h`):

- `ui_menu_catalog_seed_from_text`, `ui_menu_catalog_name_clean`, `ui_menu_catalog_directory_name`: from what was typed to what is kept.
- `ui_menu_catalog_meta_parse`: a `world.meta`, exactly, or nothing.
- `ui_menu_catalog_create`, `ui_menu_catalog_read`: make one; list them all.

**May be called by:** `ui/menu`.
**Depends on:** `file`, and glibc's `opendir` and `readdir`, which are what list a directory.

## On disk

    <saves>/<directory>/world.meta

`world.meta` is exactly three lines, made with `file_write_atomic`, so it arrives whole or not at all:

    minecraft_c world 1
    seed 12345678901234567890
    name My World

The directory is the name with every byte but letters, digits, `-` and `_` turned into `_`, so that no name can climb out of `saves` or hide in it, and `-2`, `-3` and so on after it if that is taken. Stage 16 keeps a world's blocks beside this file.

A directory with no `world.meta` is not a world, and is not mentioned. One whose `world.meta` is wrong in any way, or that does not fit the list, is counted, and the menu says how many: nothing here deletes or rewrites it, because it may be the only copy of something.

## Seeds

Blank is a seed the caller picks. A whole number that fits 64 bits is that number, a negative one being its two's complement. Any other text is FNV-1a of it, so that a word gives one world on any machine. The numbers a seed turns into must never change: they are what a world is.
