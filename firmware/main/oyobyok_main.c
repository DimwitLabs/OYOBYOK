// Entry point: power latch, storage, drivers, device hooks, then the app loop.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#include "oyobyok_lcd.h"
#include "oyobyok_host.h"
#include "git_engine.h"
#include "oyobyok_power.h"
#include "oyobyok_buttons.h"
#include "oyobyok_led.h"
#include "oyobyok_disk.h"
#include "oyobyok_ssh.h"
#include "oyobyok_wifi.h"
#include "oyobyok_blehid.h"

static const char* TAG="oyobyok";
static QueueHandle_t s_key_q;

#define GIT_CFG   "/sdcard/git"
#define KEY_PATH  GIT_CFG "/keys/oyobyok_rsa"
#define TIMEZONE  "IST-5:30"

static void post_ctl(uint8_t code){ oyobyok_key_t k={0}; k.is_control=true; k.keycode=code; if(s_key_q) xQueueSend(s_key_q,&k,0); }

// ---- background network task ------------------------------------------------------------------
// Git, SSH and SFTP run on one task with a 40 KB stack in internal RAM (the WiFi TX path hands
// stack buffers to DMA, which cannot reach PSRAM). The stack is reserved before WiFi and BLE
// fragment the heap and reused for every operation; only one runs at a time.
#define NET_TASK_STACK 40960
static StaticTask_t s_net_tcb;
static StackType_t* s_net_stack=NULL;
static volatile bool s_net_running=false;
static TaskFunction_t s_net_fn=NULL;
static void net_tramp(void* arg){ s_net_fn(arg); s_net_running=false; vTaskDelete(NULL); }
static bool net_spawn(TaskFunction_t fn,const char* name,char* failmsg,size_t failsz,uint8_t donekey){
    if(s_net_running){
        snprintf(failmsg,failsz,"Still busy with the previous task. Try again shortly.");
        post_ctl(donekey); return false;
    }
    s_net_fn=fn; s_net_running=true;
    TaskHandle_t h = s_net_stack ? xTaskCreateStaticPinnedToCore(net_tramp,name,NET_TASK_STACK,NULL,4,s_net_stack,&s_net_tcb,1) : NULL;
    if(!h){
        s_net_running=false;
        snprintf(failmsg,failsz,"Out of memory to start %s",name);
        post_ctl(donekey);
    }
    return h!=NULL;
}

// ---- SSH test ------------------------------------------------------------------------------------
static char s_ssh_msg[192]="not run yet";
static const char* ssh_result_hook(void){ return s_ssh_msg; }
static void ssh_test_task(void* arg){
    (void)arg;
    char pub[128]; snprintf(pub,sizeof pub,"%s.pub",KEY_PATH);
    oyobyok_ssh_opts_t o={ .host="github.com", .port=22, .user="git", .privkey=KEY_PATH, .pubkey=pub, .passphrase="" };
    oyobyok_ssh_session_t* s=NULL; char err[128]="";
    oyobyok_ssh_err_t rc=oyobyok_ssh_connect(&o,&s,err,sizeof err);
    if(rc==OYOBYOK_SSH_OK){
        char greet[160]=""; int ec=0;
        oyobyok_ssh_exec(s,"",greet,sizeof greet,&ec);      // GitHub answers with a greeting and closes
        for(int i=0;greet[i];i++){ if(greet[i]=='\n'||greet[i]=='\r'){ greet[i]=0; break; } }
        if(greet[0]) snprintf(s_ssh_msg,sizeof s_ssh_msg,"OK! %s",greet);
        else         snprintf(s_ssh_msg,sizeof s_ssh_msg,"Authenticated to GitHub!");
        oyobyok_ssh_disconnect(&s);
    } else {
        snprintf(s_ssh_msg,sizeof s_ssh_msg,"Failed: %s",err[0]?err:oyobyok_ssh_strerror(rc));
    }
    post_ctl(OYOBYOK_CTL_SSH_DONE);
}
static void ssh_test_hook(void){ net_spawn(ssh_test_task,"ssh_test",s_ssh_msg,sizeof s_ssh_msg,OYOBYOK_CTL_SSH_DONE); }

