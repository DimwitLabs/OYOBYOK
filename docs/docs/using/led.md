---
id: led
title: Status LED
---

# Status LED

The single RGB LED on the side is the device's only way of talking to you while the screen is busy or off. It has a resting colour, chosen by what is going on, and short flashes for things that just happened. A flash always wins for its second or two, then the resting colour comes back.

## Resting Colours

Listed in the order they take priority. Disk Mode beats everything, the keyboard is last.

| Colour | Meaning |
| --- | --- |
| cyan, breathing | Disk Mode; the card is a USB drive |
| rainbow, sweeping | a Git sync or SFTP push is running |
| red, blinking | battery below 15 percent and not on the charger |
| amber, breathing | charging |
| green, steady | charged |
| dim teal | a keyboard is connected and nothing else is happening |
| off | on battery, nothing to report |

## Flashes

| Colour | Meaning |
| --- | --- |
| green, two quick blinks | keyboard connected |
| amber, blinking | keyboard dropped; the device is looking for it |
| green, one soft pulse | a file was saved |
| green, steady for a moment | sync or push succeeded |
| red, blinking | sync or push failed |

At power-on the LED blips green once so you know the firmware is up before the screen is.

## Reading It from Across the Room

Rainbow means leave it alone, it is talking to a server. Red blinking with nothing else going on means find the charger. Dim teal is the normal state while you write with the device on battery; green steady is the normal state on the dock once it has filled up.
