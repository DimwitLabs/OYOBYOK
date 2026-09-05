// 240x80 monochrome LCD on a UC1611s controller, driven over I2C.
//
//   SDA GPIO18, SCL GPIO10 (software I2C, ~150 kHz)
//   address 0x38 takes command opcodes, 0x39 takes command parameters and pixel data
//   one byte per I2C transaction; reset is the 0xE1/0xE2 command pair
//   the visible area is controller pages 10..19 (a +10 page offset)
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define OYOBYOK_LCD_W       240
#define OYOBYOK_LCD_H       80
#define OYOBYOK_LCD_STRIDE  (OYOBYOK_LCD_W / 8)
#define OYOBYOK_LCD_FBSIZE  (OYOBYOK_LCD_STRIDE * OYOBYOK_LCD_H)

#define OYOBYOK_LCD_SDA_GPIO    18
#define OYOBYOK_LCD_SCL_GPIO    10
#define OYOBYOK_LCD_ADDR_CMD    0x38
#define OYOBYOK_LCD_ADDR_DATA   0x39

int      oyobyok_lcd_init(void);
uint8_t *oyobyok_lcd_framebuffer(void);          // row-major, MSB-left, bit set = ink
void     oyobyok_lcd_clear(bool white);
void     oyobyok_lcd_flush(void);                // sends only the pages that changed
void     oyobyok_lcd_set_contrast(uint8_t value);   // UC1611s VBIAS, applied live