// ---- SFTP ----------------------------------------------------------------------------------------
// Servers come from /sdcard/git/sftp.conf, one [name] section each: host, port, user, key, remote_path.
#define SFTP_MAX 8
typedef struct { char name[32], host[128], user[64], key[96], rpath[96]; int port; } sftp_srv_t;
static sftp_srv_t s_sftp[SFTP_MAX]; static int s_sftp_n=0;
static void sftp_conf_load(void){
    s_sftp_n=0;
    FILE* f=fopen(GIT_CFG "/sftp.conf","rb"); if(!f) return;
    char line[256]; sftp_srv_t* cur=NULL;
    while(fgets(line,sizeof line,f)){
        char* p=line; while(*p==' '||*p=='\t') p++;
        if(*p=='#'||*p==';'||*p=='\r'||*p=='\n'||*p==0) continue;
        int L=(int)strlen(p); while(L>0 && (p[L-1]=='\n'||p[L-1]=='\r'||p[L-1]==' '||p[L-1]=='\t')) p[--L]=0;
        if(*p=='['){ char* e=strchr(p,']'); if(!e) continue; *e=0;
            if(s_sftp_n<SFTP_MAX){ cur=&s_sftp[s_sftp_n++]; memset(cur,0,sizeof *cur);
                snprintf(cur->name,sizeof cur->name,"%s",p+1); cur->port=22;
                snprintf(cur->rpath,sizeof cur->rpath,"OYOBYOK");
                snprintf(cur->key,sizeof cur->key,"keys/oyobyok_rsa"); } else cur=NULL;
            continue; }
        if(!cur) continue;
        char* sep=strpbrk(p,":="); if(!sep) continue; *sep=0;
        char* k=p; char* v=sep+1; int kl=(int)strlen(k);
        while(kl>0 && (k[kl-1]==' '||k[kl-1]=='\t')) k[--kl]=0;
        while(*v==' '||*v=='\t') v++;
        if(!strcmp(k,"host")) snprintf(cur->host,sizeof cur->host,"%s",v);
        else if(!strcmp(k,"user")) snprintf(cur->user,sizeof cur->user,"%s",v);
        else if(!strcmp(k,"port")) cur->port=atoi(v);
        else if(!strcmp(k,"key")) snprintf(cur->key,sizeof cur->key,"%s",v);
        else if(!strcmp(k,"remote_path")||!strcmp(k,"path")) snprintf(cur->rpath,sizeof cur->rpath,"%s",v);
    }
    fclose(f);
}
static int sftp_list_hook(char names[][33],int max){
    sftp_conf_load();
    int n = s_sftp_n<max ? s_sftp_n : max;
    for(int i=0;i<n;i++) snprintf(names[i],33,"%.32s",s_sftp[i].name);
    return n;
}
static char s_sftp_msg[192]="";
static const char* sftp_result_hook(void){ return s_sftp_msg; }
static int s_sftp_idx=0; static char s_sftp_proj[64]="";
static void sftp_push_task(void* arg){
    (void)arg; int idx=s_sftp_idx;
    if(idx<0||idx>=s_sftp_n){
        snprintf(s_sftp_msg,sizeof s_sftp_msg,"No such server");
    } else {
        sftp_srv_t sv=s_sftp[idx];
        char priv[160]; snprintf(priv,sizeof priv,GIT_CFG "/%s",sv.key);
        char pub[176];  snprintf(pub,sizeof pub,"%s.pub",priv);
        oyobyok_ssh_opts_t o={ .host=sv.host,.port=sv.port,.user=sv.user,.privkey=priv,.pubkey=pub,.passphrase="" };
        oyobyok_ssh_session_t* s=NULL; char err[128]="";
        char lpath[192], rpath[192];
        snprintf(lpath,sizeof lpath,"/sdcard/projects/%s",s_sftp_proj);
        snprintf(rpath,sizeof rpath,"%s/%s",sv.rpath,s_sftp_proj);
        oyobyok_ssh_err_t rc=oyobyok_ssh_connect(&o,&s,err,sizeof err);
        if(rc!=OYOBYOK_SSH_OK){
            snprintf(s_sftp_msg,sizeof s_sftp_msg,"Connect: %s",err[0]?err:oyobyok_ssh_strerror(rc));
        } else {
            char perr[128]="";
            int nf=oyobyok_ssh_sftp_push_dir(s,lpath,rpath,perr,sizeof perr);
            oyobyok_ssh_disconnect(&s);
            if(nf<0) snprintf(s_sftp_msg,sizeof s_sftp_msg,"Push failed: %s",perr[0]?perr:"error");
            else     snprintf(s_sftp_msg,sizeof s_sftp_msg,"Pushed %d file%s of %.20s to %.16s",nf,nf==1?"":"s",s_sftp_proj,sv.name);
        }
    }
    post_ctl(OYOBYOK_CTL_SFTP_DONE);
}
static void sftp_push_hook(int idx,const char* project){
    s_sftp_idx=idx; snprintf(s_sftp_proj,sizeof s_sftp_proj,"%s",project?project:"");
    net_spawn(sftp_push_task,"sftp_push",s_sftp_msg,sizeof s_sftp_msg,OYOBYOK_CTL_SFTP_DONE);
}

