// OYOBYOK app core: the whole screen state machine, shared by the on-device host and the desktop
// emulator. Platform code feeds keys through app_nav/app_type/app_ctrl/app_esc/app_enter, calls
// render_current() to draw into the render.h framebuffer, and registers device hooks through the
// oyobyok_app_set_* functions (the emulator leaves them NULL).
//
// The includer must have included render.h, git_engine.h and oyobyok_menus.h first. This is a .inc
// file: it is included exactly once, into one translation unit.

#include "oyobyok_key.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#define items_for oyobyok_items_for

#ifdef ESP_PLATFORM
  #include "esp_log.h"
  #define APPLOG(fmt,...) ESP_LOGI("oyobyok_app", fmt, ##__VA_ARGS__)
#else
  #include <stdio.h>
  #define APPLOG(fmt,...) fprintf(stderr, "[oyobyok_app] " fmt "\n", ##__VA_ARGS__)
#endif

#define OYOBYOK_VERSION "1.0.0"
enum { SCR_MENU=1, SCR_EDITOR, SCR_LIST, SCR_ENTRY, SCR_ALERT, SCR_GITSTATUS, SCR_NLIST, SCR_BTSCAN, SCR_CONTRAST, SCR_DISK };

// SCR_LIST contents. Every list has "Back" as row 0.
#define LT_FILE       0     // file manager under the Projects root
#define LT_BT         200   // BLE scan results
#define LT_WIFI_SCAN  202
#define LT_WIFI_KNOWN 203
#define LT_KB         204   // keyboard layout picker
#define LT_CURSOR     205   // cursor style picker
#define LT_SFTP       206   // configured SFTP servers
#define LT_GITREPO    207   // configured Git repos
#define LT_SFTPPROJ   208   // project folders to push to the chosen SFTP server

// ---- device hooks (NULL on the emulator) --------------------------------------------------------
static int         (*s_bt_scan)(int seconds)=0;
static const char* (*s_bt_name)(int index)=0;
static int         (*s_bt_connect)(int index)=0;
static int         (*s_bt_saved_count)(void)=0;
static const char* (*s_bt_saved_name)(int index)=0;
static void        (*s_bt_forget_saved)(void)=0;
static int         (*s_bt_conn_name)(char*,int)=0;
static long        (*s_free_mb)(void)=0;
static int         (*s_batt_pct)(void)=0;
static void        (*s_set_contrast)(int v)=0;
static void        (*s_set_kblayout)(int i)=0;
static void        (*s_led)(int state)=0;
static int         (*s_disk_enter)(void)=0;
static void        (*s_ssh_test)(void)=0;
static const char* (*s_ssh_result)(void)=0;
static int         (*s_sftp_list)(char(*)[33],int)=0;
static void        (*s_sftp_push)(int index,const char* project)=0;
static int         (*s_sftp_progress)(void)=0;
static const char* (*s_sftp_result)(void)=0;
static void        (*s_git_sync)(const char* project)=0;
static void        (*s_git_status_async)(const char* project)=0;
static int         (*s_git_status_take)(GitResult* out)=0;
static const char* (*s_git_result)(void)=0;
static int   (*s_wifi_scan)(char(*)[33],int)=0;
static int   (*s_wifi_known)(char(*)[33],int)=0;
static void  (*s_wifi_connect)(const char*,const char*)=0;
static void  (*s_wifi_connect_known)(const char*)=0;
static void  (*s_wifi_disconnect)(void)=0;
static int   (*s_wifi_is_en)(void)=0;
static void  (*s_wifi_set_en)(int)=0;
static int   (*s_wifi_conn_name)(char*,int)=0;

// Mirrors oyobyok_led_state_t.
enum { BLED_OFF=0,BLED_KB_CONNECTED,BLED_KB_LOST,BLED_SYNCING,BLED_SYNC_OK,BLED_SYNC_FAIL,
       BLED_CHARGING,BLED_CHARGED,BLED_LOW_BATTERY,BLED_SAVED,BLED_DISK_MODE };
#define LED(st) do{ if(s_led) s_led(st); }while(0)

// ---- UI state ----------------------------------------------------------------------------------
static int  screen=SCR_MENU;
static int  curmenu=M_MAIN, selByMenu[128];
static int  have_editor=0, status_on=0;
static char edbuf[8192]; static int edlen=0;
static char entryPrompt[64]="", entryBuf[256];
static char alertTitle[64], alertMsg[256];
static char listItems[32][48]; static int listN=0, listSel=0, listType=0;
enum { FK_FILE=0, FK_DIR=1, FK_UP=2, FK_NEWFILE=3, FK_NEWFOLDER=4, FK_NEWPROJ=5, FK_BACK=6, FK_PLAIN=7, FK_MANAGE=8 };
static int  listKind[32];
enum { NL_CONFLICT, NL_MANAGE, NL_DELCONFIRM };
static int  nMode=NL_CONFLICT, nN=0, nSel=0;
static char nItems[20][48];
static int  s_bt_pending=0, s_bt_ndev=0;
static int  entryCtxFS=0;        // naming a new object: 1 file, 2 folder, 3 project
static int  entryCtxRename=0;
static int  entryCtxWifi=0, entryCtxWifiName=0;
static int  s_manage=0;          // file manager in Manage (rename/delete) mode
static char mgTarget[64]="";  static int mgIsDir=0;
static char wifiSSID[33]="";
static char curProject[64]="", curFile[64]="", lastPush[24]="never";
static char curDir[400]="", appReposRoot[256]="";
static int  s_sftpSrv=0; static char s_sftpSrvName[33]=""; static int s_sftpEstFiles=0; static char s_sftpEst[40]="";
static GitResult gStat; static GitRemote gRem;

static int  ci_eq(const char* a,const char* b){ for(;*a&&*b;a++,b++){
        char x=*a,y=*b; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0; }
    return *a==*b; }
static int  name_reserved(const char* nm){ return ci_eq(nm,"Back")||ci_eq(nm,"New Project")||ci_eq(nm,"Manage"); }

static void mark_synced(void){
    time_t t=time(NULL);
    if(t>1700000000L){ struct tm tm; localtime_r(&t,&tm); strftime(lastPush,sizeof lastPush,"%d %b %H:%M",&tm); }
    else snprintf(lastPush,sizeof lastPush,"just now");
}

// ---- busy alerts and the radio -----------------------------------------------------------------
// The Synchronise page owns WiFi: up on entry, down on exit, unless a network task is still running.
// Long operations show a busy alert: an elapsed counter, Esc to stop waiting (the task finishes in
// the background) and a timeout. The host calls oyobyok_app_tick() once a second while busy.
static int  s_sync_busy=0;
static int  alertBusy=0; static unsigned alertSecs=0, alertLimit=0;
#define BUSY(lim) do{ alertBusy=1; alertSecs=0; alertLimit=(lim); }while(0)
static void sync_radio(int on){
    if(!s_wifi_set_en) return;
    if(on) s_wifi_set_en(1);
    else if(!s_sync_busy) s_wifi_set_en(0);
}
static int in_sync_area(void){
    if(screen==SCR_EDITOR) return 0;
    return curmenu==M_CONN||curmenu==M_GIT||curmenu==M_WIFI
        ||listType==LT_WIFI_SCAN||listType==LT_WIFI_KNOWN||listType==LT_SFTP||listType==LT_SFTPPROJ||listType==LT_GITREPO;
}
static void op_finished(void){ s_sync_busy=0; alertBusy=0; if(!in_sync_area()) sync_radio(0); }
static void alert(const char* title,const char* msg){
    snprintf(alertTitle,sizeof alertTitle,"%s",title); snprintf(alertMsg,sizeof alertMsg,"%s",msg); screen=SCR_ALERT;
}
static int wifi_ready_or_alert(void){
    if(!s_wifi_set_en) return 1;
    char nm[33]=""; if(s_wifi_conn_name && s_wifi_conn_name(nm,sizeof nm)) return 1;
    alert("Git","WiFi is still connecting. Try again in a moment."); return 0;
}

