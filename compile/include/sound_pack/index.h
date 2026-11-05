/* The tool itself: sounds/ -> one blob. Only the command line calls it, so this holds its exit statuses. See compile/src/sound_pack/README.md. */
#ifndef SOUND_PACK_INDEX_H
#define SOUND_PACK_INDEX_H

#define SOUND_PACK_EXIT_OK 0
#define SOUND_PACK_EXIT_REFUSED 1 /* the tree or the recording is wrong */
#define SOUND_PACK_EXIT_USAGE 2

#endif
