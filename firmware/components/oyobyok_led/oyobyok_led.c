// WS2812 on GPIO14 through the legacy RMT driver: APB 80 MHz / 2 = 25 ns per tick,
// T0H 350 ns, T0L 1000 ns, T1H 1000 ns, T1L 350 ns, GRB wire order.
#include "oyobyok_led.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt.h"
#include "esp_log.h"

static const char* TAG="oyobyok_led";

#define LED_GPIO      14
#define LED_RMT_CH    RMT_CHANNEL_1
#define LED_CLK_DIV   2
#define LED_BRIGHT    40
#define LED_PIXELS    4      // the same colour is sent to 4 pixels in case the LED is not first in its chain
#define T0H 14
#define T0L 40
#define T1H 40
#define T1L 14

static bool s_ready=false;

typedef enum { CH_NONE=0, CH_CHARGING, CH_CHARGED, CH_LOW } chg_t;
static volatile bool  s_disk=false;
static volatile bool  s_syncing=false;
static volatile bool  s_kb=false;
static volatile chg_t s_chg=CH_NONE;

typedef enum { OV_NONE=0, OV_KB_CONN, OV_KB_LOST, OV_SAVED, OV_SYNC_OK, OV_SYNC_FAIL } ov_t;
static volatile ov_t s_ov=OV_NONE;
static volatile int  s_ov_ms=0, s_ov_tot=0;

static void led_write(uint8_t r,uint8_t g,uint8_t b){
    if(!s_ready) return;
    static rmt_item32_t items[LED_PIXELS*24+1];
    uint8_t grb[3]={g,r,b}; int n=0;
    for(int px=0;px<LED_PIXELS;px++)
        for(int byte=0;byte<3;byte++)
            for(int bit=7;bit>=0;bit--){
                int one=(grb[byte]>>bit)&1;
                items[n].level0=1; items[n].duration0=one?T1H:T0H;
                items[n].level1=0; items[n].duration1=one?T1L:T0L; n++;
            }
    items[n].level0=0; items[n].duration0=1600; items[n].level1=0; items[n].duration1=1600; n++;   // 80 us reset
    esp_err_t e=rmt_write_items(LED_RMT_CH,items,n,true);
    if(e!=ESP_OK) ESP_LOGE(TAG,"rmt_write_items: %s",esp_err_to_name(e));
}

static inline uint8_t sc(uint8_t c,int f){ return (uint8_t)((c*f)/255); }

static void wheel(int pos,uint8_t*r,uint8_t*g,uint8_t*b){
    pos&=255; pos=255-pos;
    if(pos<85){ *r=255-pos*3; *g=0; *b=pos*3; }
    else if(pos<170){ pos-=85; *r=0; *g=pos*3; *b=255-pos*3; }
    else { pos-=170; *r=pos*3; *g=255-pos*3; *b=0; }
}

static void render_resting(int p){
    int tri=(p%2000); tri = tri<1000 ? (tri*255)/1000 : ((2000-tri)*255)/1000;
    int blink = ((p/300)&1) ? LED_BRIGHT : 0;
    if(s_disk)                     led_write(0, sc(LED_BRIGHT,tri), sc(LED_BRIGHT,tri));
    else if(s_syncing){ uint8_t r,g,b; wheel((p/6),&r,&g,&b); led_write(sc(r,LED_BRIGHT), sc(g,LED_BRIGHT), sc(b,LED_BRIGHT)); }
    else if(s_chg==CH_LOW)         led_write(blink,0,0);
    else if(s_chg==CH_CHARGING)    led_write(sc(LED_BRIGHT,tri), sc(LED_BRIGHT/2,tri), 0);
    else if(s_chg==CH_CHARGED)     led_write(0, LED_BRIGHT, 0);
    else if(s_kb)                  led_write(0, LED_BRIGHT/3, LED_BRIGHT/4);
    else                           led_write(0,0,0);
}

static void render_overlay(ov_t ov,int e){
    int blink = ((e/180)&1) ? LED_BRIGHT : 0;
    int up=(e%700); up = up<350 ? (up*255)/350 : ((700-up)*255)/350;
    switch(ov){
        case OV_KB_CONN:   led_write(0,blink,0); break;
        case OV_KB_LOST:   led_write(blink,sc(blink,160),0); break;
        case OV_SAVED:     led_write(0,sc(LED_BRIGHT,up),0); break;
        case OV_SYNC_OK:   led_write(0,LED_BRIGHT,0); break;
        case OV_SYNC_FAIL: led_write(blink,0,0); break;
        default: break;
    }
}

static void led_task(void* arg){
    (void)arg; int phase=0;
    for(;;){
        if(s_ov!=OV_NONE && s_ov_ms>0) render_overlay(s_ov, s_ov_tot-s_ov_ms);
        else render_resting(phase);
        vTaskDelay(pdMS_TO_TICKS(40));
        phase+=40;
        if(s_ov_ms>0){ s_ov_ms-=40; if(s_ov_ms<=0) s_ov=OV_NONE; }
    }
}

static void fire_overlay(ov_t ov,int ms){ s_ov=ov; s_ov_tot=ms; s_ov_ms=ms; }

void oyobyok_led_set(oyobyok_led_state_t state){
    switch(state){
        case OYOBYOK_LED_SYNCING:      s_syncing=true;    break;
        case OYOBYOK_LED_CHARGING:     s_chg=CH_CHARGING; break;
        case OYOBYOK_LED_CHARGED:      s_chg=CH_CHARGED;  break;
        case OYOBYOK_LED_LOW_BATTERY:  s_chg=CH_LOW;      break;
        case OYOBYOK_LED_OFF:          s_chg=CH_NONE;     break;
        case OYOBYOK_LED_DISK_MODE:    s_disk=true;       break;
        case OYOBYOK_LED_KB_CONNECTED: s_kb=true;  fire_overlay(OV_KB_CONN,1200); break;
        case OYOBYOK_LED_KB_LOST:      s_kb=false; fire_overlay(OV_KB_LOST,1500); break;
        case OYOBYOK_LED_SYNC_OK:      s_syncing=false; fire_overlay(OV_SYNC_OK,1500); break;
        case OYOBYOK_LED_SYNC_FAIL:    s_syncing=false; fire_overlay(OV_SYNC_FAIL,2000); break;
        case OYOBYOK_LED_SAVED:        fire_overlay(OV_SAVED,700); break;
        default: break;
    }
}

void oyobyok_led_disk_mode(bool on){ s_disk=on; }

bool oyobyok_led_init(void){
    if(s_ready) return true;
    rmt_config_t cfg=RMT_DEFAULT_CONFIG_TX(LED_GPIO,LED_RMT_CH);
    cfg.clk_div=LED_CLK_DIV;
    esp_err_t e=rmt_config(&cfg);
    if(e!=ESP_OK){ ESP_LOGE(TAG,"rmt_config: %s",esp_err_to_name(e)); return false; }
    e=rmt_driver_install(LED_RMT_CH,0,0);
    if(e!=ESP_OK){ ESP_LOGE(TAG,"rmt_driver_install: %s",esp_err_to_name(e)); return false; }
    s_ready=true;
    led_write(0,LED_BRIGHT,0); vTaskDelay(pdMS_TO_TICKS(250)); led_write(0,0,0);   // boot blip
    xTaskCreate(led_task,"oyobyok_led",2560,NULL,3,NULL);
    ESP_LOGI(TAG,"status LED up on GPIO%d",LED_GPIO);
    return true;
}
