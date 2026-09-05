---
id: settings
title: Settings and Disk Mode
---

# Settings and Disk Mode

## Settings

Contrast adjusts the LCD bias live with UP and DOWN; Enter keeps it. Keyboard picks the layout the letters follow (QWERTY, QWERTZ, AZERTY, Dvorak). Cursor Type switches between a vertical bar and an underline. All three are stored in `/projects/.oyobyokprefs` on the card, so they survive a reflash.

The backlight is not in Settings; the LIGHT button (or Ctrl-L) cycles it through 100, 70, 40 and 15 percent at any time.

## Disk Mode

From the main menu. The SD card appears on your computer as a drive named OYOBYOK Disk. Copy files either way, then eject on the computer, unplug the device, and power it off and on again.

While in Disk Mode the buttons do nothing and the serial console is gone, both on purpose: the ESP32-S3 has one USB PHY, and Disk Mode hands it from the serial console to USB mass storage. A soft reset does not hand it back, which is why the way out is a power-cycle. On the dock the USB power keeps the board alive, so it has to be unplugged first; on battery, hold POWER until the screen goes dark.

## The status LED

| Colour | Meaning |
| --- | --- |
| amber, breathing | charging |
| green, steady | charged |
| red, blinking | battery below 15 percent |
| rainbow sweep | a sync or push is running |
| green flash | sync succeeded, or a file was saved |
| red blink | sync failed |
| green double-blink, then dim teal | keyboard connected |
| amber blink | keyboard lost |
| cyan, breathing | Disk Mode |
