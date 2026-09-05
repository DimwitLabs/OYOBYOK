// BLE HID keyboard host. Boot-protocol reports are diffed against the previous one to emit presses;
// auto-repeat comes from the keyboard sending fresh reports.
//
// Connection model: the keyboard we want is remembered in NVS (address, type, name). A connect task
// opens it and a security task bonds the link (Just Works). On a drop, or at boot, the task scans for
// the saved name and opens whatever address the keyboard is advertising right now, because BLE
// keyboards rotate their private address. The stored bond is reused; it is wiped only after an attempt
// stalled in pairing, which means the two sides disagree about the key.
#include "oyobyok_blehid.h"
#include "oyobyok_host.h"
#include <string.h>
#include "esp_log.h"
#include "esp_hidh.h"
#include "esp_hid_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "freertos/task.h"
#include "esp_coexist.h"
#include "nvs.h"
extern void ble_store_config_init(void);

static const char* TAG="oyobyok_blehid";
static QueueHandle_t s_q;
static esp_hidh_dev_t* s_dev;
static char s_name[64];
static uint8_t s_prev[6];

static uint8_t  s_conn_bda[6];
static uint8_t  s_conn_type;
static volatile bool s_want_conn=false;          // keep this keyboard connected
static volatile bool s_conn_task_running=false;
static volatile bool s_scanning=false;           // a manual scan owns the radio
static volatile bool s_reconnect_by_name=false;  // find the current address by scanning for the name first
static volatile bool s_fresh_pair_next=false;    // wipe the bond before the next attempt

#define KB_NVS_NS   "oyobyok_kb"
#define KB_NVS_KEY  "last"
#define KB_NVS_NAME "lastname"

static void notify_ui(uint8_t code){
    oyobyok_key_t k={0}; k.is_control=true; k.keycode=code;
    if(s_q) xQueueSend(s_q,&k,0);
}

typedef struct { uint8_t bda[6]; uint8_t type; char name[64]; } scan_hit_t;
static scan_hit_t s_hits[16]; static int s_nhits;

static void emit(uint8_t mod,uint8_t usage){
    oyobyok_hid_event_t ev={ .modifiers=mod, .keycode=usage, .pressed=1 };
    oyobyok_key_t k;
    if(oyobyok_key_from_hid(&ev,&k)) xQueueSend(s_q,&k,0);
}

static void parse_report(const uint8_t* d,int len){
    if(len<8) return;
    uint8_t mod=d[0]; const uint8_t* keys=d+2;
    for(int i=0;i<6;i++){
        uint8_t u=keys[i];
        if(u<=1) continue;
        int held=0;
        for(int j=0;j<6;j++) if(s_prev[j]==u){ held=1; break; }
        if(!held) emit(mod,u);
    }
    memcpy(s_prev,keys,6);
}

static void ensure_conn_task(void);
static void drop_stale_dev(void);

static void hidh_cb(void* handler_args,esp_event_base_t base,int32_t id,void* data){
    esp_hidh_event_t ev=(esp_hidh_event_t)id; esp_hidh_event_data_t* p=data;
    switch(ev){
        case ESP_HIDH_OPEN_EVENT:
            if(p->open.status==ESP_OK){ s_dev=p->open.dev; memset(s_prev,0,sizeof s_prev);
                ESP_LOGI(TAG,"keyboard connected"); notify_ui(OYOBYOK_CTL_BT_OK); }
            else { ESP_LOGW(TAG,"keyboard open failed status=%d",p->open.status); notify_ui(OYOBYOK_CTL_BT_FAIL); }
            break;
        case ESP_HIDH_INPUT_EVENT:
            parse_report(p->input.data,p->input.length); break;
        case ESP_HIDH_CLOSE_EVENT:
            ESP_LOGI(TAG,"keyboard disconnected (reason=%d)",p->close.reason); s_dev=NULL; notify_ui(OYOBYOK_CTL_BT_FAIL);
            // 517 auth failure, 518 key missing, 534 encryption failed: the bond is dead on one side.
            if(p->close.reason==517||p->close.reason==518||p->close.reason==534) s_fresh_pair_next=true;
            if(s_want_conn && !s_scanning){ s_reconnect_by_name=true; ensure_conn_task(); }
            break;
        default: break;
    }
}

