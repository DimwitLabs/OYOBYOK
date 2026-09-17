# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `Move` in Manage mode: pick a file or folder, walk to another folder, `Move here`.
- `SFTP` > `Serve`: the device as an SFTP server over the Projects folder, public-key login with its own key pair or `/git/authorized_keys`.
- `SFTP` > `Send` can push one file or one folder, not only a whole project.
- Backspace, arrows and letters repeat after a short delay while the key stays down; the keyboard itself only reports changes.

### Changed

- `Synchronise` > `SFTP` is now a menu: `Serve` and `Send`. The old server list lives under `Send`.

### Fixed

- Git Sync no longer runs out of memory when pushing. libssh2's packet buffers now come from PSRAM, and the pack builder uses a 64 KB deflate buffer instead of 1 MB.

## [1.0.0] - 2026-09-05

### Added

- Native C firmware for the BYOK B01 on ESP-IDF 5.3.2. No scripting layer.
- Projects on the SD card with a file manager (new, rename, delete) and a word-wrapping editor with selection and clipboard.
- Git Sync over SSH with libgit2 and libssh2: commit, fetch, rebase, push in one step; conflicts kept with markers; Status with ahead/behind counts.
- SFTP push of a project folder to a configured server.
- BLE HID keyboard host on NimBLE with a single remembered keyboard, bonded, reconnected automatically by name.
- WiFi brought up only inside the Synchronise page.
- Disk Mode over USB mass storage.
- WS2812 status LED, battery gauge, charger status.
- Contrast, keyboard layout, cursor style and backlight settings persisted on the SD card.
- Desktop emulator that runs the same app core, with tests.
