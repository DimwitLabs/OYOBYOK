/* Shim: ESP-IDF newlib ships <sys/poll.h> but no top-level <poll.h>.
 * libgit2's posix.h does #include <poll.h> when GIT_IO_POLL is set. */
#include <sys/poll.h>
