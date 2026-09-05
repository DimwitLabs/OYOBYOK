// Status LED: the single WS2812 on GPIO14. Several conditions are latched independently and resolved by
// priority into one resting colour; short event flashes are overlaid on top.
//
//   Disk Mode        cyan breathe
//   Syncing          rainbow sweep
//   Low battery      red blink
//   Charging         amber breathe
//   Charged          green solid
//   Keyboard linked  dim teal
//   idle             off
#pragma once
#include <stdbool.h>

typedef enum {
    OYOBYOK_LED_OFF = 0,       // charge condition: none
    OYOBYOK_LED_KB_CONNECTED,  // event: green double-blink; latches "keyboard linked"
    OYOBYOK_LED_KB_LOST,       // event: amber blink; clears "keyboard linked"
    OYOBYOK_LED_SYNCING,       // latches "syncing"
    OYOBYOK_LED_SYNC_OK,       // event: green solid; clears "syncing"
    OYOBYOK_LED_SYNC_FAIL,     // event: red blink; clears "syncing"
    OYOBYOK_LED_CHARGING,      // charge condition: charging
    OYOBYOK_LED_CHARGED,       // charge condition: full
    OYOBYOK_LED_LOW_BATTERY,   // charge condition: low
    OYOBYOK_LED_SAVED,         // event: short green blip
    OYOBYOK_LED_DISK_MODE,     // latches "disk mode"
} oyobyok_led_state_t;

// Returns false if the RMT channel could not be created; the LED then does nothing.
bool oyobyok_led_init(void);
void oyobyok_led_set(oyobyok_led_state_t state);
void oyobyok_led_disk_mode(bool on);
