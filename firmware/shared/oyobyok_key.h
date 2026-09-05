// Normalised key event, produced by the HID keymap on the device or by the emulator's terminal input.
#ifndef OYOBYOK_KEY_H
#define OYOBYOK_KEY_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    OYOBYOK_NAV_NONE, OYOBYOK_NAV_UP, OYOBYOK_NAV_DOWN, OYOBYOK_NAV_LEFT,
    OYOBYOK_NAV_RIGHT, OYOBYOK_NAV_ENTER, OYOBYOK_NAV_BKSP,
    OYOBYOK_NAV_HOME, OYOBYOK_NAV_END, OYOBYOK_NAV_PGUP, OYOBYOK_NAV_PGDN, OYOBYOK_NAV_DEL
} oyobyok_nav_t;

typedef struct {
    bool          is_control;   // Ctrl held: keycode carries the control code (0x03 copy, 0x1B Esc ...)
    uint8_t       keycode;
    oyobyok_nav_t nav;
    bool          shift;        // Shift with a nav key extends the editor selection
    char          ch;           // printable character, else 0
} oyobyok_key_t;

#endif
