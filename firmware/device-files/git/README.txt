OYOBYOK: the git folder
=======================

This folder lives at /git on the SD card and is how the device reaches your servers. Put the device
into Disk Mode (main menu > Disk Mode), edit these files on the drive that appears, eject, then
power-cycle the device. Nothing is typed on the device itself.

FILES
-----
  remotes.conf   one [section] per project folder, plus an optional [identity] section
  sftp.conf      one [section] per SFTP server (optional)
  keys/          your SSH private key(s); the default name is keys/oyobyok_rsa with oyobyok_rsa.pub next to it
  known_hosts    filled in on the first SFTP connection; you can pre-seed it

KEYS
----
The SSH stack on the device uses mbedTLS, which handles RSA and ECDSA keys only. ed25519 keys will
not work. Generate a key in the classic PEM format on your computer:

  ssh-keygen -t rsa -b 3072 -m PEM -N "" -f oyobyok_rsa

Copy both oyobyok_rsa and oyobyok_rsa.pub into keys/, and add the .pub to your Git host (and to the
authorized_keys of any SFTP server). Keys never leave the device.

remotes.conf
------------
  [identity]
  name  = Your Name
  email = you@example.com

  [Drafts]
  remote = git@github.com:you/drafts.git
  branch = main
  key    = keys/oyobyok_rsa

The section name must match the project folder name under /projects exactly. Any Git host that
speaks SSH works.

HOW SYNC WORKS
--------------
Synchronise > Git > (project) > Sync does the whole round trip in one go: it commits whatever changed
on the device, fetches, rebases your commits on top of the remote, and pushes. If the same lines
changed on both sides the file keeps both versions with the usual <<<<<<< markers, so nothing is
lost and you can tidy it up on a computer. Status shows ahead/behind counts after a fetch.

sftp.conf
---------
  [Laptop]
  host        = 192.168.1.20
  port        = 22
  user        = you
  key         = keys/oyobyok_rsa
  remote_path = OYOBYOK

Synchronise > SFTP > (server) > (project) uploads that project folder into remote_path/<project>,
creating folders as needed. Files are only ever added or overwritten on the server, never deleted.
