# OYOBYOK

[![AI-DECLARATION: copilot](https://img.shields.io/badge/䷼%20AI--DECLARATION-copilot-fee2e2?labelColor=fee2e2)](https://ai-declaration.md)

> Own Your Own Bring Your Own Keyboard.

OYOBYOK is an alternative firmware for the BYOK writing device, written entirely in C on ESP-IDF. It keeps the parts of the device that make it lovely to write on, the paper-like screen, the five buttons, the Bluetooth keyboard, the SD card full of plain text files, and replaces the cloud with things you already own: a Git repository over SSH, or an SFTP server in your house. Your writing goes where you say it goes, signed with your key, and nowhere else.

> [!NOTE]
> BYOK is a trademark of [BYOK](https://byok.io/). This project is not affiliated with, endorsed by, or supported by them. OYOBYOK is an independent firmware written for people who bought a BYOK and want more control over it. Buy their device, it is a good one; then decide for yourself what runs on it. Note that using a custom firmware is your own decision and that the folks at BYOK reserve every right to terminate your license and support for this cause. 

> [!WARNING]
> This firmware has been built and tested on exactly one BYOK, a model B01 from the early batch. Flashing replaces the stock firmware. Read the flashing guide all the way through before you do anything, and back up the stock image first so you can always go back.

## What it does

Projects of plain text files on the SD card, with a proper editor. One-step Git Sync over SSH (commit, fetch, rebase, push; conflicts kept with markers, never blocking). SFTP push of a project to a machine of yours. A Bluetooth keyboard that pairs once and comes back on its own. WiFi that exists only while you are on the Synchronise page. Disk Mode for moving files over USB. A status LED that tells you what is going on. Contrast, layout, cursor and backlight settings that stick.

Everything is native C. There is no scripting layer, no runtime, no app store. The whole user interface is one state machine that also compiles on a laptop, so most of it was written and tested without touching the hardware.

## Building and flashing

You need ESP-IDF v5.3.2 with one small patch applied to its NimBLE HID host, the BYOK dock (plugged straight into a laptop the BYOK acts as a power source and never shows up as a serial port), and a backup of the stock firmware before you begin. On the stock firmware the serial port appears for a split second on connect, so getting into the bootloader means hammering `esptool` in a loop while holding the power button; the exact command is in the flashing guide. Once OYOBYOK is on, the port is stable and none of that is needed again.

```bash
cd ~/esp/esp-idf && git apply /path/to/OYOBYOK/firmware/patches/esp-idf-v5.3.2-nimble_hidh.patch
cd /path/to/OYOBYOK/firmware
idf.py build
./flash.sh
```

## Documentation

The docs site lives in [docs/](docs/) and is published to GitHub Pages. Start with [Flashing](docs/docs/getting-started/flashing.md) and [First boot](docs/docs/getting-started/first-boot.md), then [Writing](docs/docs/using/writing.md), [Git Sync](docs/docs/using/git-sync.md), [SFTP](docs/docs/using/sftp.md) and [Settings and Disk Mode](docs/docs/using/settings.md). The reference section covers [Configuration](docs/docs/reference/configuration.md), [Hardware](docs/docs/reference/hardware.md) (which GPIO does what) and [Architecture](docs/docs/reference/architecture.md) (how the firmware is put together and why).

Releases are listed in [CHANGELOG.md](CHANGELOG.md).

## Emulator

```bash
cd firmware/sim
make emu
printf 'down\nenter\n' | ./sim_emu
```

The emulator runs the same app core the device does. It reads key tokens from stdin, prints each frame to the terminal, and writes a PNG of it to `emu_frames/`. `make test` runs the Git sync test against a local bare repository and the LCD packing test.

## License

[MIT License](LICENSE).
