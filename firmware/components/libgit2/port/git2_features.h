/*
 * git2_features.h — hand-written for the ESP32-S3 (ESP-IDF) OYOBYOK port.
 *
 * This replaces the file libgit2's CMake would normally generate from
 * src/util/git2_features.h.in. It selects an SSH-only, single-threaded build:
 *   - SSH via bundled libssh2 (skuodi/libssh2_esp), in-memory credentials
 *   - SHA1/SHA256 via ESP-IDF's mbedTLS (3.x, unsuffixed API)
 *   - Regex via bundled PCRE2 (deps/pcre2)
 *   - HTTP parser via bundled llhttp (deps/llhttp) — http.c/httpclient.c
 *     compile unconditionally on non-Windows, so a parser backend is required
 *     even though we never open an https:// remote
 *   - No threads, no HTTPS/TLS stream, no NTLM/GSSAPI
 */
#ifndef INCLUDE_features_h__
#define INCLUDE_features_h__

/* ESP32-S3 is a 32-bit target. */
#define GIT_ARCH_32 1

/* Single-threaded: no GIT_THREADS (libgit2 skips all internal locking). */

/* SSH transport: bundled libssh2, pass keys from memory. */
#define GIT_SSH 1
#define GIT_SSH_LIBSSH2 1
#define GIT_SSH_LIBSSH2_MEMORY_CREDENTIALS 1

/* Regex: libgit2's bundled PCRE2 (deps/pcre2). */
#define GIT_REGEX_BUILTIN 1

/* HTTP parser: libgit2's bundled llhttp (deps/llhttp). */
#define GIT_HTTPPARSER_BUILTIN 1

/* Object hashing via mbedTLS — matches ESP-IDF mbedTLS 3.x exactly. */
#define GIT_SHA1_MBEDTLS 1
#define GIT_SHA256_MBEDTLS 1

/* I/O multiplexing: lwIP provides poll(). */
#define GIT_IO_POLL 1

/* Random seed: ESP-IDF newlib provides getentropy(). */
#define GIT_RAND_GETENTROPY 1

#endif
