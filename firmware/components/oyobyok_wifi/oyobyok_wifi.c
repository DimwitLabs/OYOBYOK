#include "oyobyok_wifi.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "oyobyok_key.h"

static const char* TAG="oyobyok_wifi";
#define KNOWN_NS      "oyobyok_wifi"
#define WIFI_CTL_OK   0x82
#define WIFI_CTL_FAIL 0x83
#define MAX_RETRY     3

static QueueHandle_t s_q=NULL;
static bool s_started=false;
static bool s_enabled=false;
static bool s_connecting=false;
static int  s_retry=0;
static char s_target[33]={0};
static char s_connected[33]={0};
static esp_netif_t* s_netif=NULL;

static void post(uint8_t code){ if(s_q){ oyobyok_key_t k={0}; k.is_control=true; k.keycode=code; xQueueSend(s_q,&k,0);} }

static void on_wifi(void* arg,esp_event_base_t base,int32_t id,void* data){
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START){
        if(s_connecting) esp_wifi_connect();
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
        s_connected[0]=0;
        if(s_connecting){
            if(s_retry<MAX_RETRY){ s_retry++; esp_wifi_connect(); }
            else { s_connecting=false; ESP_LOGW(TAG,"connect failed for '%s'",s_target); post(WIFI_CTL_FAIL); }
        }
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){
        s_connecting=false; s_retry=0;
        snprintf(s_connected,sizeof s_connected,"%s",s_target);
        ESP_LOGI(TAG,"connected: '%s'",s_connected);
        // No RTC battery, so the clock starts at 1970: SNTP gives commits a real date.
        if(!esp_sntp_enabled()){
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0,"pool.ntp.org");
            esp_sntp_init();
        }
        post(WIFI_CTL_OK);
    }
}

static bool ensure_started(void){
    if(s_started) return true;
    static int64_t s_last_try=0;
    int64_t now=esp_timer_get_time();
    if(now - s_last_try < 3000000) return false;   // don't retry a failed bring-up more than every 3 s
    s_last_try=now;
    esp_err_t e;
    e=esp_netif_init(); if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE){ ESP_LOGE(TAG,"netif_init %s",esp_err_to_name(e)); return false; }
    e=esp_event_loop_create_default(); if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE){ ESP_LOGE(TAG,"event loop %s",esp_err_to_name(e)); return false; }
    if(!s_netif) s_netif=esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
    if((e=esp_wifi_init(&cfg))!=ESP_OK){ ESP_LOGE(TAG,"wifi_init %s",esp_err_to_name(e)); return false; }
    static bool s_handlers=false;
    if(!s_handlers){
        esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,on_wifi,NULL,NULL);
        esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,on_wifi,NULL,NULL);
        s_handlers=true;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if((e=esp_wifi_start())!=ESP_OK){ ESP_LOGE(TAG,"wifi_start %s",esp_err_to_name(e)); return false; }
    s_started=true;
    return true;
}

void oyobyok_wifi_init(QueueHandle_t result_q){ s_q=result_q; }

bool oyobyok_wifi_is_enabled(void){ return s_enabled; }

void oyobyok_wifi_set_enabled(bool enable){
    if(enable){
        if(s_started && s_enabled) return;
        if(!ensure_started()){ ESP_LOGW(TAG,"radio bring-up failed"); return; }
        s_enabled=true;
        char aps[8][33]; int n=oyobyok_wifi_known_list(aps,8);
        if(n>0) oyobyok_wifi_connect_known(aps[0]);
        return;
    }
    s_enabled=false; s_connecting=false; s_connected[0]=0;
    if(s_started){
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        s_started=false;
    }
    ESP_LOGI(TAG,"WiFi off");
}

int oyobyok_wifi_scan(char ssids[][33], int max){
    if(!ensure_started()) return 0;
    s_enabled=true;
    wifi_scan_config_t sc={0};
    if(esp_wifi_scan_start(&sc,true)!=ESP_OK){ ESP_LOGW(TAG,"scan failed"); return 0; }
    uint16_t n=0; esp_wifi_scan_get_ap_num(&n);
    if(n>32) n=32;
    static wifi_ap_record_t recs[32];
    esp_wifi_scan_get_ap_records(&n,recs);
    int out=0;
    for(int i=0;i<n && out<max;i++){
        if(recs[i].ssid[0]==0) continue;
        int dup=0; for(int j=0;j<out;j++) if(strcmp(ssids[j],(char*)recs[i].ssid)==0){dup=1;break;}
        if(dup) continue;
        snprintf(ssids[out],33,"%s",(char*)recs[i].ssid); out++;
    }
    return out;
}

static void save_known(const char* ssid,const char* pass){
    nvs_handle_t h; if(nvs_open(KNOWN_NS,NVS_READWRITE,&h)!=ESP_OK) return;
    nvs_set_str(h,ssid,pass?pass:""); nvs_commit(h); nvs_close(h);
}
static bool load_known(const char* ssid,char* pass,int n){
    nvs_handle_t h; if(nvs_open(KNOWN_NS,NVS_READONLY,&h)!=ESP_OK) return false;
    size_t len=n; esp_err_t e=nvs_get_str(h,ssid,pass,&len); nvs_close(h);
    return e==ESP_OK;
}

static void do_connect(const char* ssid,const char* pass){
    if(!ensure_started()){ post(WIFI_CTL_FAIL); return; }
    s_enabled=true;
    wifi_config_t wc={0};
    snprintf((char*)wc.sta.ssid,sizeof wc.sta.ssid,"%s",ssid);
    snprintf((char*)wc.sta.password,sizeof wc.sta.password,"%s",pass?pass:"");
    wc.sta.threshold.authmode = (pass&&*pass)?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_STA,&wc);
    snprintf(s_target,sizeof s_target,"%s",ssid);
    s_retry=0; s_connecting=true;
    ESP_LOGI(TAG,"connecting to '%s'",ssid);
    esp_wifi_disconnect();
    esp_wifi_connect();
}

void oyobyok_wifi_connect(const char* ssid,const char* password){
    if(!ssid||!*ssid){ post(WIFI_CTL_FAIL); return; }
    save_known(ssid,password);
    do_connect(ssid,password);
}
void oyobyok_wifi_connect_known(const char* ssid){
    char pass[65]={0}; load_known(ssid,pass,sizeof pass);
    do_connect(ssid,pass);
}

int oyobyok_wifi_known_list(char ssids[][33], int max){
    int out=0;
    nvs_iterator_t it=NULL;
    esp_err_t e=nvs_entry_find(NVS_DEFAULT_PART_NAME,KNOWN_NS,NVS_TYPE_STR,&it);
    while(e==ESP_OK && out<max){
        nvs_entry_info_t info; nvs_entry_info(it,&info);
        snprintf(ssids[out],33,"%s",info.key); out++;
        e=nvs_entry_next(&it);
    }
    if(it) nvs_release_iterator(it);
    return out;
}

void oyobyok_wifi_disconnect(void){
    s_connecting=false; s_connected[0]=0;
    if(s_started) esp_wifi_disconnect();
}

bool oyobyok_wifi_connected_name(char* buf,int n){
    snprintf(buf,n,"%s",s_connected);
    return s_connected[0]!=0;
}
