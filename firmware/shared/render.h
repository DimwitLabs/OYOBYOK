// 240x80 monochrome framebuffer and text primitives, shared by the device host and the emulator.
// fb bit set = ink (dark pixel). The 5x8 font is column-major (glcdfont.c).
#ifndef OYOBYOK_RENDER_H
#define OYOBYOK_RENDER_H
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#define PROGMEM
#include "glcdfont.c"

#define W 240
#define H 80
#define STRIDE (W/8)
#define ADV 6                 // 5 px glyph + 1 px gap
#define BAND_Y  (H-14)        // bottom hint band: divider line
#define BAND_T  (H-11)        // bottom hint band: text row
static uint8_t fb[STRIDE*H];

static void px(int x,int y,int ink){ if((unsigned)x>=W||(unsigned)y>=H)return;
    uint8_t*b=&fb[y*STRIDE+(x>>3)]; uint8_t m=0x80>>(x&7); if(ink)*b|=m; else *b&=~m; }
static void hline(int x0,int x1,int y){ for(int x=x0;x<=x1;x++)px(x,y,1); }
static void ch(int x,int y,char c){ const unsigned char*g=&font[(unsigned char)c*5];
    for(int col=0;col<5;col++)for(int r=0;r<8;r++) if(g[col]&(1<<r)) px(x+col,y+r,1); }
static int textA(int x,int y,const char*s,int adv){ for(;*s;s++){ ch(x,y,*s); x+=adv; } return x; }
static int text(int x,int y,const char*s){ return textA(x,y,s,ADV); }
static void rtext(int xr,int y,const char*s){ text(xr-ADV*(int)strlen(s),y,s); }

// Small font: the 5x8 glyph shrunk into a 4x6 cell. Each target pixel is the OR of the source
// pixels it covers, so strokes survive the downscale.
#define SADV 5
static void ch_small(int x,int y,char c){ const unsigned char*g=&font[(unsigned char)c*5];
    for(int dc=0;dc<4;dc++){ int s0=(dc*5)/4, s1=((dc+1)*5)/4; if(s1<=s0)s1=s0+1; if(s1>5)s1=5;
        for(int dr=0;dr<6;dr++){ int r0=(dr*8)/6, r1=((dr+1)*8)/6; if(r1<=r0)r1=r0+1; if(r1>8)r1=8;
            int ink=0; for(int sc=s0;sc<s1&&!ink;sc++) for(int rr=r0;rr<r1;rr++) if(g[sc]&(1<<rr)){ink=1;break;}
            if(ink) px(x+dc,y+dr,1); } } }
static int text_small(int x,int y,const char*s){ for(;*s;s++){ ch_small(x,y,*s); x+=SADV; } return x; }
static void rtext_small(int xr,int y,const char*s){ text_small(xr-SADV*(int)strlen(s),y,s); }

// Bitmap glyphs, one byte per row, MSB on the left.
static void blit(int x,int y,const uint8_t*rows,int h,int w){
    for(int r=0;r<h;r++)for(int c=0;c<w;c++) if(rows[r]&(1<<(w-1-c))) px(x+c,y+r,1); }
static const uint8_t G_POINT[7] ={0x40,0x60,0x70,0x78,0x70,0x60,0x40};        // selection pointer
static const uint8_t G_UP[5]    ={0x04,0x0E,0x1F,0x00,0x00};                  // more above
static const uint8_t G_DOWN[5]  ={0x00,0x00,0x1F,0x0E,0x04};                  // more below
static const uint8_t G_BACK[7]  ={0x00,0x10,0x20,0x7F,0x20,0x10,0x00};        // back arrow
static const uint8_t G_FOLDER[7]={0xE0,0xFE,0x82,0x82,0x82,0x82,0xFE};        // folder
static const uint8_t G_DOC[8]   ={0x3E,0x23,0x21,0x2D,0x21,0x2D,0x21,0x3F};   // document
static const uint8_t G_PENCIL[7]={0x03,0x06,0x0C,0x18,0x30,0x60,0x40};        // Manage
static const uint8_t G_CHECK[7] ={0x00,0x01,0x02,0x44,0x28,0x10,0x00};        // Done

// Plain scrolling list with a pointer (used by the conflict and manage choosers).
static void list(const char*items[],int n,int sel,int top,int visible,int rowh,int y0,int botlimit){
    memset(fb,0,sizeof fb);
    for(int i=0;i<visible && top+i<n;i++){
        int idx=top+i, y=y0+i*rowh;
        if(idx==sel) blit(1,y+1,G_POINT,7,5);
        text(9,y,items[idx]);
    }
    if(top>0)        blit(2,y0-6,G_UP,5,5);
    if(top+visible<n)blit(2,botlimit-6,G_DOWN,5,5);
}

// Two-row status panel drawn over the bottom of whatever is on screen (power button).
static void statuspanel(const char* l1,const char* r1,const char* l2,const char* r2){
    int top=H-26;
    for(int y=top;y<H;y++) memset(&fb[y*STRIDE],0,STRIDE);
    hline(0,W-1,top);
    int y1=top+5, y2=top+16;
    if(l1&&*l1){ text(4,y1,l1); }
    if(r1&&*r1){ rtext(W-4,y1,r1); }
    if(l2&&*l2){ text(4,y2,l2); }
    if(r2&&*r2){ rtext(W-4,y2,r2); }
}

// Text entry: prompt, typed value, block cursor.
static void entry(const char*prompt,const char*value,int cursor){
    memset(fb,0,sizeof fb);
    text(2,3,prompt);
    int x=text(2,20,value);
    if(cursor){ for(int y=19;y<28;y++) px(x+1,y,1); }
    hline(0,W-1,BAND_Y);
    text(2,BAND_T,"enter: save"); rtext(W-2,BAND_T,"esc: cancel");
}

// Editor metrics.
#define EADV 6
#define ELH  9
#define ECOLS ((W-3)/EADV)

#ifndef OYOBYOK_NO_HOST_IO   // emulator only
static void export_png(const char*name){
    int S=6; char ppm[128]; snprintf(ppm,sizeof ppm,"%s.ppm",name);
    FILE*f=fopen(ppm,"wb"); if(!f) return;
    fprintf(f,"P6\n%d %d\n255\n",W*S,H*S);
    for(int y=0;y<H;y++)for(int sy=0;sy<S;sy++)for(int x=0;x<W;x++){
        int ink=fb[y*STRIDE+(x>>3)]&(0x80>>(x&7)); uint8_t p[3];
        if(ink){p[0]=40;p[1]=34;p[2]=18;}else{p[0]=196;p[1]=186;p[2]=120;}
        for(int sx=0;sx<S;sx++)fwrite(p,1,3,f); }
    fclose(f);
    char cmd[256]; snprintf(cmd,sizeof cmd,"sips -s format png %s --out %s.png >/dev/null 2>&1",ppm,name);
    if(system(cmd)==0) printf("-> %s.png\n",name); else printf("wrote %s\n",ppm);
}

// Terminal preview: two pixel rows per text row using half blocks.
static void fb_to_ascii(void){
    for(int y=0;y<H;y+=2){
        for(int x=0;x<W;x+=2){
            int t=fb[y*STRIDE+(x>>3)]&(0x80>>(x&7));
            int b=(y+1<H)?(fb[(y+1)*STRIDE+(x>>3)]&(0x80>>(x&7))):0;
            fputs(t&&b?"█":t?"▀":b?"▄":" ",stdout);
        }
        putchar('\n');
    }
}
#endif
#endif
