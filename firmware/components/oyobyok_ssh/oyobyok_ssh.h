// SSH over libssh2 (mbedTLS backend): connect, verify the host key against known_hosts (trust on first
// use), authenticate with a key file, then run a command or push files over SFTP.
//
// Keys must be RSA or ECDSA in PEM form; the mbedTLS backend does not support ed25519.
#pragma once
#include <stdbool.h>
#include "libssh2.h"

typedef enum {
    OYOBYOK_SSH_OK = 0,
    OYOBYOK_SSH_E_NOKEY,
    OYOBYOK_SSH_E_NONET,
    OYOBYOK_SSH_E_DNS,
    OYOBYOK_SSH_E_CONNECT,
    OYOBYOK_SSH_E_HANDSHAKE,
    OYOBYOK_SSH_E_HOSTKEY,
    OYOBYOK_SSH_E_AUTH,
    OYOBYOK_SSH_E_MEM,
    OYOBYOK_SSH_E_INTERNAL,
} oyobyok_ssh_err_t;

const char* oyobyok_ssh_strerror(oyobyok_ssh_err_t e);

typedef struct {
    const char* host;
    int         port;         // 0 = 22
    const char* user;
    const char* privkey;      // absolute path of the PEM private key
    const char* pubkey;       // optional .pub path; NULL derives it from the private key (RSA only)
    const char* passphrase;   // NULL or "" for an unencrypted key
} oyobyok_ssh_opts_t;

typedef struct oyobyok_ssh_session oyobyok_ssh_session_t;

void oyobyok_ssh_init(void);                                  // once at boot
void oyobyok_ssh_set_config_dir(const char* dir);             // holds known_hosts; default /sdcard/git
bool oyobyok_ssh_have_key(const char* privkey_path);

oyobyok_ssh_err_t oyobyok_ssh_connect(const oyobyok_ssh_opts_t* o, oyobyok_ssh_session_t** out, char* err, int errlen);
void              oyobyok_ssh_disconnect(oyobyok_ssh_session_t** s);

// Run a remote command and capture up to outlen-1 bytes of its output.
oyobyok_ssh_err_t oyobyok_ssh_exec(oyobyok_ssh_session_t* s, const char* cmd, char* out, int outlen, int* exitcode);

// Upload a local directory tree into remote_dir, creating directories and overwriting files. Nothing is
// deleted on the server. Returns the number of files sent, or -1 with a message in err.
int oyobyok_ssh_sftp_push_dir(oyobyok_ssh_session_t* s, const char* local_dir, const char* remote_dir, char* err, int errlen);
int oyobyok_ssh_sftp_files_done(void);   // progress of the push in flight