// ---- boot splash --------------------------------------------------------------------------------
static const char* const SPLASH_Q[]={
    "Everything was beautiful and nothing hurt.",
    "I would always rather be happy than dignified.",
    "It was the best of times, it was the worst of times.",
    "There is nothing to writing.",
    "The first draft of anything is shit.",
};
static const char* const SPLASH_BY[]={ "Kurt Vonnegut", "Charlotte Bronte", "Charles Dickens", "Ernest Hemingway", "Ernest Hemingway" };
static void __attribute__((unused)) app_draw_splash(unsigned seed){
    memset(fb,0,sizeof fb);
    const int NQ=(int)(sizeof SPLASH_Q/sizeof SPLASH_Q[0]);
    int q=(int)(seed%(unsigned)NQ);
    const int COLS=(W-8)/ADV;
    char lines[4][64]; int nl=0;
    const char* p=SPLASH_Q[q]; char cur[64]=""; int cl=0;
    while(*p && nl<4){
        const char* w=p; int wl=0; while(w[wl]&&w[wl]!=' ') wl++;
        if(cl && cl+1+wl>COLS){ snprintf(lines[nl++],64,"%s",cur); cur[0]=0; cl=0; }
        if(cl){ cur[cl++]=' '; cur[cl]=0; }
        if(wl>63-cl) wl=63-cl;
        memcpy(cur+cl,w,wl); cl+=wl; cur[cl]=0;
        p=w+wl; while(*p==' ') p++;
    }
    if(cl && nl<4) snprintf(lines[nl++],64,"%s",cur);
    const int LH=10, blockH=nl*LH;
    int y=(H-14-blockH)/2; if(y<2) y=2;
    for(int i=0;i<nl;i++,y+=LH){ int x=(W-ADV*(int)strlen(lines[i]))/2; text(x<2?2:x,y,lines[i]); }
    char by[48]; snprintf(by,sizeof by,"- %s",SPLASH_BY[q]);
    rtext_small(W-4,H-9,by);
}

// ---- files and preferences ----------------------------------------------------------------------
static const char* cur_dir(void){
    static char d[400];
    if(curDir[0]) return curDir;
    snprintf(d,sizeof d,"%s/%s",appReposRoot,curProject); return d;
}

static int  s_cursorHoriz=0;
static int  s_kbLayout=0;         // 0 QWERTY, 1 QWERTZ, 2 AZERTY, 3 Dvorak
static int  s_contrast=0xB0;
static void prefs_path(char* out,size_t n){ snprintf(out,n,"%s/.oyobyokprefs",appReposRoot[0]?appReposRoot:"."); }
static void prefs_save(void){
    char p[300]; prefs_path(p,sizeof p);
    mkdir(appReposRoot,0777);
    FILE* f=fopen(p,"w"); if(!f){ APPLOG("prefs save failed '%s'",p); return; }
    fprintf(f,"cursor=%d\nkb=%d\ncontrast=%d\n",s_cursorHoriz,s_kbLayout,s_contrast);
    fclose(f);
}
static void prefs_load(void){
    char p[300]; prefs_path(p,sizeof p);
    FILE* f=fopen(p,"r"); if(!f) return;
    char line[64];
    while(fgets(line,sizeof line,f)){
        int v;
        if(sscanf(line,"cursor=%d",&v)==1) s_cursorHoriz=v?1:0;
        else if(sscanf(line,"kb=%d",&v)==1){ if(v>=0&&v<4) s_kbLayout=v; }
        else if(sscanf(line,"contrast=%d",&v)==1){ if(v>=0&&v<=255) s_contrast=v; }
    }
    fclose(f);
}

static int save_current_file(void){
    mkdir(appReposRoot,0777); mkdir(cur_dir(),0777);
    char path[460]; snprintf(path,sizeof path,"%s/%s",cur_dir(),curFile);
    FILE* f=fopen(path,"w");
    if(!f){ APPLOG("save failed '%s'",path); return 0; }
    fwrite(edbuf,1,edlen,f); fclose(f);
    return 1;
}
static void load_current_file(void){
    char path[460]; snprintf(path,sizeof path,"%s/%s",cur_dir(),curFile);
    FILE* f=fopen(path,"r"); edlen=0; edbuf[0]=0;
    if(f){ edlen=(int)fread(edbuf,1,sizeof edbuf-1,f); if(edlen<0)edlen=0; edbuf[edlen]=0; fclose(f); }
}
static int word_count(const char* s){ int n=0,in=0;
    for(;*s;s++){ if(*s==' '||*s=='\n'||*s=='\t'||*s=='\r') in=0; else if(!in){ in=1; n++; } } return n; }

// ---- editor -------------------------------------------------------------------------------------
static int cur=0, edTop=0, selPos=-1, selLen=0, selAnchor=-1;
static char s_clip[8192]; static int s_cliplen=0;
#define EVIS (H/ELH)
#define ED_MAXLN 4096
static int ed_ls[ED_MAXLN+1], ed_nl=0;   // ed_ls[k] = buffer index where wrapped line k starts

static void ed_layout(void){
    ed_nl=0; ed_ls[0]=0; int col=0, i=0;
    while(i<edlen){
        char c=edbuf[i];
        if(c=='\n'){ i++; if(ed_nl<ED_MAXLN-1) ed_ls[++ed_nl]=i; col=0; continue; }
        int w=i; while(w<edlen && edbuf[w]!=' ' && edbuf[w]!='\n') w++;
        int wl=w-i;
        if(col>0 && col+wl>ECOLS){ if(ed_nl<ED_MAXLN-1) ed_ls[++ed_nl]=i; col=0; }
        if(wl>ECOLS){ while(i<w){ if(col==ECOLS){ if(ed_nl<ED_MAXLN-1) ed_ls[++ed_nl]=i; col=0; } i++; col++; } }
        else { col+=wl; i=w; }
        if(i<edlen && edbuf[i]==' '){ col++; i++; if(col>=ECOLS){ if(ed_nl<ED_MAXLN-1) ed_ls[++ed_nl]=i; col=0; } }
    }
    ed_nl++; ed_ls[ed_nl]=edlen;
}
static int ed_cursor_line(void){
    for(int k=0;k<ed_nl;k++){ int s=ed_ls[k], e=ed_ls[k+1]; if(cur>=s && cur<e) return k; }
    return ed_nl-1;
}
static int ed_line_cols(int k){ int len=ed_ls[k+1]-ed_ls[k];
    if(len>0 && edbuf[ed_ls[k+1]-1]=='\n') len--;
    return len; }
static void editor_render(void){
    memset(fb,0,sizeof fb);
    ed_layout();
    int cl=ed_cursor_line(); int ccol=cur-ed_ls[cl];
    if(cl<edTop) edTop=cl;
    if(cl>=edTop+EVIS) edTop=cl-EVIS+1;
    if(edTop<0) edTop=0;
    for(int v=0; v<EVIS && edTop+v<ed_nl; v++){
        int k=edTop+v, s=ed_ls[k], e=ed_ls[k+1]; int x=2, y=v*ELH;
        for(int i=s;i<e;i++){ char c=edbuf[i]; if(c=='\n') break;
            ch(x,y,c);
            if(selPos>=0 && i>=selPos && i<selPos+selLen) hline(x,x+EADV-2,y+8);
            x+=EADV;
        }
    }
    int cy=(cl-edTop)*ELH, cx=2+ccol*EADV;
    if(s_cursorHoriz) hline(cx,cx+EADV-1,cy+8<H?cy+8:H-1);
    else for(int yy=cy; yy<cy+8 && yy<H; yy++) px(cx,yy,1);
}
static void ed_insert(char c){ if(edlen>=(int)sizeof edbuf-1) return;
    memmove(edbuf+cur+1,edbuf+cur,edlen-cur); edbuf[cur]=c; edlen++; cur++; edbuf[edlen]=0; selPos=-1; }
static void ed_backspace(void){ if(cur<=0) return;
    memmove(edbuf+cur-1,edbuf+cur,edlen-cur); edlen--; cur--; edbuf[edlen]=0; selPos=-1; }
static void ed_up(void){ ed_layout(); int cl=ed_cursor_line(); int col=cur-ed_ls[cl];
    if(cl==0){ cur=0; return; } int pl=cl-1, plen=ed_line_cols(pl); if(col>plen) col=plen; cur=ed_ls[pl]+col; }
