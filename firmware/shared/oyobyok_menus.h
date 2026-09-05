// Menu model shared by the device host and the emulator.
#ifndef OYOBYOK_MENUS_H
#define OYOBYOK_MENUS_H

enum { M_MAIN=0, M_CONN=2, M_BT=3, M_WIFI=4, M_SETTINGS=7, M_GIT=90 };

enum {
    EV_BACK=201, EV_PROJECTS=250, EV_SYNC=105, EV_BT=101, EV_DISK=121, EV_SETTINGS=104,
    EV_GIT=200, EV_WIFI=102, EV_SFTP=131, EV_SSH_TEST=130,
    EV_BT_PAIR=109, EV_BT_FORGET=110,
    EV_WIFI_SCAN=112, EV_WIFI_KNOWN=111, EV_WIFI_TOGGLE=113, EV_WIFI_DISCONNECT=114,
    EV_CONTRAST=123, EV_KEYBOARD=125, EV_CURSOR=126,
    EV_G_SYNC=215, EV_G_STATUS=212,
};

typedef struct { const char* label; int ev; } Item;

// Every submenu starts with Back: the device's five buttons have no Escape, so Back must be a row.
// Entering Synchronise brings WiFi up; leaving it takes WiFi down again (see oyobyok_app.inc).
static const Item MAIN[]  ={{"Projects",EV_PROJECTS},{"Synchronise",EV_SYNC},{"Bluetooth",EV_BT},{"Disk Mode",EV_DISK},{"Settings",EV_SETTINGS}};
static const Item CONN[]  ={{"Back",EV_BACK},{"Git",EV_GIT},{"WiFi",EV_WIFI},{"SFTP",EV_SFTP},{"SSH Test",EV_SSH_TEST}};
static const Item BTM[]   ={{"Back",EV_BACK},{"Connect a keyboard",EV_BT_PAIR},{"Forget Keyboard",EV_BT_FORGET}};
static const Item WIFIM[] ={{"Back",EV_BACK},{"Scan & Connect",EV_WIFI_SCAN},{"Saved Networks",EV_WIFI_KNOWN},{"Turn WiFi On/Off",EV_WIFI_TOGGLE},{"Disconnect",EV_WIFI_DISCONNECT}};
static const Item SETT[]  ={{"Back",EV_BACK},{"Contrast",EV_CONTRAST},{"Keyboard",EV_KEYBOARD},{"Cursor Type",EV_CURSOR}};
static const Item GITM[]  ={{"Back",EV_BACK},{"Sync",EV_G_SYNC},{"Status",EV_G_STATUS}};

static inline const Item* oyobyok_items_for(int id,int* n){
    switch(id){
        case M_MAIN:     *n=5; return MAIN;
        case M_CONN:     *n=5; return CONN;
        case M_BT:       *n=3; return BTM;
        case M_WIFI:     *n=5; return WIFIM;
        case M_SETTINGS: *n=4; return SETT;
        case M_GIT:      *n=3; return GITM;
        default:         *n=0; return 0;
    }
}
#endif
