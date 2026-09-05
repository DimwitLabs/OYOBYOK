---
id: intro
title: Overview
sidebar_label: Overview
slug: /
---

# Overview

OYOBYOK is an alternative firmware for the BYOK writing device, written entirely in C on ESP-IDF. It keeps the parts of the device that make it lovely to write on, the paper-like screen, the five buttons, the Bluetooth keyboard, the SD card full of plain text files, and replaces the cloud with things you already own: a Git repository over SSH, or an SFTP server in your house. Your writing goes where you say it goes, signed with your key, and nowhere else.

:::note[About the name and the trademark]
OYOBYOK is Own Your Own Bring Your Own Keyboard. BYOK is a trademark of [BYOK](https://byok.io/). This project is not affiliated with, endorsed by, or supported by them. It is an independent firmware written for people who bought a BYOK and want more control over it. Buy their device, it is a good one; then decide for yourself what runs on it.
:::

:::warning[One device]
This firmware has been built and tested on exactly one BYOK, a model B01 from the early batch. Flashing replaces the stock firmware. Read [Flashing](getting-started/flashing) all the way through before you do anything, and back up the stock image first so you can always go back.
:::

## Why

The BYOK is a distraction-free writing device: a wide monochrome LCD, a battery, an SD card, and a Bluetooth link to whatever keyboard you already love. The stock firmware syncs your drafts through the maker's cloud. I wanted the same device with my own plumbing underneath: a Git remote I control, an identity I choose, a commit for every session, and no account in between me and my files. So I wrote the firmware that does that, and nothing else.

Everything is native C. There is no scripting layer, no runtime, no app store. The whole user interface is one state machine that also compiles on a laptop, so most of it was written and tested without touching the hardware.

## What it does

- **Projects.** Folders of plain text on the SD card. Make a project, make files and sub-folders inside it, rename and delete from the device. The editor wraps words, has a cursor you can move around with the arrow keys, Home/End, Page Up/Down, Shift-select, and copy, cut and paste.
- **Git Sync.** One verb. Sync commits whatever changed on the device, fetches, rebases your commits on top of the remote, and pushes. Conflicts never stop the device: the file keeps both versions with the usual markers, and you tidy it up on a computer later. Any Git host that speaks SSH works.
- **SFTP push.** Pick a server, pick a project, and it uploads that folder into a directory on your machine. Nothing on the server is ever deleted.
- **Bluetooth keyboard.** Pair once. The device remembers it and reconnects on its own, at boot and after a drop, even as the keyboard rotates its private address.
- **WiFi on demand.** The radio is off while you write. It comes up when you open the Synchronise page and goes away when you leave it, so the keyboard has the radio and the memory to itself the rest of the time.
- **Disk Mode.** The SD card becomes a USB drive on your computer. This is how you drop in keys and config, and how you get files off without any network at all.
- **Status LED.** Charging, charged, low battery, syncing, saved, keyboard connected. The single RGB LED on the side tells you what the device is up to.
- **Settings.** Contrast, keyboard layout (QWERTY, QWERTZ, AZERTY, Dvorak), cursor style, backlight brightness. Preferences live on the SD card.

## Where to go next

1. [Flashing](getting-started/flashing): the toolchain, the one ESP-IDF patch, backing up the stock firmware, the trick for getting the stock device into the bootloader, and recovery.
2. [First boot](getting-started/first-boot): the SD card, keys, WiFi, and pairing your keyboard.
3. [Writing](using/writing), [Git Sync](using/git-sync), [SFTP](using/sftp) and [Settings and Disk Mode](using/settings) for day-to-day use.
4. [Configuration](reference/configuration), [Hardware](reference/hardware) and [Architecture](reference/architecture) when you want to know exactly what is going on.
