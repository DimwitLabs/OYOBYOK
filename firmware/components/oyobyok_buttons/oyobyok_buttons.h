// Physical buttons: POWER, EXECUTE, LIGHT, UP, DOWN. A debounced poll task turns presses into
// oyobyok_key_t events on the same queue the keyboard feeds.
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void oyobyok_buttons_start(QueueHandle_t key_q);
