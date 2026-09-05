#include "oyobyok_ssh.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "libssh2_sftp.h"

static const char* TAG = "oyobyok_ssh";

static char s_cfg_dir[192] = "/sdcard/git";

struct oyobyok_ssh_session {
    int              sock;
    LIBSSH2_SESSION* ssh;
};

void oyobyok_ssh_set_config_dir(const char* dir){ if(dir&&*dir) snprintf(s_cfg_dir,sizeof s_cfg_dir,"%s",dir); }

const char* oyobyok_ssh_strerror(oyobyok_ssh_err_t e){
    switch(e){
        case OYOBYOK_SSH_OK:          return "ok";
        case OYOBYOK_SSH_E_NOKEY:     return "SSH key not found on SD card";
        case OYOBYOK_SSH_E_NONET:     return "network not available";
        case OYOBYOK_SSH_E_DNS:       return "could not resolve host";
        case OYOBYOK_SSH_E_CONNECT:   return "could not connect to server";
        case OYOBYOK_SSH_E_HANDSHAKE: return "SSH handshake failed";
        case OYOBYOK_SSH_E_HOSTKEY:   return "host key mismatch, refused";
        case OYOBYOK_SSH_E_AUTH:      return "key rejected by server (is the .pub added?)";
        case OYOBYOK_SSH_E_MEM:       return "out of memory";
        default:                      return "internal SSH error";
    }
}

bool oyobyok_ssh_have_key(const char* privkey_path){
    if(!privkey_path||!*privkey_path) return false;
    FILE* f=fopen(privkey_path,"rb"); if(!f) return false;
    fseek(f,0,SEEK_END); long n=ftell(f); fclose(f);
    return n>0;
}

static bool s_lib_inited=false;
static oyobyok_ssh_err_t ensure_lib(void){
    if(!s_lib_inited){ if(libssh2_init(0)) return OYOBYOK_SSH_E_INTERNAL; s_lib_inited=true; }
    return OYOBYOK_SSH_OK;
}
void oyobyok_ssh_init(void){
    if(ensure_lib()==OYOBYOK_SSH_OK) ESP_LOGI(TAG,"libssh2 %s ready",libssh2_version(0));
    else ESP_LOGE(TAG,"libssh2_init failed");
}

static int tcp_connect(const char* host,int port,oyobyok_ssh_err_t* why){
    char portstr[16]; snprintf(portstr,sizeof portstr,"%d",port>0?port:22);
    struct addrinfo hints; memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    struct addrinfo* res=NULL;
    if(getaddrinfo(host,portstr,&hints,&res)!=0 || !res){ *why=OYOBYOK_SSH_E_DNS; return -1; }
    int sock=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
    if(sock<0){ freeaddrinfo(res); *why=OYOBYOK_SSH_E_CONNECT; return -1; }
    struct timeval tv={ .tv_sec=15, .tv_usec=0 };
    setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
    setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
    int rc=connect(sock,res->ai_addr,res->ai_addrlen);
    freeaddrinfo(res);
    if(rc!=0){ close(sock); *why=OYOBYOK_SSH_E_CONNECT; return -1; }
    return sock;
}

