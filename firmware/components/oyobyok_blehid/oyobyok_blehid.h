// BLE HID keyboard host on NimBLE + esp_hidh. One bonded keyboard, reconnected automatically.
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

esp_err_t oyobyok_blehid_init(QueueHandle_t key_q);   // key_q receives oyobyok_key_t

// Scan for keyboards; results are cached for the picker.
int         oyobyok_blehid_scan(int seconds);
const char* oyobyok_blehid_result_name(int index);
// Pair with a scan result. Asynchronous: the outcome arrives on key_q as OYOBYOK_CTL_BT_OK/FAIL.
esp_err_t   oyobyok_blehid_connect_index(int index);

// The saved keyboard (there is at most one).
void        oyobyok_blehid_autoreconnect(void);   // call once at boot
int         oyobyok_blehid_saved_count(void);
const char* oyobyok_blehid_saved_name(int index);
void        oyobyok_blehid_forget_saved(void);

esp_err_t   oyobyok_blehid_disconnect(void);
bool        oyobyok_blehid_connected(void);
const char* oyobyok_blehid_connected_name(void);

// Hand the radio to WiFi for the length of a network operation, then back to the keyboard.
void        oyobyok_blehid_set_sync_mode(bool sync);
