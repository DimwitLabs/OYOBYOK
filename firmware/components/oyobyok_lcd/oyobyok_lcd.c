#include "oyobyok_lcd.h"
#include "oyobyok_lcd_pack.h"
#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "oyobyok_lcd";

// VBIAS operating point for this panel: 0x73 is faint, 0xF0 ghosts, 0xB0 is crisp.
#define OYOBYOK_LCD_CONTRAST     0xB0
#define OYOBYOK_LCD_PAGE_OFFSET  10

static uint8_t s_fb[OYOBYOK_LCD_FBSIZE];
static uint8_t s_pages[LCDP_PAGESZ];
static uint8_t s_sent[LCDP_PAGESZ];
static bool    s_sent_valid=false;
static int     s_err;

// Software I2C, open-drain on both lines.
#define BB_DLY() esp_rom_delay_us(2)
static inline void bb_sda(int v){ gpio_set_level(OYOBYOK_LCD_SDA_GPIO, v); }
static inline void bb_scl(int v){ gpio_set_level(OYOBYOK_LCD_SCL_GPIO, v); }
static void bb_bus_init(void){
    gpio_config_t io = { .pin_bit_mask=(1ULL<<OYOBYOK_LCD_SDA_GPIO)|(1ULL<<OYOBYOK_LCD_SCL_GPIO),
        .mode=GPIO_MODE_INPUT_OUTPUT_OD, .pull_up_en=GPIO_PULLUP_ENABLE,
        .pull_down_en=GPIO_PULLDOWN_DISABLE, .intr_type=GPIO_INTR_DISABLE };
    gpio_config(&io);
    bb_sda(1); bb_scl(1); BB_DLY();
}
static void bb_start(void){ bb_sda(1);BB_DLY(); bb_scl(1);BB_DLY(); bb_sda(0);BB_DLY(); bb_scl(0);BB_DLY(); }
static void bb_stop(void){  bb_sda(0);BB_DLY(); bb_scl(1);BB_DLY(); bb_sda(1);BB_DLY(); }
static int bb_wbyte(uint8_t v){
    for(int i=0;i<8;i++){ bb_sda((v>>7)&1); v<<=1; BB_DLY(); bb_scl(1); BB_DLY(); bb_scl(0); BB_DLY(); }
    bb_sda(1); BB_DLY(); bb_scl(1); BB_DLY();
    int nack = gpio_get_level(OYOBYOK_LCD_SDA_GPIO);
    bb_scl(0); BB_DLY();
    return nack;
}
static int bb_write(uint8_t addr7, uint8_t data){
    bb_start();
    int n1 = bb_wbyte((uint8_t)(addr7<<1));
    int n2 = bb_wbyte(data);
    bb_stop();
    return n1 | n2;
}
static inline void c(uint8_t b){ if(bb_write(OYOBYOK_LCD_ADDR_CMD,b)  && s_err++<8) ESP_LOGE(TAG,"cmd 0x%02x NACK",b); }
static inline void d(uint8_t b){ if(bb_write(OYOBYOK_LCD_ADDR_DATA,b) && s_err++<8) ESP_LOGE(TAG,"data 0x%02x NACK",b); }

void oyobyok_lcd_set_contrast(uint8_t value){ c(0x81); d(value); }

uint8_t *oyobyok_lcd_framebuffer(void) { return s_fb; }
void oyobyok_lcd_clear(bool white) { memset(s_fb, white ? 0x00 : 0xFF, sizeof s_fb); }

static void set_window(int col0, int col1, int page0, int page1)
{
    c(0x89);
    c(0xF8);
    c(0xF5); d((uint8_t)(page0 + OYOBYOK_LCD_PAGE_OFFSET));
    c(0xF7); d((uint8_t)(page1 + OYOBYOK_LCD_PAGE_OFFSET));
    c(0xF4); d((uint8_t)col0);
    c(0xF6); d((uint8_t)col1);
    c(0xF9);
    c(0x01);
}

void oyobyok_lcd_flush(void)
{
    oyobyok_lcd_pack_pages(s_fb, s_pages);
    for (int i = 0; i < LCDP_PAGESZ; i++) s_pages[i] = (uint8_t)~s_pages[i];   // panel runs inverted
    bool dirty[LCDP_PAGES]; int ndirty=0;
    for (int pg = 0; pg < LCDP_PAGES; pg++) {
        dirty[pg] = !s_sent_valid || memcmp(&s_pages[pg*LCDP_W], &s_sent[pg*LCDP_W], LCDP_W) != 0;
        if (dirty[pg]) ndirty++;
    }
    if (ndirty == 0) return;
    for (int pg = 0; pg < LCDP_PAGES; ) {
        if (!dirty[pg]) { pg++; continue; }
        int p1 = pg; while (p1 + 1 < LCDP_PAGES && dirty[p1 + 1]) p1++;
        set_window(0, LCDP_W - 1, pg, p1);
        for (int i = pg*LCDP_W; i < (p1+1)*LCDP_W; i++) d(s_pages[i]);
        pg = p1 + 1;
    }
    memcpy(s_sent, s_pages, LCDP_PAGESZ); s_sent_valid = true;
}

// Controller configuration as (destination, byte) pairs: 0 = command address, 1 = data address.
static const uint8_t s_init_tbl[] = {
    0x00, 0xC9,   0x01, 0xAC,
    0x00, 0x24,
    0x00, 0x2D,
    0x00, 0xE9,
    0x00, 0xA3,
    0x00, 0xC4,
    0x00, 0x88,
    0x00, 0xD2,
    0x00, 0xD5,
    0x00, 0x81,   0x01, 0x80,
    0x00, 0x95,
    0x00, 0xA7,
};

int oyobyok_lcd_init(void)
{
    bb_bus_init();

    vTaskDelay(pdMS_TO_TICKS(150));
    c(0xE1);
    d(0xE2);
    vTaskDelay(pdMS_TO_TICKS(150));

    for (unsigned i = 0; i + 1 < sizeof s_init_tbl; i += 2) {
        if (s_init_tbl[i]) d(s_init_tbl[i + 1]); else c(s_init_tbl[i + 1]);
    }
    c(0x81);
    d(OYOBYOK_LCD_CONTRAST);

    // Clear the controller's whole RAM (pages 0..19) so the margins outside our window stay light.
    c(0x89); c(0xF8);
    c(0xF5); d(0);      c(0xF7); d(19);
    c(0xF4); d(0);      c(0xF6); d(239);
    c(0xF9); c(0x01);
    for (int i = 0; i < 20 * 240; i++) d(0xFF);

    oyobyok_lcd_clear(true);
    c(0xC9);
    d(0xAD);
    oyobyok_lcd_flush();
    ESP_LOGI(TAG, "LCD up (sda=%d scl=%d)", OYOBYOK_LCD_SDA_GPIO, OYOBYOK_LCD_SCL_GPIO);
    return s_err ? -1 : 0;
}
