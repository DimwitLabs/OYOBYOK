// Device host for the shared app core: key queue in, LCD frames out, device hooks registered here.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "oyobyok_key.h"
#include "oyobyok_hid.h"
#include "git_engine.h"

// Control codes that other tasks post into the key queue to report an asynchronous result.
#define OYOBYOK_CTL_BT_OK     0x80
#define OYOBYOK_CTL_BT_FAIL   0x81
#define OYOBYOK_CTL_WIFI_OK   0x82
#define OYOBYOK_CTL_WIFI_FAIL 0x83
#define OYOBYOK_CTL_SSH_DONE  0x84
#define OYOBYOK_CTL_SFTP_DONE 0x86
#define OYOBYOK_CTL_GIT_DONE  0x88

// Translate one HID event into an app key. Returns false for releases, lone modifiers and unmapped keys.
bool oyobyok_key_from_hid(const oyobyok_hid_event_t* ev, oyobyok_key_t* out);
void oyobyok_keymap_set_layout(int layout);   // 0 QWERTY, 1 QWERTZ, 2 AZERTY, 3 Dvorak

// Draw the boot splash. Call once after the LCD is up; the first app frame waits until it has been
// visible for a moment.
void oyobyok_host_splash(void);

// Runs the app forever. Call after the SD card and all drivers are up and every hook is registered.
void oyobyok_host_run(QueueHandle_t key_q);

// Hooks. Every one is optional; the app hides what is not registered.
void oyobyok_app_set_bt(int(*scan)(int seconds), const char*(*name)(int index), int(*connect)(int index));
void oyobyok_app_set_bt_saved(int(*count)(void), const char*(*name)(int), void(*forget)(void));
void oyobyok_app_set_bt_status(int(*conn_name)(char*,int));
void oyobyok_app_set_status_metrics(long(*free_mb)(void), int(*batt_pct)(void));
void oyobyok_app_set_hw(void(*set_contrast)(int v), void(*set_kblayout)(int layout));
void oyobyok_app_set_led(void(*led)(int state));
void oyobyok_app_set_disk(int(*enter)(void));
void oyobyok_app_set_ssh(void(*test)(void), const char*(*result)(void));
// push(server, project) uploads <projects>/<project> to that server; progress() is files done so far.
void oyobyok_app_set_sftp(int(*list)(char(*)[33],int), void(*push)(int,const char*), const char*(*result)(void), int(*progress)(void));
// sync/status run on a background task and finish with OYOBYOK_CTL_GIT_DONE; take() hands over a Status result.
void oyobyok_app_set_git(void(*sync)(const char*), void(*status)(const char*), int(*take)(GitResult*), const char*(*result)(void));
void oyobyok_app_set_wifi(int(*scan)(char(*)[33],int), int(*known)(char(*)[33],int),
                          void(*connect)(const char*,const char*), void(*connect_known)(const char*),
                          void(*disconnect)(void), int(*is_en)(void), void(*set_en)(int),
                          int(*conn_name)(char*,int));

// Result delivery, called from the host loop when a control code arrives.
void oyobyok_app_bt_result(int connected);
void oyobyok_app_wifi_result(int ok);
void oyobyok_app_ssh_result(void);
void oyobyok_app_sftp_result(void);
void oyobyok_app_git_result(void);
void oyobyok_app_save_close(void);
