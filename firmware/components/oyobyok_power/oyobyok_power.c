#include "oyobyok_power.h"
#include "oyobyok_lcd.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char* TAG="oyobyok_power";

// Vbat = Vpin * ratio, anchored at the charger's DONE state (4.20 V full cell reads ~2772 mV at the pin).
#define OYOBYOK_BATT_DIV_RATIO   1.515f

void oyobyok_power_latch_on(void){
    gpio_config_t io = {
        .pin_bit_mask = (1ULL<<OYOBYOK_PWR_LATCH_GPIO) | (1ULL<<OYOBYOK_PWR_AUX_GPIO)
                      | (1ULL<<OYOBYOK_LCD_EN_GPIO)    | (1ULL<<OYOBYOK_LCD_A0_GPIO)
                      | (1ULL<<OYOBYOK_LCD_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(OYOBYOK_PWR_LATCH_GPIO, 0);
    gpio_set_level(OYOBYOK_LCD_RESET_GPIO, 0);
    gpio_set_level(OYOBYOK_LCD_A0_GPIO,   0);
    gpio_set_level(OYOBYOK_LCD_EN_GPIO,   0);
    gpio_set_level(OYOBYOK_PWR_AUX_GPIO,  0);
    ESP_LOGI(TAG,"power latch + display lines asserted (GPIO 42,17,35,47,39 low)");
}

static bool s_bl_ready=false;
void oyobyok_backlight_set(int pct){
    if(pct<0) pct=0;
    if(pct>100) pct=100;
    if(!s_bl_ready){
        ledc_timer_config_t t={ .speed_mode=LEDC_LOW_SPEED_MODE, .duty_resolution=LEDC_TIMER_13_BIT,
            .timer_num=LEDC_TIMER_0, .freq_hz=5000, .clk_cfg=LEDC_AUTO_CLK };
        ledc_timer_config(&t);
        ledc_channel_config_t c={ .gpio_num=OYOBYOK_BL_GPIO, .speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=LEDC_CHANNEL_0, .timer_sel=LEDC_TIMER_0, .duty=0, .hpoint=0 };
        ledc_channel_config(&c);
        s_bl_ready=true;
    }
    uint32_t duty = (uint32_t)((8191*pct)/100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG,"backlight %d%%", pct);
}

static const int s_bl_levels[] = { 0, 10, 30, 50, 70, 100 };
static int s_bl_idx = 4;
int oyobyok_backlight_cycle(void){
    s_bl_idx = (s_bl_idx + 1) % (int)(sizeof s_bl_levels/sizeof s_bl_levels[0]);
    oyobyok_backlight_set(s_bl_levels[s_bl_idx]);
    return s_bl_levels[s_bl_idx];
}

static bool s_chg_ready=false;
void oyobyok_power_charge_init(void){
    gpio_config_t io={
        .pin_bit_mask=(1ULL<<OYOBYOK_CHG_STAT1_GPIO)|(1ULL<<OYOBYOK_CHG_STAT2_GPIO),
        .mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE,
        .pull_down_en=GPIO_PULLDOWN_DISABLE,
        .intr_type=GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    s_chg_ready=true;
}

void oyobyok_power_charge_read(int* stat1,int* stat2){
    if(!s_chg_ready) oyobyok_power_charge_init();
    if(stat1) *stat1=gpio_get_level(OYOBYOK_CHG_STAT1_GPIO);
    if(stat2) *stat2=gpio_get_level(OYOBYOK_CHG_STAT2_GPIO);
}

oyobyok_charge_state_t oyobyok_power_charge_state(void){
    int s1=1,s2=1; oyobyok_power_charge_read(&s1,&s2);
    if(s1==0) return OYOBYOK_CHG_CHARGING;
    if(s2==0) return OYOBYOK_CHG_FULL;
    return OYOBYOK_CHG_DISCHARGE;
}

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cali;
static bool s_adc_ready=false, s_adc_cali_ok=false;

static void batt_adc_init(void){
    adc_oneshot_unit_init_cfg_t uc={ .unit_id=ADC_UNIT_1 };
    if(adc_oneshot_new_unit(&uc,&s_adc)!=ESP_OK){ ESP_LOGW(TAG,"ADC init failed; battery unavailable"); return; }
    adc_oneshot_chan_cfg_t cc={ .atten=ADC_ATTEN_DB_12, .bitwidth=ADC_BITWIDTH_DEFAULT };
    adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &cc);
    adc_cali_curve_fitting_config_t cal={ .unit_id=ADC_UNIT_1, .chan=ADC_CHANNEL_0,
        .atten=ADC_ATTEN_DB_12, .bitwidth=ADC_BITWIDTH_DEFAULT };
    s_adc_cali_ok=(adc_cali_create_scheme_curve_fitting(&cal,&s_adc_cali)==ESP_OK);
    s_adc_ready=true;
}

int oyobyok_power_battery_mv(void){
    if(!s_adc_ready) batt_adc_init();
    if(!s_adc) return -1;
    long sum=0; int n=0;
    for(int i=0;i<64;i++){ int raw; if(adc_oneshot_read(s_adc,ADC_CHANNEL_0,&raw)==ESP_OK){ sum+=raw; n++; } }
    if(!n) return -1;
    int raw_avg=(int)(sum/n), pin_mv=0;
    if(s_adc_cali_ok) adc_cali_raw_to_voltage(s_adc_cali,raw_avg,&pin_mv);
    else pin_mv=(raw_avg*3100)/4095;
    return (int)(pin_mv*OYOBYOK_BATT_DIV_RATIO);
}

int oyobyok_power_battery_pct_from_mv(int mv){
    if(mv<0) return -1;
    static const int lut[][2]={ {4200,100},{4100,90},{4000,80},{3900,70},{3800,60},
                                {3700,45},{3600,30},{3500,15},{3400,5},{3300,0} };
    const int N=sizeof(lut)/sizeof(lut[0]);
    if(mv>=lut[0][0]) return 100;
    if(mv<=lut[N-1][0]) return 0;
    for(int i=0;i<N-1;i++){
        int hi=lut[i][0], lo=lut[i+1][0];
        if(mv<=hi && mv>=lo) return lut[i+1][1]+(mv-lo)*(lut[i][1]-lut[i+1][1])/(hi-lo);
    }
    return -1;
}

void oyobyok_power_off(void){
    ESP_LOGW(TAG,"powering off");
    for(int i=0;i<3;i++){ oyobyok_lcd_clear(true); oyobyok_lcd_flush(); vTaskDelay(pdMS_TO_TICKS(40)); }
    oyobyok_backlight_set(0);
    gpio_set_level(OYOBYOK_PWR_LATCH_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    // Still running means USB is feeding the rail; restart so the device stays usable on the dock.
    esp_restart();
}

void oyobyok_enter_download_mode(void){
    ESP_LOGW(TAG,"rebooting into ROM download mode");
    REG_SET_BIT(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}
