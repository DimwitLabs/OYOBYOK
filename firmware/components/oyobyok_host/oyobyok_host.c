// Device host: pulls keys from the queue, feeds the shared app core, paints frames to the LCD.
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "oyobyok_lcd.h"
#include "oyobyok_host.h"
#include "oyobyok_power.h"

#define OYOBYOK_NO_HOST_IO
#include "render.h"
#include "git_engine.h"
#include "oyobyok_menus.h"
#include "oyobyok_app.inc"

static const char* TAG="oyobyok_host";

#define REPOS_ROOT   "/sdcard/projects"
#define GIT_CFG      "/sdcard/git"
#define SPLASH_MIN_MS 2500

static void flush_lcd(void){
    memcpy(oyobyok_lcd_framebuffer(), fb, OYOBYOK_LCD_FBSIZE);
    oyobyok_lcd_flush();
}

static void apply_key(const oyobyok_key_t* k){
    if(k->is_control){
        switch(k->keycode){
            case 0x00: app_power(); break;                          // Ctrl-Space or the power button
            case OYOBYOK_CTL_BT_OK:     oyobyok_app_bt_result(1);   break;
            case OYOBYOK_CTL_BT_FAIL:   oyobyok_app_bt_result(0);   break;
            case OYOBYOK_CTL_WIFI_OK:   oyobyok_app_wifi_result(1); break;
            case OYOBYOK_CTL_WIFI_FAIL: oyobyok_app_wifi_result(0); break;
            case OYOBYOK_CTL_SSH_DONE:  oyobyok_app_ssh_result();   break;
            case OYOBYOK_CTL_SFTP_DONE: oyobyok_app_sftp_result();  break;
            case OYOBYOK_CTL_GIT_DONE:  oyobyok_app_git_result();   break;
            case 0x1B: app_esc(); break;
            case 0x13: oyobyok_app_save_close(); break;              // Ctrl-S
            case 0x04: if(!app_in_editor()) oyobyok_enter_download_mode(); break;   // Ctrl-D: flash mode
            case 0x0C: oyobyok_backlight_cycle(); break;             // Ctrl-L or the LIGHT button
            default:   app_ctrl(k->keycode); break;                  // Ctrl-C/X/V in the editor
        }
    } else if(k->nav!=OYOBYOK_NAV_NONE){
        app_nav(k->nav, k->shift);
    } else if(k->ch){
        char s[2]={k->ch,0}; app_type(s);
    }
}

static int64_t s_splash_us=0;
void oyobyok_host_splash(void){
    app_draw_splash((unsigned)esp_random());
    flush_lcd();
    s_splash_us=esp_timer_get_time();
}

void oyobyok_host_run(QueueHandle_t key_q){
    if(app_init(REPOS_ROOT, GIT_CFG)!=0){ ESP_LOGE(TAG,"app_init failed"); return; }
    if(s_splash_us){
        int64_t left=SPLASH_MIN_MS*1000LL-(esp_timer_get_time()-s_splash_us);
        if(left>0) vTaskDelay(pdMS_TO_TICKS((uint32_t)(left/1000)));
    }
    render_current(); flush_lcd();
    oyobyok_key_t k;
    for(;;){
        TickType_t wait = oyobyok_app_busy() ? pdMS_TO_TICKS(1000) : portMAX_DELAY;
        if(xQueueReceive(key_q,&k,wait)!=pdTRUE){
            if(oyobyok_app_tick()){ render_current(); flush_lcd(); }
            continue;
        }
        apply_key(&k);
        // A flush takes a while over bit-banged I2C, so drain a typing burst into one frame.
        while(xQueueReceive(key_q,&k,0)==pdTRUE) apply_key(&k);
        render_current(); flush_lcd();
        if(oyobyok_app_service()){ render_current(); flush_lcd(); }
    }
}
