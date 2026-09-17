// A small SSH server that speaks only SFTP: one client at a time, public-key login only, one
// directory tree served. Written on mbedTLS (ECDH P-256 key exchange, ECDSA P-256 host key,
// aes128-ctr, hmac-sha2-256) plus TweetNaCl for ed25519 client keys.
#pragma once
#include <stdbool.h>

typedef struct {
    const char* root;
    const char* host_key_path;     // created here if missing
    const char* authorized_keys;   // optional
    const char* own_pubkey;        // whoever holds the matching private key may log in
    int         port;              // 0 = 22
} oyobyok_sshd_cfg_t;

// Blocks until oyobyok_sshd_stop(); needs a task whose stack is in internal RAM (WiFi DMA).
// -1 when it could not start, with the reason in err.
int  oyobyok_sshd_run(const oyobyok_sshd_cfg_t* cfg, char* err, int errlen);
void oyobyok_sshd_stop(void);

bool oyobyok_sshd_client_connected(void);
int  oyobyok_sshd_files_touched(void);   // since run() started

void sftpd_note_file(void);

// Implemented in oyobyok_sftpd.c.
typedef struct sftpd sftpd_t;
sftpd_t* sftpd_new(const char* root);
void     sftpd_free(sftpd_t* s);
// pkt is one request without its length prefix; -1 asks the transport to close the session.
int      sftpd_handle(sftpd_t* s, const unsigned char* pkt, int len,
                      int (*send)(void* ctx, const unsigned char* pkt, int len), void* ctx);
