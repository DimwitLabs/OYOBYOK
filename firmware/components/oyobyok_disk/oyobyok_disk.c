#include "oyobyok_disk.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"

static const char* TAG="oyobyok_disk";

extern sdmmc_card_t* oyobyok_sd_card(void);   // the card mounted by oyobyok_main

esp_err_t oyobyok_disk_enter(void){
    sdmmc_card_t* app_card = oyobyok_sd_card();
    if(!app_card){ ESP_LOGE(TAG,"no SD card mounted"); return ESP_ERR_NOT_FOUND; }

    // Release the app's /sdcard mount, then re-init the card raw for the MSC layer.
    esp_err_t e = esp_vfs_fat_sdcard_unmount("/sdcard", app_card);
    if(e!=ESP_OK) ESP_LOGW(TAG,"sdcard unmount returned %s (continuing)", esp_err_to_name(e));

    static sdmmc_host_t host;   host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1; host.flags = SDMMC_HOST_FLAG_1BIT; host.max_freq_khz = 40000;
    static sdmmc_slot_config_t slot; slot = (sdmmc_slot_config_t)SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width=1; slot.clk=GPIO_NUM_5; slot.cmd=GPIO_NUM_3; slot.d0=GPIO_NUM_4;
    slot.cd=SDMMC_SLOT_NO_CD; slot.wp=SDMMC_SLOT_NO_WP;
    ESP_RETURN_ON_ERROR(sdmmc_host_init(), TAG, "sdmmc host init");
    ESP_RETURN_ON_ERROR(sdmmc_host_init_slot(host.slot, &slot), TAG, "sdmmc slot init");
    static sdmmc_card_t card;
    ESP_RETURN_ON_ERROR(sdmmc_card_init(&host, &card), TAG, "sdmmc card init");

    const tinyusb_msc_sdmmc_config_t mcfg = { .card = &card };
    ESP_RETURN_ON_ERROR(tinyusb_msc_storage_init_sdmmc(&mcfg), TAG, "msc storage init");

    const tinyusb_config_t tcfg = { 0 };
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tcfg), TAG, "tinyusb install");

    ESP_LOGI(TAG,"Disk Mode active (power-cycle to exit)");
    return ESP_OK;
}
