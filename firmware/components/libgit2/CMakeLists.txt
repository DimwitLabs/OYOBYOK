# libgit2 v1.8.7 vendored as an ESP-IDF component (OYOBYOK port).
#
# SSH-only, single-threaded build. Backends are pinned by the hand-written
# port/git2_features.h (NOT libgit2's CMake): libssh2 (skuodi/libssh2_esp),
# mbedTLS SHA, bundled PCRE2 regex, bundled llhttp parser, bundled zlib.
#
# We do NOT run libgit2's own CMake — we mirror its source lists here so the
# whole thing builds as one IDF component. Disabled backends (OpenSSL, WinHTTP,
# Schannel, ssh_exec, GSSAPI, NTLM) are self-guarded by #ifdef and compile to
# empty objects, so we can glob broadly.

set(LG2 ${CMAKE_CURRENT_SOURCE_DIR})

# --- Core library + selected util (skip win32/, pick mbedTLS hash only) ---
file(GLOB LIBGIT2_SRC
    ${LG2}/src/libgit2/*.c
    ${LG2}/src/libgit2/streams/*.c
    ${LG2}/src/libgit2/transports/*.c
    ${LG2}/src/util/*.c
    ${LG2}/src/util/allocators/*.c
    ${LG2}/src/util/unix/*.c
    ${LG2}/src/util/hash/mbedtls.c
)
# unix/process.c forks/execve — no such thing on ESP32, and nothing references
# its symbols (git_process__is_cmdline_option is a header inline; ssh_exec is off).
list(REMOVE_ITEM LIBGIT2_SRC ${LG2}/src/util/unix/process.c)
# unix/map.c needs <sys/mman.h>/mmap — absent on ESP-IDF. Replaced by
# port/map_emu.c (malloc + read/write-back). Added to SRCS below.
list(REMOVE_ITEM LIBGIT2_SRC ${LG2}/src/util/unix/map.c)

# --- Bundled deps: xdiff (diff), pcre2 (regex), llhttp (http parser), zlib ---
file(GLOB PCRE2_SRC ${LG2}/deps/pcre2/*.c)
list(REMOVE_ITEM PCRE2_SRC ${LG2}/deps/pcre2/pcre2_fuzzsupport.c)

file(GLOB XDIFF_SRC  ${LG2}/deps/xdiff/*.c)
file(GLOB LLHTTP_SRC ${LG2}/deps/llhttp/*.c)
file(GLOB ZLIB_SRC   ${LG2}/deps/zlib/*.c)

idf_component_register(
    SRCS ${LIBGIT2_SRC} ${PCRE2_SRC} ${XDIFF_SRC} ${LLHTTP_SRC} ${ZLIB_SRC}
         "port/map_emu.c" "port/posix_stubs.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "port"
        "include"
        "src/libgit2"
        "src/util"
        "deps/xdiff"
        "deps/pcre2"
        "deps/llhttp"
        "deps/zlib"
    REQUIRES mbedtls skuodi__libssh2_esp lwip
)

# PCRE2 wants its config.h + a code-unit width; scope these to PCRE2 sources only.
# PCRE2_EXPORT= (empty) mirrors libgit2's cmake/SelectRegex.cmake — without it
# pcre2_internal.h expands PCRE2_EXP_DECL to `extern PCRE2_EXPORT` (undefined).
set_source_files_properties(${PCRE2_SRC}
    PROPERTIES COMPILE_DEFINITIONS "HAVE_CONFIG_H;PCRE2_CODE_UNIT_WIDTH=8;PCRE2_EXPORT="
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# Vendored C90 code trips a lot of IDF's warnings; silence them for this component.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
