// Disk Mode: expose the SD card to the computer as a USB drive. Entering it hands the USB PHY from
// USB-Serial/JTAG to USB-OTG, so the console and flashing are gone until the device is power-cycled.
#pragma once
#include "esp_err.h"

// Returns ESP_OK once the host can see the drive. Exit is a power-cycle.
esp_err_t oyobyok_disk_enter(void);
