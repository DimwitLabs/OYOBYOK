// Desktop emulator: the shared app core with keys from stdin and frames written to emu_frames/.
// One token per line: up down left right enter bksp del home end pgup pgdn esc power copy cut paste
// s<move> for a shifted move (sleft, sright, sup, sdown, shome, send), "t <text>" to type,
// "splash <n>" to draw the boot splash with quote n, "tick" to advance a busy alert by one second,
// and "snap <path>" to save the current frame as <path>.png.
// With OYOBYOK_EMU_DEMO=1 the SFTP, Git and Disk Mode hooks are stand-ins that never finish, for screenshots.
#include "render.h"
#include "git_engine.h"
#include "oyobyok_menus.h"

#define EMU_CFG   "emu_git/cfg"
#define EMU_REPOS "emu_git/repos"

#include "oyobyok_app.inc"

static int  demo_sftp_list(char names[][33],int max){ (void)max; snprintf(names[0],33,"Laptop"); snprintf(names[1],33,"Study Pi"); return 2; }
static int  demo_files=0;
static void demo_sftp_push(int i,const char* p){ (void)i;(void)p; demo_files=0; }
static int  demo_sftp_progress(void){ if(demo_files<42) demo_files+=3; return demo_files; }
static const char* demo_result(void){ return ""; }
static void demo_git_sync(const char* p){ (void)p; }
static int  demo_disk_enter(void){ return 0; }
static void demo_git_status(const char* p){ (void)p; }
static int  demo_git_take(GitResult* g){ (void)g; return 0; }

int main(void){
    system("mkdir -p emu_frames " EMU_REPOS " " EMU_CFG);
    if(app_init(EMU_REPOS,EMU_CFG)!=0) return 1;
    if(getenv("OYOBYOK_EMU_DEMO")){
        oyobyok_app_set_sftp(demo_sftp_list,demo_sftp_push,demo_result,demo_sftp_progress);
        oyobyok_app_set_git(demo_git_sync,demo_git_status,demo_git_take,demo_result);
        oyobyok_app_set_disk(demo_disk_enter);
    }
    char line[512]; int frame=0; char png[64];
    render_current(); snprintf(png,sizeof png,"emu_frames/%03d",frame++); export_png(png);
    while(fgets(line,sizeof line,stdin)){
        char* nl=strchr(line,'\n'); if(nl)*nl=0; if(!line[0])continue;
        if(!strcmp(line,"up"))app_nav(OYOBYOK_NAV_UP,0);
        else if(!strcmp(line,"down"))app_nav(OYOBYOK_NAV_DOWN,0);
        else if(!strcmp(line,"left"))app_nav(OYOBYOK_NAV_LEFT,0);
        else if(!strcmp(line,"right"))app_nav(OYOBYOK_NAV_RIGHT,0);
        else if(!strcmp(line,"enter"))app_nav(OYOBYOK_NAV_ENTER,0);
        else if(!strcmp(line,"bksp"))app_nav(OYOBYOK_NAV_BKSP,0);
        else if(!strcmp(line,"del"))app_nav(OYOBYOK_NAV_DEL,0);
        else if(!strcmp(line,"home"))app_nav(OYOBYOK_NAV_HOME,0);
        else if(!strcmp(line,"end"))app_nav(OYOBYOK_NAV_END,0);
        else if(!strcmp(line,"pgup"))app_nav(OYOBYOK_NAV_PGUP,0);
        else if(!strcmp(line,"pgdn"))app_nav(OYOBYOK_NAV_PGDN,0);
        else if(!strcmp(line,"sleft"))app_nav(OYOBYOK_NAV_LEFT,1);
        else if(!strcmp(line,"sright"))app_nav(OYOBYOK_NAV_RIGHT,1);
        else if(!strcmp(line,"sup"))app_nav(OYOBYOK_NAV_UP,1);
        else if(!strcmp(line,"sdown"))app_nav(OYOBYOK_NAV_DOWN,1);
        else if(!strcmp(line,"shome"))app_nav(OYOBYOK_NAV_HOME,1);
        else if(!strcmp(line,"send"))app_nav(OYOBYOK_NAV_END,1);
        else if(!strcmp(line,"copy"))app_ctrl(0x03);
        else if(!strcmp(line,"cut"))app_ctrl(0x18);
        else if(!strcmp(line,"paste"))app_ctrl(0x16);
        else if(!strcmp(line,"esc"))app_esc();
        else if(!strcmp(line,"power"))app_power();
        else if(!strncmp(line,"t ",2))app_type(line+2);
        else if(!strncmp(line,"snap ",5)){ export_png(line+5); continue; }
        else if(!strcmp(line,"tick"))oyobyok_app_tick();
        printf("\n== step %d: [%s] screen=%d menu=%d sel=%d ==\n",frame,line,screen,curmenu,selByMenu[curmenu&127]);
        if(!strncmp(line,"splash",6)) app_draw_splash((unsigned)atoi(line+6)); else render_current();
        fb_to_ascii();
        snprintf(png,sizeof png,"emu_frames/%03d",frame++); export_png(png);
    }
    return 0;
}