static void ed_down(void){ ed_layout(); int cl=ed_cursor_line(); int col=cur-ed_ls[cl];
    if(cl>=ed_nl-1){ cur=edlen; return; } int nl=cl+1, nlen=ed_line_cols(nl); if(col>nlen) col=nlen; cur=ed_ls[nl]+col; }
static void ed_home(void){ ed_layout(); cur=ed_ls[ed_cursor_line()]; }
static void ed_end(void){ ed_layout(); int cl=ed_cursor_line(); cur=ed_ls[cl]+ed_line_cols(cl); }
static void ed_page(int dir){ for(int i=0;i<EVIS-1;i++){ if(dir<0) ed_up(); else ed_down(); } }
static void ed_del_forward(void){ if(cur>=edlen) return;
    memmove(edbuf+cur,edbuf+cur+1,edlen-cur-1); edlen--; edbuf[edlen]=0; }
static int  sel_active(void){ return selPos>=0 && selLen>0; }
static void sel_clear(void){ selAnchor=-1; selPos=-1; selLen=0; }
static void sel_delete(void){ if(!sel_active()) return;
    memmove(edbuf+selPos,edbuf+selPos+selLen,edlen-selPos-selLen); edlen-=selLen; edbuf[edlen]=0;
    cur=selPos; sel_clear(); }
static void ed_copy(void){ if(!sel_active()) return; int n=selLen; if(n>(int)sizeof s_clip-1) n=sizeof s_clip-1;
    memcpy(s_clip,edbuf+selPos,n); s_clip[n]=0; s_cliplen=n; }
static void ed_cut(void){ if(!sel_active()) return; ed_copy(); sel_delete(); }
static void ed_paste(void){ if(sel_active()) sel_delete();
    for(int i=0;i<s_cliplen && edlen<(int)sizeof edbuf-1;i++){
        memmove(edbuf+cur+1,edbuf+cur,edlen-cur); edbuf[cur]=s_clip[i]; edlen++; cur++; }
    edbuf[edlen]=0; sel_clear(); }
static void sel_update_from_anchor(void){
    if(selAnchor<0){ selPos=-1; selLen=0; return; }
    int a=selAnchor, b=cur; if(a>b){ int t=a; a=b; b=t; } selPos=a; selLen=b-a; }

// ---- rendering ----------------------------------------------------------------------------------
static void browser_render(void){
    memset(fb,0,sizeof fb);
    const int rowh=11, y0=1, MAXV=7;
    int vis = listN<MAXV?listN:MAXV;
    int top = listSel - vis/2; if(top<0)top=0; if(top>listN-vis)top=listN-vis; if(top<0)top=0;
    for(int i=0;i<vis;i++){
        int idx=top+i, y=y0+i*rowh; int k=listKind[idx]; int tx=19;
        if(idx==listSel) blit(1,y+2,G_POINT,7,5);
        if(k==FK_DIR)                 blit(9,y+1,G_FOLDER,7,8);
        else if(k==FK_UP||k==FK_BACK) blit(9,y+1,G_BACK,7,7);
        else if(k==FK_FILE)           blit(10,y,G_DOC,8,6);
        else if(k==FK_MANAGE)         blit(10,y+1,s_manage?G_CHECK:G_PENCIL,7,7);
        else tx=9;
        text(tx,y,listItems[idx]);
    }
    if(top>0)         blit(W-6,0,  G_UP,  5,5);
    if(top+vis<listN) blit(W-6,H-5,G_DOWN,5,5);
    if(s_manage) rtext_small(W-8,1,"MANAGE");
}

static void batt_str(char* b,int n){ int p=s_batt_pct?s_batt_pct():-1; if(p>=0) snprintf(b,n,"%d%%",p); else b[0]=0; }
// Power-button overlay, drawn on top of any screen.
static void draw_status_overlay(void){
    char r1[40]="", batt[12]=""; batt_str(batt,sizeof batt);
    if(screen==SCR_EDITOR){
        char l1[40]; snprintf(l1,sizeof l1,"%.28s",curProject[0]?curProject:"(no project)");
        snprintf(r1,sizeof r1,"%d words",word_count(edbuf));
        char push[40]; snprintf(push,sizeof push,"Synced: %.20s",lastPush);
        statuspanel(l1,r1,push,batt);
    } else if(screen==SCR_LIST && listType==LT_FILE){
        int items=0; for(int i=0;i<listN;i++) if(listKind[i]==FK_DIR||listKind[i]==FK_FILE) items++;
        int atRoot=(strcmp(curDir,appReposRoot)==0);
        char l1[40]; snprintf(l1,sizeof l1,"%.28s",atRoot?"Projects":(curProject[0]?curProject:"Folder"));
        snprintf(r1,sizeof r1,"%d item%s",items,items==1?"":"s");
        statuspanel(l1,r1,"",batt);
    } else {
        char nm[33]=""; int en=s_wifi_is_en?s_wifi_is_en():0;
        int cn=(en&&s_wifi_conn_name)?s_wifi_conn_name(nm,sizeof nm):0;
        char l1[40];
        if(!en)     snprintf(l1,sizeof l1,"WiFi: off");
        else if(cn) snprintf(l1,sizeof l1,"WiFi: %.24s",nm);
        else        snprintf(l1,sizeof l1,"WiFi: on");
        if(s_free_mb) snprintf(r1,sizeof r1,"%ld MB free",s_free_mb());
        char kbn[33]=""; int kb=s_bt_conn_name?s_bt_conn_name(kbn,sizeof kbn):0;
        char l2[40]; if(kb) snprintf(l2,sizeof l2,"KB: %.26s",kbn); else snprintf(l2,sizeof l2,"KB: none");
        statuspanel(l1,r1,l2,batt);
    }
}
static void render_current(void){
    memset(fb,0,sizeof fb);
    if(screen==SCR_MENU){
        int n; const Item* it=items_for(curmenu,&n);
        int sel=selByMenu[curmenu&127];
        const int MAXVIS=7, PMAX=11, glyphH=7, YTOP=2;
        int vis = n>MAXVIS ? MAXVIS : n;
        int top = 0;
        if(n>MAXVIS){ top = sel>=vis ? sel-vis+1 : 0; if(top>n-vis) top=n-vis; if(top<0) top=0; }
        int fill  = vis>1 ? (H-YTOP-glyphH-YTOP)/(vis-1) : 0;
        int pitch = fill>PMAX ? PMAX : fill;
        for(int i=0;i<vis && top+i<n;i++){
            int idx=top+i, y=YTOP+i*pitch;
            if(idx==sel) blit(1,y+1,G_POINT,7,5);
            if(it[idx].ev==EV_BACK){ blit(9,y,G_BACK,7,7); text(19,y,"Back"); }
            else                     text(9,y,it[idx].label);
        }
        if(n>MAXVIS){
            if(top>0)      blit(W-6,0,   G_UP,  5,5);
            if(top+vis<n)  blit(W-6,H-5, G_DOWN,5,5);
        }
        if(curmenu!=M_WIFI) rtext_small(W-2,H-7,"OYOBYOK " OYOBYOK_VERSION);
        if(curmenu==M_WIFI){
            char st[64]; char nm[33]="";
            int en = s_wifi_is_en? s_wifi_is_en():0;
            int cn = (en && s_wifi_conn_name)? s_wifi_conn_name(nm,sizeof nm):0;
            if(!en)      snprintf(st,sizeof st,"WiFi off");
            else if(cn)  snprintf(st,sizeof st,"Connected: %.24s",nm);
            else         snprintf(st,sizeof st,"On - not connected");
            hline(0,W-1,BAND_Y); rtext(W-2,BAND_T,st);
        }
    } else if(screen==SCR_EDITOR){
        editor_render();
    } else if(screen==SCR_LIST){
        if(listN) browser_render();
        else { text(9, H/2-10, "Nothing found"); hline(0,W-1,BAND_Y); rtext(W-2,BAND_T,"< back"); }
    } else if(screen==SCR_BTSCAN){
        text(2,6,"Bluetooth");
        text(2,26,"Scanning for keyboards...");
        text(2,40,"(put your keyboard in pairing mode)");
    } else if(screen==SCR_CONTRAST){
        text(2,6,"Contrast");
        int pct=(s_contrast*100)/255;
        char l[32]; snprintf(l,sizeof l,"%d%%",pct); rtext(W-2,6,l);
        int bw=W-20, fillw=(bw*pct)/100;
        for(int yy=30;yy<42;yy++){ px(10,yy,1); px(10+bw,yy,1); }
        hline(10,10+bw,30); hline(10,10+bw,41);
        for(int yy=32;yy<40;yy++) hline(11,11+fillw,yy);
        hline(0,W-1,BAND_Y); rtext(W-2,BAND_T,"Up/Down  Enter: OK");
    } else if(screen==SCR_DISK){
        text(2,6,"Disk Mode");
        text(2,26,"Connected as a USB drive.");
        text(2,40,"To exit: power off, then on.");
        hline(0,W-1,BAND_Y); rtext(W-2,BAND_T,"eject on the computer first");
    } else if(screen==SCR_ENTRY){
        entry(entryPrompt,entryBuf,1);
    } else if(screen==SCR_ALERT){
        text(2,6,alertTitle);
        const char* nl=strchr(alertMsg,'\n');
        if(nl){ char l1[64]; snprintf(l1,sizeof l1,"%.*s",(int)(nl-alertMsg),alertMsg); text(2,22,l1); text(2,34,nl+1); }
        else text(2,26,alertMsg);
        hline(0,W-1,BAND_Y);
        if(alertBusy){ char b[40]; snprintf(b,sizeof b,"%us   Esc: cancel",alertSecs); rtext(W-2,BAND_T,b); }
        else rtext(W-2,BAND_T,"Enter: OK");
    } else if(screen==SCR_GITSTATUS){
        char l[96]; text(2,2,curProject);
        const char* rm=gRem.remote[0]?gRem.remote:"(no remote)";
        char rmshort[42]; if(strlen(rm)>39){ snprintf(rmshort,sizeof rmshort,"...%s",rm+strlen(rm)-36); rm=rmshort; }
        text(2,14,rm);
        snprintf(l,sizeof l,"branch %.24s  %d ahead  %d behind",gRem.branch,gStat.ahead,gStat.behind); text(2,26,l);
        snprintf(l,sizeof l,"%d local change%s",gStat.changed,gStat.changed==1?"":"s"); text(2,38,l);
        hline(0,W-1,BAND_Y); char b[48]; snprintf(b,sizeof b,"Synced: %s",lastPush); rtext(W-2,BAND_T,b);
    } else if(screen==SCR_NLIST){
        const char* labels[20]; for(int i=0;i<nN;i++)labels[i]=nItems[i];
        int top=nSel>4?nSel-4:0, vis=nN-top>5?5:nN-top;
        list(labels,nN,nSel,top,vis,10,2,63);
    }
    if(status_on) draw_status_overlay();
}

