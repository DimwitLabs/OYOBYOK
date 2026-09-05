---
id: hardware
title: Hardware
---

# Hardware

What the BYOK B01 is made of and how the firmware talks to it. Everything on this page is what the firmware does today on the one device it was written against; a different board revision may move a pin or two.

## Overview

- ESP32-S3 with 16 MB flash and 2 MB quad-SPI PSRAM. One radio shared between WiFi and Bluetooth LE.
- 240 x 80 monochrome LCD on a UC1611s controller, spoken to over I2C.
- Five buttons, one WS2812 RGB LED, a backlight, a Li-ion cell with a charger, a micro-SD slot.
- USB-C through a TUSB320 role controller. The ESP32-S3's built-in USB-Serial/JTAG provides the console and flashing; the same PHY is re-routed to USB-OTG for Disk Mode.
- There is no separate Bluetooth co-processor on this board. The ESP32-S3 does the keyboard link itself.

## GPIO map

| GPIO | Function | Notes |
| --- | --- | --- |
| 42 | Power keep-alive latch | Output, active low. Driven low first thing at boot and held there. Driving it high powers the device off on battery. |
| 2 | Backlight | LEDC PWM. |
| 18 | LCD SDA | Software I2C, about 150 kHz. |
| 10 | LCD SCL | |
| 17 | LCD reset line | Held low for the whole run; the panel is reset in software with the 0xE1/0xE2 commands. |
| 35 | LCD A0 | Held low. |
| 47 | LCD panel enable | Held low. |
| 39 | Display auxiliary line | Held low. |
| 6 | POWER button | Input, pull-up, active low. |
| 16 | EXECUTE button | Input, pull-up, active low. |
| 11 | LIGHT button | Input, pull-up, active low. |
| 15 | UP button | Input, pull-up, active low. |
| 7 | DOWN button | Input, pull-up, active low. |
| 14 | WS2812 status LED | RMT, GRB order. |
| 1 | Battery voltage | ADC1 channel 0, 12 dB attenuation, through a divider: Vbat = Vpin x 1.515. |
| 12 | Charger CHRG | Input, pull-up, low while charging. |
| 13 | Charger DONE | Input, pull-up, low when full. |
| 5 | SD CLK | SDMMC slot 1, 1-bit mode, 40 MHz. |
| 3 | SD CMD | |
| 4 | SD D0 | |
| 8, 9 | I2C to the TUSB320 | Port 1 at 400 kHz, address 0x60. The firmware does not touch it; the USB role is fixed by hardware strapping. |
| 19, 20 | USB D-, D+ | The one USB PHY. |
| 43, 44 | UART0 | Unused by this firmware. |
| 26 to 32 | Flash and PSRAM | |
| 0, 45, 46 | Strapping pins | |
| 36, 37 | UART1 | Present on the stock board for a co-processor that this revision does not have. Unused. |

## The LCD

The controller is a UC1611s. Two I2C addresses: 0x38 takes command opcodes, 0x39 takes command parameters and pixel data. Every byte is its own I2C transaction. The visible 80 rows are controller pages 10 to 19, so the driver writes with a page offset of 10. The framebuffer is row-major (30 bytes per row, MSB on the left) and is packed into the controller's page format (each byte is 8 vertical pixels) on the way out; only pages that changed since the last flush are sent. The default bias (VBIAS) is 0xB0 and Settings > Contrast changes it live.

## Power

The device stays on because GPIO42 is held low. The firmware asserts it before anything else runs. When USB power is present the rail is held up regardless, which is why on the dock a long POWER press reboots rather than powers off.

Battery percentage comes from 64 averaged ADC samples with the calibrated curve, then a Li-ion voltage-to-percent table. Charger state comes from the two open-drain status lines. Both are polled once a second by a small task that also drives the LED's charge states.

## Radio

WiFi and BLE share one radio and, more to the point, one pool of internal DMA-capable RAM. WiFi's frame buffers must live there, so the firmware keeps WiFi off until it is needed (the Synchronise page), gives the keyboard a wide connection interval and hands the coexistence arbiter to WiFi while a network operation runs, then restores a fast interval for typing. Bringing WiFi up costs about 70 KB of internal RAM.

## Flash layout

| Offset | Size | What |
| --- | --- | --- |
| 0x0 | | bootloader |
| 0x8000 | | partition table |
| 0x9000 | 16 KB | NVS (WiFi networks, keyboard bond) |
| 0xF000 | 4 KB | PHY calibration |
| 0x10000 | 3 MB | factory app |
| 0x310000 | 3 MB | ota_0 (unused) |
| 0x610000 | 3 MB | ota_1 (unused) |
| 0x910000 | 8 KB | OTA data |

The layout matches the stock firmware's app slots so that restoring the stock image puts everything back exactly.

## Things to leave alone

If you are extending the firmware: do not toggle GPIO19/20 (USB, the device loses its serial port until the battery is drained or it is power-cycled), do not drive GPIO42 high unless you mean to power off, and do not sweep unknown GPIOs looking for peripherals. The list above is what is known to be safe.
