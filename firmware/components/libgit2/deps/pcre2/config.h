/* config.h — hand-written for the OYOBYOK ESP-IDF port of libgit2's PCRE2.
 * Mirrors deps/pcre2/CMakeLists.txt "Static configuration" plus the
 * HAVE_* probes as they resolve on ESP-IDF newlib + xtensa GCC. */

#define HAVE_ASSERT_H 1
#define HAVE_ATTRIBUTE_UNINITIALIZED 1
#define HAVE_BUILTIN_MUL_OVERFLOW 1
#define HAVE_BUILTIN_UNREACHABLE 1
#define HAVE_DIRENT_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1

#define SUPPORT_PCRE2_8 1
#define SUPPORT_UNICODE 1

#define LINK_SIZE               2
#define HEAP_LIMIT              20000000
#define MATCH_LIMIT             10000000
#define MATCH_LIMIT_DEPTH       MATCH_LIMIT
#define MAX_VARLOOKBEHIND       255
#define NEWLINE_DEFAULT         2
#define PARENS_NEST_LIMIT       250
#define PCRE2GREP_BUFSIZE       20480
#define PCRE2GREP_MAX_BUFSIZE   1048576

#define MAX_NAME_SIZE           128
#define MAX_NAME_COUNT          10000