static void open_conflict(void){
    nN=0; snprintf(nItems[nN++],48,"Take remote (save mine)");
    snprintf(nItems[nN++],48,"Keep mine (push over)");
    nMode=NL_CONFLICT; nSel=0; screen=SCR_NLIST;
}

// ---- file manager -------------------------------------------------------------------------------
// A project is a top-level folder under appReposRoot; it may contain files and sub-folders.
static void browser_refill(void){
    listN=0; listSel=0; listType=LT_FILE;
    int atRoot = (strcmp(cur_dir(),appReposRoot)==0);
    if(atRoot){
        snprintf(listItems[listN],48,"Back");          listKind[listN]=FK_BACK;    listN++;
        snprintf(listItems[listN],48,"+ New Project"); listKind[listN]=FK_NEWPROJ; listN++;
    } else {
        snprintf(listItems[listN],48,"Back");          listKind[listN]=FK_UP;        listN++;
        snprintf(listItems[listN],48,"+ New File");    listKind[listN]=FK_NEWFILE;   listN++;
        snprintf(listItems[listN],48,"+ New Folder");  listKind[listN]=FK_NEWFOLDER; listN++;
    }
    DIR* dp=opendir(cur_dir());
    if(dp){
        for(int pass=0;pass<2;pass++){                  // folders first, then files
            rewinddir(dp); struct dirent* e;
            while((e=readdir(dp)) && listN<30){
                if(e->d_name[0]=='.') continue;
                char full[600]; snprintf(full,sizeof full,"%s/%.90s",cur_dir(),e->d_name);
                struct stat st; if(stat(full,&st)!=0) continue;
                int isdir=S_ISDIR(st.st_mode)?1:0;
                if(atRoot && !isdir) continue;
                if((pass==0)!=(isdir!=0)) continue;
                snprintf(listItems[listN],48,"%.46s",e->d_name);
                listKind[listN]=isdir?FK_DIR:FK_FILE; listN++;
            }
        }
        closedir(dp);
    }
    int hasItems=0; for(int i=0;i<listN;i++) if(listKind[i]==FK_DIR||listKind[i]==FK_FILE){ hasItems=1; break; }
    if(hasItems && listN<31){
        snprintf(listItems[listN],48,s_manage?"Done":"Manage");
        listKind[listN]=FK_MANAGE; listN++;
    } else if(!hasItems) s_manage=0;
}
static void dir_stats(const char* path,int* files,unsigned long* bytes,int depth){
    if(depth>8) return;
    DIR* dp=opendir(path); if(!dp) return; struct dirent* e;
    while((e=readdir(dp))){
        if(e->d_name[0]=='.') continue;
        char child[640]; snprintf(child,sizeof child,"%s/%.90s",path,e->d_name);
        struct stat st; if(stat(child,&st)!=0) continue;
        if(S_ISDIR(st.st_mode)) dir_stats(child,files,bytes,depth+1);
        else if(S_ISREG(st.st_mode)){ (*files)++; *bytes+=(unsigned long)st.st_size; }
    }
    closedir(dp);
}
static int rm_rf(const char* path){
    struct stat st; if(stat(path,&st)!=0) return -1;
    if(S_ISDIR(st.st_mode)){
        DIR* dp=opendir(path);
        if(dp){ struct dirent* e;
            while((e=readdir(dp))){ if(!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue;
                char child[640]; snprintf(child,sizeof child,"%s/%.90s",path,e->d_name); rm_rf(child); }
            closedir(dp); }
        return rmdir(path);
    }
    return unlink(path);
}
static void browser_open(void){
    snprintf(curDir,sizeof curDir,"%s",appReposRoot);
    curProject[0]=0;
    browser_refill(); screen=SCR_LIST;
}

// ---- Git ----------------------------------------------------------------------------------------
static void git_action(int ev){
    if(ev==EV_G_SYNC){
        if(!wifi_ready_or_alert()) return;
        LED(BLED_SYNCING);
        if(s_git_sync){
            snprintf(alertTitle,sizeof alertTitle,"Git"); snprintf(alertMsg,sizeof alertMsg,"Syncing %.20s ...",curProject);
            screen=SCR_ALERT; BUSY(300); s_sync_busy=1; s_git_sync(curProject); return;
        }
        GitResult g=git_sync(curProject);            // emulator: synchronous
        if(g.conflict){ LED(BLED_SYNC_FAIL); open_conflict(); return; }
        LED(g.ok?BLED_SYNC_OK:BLED_SYNC_FAIL);
        if(g.ok) mark_synced();
        alert("Git",g.msg);
    } else if(ev==EV_G_STATUS){
        gRem=git_lookup(curProject);
        if(s_git_status_async){
            if(!wifi_ready_or_alert()) return;
            snprintf(alertTitle,sizeof alertTitle,"Git"); snprintf(alertMsg,sizeof alertMsg,"Checking %.20s ...",curProject);
            screen=SCR_ALERT; BUSY(120); s_sync_busy=1; s_git_status_async(curProject); return;
        }
        gStat=git_status_fetch(curProject); screen=SCR_GITSTATUS;
    }
}

// ---- hook registration and async results (called by the host) -----------------------------------
void oyobyok_app_set_bt(int(*scan)(int),const char*(*name)(int),int(*conn)(int)){
    s_bt_scan=scan; s_bt_name=name; s_bt_connect=conn;
}
void oyobyok_app_set_bt_saved(int(*count)(void),const char*(*name)(int),void(*forget)(void)){
    s_bt_saved_count=count; s_bt_saved_name=name; s_bt_forget_saved=forget;
}
void oyobyok_app_set_bt_status(int(*conn_name)(char*,int)){ s_bt_conn_name=conn_name; }
void oyobyok_app_set_status_metrics(long(*free_mb)(void),int(*batt_pct)(void)){ s_free_mb=free_mb; s_batt_pct=batt_pct; }
void oyobyok_app_set_hw(void(*set_contrast)(int),void(*set_kblayout)(int)){ s_set_contrast=set_contrast; s_set_kblayout=set_kblayout; }
void oyobyok_app_set_led(void(*led)(int)){ s_led=led; }
void oyobyok_app_set_disk(int(*enter)(void)){ s_disk_enter=enter; }
void oyobyok_app_set_ssh(void(*test)(void),const char*(*result)(void)){ s_ssh_test=test; s_ssh_result=result; }
void oyobyok_app_set_sftp(int(*list)(char(*)[33],int),void(*push)(int,const char*),const char*(*result)(void),int(*progress)(void)){
    s_sftp_list=list; s_sftp_push=push; s_sftp_result=result; s_sftp_progress=progress;
}
void oyobyok_app_set_git(void(*sync)(const char*),void(*status)(const char*),int(*take)(GitResult*),const char*(*result)(void)){
    s_git_sync=sync; s_git_status_async=status; s_git_status_take=take; s_git_result=result;
}
void oyobyok_app_set_wifi(int(*scan)(char(*)[33],int),int(*known)(char(*)[33],int),
                       void(*connect)(const char*,const char*),void(*connect_known)(const char*),
                       void(*disconnect)(void),int(*is_en)(void),void(*set_en)(int),
                       int(*conn_name)(char*,int)){
    s_wifi_scan=scan; s_wifi_known=known; s_wifi_connect=connect; s_wifi_connect_known=connect_known;
    s_wifi_disconnect=disconnect; s_wifi_is_en=is_en; s_wifi_set_en=set_en; s_wifi_conn_name=conn_name;
}

void oyobyok_app_wifi_result(int ok){
    // Only the WiFi alert: the Synchronise page autoconnects in the background, so this also fires
    // while an unrelated alert is up.
    if(screen==SCR_ALERT && strcmp(alertTitle,"WiFi")==0){ alertBusy=0;
        char nm[33]=""; int have = s_wifi_conn_name? s_wifi_conn_name(nm,sizeof nm):0;
        if(ok) snprintf(alertMsg,sizeof alertMsg,"Connected%s%.24s.", have?" to ":"", have?nm:"");
        else   snprintf(alertMsg,sizeof alertMsg,"Connection failed.");
    }
}
int oyobyok_app_busy(void){ return screen==SCR_ALERT && alertBusy; }
int oyobyok_app_tick(void){
    if(!oyobyok_app_busy()) return 0;
    alertSecs++;
    if(s_sftp_progress && strncmp(alertTitle,"SFTP",4)==0){
        char* nl=strchr(alertMsg,'\n');
        if(nl) snprintf(nl+1,sizeof alertMsg-(nl+1-alertMsg),"%d/%d files  (%s)",s_sftp_progress(),s_sftpEstFiles,s_sftpEst);
    }
    if(alertLimit && alertSecs>=alertLimit){
        alertBusy=0;
        snprintf(alertMsg,sizeof alertMsg,"Timed out after %us.",alertSecs);
    }
    return 1;
}
// Runs after a frame is drawn: a requested BLE scan happens here so "Scanning..." is visible first.
int oyobyok_app_service(void){
    if(!s_bt_pending) return 0;
    s_bt_pending=0;
    int n = s_bt_scan ? s_bt_scan(4) : 0;
    if(n>31) n=31;
    listN=0; listSel=0; listType=LT_BT;
    listKind[listN]=FK_BACK;  snprintf(listItems[listN++],48,"Back");
    for(int i=0;i<n;i++){
        const char* nm = s_bt_name ? s_bt_name(i) : 0;
        listKind[listN]=FK_PLAIN; snprintf(listItems[listN++],48,"%s", (nm && *nm) ? nm : "(unnamed)");
    }
    s_bt_ndev=n;
    screen=SCR_LIST;
    return 1;
}
void oyobyok_app_bt_result(int connected){
    LED(connected ? BLED_KB_CONNECTED : BLED_KB_LOST);
    if(screen!=SCR_ALERT || strcmp(alertTitle,"Bluetooth")!=0) return;   // background reconnects fire this too
    alertBusy=0;
    snprintf(alertMsg,sizeof alertMsg,"%s", connected ? "Connected. Press Enter." : "Couldn't pair. Try again?");
}
void oyobyok_app_ssh_result(void){
    op_finished();
    if(screen!=SCR_ALERT) return;
    const char* m = s_ssh_result ? s_ssh_result() : 0;
    snprintf(alertMsg,sizeof alertMsg,"%s", (m&&*m)?m:"Done.");
}
void oyobyok_app_sftp_result(void){
    op_finished();
    if(screen!=SCR_ALERT) return;
    const char* m = s_sftp_result ? s_sftp_result() : 0;
    snprintf(alertMsg,sizeof alertMsg,"%s", (m&&*m)?m:"Done.");
}
void oyobyok_app_git_result(void){
    op_finished();
    if(screen!=SCR_ALERT) return;
    if(s_git_status_take && s_git_status_take(&gStat)){ gRem=git_lookup(curProject); screen=SCR_GITSTATUS; return; }
    const char* m = s_git_result ? s_git_result() : 0;
    int synced = m && (strncmp(m,"Synced",6)==0 || strstr(m,"Already in sync"));
    if(synced) mark_synced();
    LED(synced ? BLED_SYNC_OK : BLED_SYNC_FAIL);
    if(m && (strstr(m,"pull first")||strstr(m,"same lines"))){ open_conflict(); return; }
    alert("Git",(m&&*m)?m:"Done.");
}

// Ctrl-S and Esc in the editor: write the buffer to the SD card and return to the folder.
void oyobyok_app_save_close(void){
    if(!have_editor) return;
    if(save_current_file()){
        LED(BLED_SAVED);
        have_editor=0; edlen=0; edbuf[0]=0; cur=0; edTop=0; selPos=-1;
        browser_refill(); screen=SCR_LIST;
    } else alert("Save failed","No storage to write to (SD card?).");
}

// ---- menus and pickers --------------------------------------------------------------------------
static void open_native_list(int lt);
static void menu_back(void){
    if(curmenu==M_GIT){ open_native_list(LT_GITREPO); return; }
    if(curmenu==M_CONN){ sync_radio(0); curmenu=M_MAIN; }
    else if(curmenu==M_SETTINGS||curmenu==M_BT) curmenu=M_MAIN;
    else if(curmenu==M_WIFI) curmenu=M_CONN;
    screen=SCR_MENU;
}
static void open_native_list(int lt){
    listType=lt; listN=0; listSel=0; s_manage=0;
    snprintf(listItems[listN],48,"Back"); listKind[listN]=FK_BACK; listN++;
    if(lt==LT_WIFI_SCAN){
        snprintf(listItems[listN],48,"+ Enter network name"); listKind[listN]=FK_NEWFILE; listN++;
    }
    if(lt==LT_WIFI_SCAN || lt==LT_WIFI_KNOWN){
        static char aps[32][33];
        int cnt = (lt==LT_WIFI_SCAN) ? (s_wifi_scan ? s_wifi_scan(aps,32):0)
                                     : (s_wifi_known? s_wifi_known(aps,32):0);
        for(int i=0;i<cnt && listN<30;i++){ snprintf(listItems[listN],48,"%.47s",aps[i]); listKind[listN]=FK_PLAIN; listN++; }
    } else if(lt==LT_KB){
        static const char* kbs[]={"QWERTY","QWERTZ","AZERTY","Dvorak"};
        for(int i=0;i<4;i++){ snprintf(listItems[listN],48,"%s",kbs[i]); listKind[listN]=FK_PLAIN; listN++; }
        if(s_kbLayout>=0&&s_kbLayout<4) listSel=1+s_kbLayout;
    } else if(lt==LT_CURSOR){
        static const char* curs[]={"Vertical Cursor","Horizontal Cursor"};
        for(int i=0;i<2;i++){ snprintf(listItems[listN],48,"%s",curs[i]); listKind[listN]=FK_PLAIN; listN++; }
        listSel=1+(s_cursorHoriz?1:0);
    } else if(lt==LT_SFTP){
        static char srv[16][33];
        int cnt = s_sftp_list ? s_sftp_list(srv,16) : 0;
        for(int i=0;i<cnt && listN<30;i++){ snprintf(listItems[listN],48,"%.47s",srv[i]); listKind[listN]=FK_PLAIN; listN++; }
        if(cnt==0){ snprintf(listItems[listN],48,"(add sftp.conf via Disk Mode)"); listKind[listN]=FK_PLAIN; listN++; }
    } else if(lt==LT_SFTPPROJ){
        DIR* dp=opendir(appReposRoot); struct dirent* e;
        if(dp){ while((e=readdir(dp)) && listN<30){
            if(e->d_name[0]=='.') continue;
            char pth[400]; snprintf(pth,sizeof pth,"%s/%.64s",appReposRoot,e->d_name);
            struct stat st; if(stat(pth,&st)!=0||!S_ISDIR(st.st_mode)) continue;
            snprintf(listItems[listN],48,"%.47s",e->d_name); listKind[listN]=FK_DIR; listN++; }
          closedir(dp); }
        if(listN==1){ snprintf(listItems[listN],48,"(no projects yet)"); listKind[listN]=FK_PLAIN; listN++; }
        for(int i=1;i<listN;i++) if(curProject[0]&&strcmp(listItems[i],curProject)==0){ listSel=i; break; }
    } else if(lt==LT_GITREPO){
        static char repos[24][64];
        int cnt = git_list_repos(repos,24);
        for(int i=0;i<cnt && listN<30;i++){ snprintf(listItems[listN],48,"%.47s",repos[i]); listKind[listN]=FK_DIR; listN++; }
        if(cnt==0){ snprintf(listItems[listN],48,"(add remotes.conf via Disk Mode)"); listKind[listN]=FK_PLAIN; listN++; }
    }
    screen=SCR_LIST;
}
static void not_available(const char* what){ alert(what,"Not available here."); }
static void menu_select(int ev){
    switch(ev){
        case EV_SYNC:     curmenu=M_CONN;     selByMenu[M_CONN&127]=0;     screen=SCR_MENU; sync_radio(1); break;
        case EV_BT:       curmenu=M_BT;       selByMenu[M_BT&127]=0;       screen=SCR_MENU; break;
        case EV_WIFI:     curmenu=M_WIFI;     selByMenu[M_WIFI&127]=0;     screen=SCR_MENU; break;
        case EV_SETTINGS: curmenu=M_SETTINGS; selByMenu[M_SETTINGS&127]=0; screen=SCR_MENU; break;
        case EV_SSH_TEST:
            if(!s_ssh_test){ not_available("SSH Test"); break; }
            alert("SSH Test","Connecting to GitHub..."); BUSY(90); s_sync_busy=1; s_ssh_test();
            break;
        case EV_SFTP: open_native_list(LT_SFTP); break;
        case EV_DISK:
            if(!s_disk_enter){ not_available("Disk Mode"); break; }
            LED(BLED_DISK_MODE);
            if(s_disk_enter()==0) screen=SCR_DISK;
            else alert("Disk Mode","Could not start (no SD card?).");
            break;
        case EV_WIFI_SCAN:  open_native_list(LT_WIFI_SCAN);  break;
        case EV_WIFI_KNOWN: open_native_list(LT_WIFI_KNOWN); break;
        case EV_WIFI_TOGGLE: { int en=s_wifi_is_en?s_wifi_is_en():0; if(s_wifi_set_en) s_wifi_set_en(!en);
                    snprintf(alertTitle,sizeof alertTitle,"WiFi");
                    snprintf(alertMsg,sizeof alertMsg,"WiFi turned %s.",!en?"on":"off"); screen=SCR_ALERT; break; }
        case EV_WIFI_DISCONNECT: if(s_wifi_disconnect) s_wifi_disconnect(); alert("WiFi","Disconnected."); break;
        case EV_CONTRAST: if(s_set_contrast) s_set_contrast(s_contrast); screen=SCR_CONTRAST; break;
        case EV_KEYBOARD: open_native_list(LT_KB);     break;
        case EV_CURSOR:   open_native_list(LT_CURSOR); break;
        default: break;
    }
}

// ---- input --------------------------------------------------------------------------------------
// In Disk Mode keys are swallowed: the device stays a USB drive until it is power-cycled.
static void app_enter(void){
    if(screen==SCR_DISK) return;
    if(screen==SCR_MENU){
        int n; const Item* it=items_for(curmenu,&n); if(!n)return;
        int ev=it[selByMenu[curmenu&127]].ev;
        if(ev==EV_BACK){ menu_back(); return; }
        if(ev==EV_PROJECTS){ browser_open(); return; }
        if(curmenu==M_GIT){ git_action(ev); return; }
        if(ev==EV_GIT){ open_native_list(LT_GITREPO); return; }
        if(ev==EV_BT_PAIR){ if(s_bt_scan){ s_bt_pending=1; screen=SCR_BTSCAN; } else not_available("Bluetooth"); return; }
        if(ev==EV_BT_FORGET){
            int have = s_bt_saved_count ? s_bt_saved_count() : 0;
            if(have>0 && s_bt_forget_saved){
                const char* nm = s_bt_saved_name ? s_bt_saved_name(0) : 0;
                char msg[80]; snprintf(msg,sizeof msg,"Forgot %.40s. It won't reconnect.",(nm&&*nm)?nm:"keyboard");
                s_bt_forget_saved();
                alert("Bluetooth",msg);
            } else alert("Bluetooth","No keyboard paired.");
            return;
        }
        menu_select(ev);
    } else if(screen==SCR_LIST){
        if(listType==LT_BT){
            if(listSel==0 || listSel>s_bt_ndev){ listType=0; screen=SCR_MENU; return; }
            if(s_bt_connect) s_bt_connect(listSel-1);
            snprintf(alertTitle,sizeof alertTitle,"Bluetooth");
            snprintf(alertMsg,sizeof alertMsg,"Connecting to %.32s ...", listItems[listSel]);
            listType=0; screen=SCR_ALERT; BUSY(60);
        } else if(listType==LT_FILE && listSel<listN){
            int k=listKind[listSel];
            if(k==FK_MANAGE){ s_manage=!s_manage; browser_refill(); return; }
            if(s_manage && (k==FK_DIR||k==FK_FILE)){
                snprintf(mgTarget,sizeof mgTarget,"%.63s",listItems[listSel]); mgIsDir=(k==FK_DIR);
                nN=0; snprintf(nItems[nN++],48,"Rename"); snprintf(nItems[nN++],48,"Delete"); snprintf(nItems[nN++],48,"Cancel");
                nMode=NL_MANAGE; nSel=0; screen=SCR_NLIST; return;
            }
            if(s_manage && (k==FK_BACK||k==FK_UP)){ s_manage=0; browser_refill(); return; }
            if(k==FK_BACK){ listType=0; curmenu=M_MAIN; screen=SCR_MENU; }
            else if(k==FK_UP){
                char* s=strrchr(curDir,'/'); if(s) *s=0;
                if(strcmp(curDir,appReposRoot)==0) curProject[0]=0;
                browser_refill();
            }
            else if(k==FK_DIR){
                int wasRoot=(strcmp(curDir,appReposRoot)==0);
                size_t n=strlen(curDir); snprintf(curDir+n,sizeof curDir-n,"/%.63s",listItems[listSel]);
                if(wasRoot) snprintf(curProject,sizeof curProject,"%.63s",listItems[listSel]);
                browser_refill();
            }
            else if(k==FK_FILE){
                snprintf(curFile,sizeof curFile,"%.63s",listItems[listSel]);
                load_current_file(); cur=0; edTop=0; selPos=-1; have_editor=1; screen=SCR_EDITOR;
            }
            else if(k==FK_NEWFILE){ entryCtxFS=1; snprintf(entryPrompt,sizeof entryPrompt,"New file name:"); entryBuf[0]=0; screen=SCR_ENTRY; }
            else if(k==FK_NEWFOLDER){ entryCtxFS=2; snprintf(entryPrompt,sizeof entryPrompt,"New folder name:"); entryBuf[0]=0; screen=SCR_ENTRY; }
            else if(k==FK_NEWPROJ){ entryCtxFS=3; snprintf(entryPrompt,sizeof entryPrompt,"New project name:"); entryBuf[0]=0; screen=SCR_ENTRY; }
        } else if(listType==LT_WIFI_SCAN){
            if(listSel==0){ curmenu=M_WIFI; screen=SCR_MENU; return; }
            if(listSel==1){
                entryCtxWifiName=1; snprintf(entryPrompt,sizeof entryPrompt,"Network name (SSID):");
                entryBuf[0]=0; screen=SCR_ENTRY; return;
            }
            snprintf(wifiSSID,sizeof wifiSSID,"%.32s",listItems[listSel]);
            entryCtxWifi=1; snprintf(entryPrompt,sizeof entryPrompt,"Password for %.18s:",wifiSSID);
            entryBuf[0]=0; screen=SCR_ENTRY;
        } else if(listType==LT_WIFI_KNOWN){
            if(listSel==0){ curmenu=M_WIFI; screen=SCR_MENU; return; }
            if(s_wifi_connect_known) s_wifi_connect_known(listItems[listSel]);
            snprintf(alertTitle,sizeof alertTitle,"WiFi");
            snprintf(alertMsg,sizeof alertMsg,"Connecting to %.24s ...",listItems[listSel]); screen=SCR_ALERT; BUSY(45);
        } else if(listType==LT_KB){
            if(listSel>0){ int idx=listSel-1; if(idx<4){ s_kbLayout=idx; if(s_set_kblayout) s_set_kblayout(idx); prefs_save(); } }
            curmenu=M_SETTINGS; screen=SCR_MENU;
        } else if(listType==LT_CURSOR){
            if(listSel>0){ s_cursorHoriz=(listSel-1)?1:0; prefs_save(); }
            curmenu=M_SETTINGS; screen=SCR_MENU;
        } else if(listType==LT_SFTP){
            if(listSel==0 || !s_sftp_push || listKind[listSel]!=FK_PLAIN){ listType=0; curmenu=M_CONN; screen=SCR_MENU; return; }
            s_sftpSrv=listSel-1; snprintf(s_sftpSrvName,sizeof s_sftpSrvName,"%.32s",listItems[listSel]);
            open_native_list(LT_SFTPPROJ);
        } else if(listType==LT_SFTPPROJ){
            if(listSel==0 || listKind[listSel]!=FK_DIR){ open_native_list(LT_SFTP); return; }
            char proj[48]; snprintf(proj,sizeof proj,"%s",listItems[listSel]);
            char pth[320]; snprintf(pth,sizeof pth,"%s/%s",appReposRoot,proj);
            int files=0; unsigned long bytes=0; dir_stats(pth,&files,&bytes,0);
            // Measured: about 1.4 s per file of SFTP round trips plus roughly 40 KB/s.
            unsigned est_s=(unsigned)(files*1.4 + bytes/40000.0 + 2);
            char sz[16]; if(bytes<1024) snprintf(sz,sizeof sz,"%lu B",bytes);
                         else if(bytes<1024UL*1024) snprintf(sz,sizeof sz,"%lu KB",(bytes+512)/1024);
                         else snprintf(sz,sizeof sz,"%.1f MB",bytes/1048576.0);
            char tm[16]; if(est_s<90) snprintf(tm,sizeof tm,"~%u s",est_s); else snprintf(tm,sizeof tm,"~%u min",(est_s+30)/60);
            s_sftpEstFiles=files; snprintf(s_sftpEst,sizeof s_sftpEst,"est %s, %s",sz,tm);
            snprintf(alertTitle,sizeof alertTitle,"SFTP: %.20s",s_sftpSrvName);
            snprintf(alertMsg,sizeof alertMsg,"Pushing %.24s ...\n%s",proj,s_sftpEst);
            listType=0; screen=SCR_ALERT; BUSY(600); s_sync_busy=1; s_sftp_push(s_sftpSrv,proj);
        } else if(listType==LT_GITREPO){
            if(listSel==0 || listKind[listSel]!=FK_DIR){ listType=0; curmenu=M_CONN; screen=SCR_MENU; return; }
            snprintf(curProject,sizeof curProject,"%.63s",listItems[listSel]);
            listType=0; curmenu=M_GIT; selByMenu[M_GIT&127]=0; screen=SCR_MENU;
        }
    } else if(screen==SCR_ENTRY){
        char nm[64]; { char* s=entryBuf; while(*s==' ')s++; snprintf(nm,sizeof nm,"%.63s",s);
                      int e=(int)strlen(nm); while(e>0&&nm[e-1]==' ')nm[--e]=0; }
        if(entryCtxFS){
            int ctx=entryCtxFS; entryCtxFS=0;
            if(nm[0]==0){ browser_refill(); screen=SCR_LIST; }
            else if(name_reserved(nm)){ char m[64]; snprintf(m,sizeof m,"'%.24s' is reserved.",nm); alert("Reserved name",m); }
            else if(ctx==1){
                if(!strchr(nm,'.')){ size_t l=strlen(nm); snprintf(nm+l,sizeof nm-l,".txt"); }
                snprintf(curFile,sizeof curFile,"%.63s",nm);
                edbuf[0]=0; edlen=0; cur=0; edTop=0; selPos=-1; have_editor=1;
                save_current_file();
                screen=SCR_EDITOR;
            } else {
                char p[480];
                if(ctx==2){ snprintf(p,sizeof p,"%.400s/%.63s",cur_dir(),nm); mkdir(appReposRoot,0777); mkdir(cur_dir(),0777); }
                else      { snprintf(p,sizeof p,"%.400s/%.63s",appReposRoot,nm); mkdir(appReposRoot,0777); }
                mkdir(p,0777);
                browser_refill(); screen=SCR_LIST;
            }
        }
        else if(entryCtxRename){ entryCtxRename=0;
            if(nm[0] && name_reserved(nm)){ char m[64]; snprintf(m,sizeof m,"'%.24s' is reserved.",nm); alert("Reserved name",m); }
            else if(nm[0] && strcmp(nm,mgTarget)!=0){
                char op[600],np[600]; snprintf(op,sizeof op,"%s/%.63s",cur_dir(),mgTarget);
                snprintf(np,sizeof np,"%s/%.63s",cur_dir(),nm);
                struct stat st;
                if(stat(np,&st)==0){ char m[64]; snprintf(m,sizeof m,"'%.24s' already exists.",nm); alert("Rename",m); }
                else { rename(op,np); browser_refill(); screen=SCR_LIST; }
            }
            else { browser_refill(); screen=SCR_LIST; } }
        else if(entryCtxWifiName){ entryCtxWifiName=0;
            snprintf(wifiSSID,sizeof wifiSSID,"%.32s",nm);
            if(wifiSSID[0]==0){ curmenu=M_WIFI; screen=SCR_MENU; }
            else { entryCtxWifi=1; snprintf(entryPrompt,sizeof entryPrompt,"Password for %.18s:",wifiSSID);
                   entryBuf[0]=0; screen=SCR_ENTRY; } }
        else if(entryCtxWifi){ entryCtxWifi=0;
            if(s_wifi_connect) s_wifi_connect(wifiSSID, entryBuf);
            snprintf(alertTitle,sizeof alertTitle,"WiFi");
            snprintf(alertMsg,sizeof alertMsg,"Connecting to %.24s ...",wifiSSID); screen=SCR_ALERT; BUSY(45); }
    } else if(screen==SCR_NLIST){
        if(nMode==NL_MANAGE){
            if(nSel==0){ entryCtxRename=1; snprintf(entryPrompt,sizeof entryPrompt,"Rename to:");
                         snprintf(entryBuf,sizeof entryBuf,"%.63s",mgTarget); screen=SCR_ENTRY; return; }
            if(nSel==1){ nN=0; snprintf(nItems[nN++],48,mgIsDir?"Delete folder + contents":"Delete file");
                         snprintf(nItems[nN++],48,"Cancel"); nMode=NL_DELCONFIRM; nSel=1; return; }
            browser_refill(); screen=SCR_LIST; return;
        }
        if(nMode==NL_DELCONFIRM){
            if(nSel==0){ char p[600]; snprintf(p,sizeof p,"%s/%.63s",cur_dir(),mgTarget); if(mgIsDir) rm_rf(p); else unlink(p); }
            browser_refill(); screen=SCR_LIST; return;
        }
        GitResult g=(nSel==0)?git_take_remote(curProject):git_keep_mine(curProject);
        alert("Git",g.msg);
    } else if(screen==SCR_ALERT){ if(!alertBusy) screen=SCR_MENU; }
    else if(screen==SCR_GITSTATUS){ screen=SCR_MENU; }
}
static void app_esc(void){
    if(screen==SCR_DISK) return;
    if(screen==SCR_EDITOR){ oyobyok_app_save_close(); }
    else if(screen==SCR_LIST){
        if(listType==LT_FILE){
            if(strcmp(curDir,appReposRoot)==0){ listType=0; curmenu=M_MAIN; screen=SCR_MENU; }
            else { char* s=strrchr(curDir,'/'); if(s)*s=0;
                   if(strcmp(curDir,appReposRoot)==0) curProject[0]=0;
                   browser_refill(); }
        } else {
            if(listType==LT_SFTPPROJ){ open_native_list(LT_SFTP); return; }
            if(listType==LT_WIFI_SCAN||listType==LT_WIFI_KNOWN) curmenu=M_WIFI;
            else if(listType==LT_KB||listType==LT_CURSOR)       curmenu=M_SETTINGS;
            else if(listType==LT_SFTP||listType==LT_GITREPO)    curmenu=M_CONN;
            else                                               curmenu=M_BT;
            listType=0; screen=SCR_MENU;
        }
    }
    else if(screen==SCR_ENTRY){
        if(entryCtxFS){ entryCtxFS=0; browser_refill(); screen=SCR_LIST; }
        else if(entryCtxRename){ entryCtxRename=0; browser_refill(); screen=SCR_LIST; }
        else if(entryCtxWifi||entryCtxWifiName){ entryCtxWifi=0; entryCtxWifiName=0; curmenu=M_WIFI; screen=SCR_MENU; }
        else { screen=SCR_MENU; }
    }
    else if(screen==SCR_ALERT){ alertBusy=0; screen=SCR_MENU; }
    else if(screen==SCR_GITSTATUS){ screen=SCR_MENU; }
    else if(screen==SCR_NLIST){ if(nMode==NL_CONFLICT){ curmenu=M_GIT; screen=SCR_MENU; } else { browser_refill(); screen=SCR_LIST; } }
    else if(screen==SCR_CONTRAST){ prefs_save(); curmenu=M_SETTINGS; screen=SCR_MENU; }
    else if(screen==SCR_BTSCAN){ curmenu=M_BT; screen=SCR_MENU; }
    else if(screen==SCR_MENU){ menu_back(); }
}
static void app_move(int d){
    if(screen==SCR_MENU){ int n; items_for(curmenu,&n); if(!n)return; int* s=&selByMenu[curmenu&127]; *s=(*s+d+n)%n; }
    else if(screen==SCR_LIST&&listN){ listSel=(listSel+d+listN)%listN; }
    else if(screen==SCR_NLIST&&nN){ nSel=(nSel+d+nN)%nN; }
}
static void app_power(void){ if(screen!=SCR_DISK) status_on=!status_on; }
static int  __attribute__((unused)) app_in_editor(void){ return screen==SCR_EDITOR; }
static void app_type(const char* t){
    if(screen==SCR_EDITOR){ if(sel_active()) sel_delete(); for(;*t;t++) ed_insert(*t); }
    else if(screen==SCR_ENTRY){ int l=strlen(entryBuf); for(;*t&&l<(int)sizeof entryBuf-1;t++)entryBuf[l++]=*t; entryBuf[l]=0; }
}
static void app_ctrl(int code){
    if(screen==SCR_EDITOR){
        if(code==0x03) ed_copy();
        else if(code==0x18) ed_cut();
        else if(code==0x16) ed_paste();
    }
}
static void app_nav(int nav,int shift){
    if(screen==SCR_DISK) return;
    if(screen==SCR_EDITOR){
        int isMove = (nav==OYOBYOK_NAV_LEFT||nav==OYOBYOK_NAV_RIGHT||nav==OYOBYOK_NAV_UP||nav==OYOBYOK_NAV_DOWN
                      ||nav==OYOBYOK_NAV_HOME||nav==OYOBYOK_NAV_END||nav==OYOBYOK_NAV_PGUP||nav==OYOBYOK_NAV_PGDN);
        if(isMove){
            if(shift){ if(selAnchor<0) selAnchor=cur; } else sel_clear();
            switch(nav){
                case OYOBYOK_NAV_LEFT:  if(cur>0) cur--; break;
                case OYOBYOK_NAV_RIGHT: if(cur<edlen) cur++; break;
                case OYOBYOK_NAV_UP:    ed_up(); break;
                case OYOBYOK_NAV_DOWN:  ed_down(); break;
                case OYOBYOK_NAV_HOME:  ed_home(); break;
                case OYOBYOK_NAV_END:   ed_end(); break;
                case OYOBYOK_NAV_PGUP:  ed_page(-1); break;
                case OYOBYOK_NAV_PGDN:  ed_page(1); break;
            }
            if(shift) sel_update_from_anchor();
            return;
        }
        switch(nav){
            case OYOBYOK_NAV_ENTER: if(sel_active()) sel_delete(); ed_insert('\n'); break;
            case OYOBYOK_NAV_BKSP:  if(sel_active()) sel_delete(); else ed_backspace(); break;
            case OYOBYOK_NAV_DEL:   if(sel_active()) sel_delete(); else ed_del_forward(); break;
        }
        return;
    }
    if(screen==SCR_CONTRAST){
        switch(nav){
            case OYOBYOK_NAV_UP:   s_contrast=(s_contrast<=255-8)?s_contrast+8:255; if(s_set_contrast)s_set_contrast(s_contrast); break;
            case OYOBYOK_NAV_DOWN: s_contrast=(s_contrast>=8)?s_contrast-8:0;       if(s_set_contrast)s_set_contrast(s_contrast); break;
            case OYOBYOK_NAV_ENTER: case OYOBYOK_NAV_RIGHT: case OYOBYOK_NAV_LEFT: case OYOBYOK_NAV_BKSP:
                prefs_save(); curmenu=M_SETTINGS; screen=SCR_MENU; break;
        }
        return;
    }
    // Text entry: Backspace deletes, Enter commits, Up cancels (the device's buttons have no Esc).
    if(screen==SCR_ENTRY){
        switch(nav){
            case OYOBYOK_NAV_BKSP: case OYOBYOK_NAV_LEFT: { int l=strlen(entryBuf); if(l>0) entryBuf[l-1]=0; break; }
            case OYOBYOK_NAV_ENTER: case OYOBYOK_NAV_RIGHT: app_enter(); break;
            case OYOBYOK_NAV_UP:    app_esc(); break;
            default: break;
        }
        return;
    }
    // On screens with nothing to scroll, Up doubles as Escape so the five buttons can always get out.
    int scrolls = (screen==SCR_MENU || screen==SCR_LIST || screen==SCR_NLIST);
    switch(nav){
        case OYOBYOK_NAV_UP:    if(scrolls) app_move(-1); else app_esc(); break;
        case OYOBYOK_NAV_DOWN:  app_move(1);  break;
        case OYOBYOK_NAV_ENTER: case OYOBYOK_NAV_RIGHT: app_enter(); break;
        case OYOBYOK_NAV_LEFT:  case OYOBYOK_NAV_BKSP:  app_esc();   break;
        default: break;
    }
}

static int app_init(const char* repos_root,const char* cfg_root){
    snprintf(appReposRoot,sizeof appReposRoot,"%s",repos_root);
    prefs_load();
    if(s_set_kblayout) s_set_kblayout(s_kbLayout);
    git_configure(cfg_root,repos_root);
    curmenu=M_MAIN; selByMenu[M_MAIN&127]=0; screen=SCR_MENU;
    return 0;
}
