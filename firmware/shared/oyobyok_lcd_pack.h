// Framebuffer packing for the UC1611s controller. render.h draws a row-major 1bpp buffer
// (30 bytes per row, MSB = left pixel, bit set = ink); the controller wants page format
// (10 pages of 240 columns, each byte = 8 vertical pixels, bit0 = top). Shared by the device
// driver and the desktop test.
#ifndef OYOBYOK_LCD_PACK_H
#define OYOBYOK_LCD_PACK_H
#include <stdint.h>

#define LCDP_W 240
#define LCDP_H 80
#define LCDP_STRIDE (LCDP_W/8)
#define LCDP_PAGES  (LCDP_H/8)
#define LCDP_PAGESZ (LCDP_PAGES*LCDP_W)

static inline int lcdp_pixel(const uint8_t* rowmajor,int x,int y){
    return (rowmajor[y*LCDP_STRIDE + (x>>3)] >> (7-(x&7))) & 1;
}
static inline void oyobyok_lcd_pack_pages(const uint8_t* src,uint8_t* dst){
    for(int page=0; page<LCDP_PAGES; page++)
        for(int col=0; col<LCDP_W; col++){
            uint8_t b=0;
            for(int bit=0; bit<8; bit++)
                if(lcdp_pixel(src,col,page*8+bit)) b |= (uint8_t)(1u<<bit);
            dst[page*LCDP_W+col]=b;
        }
}
#endif
