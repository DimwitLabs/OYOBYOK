#include "oyobyok_buttons.h"
#include "oyobyok_key.h"
#include "oyobyok_power.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG="oyobyok_btn";

typedef struct {
    const char*   name;
    int           gpio;
    oyobyok_key_t key;        // event emitted on a press edge
    bool          is_power;   // POWER: short press on release, 3 s hold powers off
} btn_t;

// All five buttons are active-low with the internal pull-up.
static const btn_t s_btns[] = {
    { "POWER",    6, { .is_control=true, .keycode=0x00 }, true  },
    { "EXECUTE", 16, { .nav=OYOBYOK_NAV_ENTER },          false },
    { "LIGHT",   11, { .is_control=true, .keycode=0x0C }, false },
    { "UP",      15, { .nav=OYOBYOK_NAV_UP },             false },
    { "DOWN",     7, { .nav=OYOBYOK_NAV_DOWN },           false },
};
#define NBTN (sizeof(s_btns)/sizeof(s_btns[0]))

#define POLL_MS      10
#define DEBOUNCE_N   4       // consecutive stable samples (~40 ms)
#define LONGHOLD_MS  3000

static QueueHandle_t s_q;

static void btn_task(void* arg){
    (void)arg;
    bool stable[NBTN]={0}, cand[NBTN]={0}, longfired[NBTN]={0};
    int  cnt[NBTN]={0};
    TickType_t downtick[NBTN]={0};

    for(;;){
        for(size_t i=0;i<NBTN;i++){
            const btn_t* b=&s_btns[i];
            bool p = gpio_get_level(b->gpio)==0;
            if(p!=cand[i]){ cand[i]=p; cnt[i]=0; }
            else if(cnt[i]<DEBOUNCE_N){ cnt[i]++; }

            if(cnt[i]>=DEBOUNCE_N && stable[i]!=cand[i]){
                stable[i]=cand[i];
                if(stable[i]){
                    downtick[i]=xTaskGetTickCount(); longfired[i]=false;
                    ESP_LOGI(TAG,"press: %s (GPIO%d)", b->name, b->gpio);
                    if(!b->is_power){ oyobyok_key_t k=b->key; xQueueSend(s_q,&k,0); }
                } else if(b->is_power && !longfired[i]){
                    oyobyok_key_t k=b->key; xQueueSend(s_q,&k,0);
                }
            }
            // POWER long-hold runs here, independent of the app loop, so it works even if the UI wedges.
            if(b->is_power && stable[i] && !longfired[i]
               && (xTaskGetTickCount()-downtick[i])*portTICK_PERIOD_MS >= LONGHOLD_MS){
                longfired[i]=true;
                ESP_LOGW(TAG,"POWER held %d ms -> power off",LONGHOLD_MS);
                oyobyok_power_off();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void oyobyok_buttons_start(QueueHandle_t key_q){
    s_q=key_q;
    uint64_t mask=0;
    for(size_t i=0;i<NBTN;i++) mask |= (1ULL<<s_btns[i].gpio);
    gpio_config_t io={ .pin_bit_mask=mask, .mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE, .intr_type=GPIO_INTR_DISABLE };
    gpio_config(&io);
    xTaskCreate(btn_task,"oyobyok_btn",3072,NULL,9,NULL);
    ESP_LOGI(TAG,"buttons started");
}
