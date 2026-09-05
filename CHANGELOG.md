# Changelog

## 1.0.0

First release.

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
