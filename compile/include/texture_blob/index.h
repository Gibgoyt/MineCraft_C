/* The tool itself: textures/ and their vector data -> one blob. Only the command line calls it, so this holds its exit statuses. See compile/src/texture_blob/README.md. */
#ifndef TEXTURE_BLOB_INDEX_H
#define TEXTURE_BLOB_INDEX_H

#define TEXTURE_BLOB_EXIT_OK 0
#define TEXTURE_BLOB_EXIT_REFUSED 1 /* the tree or its vector data is wrong */
#define TEXTURE_BLOB_EXIT_USAGE 2

#endif
