---
id: configuration
title: Configuration
---

# Configuration

Everything the device reads lives on the SD card. Nothing is typed on the device itself.

## The card

```
/projects/                 one folder per project, text files and sub-folders inside
/projects/.oyobyokprefs    contrast, keyboard layout, cursor style
/git/remotes.conf          Git remotes and identity
/git/sftp.conf             SFTP servers (optional)
/git/keys/                 SSH private keys
/git/known_hosts           SFTP host keys, filled in on first connection
```

Copy `firmware/device-files/git/` from the repository to `/git` on the card for a working starting point. Saved WiFi networks and the keyboard bond are kept in the device's own flash, not on the card.

## remotes.conf

One `[section]` per project folder, plus an optional `[identity]` section. Lines are `key = value` or `key: value`; `#` starts a comment.

```ini
[identity]
name  = Your Name
email = you@example.com

[Drafts]
remote = git@github.com:you/drafts.git
branch = main
key    = keys/oyobyok_rsa

[Novel]
remote = git@gitlab.com:you/novel.git
```

| Key | Default | Meaning |
| --- | --- | --- |
| `remote` | required | SSH URL of the repository |
| `branch` | `main` | branch to sync |
| `key` | `keys/oyobyok_rsa` | private key, relative to `/git`; the `.pub` is expected next to it |
| `name`, `email` (under `[identity]`) | `OYOBYOK`, placeholder | commit signature |

The section name must match the project folder under `/projects` exactly, including case. The file is read fresh for every operation, so edits made in Disk Mode take effect after the power-cycle.

## sftp.conf

One `[section]` per server; the name is what the menu shows.

```ini
[Laptop]
host        = 192.168.1.20
port        = 22
user        = you
key         = keys/oyobyok_rsa
remote_path = OYOBYOK
```

| Key | Default | Meaning |
| --- | --- | --- |
| `host` | required | hostname or address |
| `port` | `22` | |
| `user` | required | login |
| `key` | `keys/oyobyok_rsa` | private key, relative to `/git` |
| `remote_path` | `OYOBYOK` | directory on the server, relative to the login's home unless absolute; projects go inside it |

Up to eight servers.

## Keys

RSA or ECDSA in PEM form. ed25519 is not supported by the mbedTLS backend. For RSA the public key is derived from the private one if the `.pub` is missing; for ECDSA the `.pub` must be present.

```bash
ssh-keygen -t rsa -b 3072 -m PEM -N "" -f oyobyok_rsa
```

## .oyobyokprefs

Written by the device; three lines, `cursor=0|1`, `kb=0..3`, `contrast=0..255`. Safe to delete; the defaults come back.