static oyobyok_ssh_err_t verify_hostkey(LIBSSH2_SESSION* ssh,const char* host,int port){
    char path[256]; snprintf(path,sizeof path,"%s/known_hosts",s_cfg_dir);
    size_t klen=0; int ktype=0;
    const char* key=libssh2_session_hostkey(ssh,&klen,&ktype);
    if(!key) return OYOBYOK_SSH_E_HANDSHAKE;
    int mask=LIBSSH2_KNOWNHOST_TYPE_PLAIN|LIBSSH2_KNOWNHOST_KEYENC_RAW;
    switch(ktype){
        case LIBSSH2_HOSTKEY_TYPE_RSA:       mask|=LIBSSH2_KNOWNHOST_KEY_SSHRSA; break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: mask|=LIBSSH2_KNOWNHOST_KEY_ECDSA_256; break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: mask|=LIBSSH2_KNOWNHOST_KEY_ECDSA_384; break;
        case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: mask|=LIBSSH2_KNOWNHOST_KEY_ECDSA_521; break;
        case LIBSSH2_HOSTKEY_TYPE_ED25519:   mask|=LIBSSH2_KNOWNHOST_KEY_ED25519; break;
        default: break;
    }
    LIBSSH2_KNOWNHOSTS* kh=libssh2_knownhost_init(ssh);
    if(!kh) return OYOBYOK_SSH_E_MEM;
    libssh2_knownhost_readfile(kh,path,LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    struct libssh2_knownhost* found=NULL;
    int chk=libssh2_knownhost_checkp(kh,host,port>0?port:22,key,klen,mask,&found);
    oyobyok_ssh_err_t rc=OYOBYOK_SSH_OK;
    if(chk==LIBSSH2_KNOWNHOST_CHECK_MISMATCH){
        ESP_LOGE(TAG,"known_hosts mismatch for %s",host);
        rc=OYOBYOK_SSH_E_HOSTKEY;
    } else if(chk!=LIBSSH2_KNOWNHOST_CHECK_MATCH){
        ESP_LOGW(TAG,"host %s not in known_hosts; adding",host);
        libssh2_knownhost_addc(kh,host,NULL,key,klen,NULL,0,mask,NULL);
        if(libssh2_knownhost_writefile(kh,path,LIBSSH2_KNOWNHOST_FILE_OPENSSH))
            ESP_LOGW(TAG,"could not write %s",path);
    }
    libssh2_knownhost_free(kh);
    return rc;
}

oyobyok_ssh_err_t oyobyok_ssh_connect(const oyobyok_ssh_opts_t* o,oyobyok_ssh_session_t** out,char* err,int errlen){
    if(err&&errlen) err[0]=0;
    if(!o||!o->host||!o->privkey||!out) return OYOBYOK_SSH_E_INTERNAL;
    *out=NULL;
    if(!oyobyok_ssh_have_key(o->privkey)){
        if(err) snprintf(err,errlen,"key not found: %s",o->privkey);
        return OYOBYOK_SSH_E_NOKEY;
    }
    oyobyok_ssh_err_t le=ensure_lib(); if(le) return le;

    oyobyok_ssh_err_t why=OYOBYOK_SSH_E_CONNECT;
    int sock=tcp_connect(o->host,o->port,&why);
    if(sock<0){ if(err) snprintf(err,errlen,"%s",oyobyok_ssh_strerror(why)); return why; }

    LIBSSH2_SESSION* ssh=libssh2_session_init();
    if(!ssh){ close(sock); return OYOBYOK_SSH_E_MEM; }
    libssh2_session_set_blocking(ssh,1);

    if(libssh2_session_handshake(ssh,sock)){
        char* msg=NULL; libssh2_session_last_error(ssh,&msg,NULL,0);
        if(err) snprintf(err,errlen,"handshake: %s",msg?msg:"failed");
        libssh2_session_free(ssh); close(sock); return OYOBYOK_SSH_E_HANDSHAKE;
    }

    oyobyok_ssh_err_t hk=verify_hostkey(ssh,o->host,o->port);
    if(hk){
        if(err) snprintf(err,errlen,"%s",oyobyok_ssh_strerror(hk));
        libssh2_session_disconnect(ssh,"host key refused"); libssh2_session_free(ssh); close(sock);
        return hk;
    }

    const char* user=(o->user&&*o->user)?o->user:"git";
    const char* pub=(o->pubkey && oyobyok_ssh_have_key(o->pubkey)) ? o->pubkey : NULL;
    if(libssh2_userauth_publickey_fromfile(ssh,user,pub,o->privkey,o->passphrase?o->passphrase:"")){
        char* msg=NULL; libssh2_session_last_error(ssh,&msg,NULL,0);
        if(err) snprintf(err,errlen,"auth: %s",msg?msg:"rejected");
        libssh2_session_disconnect(ssh,"auth failed"); libssh2_session_free(ssh); close(sock);
        return OYOBYOK_SSH_E_AUTH;
    }

    oyobyok_ssh_session_t* s=calloc(1,sizeof *s);
    if(!s){ libssh2_session_disconnect(ssh,"oom"); libssh2_session_free(ssh); close(sock); return OYOBYOK_SSH_E_MEM; }
    s->sock=sock; s->ssh=ssh; *out=s;
    ESP_LOGI(TAG,"connected: %s@%s:%d",user,o->host,o->port>0?o->port:22);
    return OYOBYOK_SSH_OK;
}

void oyobyok_ssh_disconnect(oyobyok_ssh_session_t** s){
    if(!s||!*s) return;
    oyobyok_ssh_session_t* x=*s;
    if(x->ssh){ libssh2_session_disconnect(x->ssh,"bye"); libssh2_session_free(x->ssh); }
    if(x->sock>=0) close(x->sock);
    free(x); *s=NULL;
}

oyobyok_ssh_err_t oyobyok_ssh_exec(oyobyok_ssh_session_t* s,const char* cmd,char* out,int outlen,int* exitcode){
    if(!s||!s->ssh||!cmd) return OYOBYOK_SSH_E_INTERNAL;
    if(out&&outlen) out[0]=0;
    LIBSSH2_CHANNEL* ch=libssh2_channel_open_session(s->ssh);
    if(!ch) return OYOBYOK_SSH_E_INTERNAL;
    if(libssh2_channel_exec(ch,cmd)){ libssh2_channel_free(ch); return OYOBYOK_SSH_E_INTERNAL; }
    int total=0;
    for(;;){
        char buf[256];
        ssize_t n=libssh2_channel_read(ch,buf,sizeof buf);
        if(n>0){
            if(out&&outlen>1){ int room=outlen-1-total; int cp=(int)n<room?(int)n:room;
                               if(cp>0){ memcpy(out+total,buf,cp); total+=cp; out[total]=0; } }
        } else if(n==0){ break; }
        else if(n==LIBSSH2_ERROR_EAGAIN){ continue; }
        else break;
    }
    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_closed(ch);
    if(exitcode) *exitcode=libssh2_channel_get_exit_status(ch);
    libssh2_channel_free(ch);
    return OYOBYOK_SSH_OK;
}

static int sftp_put_file(LIBSSH2_SFTP* sftp,const char* local,const char* remote){
    FILE* f=fopen(local,"rb"); if(!f) return -1;
    LIBSSH2_SFTP_HANDLE* h=libssh2_sftp_open(sftp,remote,
        LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);
    if(!h){ fclose(f); return -1; }
    char buf[512]; size_t n; int rc=0;
    while((n=fread(buf,1,sizeof buf,f))>0){
        char* p=buf; size_t left=n;
        while(left){ ssize_t w=libssh2_sftp_write(h,p,left); if(w<0){ rc=-1; break; } p+=w; left-=(size_t)w; }
        if(rc) break;
    }
    libssh2_sftp_close(h); fclose(f);
    return rc;
}

static volatile int s_sftp_done=0;
int oyobyok_ssh_sftp_files_done(void){ return s_sftp_done; }

static int sftp_push_recursive(LIBSSH2_SFTP* sftp,const char* local_dir,const char* remote_dir,
                               int* count,char* err,int errlen){
    libssh2_sftp_mkdir(sftp,remote_dir,0755);
    DIR* d=opendir(local_dir);
    if(!d){ if(err) snprintf(err,errlen,"cannot open %s",local_dir); return -1; }
    struct dirent* e; int rc=0;
    while((e=readdir(d))){
        if(e->d_name[0]=='.') continue;   // hidden entries stay on the device
        char lp[384], rp[384];
        snprintf(lp,sizeof lp,"%s/%s",local_dir,e->d_name);
        snprintf(rp,sizeof rp,"%s/%s",remote_dir,e->d_name);
        struct stat st; if(stat(lp,&st)!=0) continue;
        if(S_ISDIR(st.st_mode)){
            if(sftp_push_recursive(sftp,lp,rp,count,err,errlen)){ rc=-1; break; }
        } else if(S_ISREG(st.st_mode)){
            if(sftp_put_file(sftp,lp,rp)){ if(err) snprintf(err,errlen,"upload failed: %s",e->d_name); rc=-1; break; }
            (*count)++; s_sftp_done=*count;
        }
    }
    closedir(d);
    return rc;
}

int oyobyok_ssh_sftp_push_dir(oyobyok_ssh_session_t* s,const char* local_dir,const char* remote_dir,char* err,int errlen){
    if(err&&errlen) err[0]=0;
    if(!s||!s->ssh||!local_dir||!remote_dir) return -1;
    LIBSSH2_SFTP* sftp=libssh2_sftp_init(s->ssh);
    if(!sftp){ if(err) snprintf(err,errlen,"SFTP init failed"); return -1; }
    int count=0; s_sftp_done=0;
    // mkdir -p the remote parents so a fresh server accepts remote_path/<project>.
    { char tmp[384]; snprintf(tmp,sizeof tmp,"%s",remote_dir);
      for(char* q=tmp+1;*q;q++) if(*q=='/'){ *q=0; libssh2_sftp_mkdir(sftp,tmp,0755); *q='/'; } }
    int rc=sftp_push_recursive(sftp,local_dir,remote_dir,&count,err,errlen);
    libssh2_sftp_shutdown(sftp);
    ESP_LOGI(TAG,"SFTP push %s: %d files -> %s",rc==0?"done":"failed",count,remote_dir);
    return rc==0 ? count : -1;
}
