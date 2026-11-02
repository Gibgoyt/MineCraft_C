/*
 * The tool itself: textures/import.yaml and a clone of an open pack -> textures/. Only the command line calls it, so this
 * header holds only its exit statuses. See compile/src/texture_pack/README.md. Its parts have their own headers below this
 * directory; mctex/ is the one a later tool wants.
 */
#ifndef TEXTURE_PACK_INDEX_H
#define TEXTURE_PACK_INDEX_H

#define TEXTURE_PACK_EXIT_OK 0
#define TEXTURE_PACK_EXIT_REFUSED 1 /* the pack, manifest or tree is wrong; a refused import has written nothing */
#define TEXTURE_PACK_EXIT_USAGE 2

#endif
