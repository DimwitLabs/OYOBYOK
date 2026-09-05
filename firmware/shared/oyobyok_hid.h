// Raw key event from the BLE HID host, before layout translation.
#ifndef OYOBYOK_HID_H
#define OYOBYOK_HID_H
#include <stdint.h>

typedef struct {
    uint8_t modifiers;   // HID modifier bitmap: bit0 LCtrl, bit1 LShift, bit4 RCtrl, bit5 RShift ...
    uint8_t keycode;     // HID usage (0x04 = 'a')
    uint8_t pressed;     // 1 on press, 0 on release
} oyobyok_hid_event_t;

#endif
