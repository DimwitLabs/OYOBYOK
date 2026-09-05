---
id: splash
title: Splash
---

# Splash

Every boot opens with a line from a writer, held on screen for a couple of seconds while the SD card, the LED, the SSH stack and the Bluetooth link come up behind it. Then the main menu appears. There are five lines and one is picked at random each time.

![Kurt Vonnegut](/img/screens/splash/0.png)

![Charlotte Bronte](/img/screens/splash/1.png)

![Charles Dickens](/img/screens/splash/2.png)

![Ernest Hemingway](/img/screens/splash/3.png)

![Ernest Hemingway](/img/screens/splash/4.png)

## Your Own

The lines and their authors are two small tables near the top of `firmware/shared/oyobyok_app.inc`, `SPLASH_Q` and `SPLASH_BY`. Add a line, remove one, or replace them all; the picker counts the table, and the layout wraps whatever you write to the width of the screen, up to four lines. Rebuild, flash, and the next boot uses yours.
