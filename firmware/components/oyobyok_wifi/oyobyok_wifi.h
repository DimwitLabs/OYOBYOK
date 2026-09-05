// WiFi station. The radio is off until the Synchronise page brings it up, and it is torn down again on
// leaving that page. Known networks (SSID + password) live in NVS. Connect outcomes are posted to the
// key queue as control keys 0x82 (connected) / 0x83 (failed).
#pragma once
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void oyobyok_wifi_init(QueueHandle_t result_q);          // does not start the radio

bool oyobyok_wifi_is_enabled(void);
void oyobyok_wifi_set_enabled(bool enable);              // on: bring up + reconnect to the saved network; off: stop + deinit

int  oyobyok_wifi_scan(char ssids[][33], int max);       // blocking, a few seconds
void oyobyok_wifi_connect(const char* ssid, const char* password);   // saves the network, then connects
void oyobyok_wifi_connect_known(const char* ssid);
int  oyobyok_wifi_known_list(char ssids[][33], int max);
void oyobyok_wifi_disconnect(void);
bool oyobyok_wifi_connected_name(char* buf, int n);
