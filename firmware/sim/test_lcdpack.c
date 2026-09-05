// Row-major framebuffer to UC1611 page packing (oyobyok_lcd_pack.h).
#include "oyobyok_lcd_pack.h"
#include <stdio.h>
#include <string.h>

static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)

static void set_px(uint8_t* fb,int x,int y){ fb[y*LCDP_STRIDE+(x>>3)] |= (uint8_t)(0x80>>(x&7)); }

int main(void){
    static uint8_t fb[LCDP_PAGESZ], pg[LCDP_PAGESZ];

    // single pixel top-left -> page 0, col 0, bit 0
    memset(fb,0,sizeof fb); set_px(fb,0,0); oyobyok_lcd_pack_pages(fb,pg);
    CHECK(pg[0]==0x01);
    for(int i=1;i<LCDP_PAGESZ;i++) CHECK(pg[i]==0);

    // pixel at (5,9): page 1 (row 9/8), col 5, bit 1 (9%8)
    memset(fb,0,sizeof fb); set_px(fb,5,9); oyobyok_lcd_pack_pages(fb,pg);
    CHECK(pg[1*LCDP_W + 5]==0x02);

    // bottom-right (239,79): last page 9, col 239, bit 7
    memset(fb,0,sizeof fb); set_px(fb,239,79); oyobyok_lcd_pack_pages(fb,pg);
    CHECK(pg[9*LCDP_W + 239]==0x80);

    // a full column (x=10, all rows) -> every page at col 10 = 0xFF
    memset(fb,0,sizeof fb); for(int y=0;y<LCDP_H;y++) set_px(fb,10,y); oyobyok_lcd_pack_pages(fb,pg);
    for(int p=0;p<LCDP_PAGES;p++) CHECK(pg[p*LCDP_W+10]==0xFF);

    // round-trip: pack then reconstruct and compare a scattered pattern
    memset(fb,0,sizeof fb);
    for(int i=0;i<500;i++){ int x=(i*37)%LCDP_W, y=(i*53)%LCDP_H; set_px(fb,x,y); }
    oyobyok_lcd_pack_pages(fb,pg);
    int mism=0;
    for(int y=0;y<LCDP_H;y++) for(int x=0;x<LCDP_W;x++){
        int got=(pg[(y/8)*LCDP_W+x]>>(y%8))&1;
        if(got!=lcdp_pixel(fb,x,y)) mism++;
    }
    CHECK(mism==0);

    printf(fails? "\n%d CHECK(s) FAILED\n":"\nall pack checks passed\n", fails);
    return fails?1:0;
}