// ---- Git -----------------------------------------------------------------------------------------
static char s_git_msg[256]="";
static char s_git_project[64]="";
static const char* git_result_hook(void){ return s_git_msg; }
static void git_sync_task(void* arg){ (void)arg;
    oyobyok_blehid_set_sync_mode(true);
    GitResult g=git_sync(s_git_project);
    oyobyok_blehid_set_sync_mode(false);
    snprintf(s_git_msg,sizeof s_git_msg,"%s",g.msg[0]?g.msg:(g.ok?"Synced":"Sync failed"));
    post_ctl(OYOBYOK_CTL_GIT_DONE);
}
static GitResult s_git_stat; static volatile int s_git_stat_ready=0;
static void git_status_task(void* arg){ (void)arg;
    oyobyok_blehid_set_sync_mode(true);
    GitResult g=git_status_fetch(s_git_project);
    oyobyok_blehid_set_sync_mode(false);
    s_git_stat=g; s_git_stat_ready=1;
    snprintf(s_git_msg,sizeof s_git_msg,"%s",g.msg[0]?g.msg:"Status failed");
    post_ctl(OYOBYOK_CTL_GIT_DONE);
}
static void git_sync_hook(const char* project){
    snprintf(s_git_project,sizeof s_git_project,"%s",project?project:"");
    net_spawn(git_sync_task,"git_sync",s_git_msg,sizeof s_git_msg,OYOBYOK_CTL_GIT_DONE);
}
static void git_status_hook(const char* project){
    snprintf(s_git_project,sizeof s_git_project,"%s",project?project:"");
    net_spawn(git_status_task,"git_status",s_git_msg,sizeof s_git_msg,OYOBYOK_CTL_GIT_DONE);
}
static int git_status_take_hook(GitResult* out){ if(!s_git_stat_ready) return 0; *out=s_git_stat; s_git_stat_ready=0; return 1; }

// ---- small adapters ------------------------------------------------------------------------------
static int  bt_connect(int i){ return (int)oyobyok_blehid_connect_index(i); }
static int  bt_conn_name_hook(char* b,int n){ if(!oyobyok_blehid_connected()) return 0;
    const char* nm=oyobyok_blehid_connected_name(); snprintf(b,n,"%s",nm?nm:"keyboard"); return 1; }
static int  wifi_is_en_hook(void){ return oyobyok_wifi_is_enabled()?1:0; }
static void wifi_set_en_hook(int e){ oyobyok_wifi_set_enabled(e!=0); }
static int  wifi_conn_name_hook(char* b,int n){ return oyobyok_wifi_connected_name(b,n)?1:0; }
static void hw_set_contrast(int v){ oyobyok_lcd_set_contrast((uint8_t)(v<0?0:(v>255?255:v))); }
static void led_set_hook(int s){ oyobyok_led_set((oyobyok_led_state_t)s); }
static int  disk_enter_hook(void){ return oyobyok_disk_enter()==ESP_OK?0:-1; }
static long storage_free_mb(void){ uint64_t total=0,freeb=0;
    if(esp_vfs_fat_info("/sdcard",&total,&freeb)!=ESP_OK) return -1;
    return (long)(freeb/(1024*1024)); }
static int  s_batt_pct=-1;
static int  battery_pct(void){ return s_batt_pct; }

