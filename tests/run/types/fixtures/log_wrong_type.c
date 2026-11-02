/* Must not compile: an integer for %s, to show the LOG_ macros check more than floats. */
#include "log/index.h"
void fixture(void);
void fixture(void) {
    LOG_INFO("%s", 1);
}
