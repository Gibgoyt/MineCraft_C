/* The tool itself: config/ -> C tables. Only the command line calls it, so this holds its exit statuses. See compile/src/game_tables/README.md. */
#ifndef GAME_TABLES_INDEX_H
#define GAME_TABLES_INDEX_H

#define GAME_TABLES_EXIT_OK 0
#define GAME_TABLES_EXIT_REFUSED 1 /* a yaml file is wrong; every problem was printed with its line */
#define GAME_TABLES_EXIT_USAGE 2

#endif
