// HID usage to app key. Letters follow the selected layout; the number row and punctuation stay US.
#include "oyobyok_host.h"
#include <ctype.h>

#define MOD_CTRL  (0x01|0x10)
#define MOD_SHIFT (0x02|0x20)

static const char* NUM_LO="1234567890";
static const char* NUM_HI="!@#$%^&*()";
static char sym(uint8_t u,bool shift){
    switch(u){
        case 0x2D: return shift?'_':'-';   case 0x2E: return shift?'+':'=';
        case 0x2F: return shift?'{':'[';   case 0x30: return shift?'}':']';
        case 0x31: return shift?'|':'\\';  case 0x33: return shift?':':';';
        case 0x34: return shift?'"':'\'';  case 0x35: return shift?'~':'`';
        case 0x36: return shift?'<':',';   case 0x37: return shift?'>':'.';
        case 0x38: return shift?'?':'/';   default:   return 0;
    }
}

static int s_layout=0;
void oyobyok_keymap_set_layout(int layout){ if(layout>=0 && layout<4) s_layout=layout; }

// QWERTZ and AZERTY: map the physical key to the US usage that prints the same letter.
static uint8_t remap_letter_usage(uint8_t u){
    if(s_layout==1){
        if(u==0x1C) return 0x1D;
        if(u==0x1D) return 0x1C;
    } else if(s_layout==2){
        switch(u){ case 0x04: return 0x14; case 0x14: return 0x04;
                   case 0x1A: return 0x1D; case 0x1D: return 0x1A; }
    }
    return u;
}

// Dvorak: US letter position to Dvorak glyph. Four positions are punctuation.
static const char DVORAK_LO[26]={
    'a','x','j','e','.','u','i','d','c','h','t','n','m','b','r','l','\'','p','o','y','g','k',',','q','f',';'
};
static char dvorak_shift(char lo){
    switch(lo){ case '.': return '>'; case ',': return '<'; case '\'': return '"'; case ';': return ':'; }
    return (char)toupper((unsigned char)lo);
}

bool oyobyok_key_from_hid(const oyobyok_hid_event_t* ev, oyobyok_key_t* out){
    if(!ev->pressed) return false;
    uint8_t u=ev->keycode, m=ev->modifiers;
    bool ctrl=m&MOD_CTRL, shift=m&MOD_SHIFT;
    out->is_control=false; out->keycode=0; out->nav=OYOBYOK_NAV_NONE; out->shift=shift; out->ch=0;

    switch(u){
        case 0x52: out->nav=OYOBYOK_NAV_UP;    return true;
        case 0x51: out->nav=OYOBYOK_NAV_DOWN;  return true;
        case 0x50: out->nav=OYOBYOK_NAV_LEFT;  return true;
        case 0x4F: out->nav=OYOBYOK_NAV_RIGHT; return true;
        case 0x4A: out->nav=OYOBYOK_NAV_HOME;  return true;
        case 0x4D: out->nav=OYOBYOK_NAV_END;   return true;
        case 0x4B: out->nav=OYOBYOK_NAV_PGUP;  return true;
        case 0x4E: out->nav=OYOBYOK_NAV_PGDN;  return true;
        case 0x4C: out->nav=OYOBYOK_NAV_DEL;   return true;
        case 0x28: out->nav=OYOBYOK_NAV_ENTER; return true;
        case 0x2A: out->nav=OYOBYOK_NAV_BKSP;  return true;
        case 0x29: out->is_control=true; out->keycode=0x1B; return true;
    }

    // Ctrl chords are keyed by physical position, so Ctrl-S is the same key on every layout.
    if(ctrl){
        if(u>=0x04 && u<=0x1D){ out->is_control=true; out->keycode=(uint8_t)(u-0x04+1); return true; }
        if(u==0x2C){ out->is_control=true; out->keycode=0x00; return true; }
        return false;
    }

    if(u>=0x04 && u<=0x1D){
        if(s_layout==3){ char lo=DVORAK_LO[u-0x04]; out->ch=shift?dvorak_shift(lo):lo; return true; }
        uint8_t ru=remap_letter_usage(u);
        char c=(char)('a'+(ru-0x04)); out->ch=shift?(char)(c-32):c; return true;
    }
    if(u>=0x1E && u<=0x27){ out->ch = shift?NUM_HI[u-0x1E]:NUM_LO[u-0x1E]; return true; }
    if(u==0x2C){ out->ch=' '; return true; }
    { char s=sym(u,shift); if(s){ out->ch=s; return true; } }
    return false;
}
