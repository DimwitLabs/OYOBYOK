---
id: flashing
title: Flashing
---

# Flashing

This is the long version. Please read it once before you plug anything in. The device has been flashed and recovered many times while writing this firmware, and every one of the notes below was earned.

## What you need

- A BYOK. This firmware was written against a model B01 from the early batch. Other revisions may differ in pins; see [Hardware](../reference/hardware).
- The BYOK dock, or another USB-A host path. Plugged straight into a USB-C laptop port the BYOK negotiates as a power source (it charges the laptop) and never shows up as a serial device. Through the dock it enumerates reliably. Everything below assumes the dock.
- ESP-IDF v5.3.2. Other 5.x versions will probably work but have not been tried. The [Espressif install guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/get-started/) is the reference; the short form on macOS is:

  ```bash
  mkdir -p ~/esp && cd ~/esp
  git clone -b v5.3.2 --recursive https://github.com/espressif/esp-idf.git
  cd esp-idf && ./install.sh esp32s3
  ```

  and then, in every shell you build from:

  ```bash
  source ~/esp/esp-idf/export.sh
  ```

## The one patch

The BLE keyboard host uses ESP-IDF's `esp_hid` component. Its NimBLE host has two behaviours that break real keyboards: it reads the HID report map before the link is encrypted (keyboards refuse, and the read never returns), and it waits forever for GATT callbacks that a dropped link will never deliver. The patch in `firmware/patches/` fixes both. Apply it once to your ESP-IDF checkout:

```bash
cd ~/esp/esp-idf
git apply /path/to/OYOBYOK/firmware/patches/esp-idf-v5.3.2-nimble_hidh.patch
```

Without it the firmware builds fine, but pairing a keyboard will hang or fail.

## Build

```bash
cd firmware
idf.py build
```

The first build fetches two managed components (`esp_tinyusb` for Disk Mode and `libssh2_esp` for SSH) and compiles libgit2, so it takes a few minutes. The result is `build/oyobyok.bin`, about 2 MB.

## Back up the stock firmware first

You want a way back. The stock image is 16 MB and reads out in about a minute:

```bash
esptool.py --chip esp32s3 -p /dev/cu.usbmodemXXXX read_flash 0x0 0x1000000 byok_stock_backup.bin
```

Keep that file somewhere safe. To restore the device to stock later, write it back to offset 0:

```bash
esptool.py --chip esp32s3 -p /dev/cu.usbmodemXXXX write_flash 0x0 byok_stock_backup.bin
```

Getting the stock device to sit still for that read is the tricky part, which brings us to the next section.

## Getting the stock firmware into the bootloader

On the stock firmware the USB serial port appears for a split second when you connect the device and then disappears. There is no button combination that helps. The trick is to hammer it: run a loop that watches for the port and fires `esptool` the instant it shows up, while you hold the power button down and keep holding it. Once esptool connects the chip is parked in the ROM bootloader and stays there; the loop prints the chip ID and stops.

```bash
export PATH="$HOME/esp/pyshim:$PATH"; source ~/esp/esp-idf/export.sh >/dev/null 2>&1; echo "HAMMERING - now HOLD the power button down and keep holding"; while true; do P=$(ls /dev/cu.* 2>/dev/null | grep -iE 'usbmodem|usbserial|wch|slab' | head -1); if [ -n "$P" ]; then esptool.py --port "$P" --before default_reset --after no_reset --connect-attempts 1 flash_id 2>&1 | grep -iE 'chip is|MAC|flash size' && break; fi; done
```

Start the loop first, then plug the device into the dock and hold POWER. When you see the chip and flash size lines, let go. The device is now in the bootloader; run the backup read and then the flash without unplugging anything.

A couple of notes on that line. The `pyshim` bit on the PATH is only there for machines where `python` is not on the path (ESP-IDF's tools want it); if `python` already works for you, drop it. The dock exposes its own serial port too, named something like `usbmodemSN234567892`; the device's port is the other one, usually `usbmodem1101` or `usbmodem11101`. The loop picks the first match, so if the dock's port wins, pass the device's port to `flash.sh` explicitly.

## Flash

```bash
cd firmware
./flash.sh                     # picks the first usbmodem port
./flash.sh /dev/cu.usbmodem1101   # or name it
```

The script writes the bootloader, partition table, application and OTA data in one go. Always flash all four: writing only the app leaves the OTA data pointing at the old slot and the device boot-loops.

Once OYOBYOK is on the device the serial port is stable, so from then on `flash.sh` connects on its own and you never need the hammer loop again. The console is on the same port at 115200 baud if you want to watch the boot log. With a keyboard paired, Ctrl-D from any menu reboots straight into the bootloader, which is handy while hacking.

## After flashing

The device restarts into the splash screen, then the main menu. From here, [First boot](first-boot) takes over.

## Recovery

Things that have gone wrong and how to get out of them.

**The screen is dark and nothing happens:** If the device still shows a serial port, it is not bricked. The ROM bootloader is in mask ROM and `esptool --before default_reset` reaches it whatever the application is doing. Flash again.

**No serial port at all, and the device is not in Disk Mode:** Disk Mode hands the single USB PHY from the serial console to USB-OTG, and a soft reset does not hand it back, which is why Disk Mode is exited by a power-cycle. If the port has gone missing for any reason, unplug the device, hold POWER until the screen goes dark (on the dock the USB power keeps the board alive, so it has to be unplugged), plug it back in. If that does not bring the port back, use the hammer loop above while holding POWER.

**The device turns itself off the moment it leaves the dock:** GPIO42 is the power keep-alive latch and the firmware drives it low as the very first thing in `app_main`. If you are hacking on the firmware and see this, that is where to look.

**Going back to stock:** Write the backup you made to offset 0 as shown above. Then, because stock keeps its own partition layout, the device is exactly as it was.
