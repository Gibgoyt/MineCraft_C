# compile/src/texture_pack/support

**Purpose:** the arena, the file reads that cannot be led outside an untrusted tree, text and path checks, and atomic writes.
**Tier:** Arena (depth 2). It allocates only from the `tp_arena_t` it is given.
**Owns:** nothing. The arena's memory belongs to `compile/src/texture_pack/`.
**Lifetime:** whatever it returns lasts until the caller resets the arena.
**Threads:** one.
**Public functions** (`compile/include/texture_pack/support/index.h`):

- `tp_arena_alloc`, `tp_arena_text`, `tp_arena_format`: zeroed memory and text from the arena. Running out ends the process.
- `tp_fail`: prints a message and returns false.
- `tp_read_path`: reads a whole file we trust the path of.
- `tp_read_contained`: reads a file inside an untrusted tree, following no link and refusing anything but a regular file.
- `tp_open_contained_directory`: opens a directory there, under the same rule.
- `tp_name_is_safe`, `tp_path_is_safe`, `tp_text_is_plain`, `tp_bytes_contain`: checks.
- `tp_make_directories`, `tp_write_if_changed`: output, through a temporary and a rename.
- `tp_builder_begin`, `tp_builder_add`: a text with a fixed capacity.

**May be called by:** the other parts of `compile/src/texture_pack/`, and its Owner.
**Depends on:** glibc.
