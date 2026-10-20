/* ghost-common's ghostlink.c, built for the tests. At -O0 GCC warns that a
 * date string there "may be truncated" (it cannot be: the fields are a
 * calendar date). That file is shared by every game and is not Blockfall's
 * to edit, so the warning is silenced for it alone and the rest of the test
 * build stays -Wall -Wextra clean. */
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include "ghostlink.c"