static void ble_host_task(void* param){
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t oyobyok_blehid_init(QueueHandle_t key_q){
    s_q=key_q;
    esp_err_t r=esp_hid_gap_init(HIDH_BLE_MODE); if(r!=ESP_OK) return r;
    esp_hidh_config_t cfg={ .callback=hidh_cb, .event_stack_size=4096, .callback_arg=NULL };
    r=esp_hidh_init(&cfg); if(r!=ESP_OK) return r;
    // Keyboards keep their report map behind an encrypted link, so bond with Just Works and persist
    // the keys for silent reconnects.
    ble_hs_cfg.sm_io_cap        = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding       = 1;
    ble_hs_cfg.sm_mitm          = 0;
    ble_hs_cfg.sm_sc            = 1;
    ble_hs_cfg.sm_our_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    return esp_nimble_enable(ble_host_task);
}

int oyobyok_blehid_scan(int seconds){
    size_t n=0; esp_hid_scan_result_t* res=NULL; s_nhits=0;
    s_want_conn=false;
    s_scanning=true;
    // A reconnect attempt in progress holds the radio; cancel it and wait for its task to unwind.
    ble_gap_conn_cancel();
    for(int i=0;i<160 && s_conn_task_running;i++) vTaskDelay(pdMS_TO_TICKS(50));
    esp_err_t sr=esp_hid_scan(seconds,&n,&res);
    s_scanning=false;
    if(sr!=ESP_OK){ ESP_LOGW(TAG,"esp_hid_scan rc=%d",sr); return 0; }
    for(esp_hid_scan_result_t* r=res; r && s_nhits<16; r=r->next){
        if(r->transport!=ESP_HID_TRANSPORT_BLE) continue;
        scan_hit_t* h=&s_hits[s_nhits++];
        memcpy(h->bda,r->bda,6); h->type=r->ble.addr_type;
        snprintf(h->name,sizeof h->name,"%s",r->name?r->name:"(keyboard)");
    }
    if(res) esp_hid_scan_results_free(res);
    return s_nhits;
}
const char* oyobyok_blehid_result_name(int i){ return (i>=0&&i<s_nhits)?s_hits[i].name:""; }

static void kb_save(void){
    nvs_handle_t h; if(nvs_open(KB_NVS_NS,NVS_READWRITE,&h)!=ESP_OK) return;
    uint8_t rec[7]; memcpy(rec,s_conn_bda,6); rec[6]=s_conn_type;
    nvs_set_blob(h,KB_NVS_KEY,rec,sizeof rec);
    nvs_set_str(h,KB_NVS_NAME,s_name[0]?s_name:"(keyboard)");
    nvs_commit(h); nvs_close(h);
    ESP_LOGI(TAG,"saved keyboard '%s'",s_name);
}
static bool kb_load(void){
    nvs_handle_t h; if(nvs_open(KB_NVS_NS,NVS_READONLY,&h)!=ESP_OK) return false;
    uint8_t rec[7]; size_t n=sizeof rec;
    bool ok = (nvs_get_blob(h,KB_NVS_KEY,rec,&n)==ESP_OK && n==7);
    if(ok){ memcpy(s_conn_bda,rec,6); s_conn_type=rec[6];
        size_t nl=sizeof s_name; if(nvs_get_str(h,KB_NVS_NAME,s_name,&nl)!=ESP_OK) snprintf(s_name,sizeof s_name,"(keyboard)"); }
    nvs_close(h); return ok;
}

int oyobyok_blehid_saved_count(void){
    nvs_handle_t h; if(nvs_open(KB_NVS_NS,NVS_READONLY,&h)!=ESP_OK) return 0;
    uint8_t rec[7]; size_t n=sizeof rec;
    int have = (nvs_get_blob(h,KB_NVS_KEY,rec,&n)==ESP_OK && n==7) ? 1 : 0;
    nvs_close(h); return have;
}
const char* oyobyok_blehid_saved_name(int i){
    static char nm[64];
    if(i!=0) return "";
    nvs_handle_t h; if(nvs_open(KB_NVS_NS,NVS_READONLY,&h)!=ESP_OK) return "";
    size_t nl=sizeof nm; if(nvs_get_str(h,KB_NVS_NAME,nm,&nl)!=ESP_OK) snprintf(nm,sizeof nm,"(keyboard)");
    nvs_close(h); return nm;
}
void oyobyok_blehid_forget_saved(void){
    s_want_conn=false;
    if(s_dev){ esp_hidh_dev_close(s_dev); s_dev=NULL; }
    nvs_handle_t h; if(nvs_open(KB_NVS_NS,NVS_READWRITE,&h)==ESP_OK){ nvs_erase_all(h); nvs_commit(h); nvs_close(h); }
    ble_addr_t a; a.type=s_conn_type; memcpy(a.val,s_conn_bda,6); ble_gap_unpair(&a);
    ESP_LOGI(TAG,"forgot saved keyboard");
}

// One per open attempt: initiate LE security until the link is encrypted, then record the peer's
// identity address so later reconnects survive address rotation. A pairing that stays pending is
// dropped cleanly and the next attempt pairs fresh.
static void ble_sec_kicker(void* arg){
    (void)arg;
    ble_addr_t a; a.type=s_conn_type; memcpy(a.val,s_conn_bda,6);
    int stalls=0;
    for(int i=0;i<100 && s_dev==NULL;i++){
        struct ble_gap_conn_desc d;
        if(ble_gap_conn_find_by_addr(&a,&d)==0){
            if(d.sec_state.encrypted){
                const uint8_t* id=d.peer_id_addr.val;
                int id_ok = d.peer_id_addr.type<=1 && (id[0]|id[1]|id[2]|id[3]|id[4]|id[5]);
                if(id_ok && memcmp(s_conn_bda,id,6)!=0){
                    memcpy(s_conn_bda,id,6); s_conn_type=d.peer_id_addr.type; kb_save();
                }
                break;
            }
            int rc=ble_gap_security_initiate(d.conn_handle);
            if(++stalls>=4 && s_dev==NULL){
                ESP_LOGW(TAG,"pairing stalled (rc=%d), dropping link; next attempt pairs fresh",rc);
                s_fresh_pair_next=true;
                ble_gap_terminate(d.conn_handle,BLE_ERR_REM_USER_CONN_TERM);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1500));
        } else vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

static bool kb_scan_find_name(const char* want,uint8_t bda[6],uint8_t* type){
    if(!want||!want[0]) return false;
    size_t n=0; esp_hid_scan_result_t* res=NULL;
    if(esp_hid_scan(3,&n,&res)!=ESP_OK) return false;
    bool found=false;
    for(esp_hid_scan_result_t* r=res; r; r=r->next){
        if(r->transport!=ESP_HID_TRANSPORT_BLE || !r->name) continue;
        if(strcmp(r->name,want)==0){ memcpy(bda,r->bda,6); *type=r->ble.addr_type; found=true; break; }
    }
    if(res) esp_hid_scan_results_free(res);
    return found;
}
static void ble_conn_task(void* arg){
    (void)arg;
    s_conn_task_running=true;
    int attempt=0;
    while(s_want_conn && s_dev==NULL && !s_scanning){
        if(s_reconnect_by_name){
            uint8_t bda[6],type;
            if(kb_scan_find_name(s_name,bda,&type)){ memcpy(s_conn_bda,bda,6); s_conn_type=type; }
            else {
                if(attempt==0){ ESP_LOGI(TAG,"'%s' not in range yet, will keep looking",s_name); notify_ui(OYOBYOK_CTL_BT_FAIL); }
                attempt++;
                for(int i=0;i<6 && s_want_conn && s_dev==NULL && !s_scanning;i++) vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
        }
        if(s_fresh_pair_next){
            ble_addr_t a; a.type=s_conn_type; memcpy(a.val,s_conn_bda,6); ble_gap_unpair(&a); ble_store_clear();
            s_fresh_pair_next=false;
            ESP_LOGI(TAG,"bond wiped, pairing fresh");
        }
        xTaskCreate(ble_sec_kicker,"ble_seck",4096,NULL,5,NULL);
        ESP_LOGI(TAG,"open %02x:%02x:%02x:%02x:%02x:%02x type=%d (attempt %d, %s)",
                 s_conn_bda[0],s_conn_bda[1],s_conn_bda[2],s_conn_bda[3],s_conn_bda[4],s_conn_bda[5],s_conn_type,
                 attempt,s_reconnect_by_name?"reconnect":"manual");
        // Give BLE the radio while the link and service discovery come up.
        esp_coex_preference_set(ESP_COEX_PREFER_BT);
        esp_hidh_dev_open(s_conn_bda,ESP_HID_TRANSPORT_BLE,s_conn_type);
        esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
        if(s_dev){ ESP_LOGI(TAG,"link up"); break; }
        if(attempt==0 && !s_reconnect_by_name){ ESP_LOGW(TAG,"open failed"); notify_ui(OYOBYOK_CTL_BT_FAIL); }
        attempt++;
        if(!s_reconnect_by_name && attempt>=2) break;   // a manual pair gets two tries
        for(int i=0;i<6 && s_want_conn && s_dev==NULL && !s_scanning;i++) vTaskDelay(pdMS_TO_TICKS(500));
    }
    s_conn_task_running=false;
    vTaskDelete(NULL);
}
static void ensure_conn_task(void){
    if(!s_conn_task_running) xTaskCreate(ble_conn_task,"ble_conn",6144,NULL,5,NULL);
}

// esp_hidh does not report a close when the keyboard powers off, and refuses to reopen a device it
// still lists. Close it properly before reconnecting.
static void drop_stale_dev(void){
    if(s_dev){ esp_hidh_dev_close(s_dev); s_dev=NULL; }
}

esp_err_t oyobyok_blehid_connect_index(int i){
    if(i<0||i>=s_nhits) return ESP_ERR_INVALID_ARG;
    s_want_conn=false; ble_gap_conn_cancel();
    for(int k=0;k<20 && s_conn_task_running;k++) vTaskDelay(pdMS_TO_TICKS(50));
    drop_stale_dev();
    snprintf(s_name,sizeof s_name,"%s",s_hits[i].name);
    memcpy(s_conn_bda,s_hits[i].bda,6); s_conn_type=s_hits[i].type;
    // A manual pair always starts clean: a multi-host keyboard may have rotated its keys elsewhere.
    ble_addr_t a; a.type=s_conn_type; memcpy(a.val,s_conn_bda,6);
    ble_gap_unpair(&a);
    kb_save();
    s_reconnect_by_name=false;
    s_want_conn=true;
    ensure_conn_task();
    return ESP_OK;
}

void oyobyok_blehid_autoreconnect(void){
    if(kb_load()){
        drop_stale_dev();
        s_reconnect_by_name=true;
        s_want_conn=true; ensure_conn_task();
    }
}
void oyobyok_blehid_set_sync_mode(bool sync){
    esp_coex_preference_set(sync ? ESP_COEX_PREFER_WIFI : ESP_COEX_PREFER_BALANCE);
    if(!s_dev) return;
    struct ble_gap_conn_desc d; ble_addr_t a; a.type=s_conn_type; memcpy(a.val,s_conn_bda,6);
    if(ble_gap_conn_find_by_addr(&a,&d)!=0) return;
    struct ble_gap_upd_params p={0};
    if(sync){ p.itvl_min=64; p.itvl_max=80; p.latency=0; p.supervision_timeout=600; }   // 80-100 ms
    else    { p.itvl_min=8;  p.itvl_max=16; p.latency=0; p.supervision_timeout=600; }   // 10-20 ms
    ble_gap_update_params(d.conn_handle,&p);
}
esp_err_t oyobyok_blehid_disconnect(void){
    s_want_conn=false;
    if(s_dev){ esp_hidh_dev_close(s_dev); s_dev=NULL; }
    return ESP_OK;
}
bool oyobyok_blehid_connected(void){ return s_dev!=NULL; }
const char* oyobyok_blehid_connected_name(void){ return s_name; }
