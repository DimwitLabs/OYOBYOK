---
id: architecture
title: Architecture
---

# Architecture

How the firmware is put together, and the reasons behind the shape of it.

## One app core, two hosts

The whole user interface lives in `firmware/shared/oyobyok_app.inc`: menus, the file manager, the editor, the settings screens, the pickers, the busy alerts, and the glue that calls out to the device. It is a plain C state machine that draws into a 240 x 80 one-bit framebuffer (`render.h`) and takes keys through a handful of functions: `app_nav`, `app_type`, `app_ctrl`, `app_esc`, `app_enter`.

Two programs include that file. The device host (`components/oyobyok_host`) reads keys from a FreeRTOS queue, renders a frame after each burst of input, and pushes the framebuffer to the LCD. The desktop emulator (`sim/sim_emu.c`) reads key tokens from stdin and writes each frame to a PNG. Everything device-specific reaches the core through function-pointer hooks registered by `oyobyok_app_set_*`; the emulator leaves them NULL and the core hides what is not there. This is why nearly all of the UI was written and debugged on a laptop.

## Components

| Component | Does |
| --- | --- |
| `oyobyok_lcd` | UC1611s over software I2C, page-format packing, dirty-page flush, contrast. |
| `oyobyok_buttons` | Debounced polling of the five buttons into key events. |
| `oyobyok_power` | Power latch, backlight, charger status, battery gauge, download-mode reboot. |
| `oyobyok_led` | WS2812 state machine: latched conditions resolved by priority, event flashes on top. |
| `oyobyok_wifi` | Station mode, known networks in NVS, scan, connect, SNTP once an address arrives. |
| `oyobyok_blehid` | NimBLE HID host: scan, bond, remember, reconnect by name, report parsing. |
| `oyobyok_ssh` | libssh2: connect, host key trust on first use, key auth, exec, SFTP directory push. |
| `oyobyok_git` | libgit2 implementation of the Git engine interface. |
| `oyobyok_disk` | USB mass storage over the SD card for Disk Mode. |
| `oyobyok_host` | The app core on the device, plus the HID-usage-to-key map with layouts. |
| `libgit2` | Vendored 1.8.7. |

`main/oyobyok_main.c` brings everything up in order (power latch, NVS, SD, LCD, buttons, LED, SSH, WiFi, BLE), registers the hooks, and starts the host loop.

## Keys and events

Every input is an `oyobyok_key_t`: a printable character, a navigation key (with a shift flag), or a control code. The keyboard, the buttons, and the background tasks all post into one queue. Background results are control codes above 0x80 (`OYOBYOK_CTL_*` in `oyobyok_host.h`): the WiFi task posts one when a connection succeeds or fails, the network task posts one when a sync finishes. The host loop turns each into a call on the app core, so all UI state changes on the one thread that owns it.

## The radio model

WiFi exists only inside the Synchronise page. Entering the page brings it up and connects to the last known network; leaving takes it down, unless a network task is still running, in which case the task's completion handler releases it. Outside that page the BLE keyboard has the radio and the internal RAM to itself. WiFi's frame buffers must live in internal DMA-capable RAM and cost about 70 KB, which is most of what is free once NimBLE is up, so this rule is what makes the keyboard and the network coexist at all.

While a network operation runs, the keyboard link is widened to an 80 to 100 ms interval and the coexistence arbiter is told to prefer WiFi. Afterwards the interval goes back to 10 to 20 ms for typing.

## Memory

Internal RAM is the scarce resource. The strategy is to keep small, frequent allocations internal (so LWIP's buffers stay DMA-safe) and to send the known heavy consumers to PSRAM by name: the NimBLE host heap, mbedTLS, libgit2 through its allocator hook, libssh2 through its session allocator. Hardware AES and SHA are turned off because the accelerators DMA their buffers, which cannot be in PSRAM; software crypto is fine at the sizes involved.

Git, SSH and SFTP run on one background task with a 40 KB stack in internal RAM. The stack is allocated once at boot, before WiFi and BLE fragment the heap, and reused for every operation; only one runs at a time. It has to be internal because the WiFi transmit path hands stack-resident buffers to DMA.

libgit2 is vendored with two changes: its default file I/O buffer is 4 KB instead of 64 KB (those buffers live on the stack and were the source of every heap corruption seen during development), and its directory removal tolerates FATFS refusing to delete a non-empty directory.

## Sync

`git_sync` is one round trip that either completes or leaves the repository as it found it:

1. Clean up any interrupted state (a leftover rebase, a detached HEAD).
2. Stage everything and commit if the tree differs from HEAD, with the message `oyobyok: update from device on <date>`.
3. Fetch the full history.
4. Compare to the remote tip. Equal: nothing to do. Strictly ahead: push. Strictly behind: fast-forward and check out. Diverged: rebase local commits onto the remote tip. A conflicting file is kept exactly as checked out, markers included, and staged as the resolution, so the device never stops on a conflict.
5. Push. If fetch or rebase failed, the commit from step 2 is rolled back first.

The desktop engine in `sim/git_engine.c` does the same with the git binary and is what the tests exercise.

## Busy alerts

Long operations put up an alert with an elapsed counter. The host loop wakes once a second while one is showing, and the core enforces a timeout per operation (a couple of minutes for Git, longer for SFTP). Esc dismisses the alert without cancelling the work; the LED reports the result when it lands. Enter is ignored while an operation is running so a stray key cannot dismiss it early.

## The keyboard link

The keyboard is remembered in NVS: address, address type and name. Keyboards advertise with a rotating private address, so a reconnect scans for the saved name first and opens whatever address it is using right now. The stored bond is reused; it is wiped and re-paired only after an attempt stalled in pairing, which means the two sides disagree about the key. A manual pair always starts with a clean bond, because a multi-device keyboard may have rotated its keys while talking to something else.

The ESP-IDF patch in `patches/` makes NimBLE's HID host initiate encryption before it reads the report map, bound its waits for GATT callbacks, and cancel a pending connection properly on timeout.

## The emulator and tests

`make emu` in `sim/` builds the app core against the desktop Git engine. `make test` runs a sync test against a local bare repository (first push, a merge from a second clone, a same-line conflict carried through with markers) and a packing test for the LCD page format.