// Charger pins once a second, battery voltage every five, smoothed; drives the LED's charge states.
static void power_sense_task(void* arg){
    (void)arg;
    oyobyok_power_charge_init();
    int tick=0, mv_ema=-1;
    oyobyok_led_state_t lastwant=(oyobyok_led_state_t)-1;
    for(;;){
        oyobyok_charge_state_t st=oyobyok_power_charge_state();
        if((tick % 5)==0){
            int mv=oyobyok_power_battery_mv();
            if(mv>=0){ mv_ema=(mv_ema<0)?mv:(mv_ema*3+mv)/4; s_batt_pct=oyobyok_power_battery_pct_from_mv(mv_ema); }
        }
        oyobyok_led_state_t want;
        if(st==OYOBYOK_CHG_CHARGING)   want=OYOBYOK_LED_CHARGING;
        else if(st==OYOBYOK_CHG_FULL)  want=OYOBYOK_LED_CHARGED;
        else                           want=(s_batt_pct>=0 && s_batt_pct<15)? OYOBYOK_LED_LOW_BATTERY : OYOBYOK_LED_OFF;
        if(want!=lastwant){ lastwant=want; oyobyok_led_set(want); }
        tick++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---- storage -------------------------------------------------------------------------------------
static sdmmc_card_t* s_sd_card=NULL;
sdmmc_card_t* oyobyok_sd_card(void){ return s_sd_card; }   // Disk Mode hands the card to USB MSC

static esp_err_t mount_sd(void){
    // SDMMC slot 1 in 1-bit mode: CLK GPIO5, CMD GPIO3, D0 GPIO4, no card-detect or write-protect.
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot         = SDMMC_HOST_SLOT_1;
    host.flags        = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = 40000;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_5;
    slot.cmd = GPIO_NUM_3;
    slot.d0  = GPIO_NUM_4;
    slot.cd  = SDMMC_SLOT_NO_CD;
    slot.wp  = SDMMC_SLOT_NO_WP;
    esp_vfs_fat_sdmmc_mount_config_t mc={ .format_if_mount_failed=false, .max_files=8, .allocation_unit_size=16*1024 };
    return esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&mc,&s_sd_card);
}

void app_main(void){
    oyobyok_power_latch_on();       // first, or the device switches itself off on battery
    ESP_LOGI(TAG,"OYOBYOK boot");

    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){ nvs_flash_erase(); nvs_flash_init(); }

    s_net_stack=heap_caps_malloc(NET_TASK_STACK,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    s_key_q=xQueueCreate(32,sizeof(oyobyok_key_t));

    esp_err_t sderr=mount_sd();
    if(sderr!=ESP_OK) ESP_LOGW(TAG,"SD mount failed: %s",esp_err_to_name(sderr));
    else { uint64_t total=0,freeb=0; esp_vfs_fat_info("/sdcard",&total,&freeb);
           ESP_LOGI(TAG,"SD mounted: %llu MB free of %llu",freeb/(1024*1024),total/(1024*1024)); }

    oyobyok_backlight_set(70);
    if(oyobyok_lcd_init()!=0) ESP_LOGE(TAG,"LCD init failed");
    else oyobyok_host_splash();

    oyobyok_buttons_start(s_key_q);
    oyobyok_app_set_hw(hw_set_contrast, oyobyok_keymap_set_layout);
    if(oyobyok_led_init()) oyobyok_app_set_led(led_set_hook);
    oyobyok_app_set_disk(disk_enter_hook);

    oyobyok_ssh_set_config_dir(GIT_CFG);
    oyobyok_ssh_init();
    oyobyok_app_set_ssh(ssh_test_hook, ssh_result_hook);
    oyobyok_app_set_sftp(sftp_list_hook, sftp_push_hook, sftp_result_hook, oyobyok_ssh_sftp_files_done);
    oyobyok_app_set_git(git_sync_hook, git_status_hook, git_status_take_hook, git_result_hook);

    xTaskCreate(power_sense_task,"pwr_sense",3072,NULL,3,NULL);

    // WiFi stays off until the Synchronise page asks for it. SNTP starts once an address arrives.
    oyobyok_wifi_init(s_key_q);
    setenv("TZ",TIMEZONE,1); tzset();
    oyobyok_app_set_wifi(oyobyok_wifi_scan, oyobyok_wifi_known_list, oyobyok_wifi_connect, oyobyok_wifi_connect_known,
                         oyobyok_wifi_disconnect, wifi_is_en_hook, wifi_set_en_hook, wifi_conn_name_hook);
    oyobyok_app_set_status_metrics(storage_free_mb, battery_pct);

    if(oyobyok_blehid_init(s_key_q)==ESP_OK){
        oyobyok_app_set_bt(oyobyok_blehid_scan, oyobyok_blehid_result_name, bt_connect);
        oyobyok_app_set_bt_saved(oyobyok_blehid_saved_count, oyobyok_blehid_saved_name, oyobyok_blehid_forget_saved);
        oyobyok_app_set_bt_status(bt_conn_name_hook);
        oyobyok_blehid_autoreconnect();
    } else ESP_LOGW(TAG,"BLE init failed; keyboard unavailable");

    oyobyok_host_run(s_key_q);
}
