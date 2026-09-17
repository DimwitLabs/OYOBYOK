TweetNaCl 20140427 (https://tweetnacl.cr.yp.to/), public domain, unchanged. Used only for
`crypto_sign_open` (ed25519 signature verification of SSH client keys). `randombytes` is
provided by oyobyok_sshd.c from the hardware RNG; the key generation paths are never called.
