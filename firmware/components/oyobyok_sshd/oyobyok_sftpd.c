// SFTP version 3 (draft-ietf-secsh-filexfer-02) over one directory tree. Paths from the client are
// normalised and pinned under the root; handles are small integers. Ownership, links and extended
// requests are not supported; setstat is accepted and ignored, since FAT keeps none of it.
#include "oyobyok_sshd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include "esp_log.h"

static const char* TAG="oyobyok_sftpd";

enum { FXP_INIT=1, FXP_VERSION=2, FXP_OPEN=3, FXP_CLOSE=4, FXP_READ=5, FXP_WRITE=6, FXP_LSTAT=7, FXP_FSTAT=8, FXP_SETSTAT=9,
       FXP_FSETSTAT=10, FXP_OPENDIR=11, FXP_READDIR=12, FXP_REMOVE=13, FXP_MKDIR=14, FXP_RMDIR=15, FXP_REALPATH=16, FXP_STAT=17,
       FXP_RENAME=18, FXP_READLINK=19, FXP_SYMLINK=20, FXP_STATUS=101, FXP_HANDLE=102, FXP_DATA=103, FXP_NAME=104, FXP_ATTRS=105,
       FXP_EXTENDED=200 };
enum { FX_OK=0, FX_EOF=1, FX_NO_SUCH_FILE=2, FX_PERMISSION_DENIED=3, FX_FAILURE=4, FX_BAD_MESSAGE=5, FX_OP_UNSUPPORTED=8 };
enum { FXF_READ=1, FXF_WRITE=2, FXF_APPEND=4, FXF_CREAT=8, FXF_TRUNC=16, FXF_EXCL=32 };
enum { ATTR_SIZE=1, ATTR_UIDGID=2, ATTR_PERMISSIONS=4, ATTR_ACMODTIME=8, ATTR_EXTENDED=0x80000000u };

#define MAX_HANDLES 8
#define MAX_READ    32000            // fits the client's 32768 channel packet with the headers
#define REPLY_CAP   (MAX_READ+64)
#define PATH_MAX_   400
#define FULL_MAX_   (PATH_MAX_+128)

typedef struct { int used; int isdir; FILE* f; DIR* d; char path[FULL_MAX_]; } handle_t;
struct sftpd { char root[128]; handle_t h[MAX_HANDLES]; unsigned char* reply; };

sftpd_t* sftpd_new(const char* root){
    sftpd_t* s=calloc(1,sizeof *s); if(!s) return NULL;
    s->reply=malloc(REPLY_CAP); if(!s->reply){ free(s); return NULL; }
    snprintf(s->root,sizeof s->root,"%s",root);
    return s;
}
void sftpd_free(sftpd_t* s){
    if(!s) return;
    for(int i=0;i<MAX_HANDLES;i++){ if(s->h[i].used){ if(s->h[i].f) fclose(s->h[i].f); if(s->h[i].d) closedir(s->h[i].d); } }
    free(s->reply); free(s);
}

typedef struct { unsigned char* p; int len, cap; } wb_t;
static void w_u8(wb_t* b,unsigned v){ if(b->len+1<=b->cap) b->p[b->len++]=(unsigned char)v; }
static void w_u32(wb_t* b,unsigned v){ if(b->len+4<=b->cap){ b->p[b->len++]=v>>24; b->p[b->len++]=v>>16; b->p[b->len++]=v>>8; b->p[b->len++]=v; } }
static void w_u64(wb_t* b,unsigned long long v){ w_u32(b,(unsigned)(v>>32)); w_u32(b,(unsigned)v); }
static void w_str(wb_t* b,const void* d,int n){ w_u32(b,(unsigned)n); if(b->len+n<=b->cap){ memcpy(b->p+b->len,d,n); b->len+=n; } }
static void w_cstr(wb_t* b,const char* s){ w_str(b,s,(int)strlen(s)); }

typedef struct { const unsigned char* p; int len, pos, bad; } rb_t;
static unsigned r_u8(rb_t* r){ if(r->pos+1>r->len){ r->bad=1; return 0; } return r->p[r->pos++]; }
static unsigned r_u32(rb_t* r){ if(r->pos+4>r->len){ r->bad=1; return 0; }
    unsigned v=((unsigned)r->p[r->pos]<<24)|((unsigned)r->p[r->pos+1]<<16)|((unsigned)r->p[r->pos+2]<<8)|r->p[r->pos+3]; r->pos+=4; return v; }
static unsigned long long r_u64(rb_t* r){ unsigned long long hi=r_u32(r); return (hi<<32)|r_u32(r); }
static const unsigned char* r_str(rb_t* r,int* n){ unsigned l=r_u32(r); if(r->bad||l>(unsigned)(r->len-r->pos)){ r->bad=1; *n=0; return (const unsigned char*)""; }
    const unsigned char* s=r->p+r->pos; r->pos+=(int)l; *n=(int)l; return s; }
static void r_attrs(rb_t* r,unsigned* perms){
    unsigned flags=r_u32(r);
    if(flags&ATTR_SIZE) r_u64(r);
    if(flags&ATTR_UIDGID){ r_u32(r); r_u32(r); }
    if(flags&ATTR_PERMISSIONS){ unsigned p=r_u32(r); if(perms) *perms=p; }
    if(flags&ATTR_ACMODTIME){ r_u32(r); r_u32(r); }
    if(flags&ATTR_EXTENDED){ unsigned n=r_u32(r); for(unsigned i=0;i<n&&!r->bad;i++){ int l; r_str(r,&l); r_str(r,&l); } }
}

static void norm_path(const unsigned char* in,int n,char* out,int cap){
    char parts[24][64]; int np=0; int i=0;
    while(i<n){
        while(i<n && in[i]=='/') i++;
        int st=i; while(i<n && in[i]!='/') i++;
        int L=i-st; if(L==0) continue;
        if(L==1 && in[st]=='.') continue;
        if(L==2 && in[st]=='.' && in[st+1]=='.'){ if(np>0) np--; continue; }
        if(np<24){ if(L>63) L=63; memcpy(parts[np],in+st,L); parts[np][L]=0; np++; }
    }
    int o=0; out[0]='/'; out[1]=0;
    for(int k=0;k<np;k++){ int w=snprintf(out+o,cap-o,"/%s",parts[k]); if(w<0||o+w>=cap) break; o+=w; }
    if(np==0) out[1]=0;
}
static void full_path(sftpd_t* s,const char* vpath,char* out,int cap){
    if(strcmp(vpath,"/")==0) snprintf(out,cap,"%s",s->root); else snprintf(out,cap,"%s%s",s->root,vpath);
}
static const char* base_name(const char* p){ const char* b=strrchr(p,'/'); return b?b+1:p; }

static void w_attrs_st(wb_t* b,const struct stat* st){
    w_u32(b,ATTR_SIZE|ATTR_PERMISSIONS|ATTR_ACMODTIME);
    w_u64(b,(unsigned long long)st->st_size);
    w_u32(b,S_ISDIR(st->st_mode)?(040000|0755):(0100000|0644));
    w_u32(b,(unsigned)st->st_mtime); w_u32(b,(unsigned)st->st_mtime);
}
static void longname(char* out,int cap,const char* name,const struct stat* st){
    char when[24]; struct tm tm; time_t t=st->st_mtime; localtime_r(&t,&tm);
    if(t>1700000000L) strftime(when,sizeof when,"%b %e %H:%M",&tm); else snprintf(when,sizeof when,"Jan  1  1970");
    snprintf(out,cap,"%s 1 byok byok %10lld %s %s",S_ISDIR(st->st_mode)?"drwxr-xr-x":"-rw-r--r--",(long long)st->st_size,when,name);
}

static int alloc_handle(sftpd_t* s){ for(int i=0;i<MAX_HANDLES;i++) if(!s->h[i].used){ memset(&s->h[i],0,sizeof s->h[i]); s->h[i].used=1; return i; } return -1; }
static handle_t* get_handle(sftpd_t* s,const unsigned char* hs,int hn){
    if(hn!=4) return NULL;
    unsigned i=((unsigned)hs[0]<<24)|((unsigned)hs[1]<<16)|((unsigned)hs[2]<<8)|hs[3];
    return (i<MAX_HANDLES && s->h[i].used) ? &s->h[i] : NULL;
}
static void w_handle(wb_t* b,int i){ unsigned char h[4]={(unsigned)i>>24,(unsigned)i>>16,(unsigned)i>>8,(unsigned)i}; w_str(b,h,4); }

static int reply_status(wb_t* b,unsigned id,int code,const char* msg){
    b->len=0; w_u8(b,FXP_STATUS); w_u32(b,id); w_u32(b,(unsigned)code); w_cstr(b,msg); w_cstr(b,""); return 0;
}
static int errno_status(void){ return errno==ENOENT?FX_NO_SUCH_FILE:(errno==EACCES?FX_PERMISSION_DENIED:FX_FAILURE); }

int sftpd_handle(sftpd_t* s,const unsigned char* pkt,int len,int (*send)(void*,const unsigned char*,int),void* ctx){
    rb_t r={ .p=pkt, .len=len, .pos=0, .bad=0 };
    wb_t b={ .p=s->reply, .len=0, .cap=REPLY_CAP };
    int type=r_u8(&r);
    if(type==FXP_INIT){
        w_u8(&b,FXP_VERSION); w_u32(&b,3); return send(ctx,b.p,b.len);
    }
    unsigned id=r_u32(&r);
    char vp[PATH_MAX_], fp[FULL_MAX_];
    switch(type){
        case FXP_REALPATH: {
            int n; const unsigned char* p=r_str(&r,&n); if(r.bad) break;
            norm_path(p,n,vp,sizeof vp); full_path(s,vp,fp,sizeof fp);
            struct stat st; if(stat(fp,&st)!=0){ memset(&st,0,sizeof st); st.st_mode=S_IFDIR; }
            w_u8(&b,FXP_NAME); w_u32(&b,id); w_u32(&b,1); w_cstr(&b,vp); w_cstr(&b,vp); w_attrs_st(&b,&st);
            return send(ctx,b.p,b.len);
        }
        case FXP_STAT: case FXP_LSTAT: {
            int n; const unsigned char* p=r_str(&r,&n); if(r.bad) break;
            norm_path(p,n,vp,sizeof vp); full_path(s,vp,fp,sizeof fp);
            struct stat st;
            if(stat(fp,&st)!=0){ reply_status(&b,id,errno_status(),"no such file"); return send(ctx,b.p,b.len); }
            w_u8(&b,FXP_ATTRS); w_u32(&b,id); w_attrs_st(&b,&st); return send(ctx,b.p,b.len);
        }
        case FXP_FSTAT: {
            int hn; const unsigned char* hs=r_str(&r,&hn); handle_t* h=get_handle(s,hs,hn);
            if(!h){ reply_status(&b,id,FX_FAILURE,"bad handle"); return send(ctx,b.p,b.len); }
            if(h->f) fflush(h->f);
            struct stat st; if(stat(h->path,&st)!=0){ reply_status(&b,id,errno_status(),"stat failed"); return send(ctx,b.p,b.len); }
            w_u8(&b,FXP_ATTRS); w_u32(&b,id); w_attrs_st(&b,&st); return send(ctx,b.p,b.len);
        }
        case FXP_SETSTAT: case FXP_FSETSTAT:
            reply_status(&b,id,FX_OK,""); return send(ctx,b.p,b.len);
        case FXP_OPENDIR: {
            int n; const unsigned char* p=r_str(&r,&n); if(r.bad) break;
            norm_path(p,n,vp,sizeof vp); full_path(s,vp,fp,sizeof fp);
            int i=alloc_handle(s); if(i<0){ reply_status(&b,id,FX_FAILURE,"too many open handles"); return send(ctx,b.p,b.len); }
            DIR* d=opendir(fp);
            if(!d){ s->h[i].used=0; reply_status(&b,id,errno_status(),"cannot open directory"); return send(ctx,b.p,b.len); }
            s->h[i].isdir=1; s->h[i].d=d; snprintf(s->h[i].path,FULL_MAX_,"%s",fp);
            w_u8(&b,FXP_HANDLE); w_u32(&b,id); w_handle(&b,i); return send(ctx,b.p,b.len);
        }
        case FXP_READDIR: {
            int hn; const unsigned char* hs=r_str(&r,&hn); handle_t* h=get_handle(s,hs,hn);
            if(!h||!h->isdir){ reply_status(&b,id,FX_FAILURE,"bad handle"); return send(ctx,b.p,b.len); }
            w_u8(&b,FXP_NAME); w_u32(&b,id); int cntpos=b.len; w_u32(&b,0); int cnt=0;
            struct dirent* e;
            while(cnt<32 && b.len<REPLY_CAP-1200 && (e=readdir(h->d))){
                char child[FULL_MAX_+256]; snprintf(child,sizeof child,"%s/%.200s",h->path,e->d_name);
                struct stat st; if(stat(child,&st)!=0) continue;
                char ln[400]; longname(ln,sizeof ln,e->d_name,&st);
                w_cstr(&b,e->d_name); w_cstr(&b,ln); w_attrs_st(&b,&st); cnt++;
            }
            if(cnt==0){ reply_status(&b,id,FX_EOF,"end of directory"); return send(ctx,b.p,b.len); }
            b.p[cntpos]=cnt>>24; b.p[cntpos+1]=cnt>>16; b.p[cntpos+2]=cnt>>8; b.p[cntpos+3]=cnt;
            return send(ctx,b.p,b.len);
        }
        case FXP_OPEN: {
            int n; const unsigned char* p=r_str(&r,&n); unsigned flags=r_u32(&r); r_attrs(&r,NULL); if(r.bad) break;
            norm_path(p,n,vp,sizeof vp); full_path(s,vp,fp,sizeof fp);
            if(strcmp(vp,"/")==0){ reply_status(&b,id,FX_FAILURE,"is a directory"); return send(ctx,b.p,b.len); }
            int i=alloc_handle(s); if(i<0){ reply_status(&b,id,FX_FAILURE,"too many open handles"); return send(ctx,b.p,b.len); }
            struct stat st; int exists=(stat(fp,&st)==0);
            if(exists && S_ISDIR(st.st_mode)){ s->h[i].used=0; reply_status(&b,id,FX_FAILURE,"is a directory"); return send(ctx,b.p,b.len); }
            if((flags&FXF_EXCL)&&exists){ s->h[i].used=0; reply_status(&b,id,FX_FAILURE,"already exists"); return send(ctx,b.p,b.len); }
            if(!exists && !(flags&FXF_CREAT)){ s->h[i].used=0; reply_status(&b,id,FX_NO_SUCH_FILE,"no such file"); return send(ctx,b.p,b.len); }
            const char* mode;
            if(flags&FXF_WRITE){
                if((flags&FXF_TRUNC)||!exists) mode="w+b"; else mode="r+b";
            } else mode="rb";
            FILE* f=fopen(fp,mode);
            if(!f){ s->h[i].used=0; reply_status(&b,id,errno_status(),"cannot open"); return send(ctx,b.p,b.len); }
            setvbuf(f,NULL,_IOFBF,4096);
            s->h[i].f=f; snprintf(s->h[i].path,FULL_MAX_,"%s",fp);
            sftpd_note_file();
            w_u8(&b,FXP_HANDLE); w_u32(&b,id); w_handle(&b,i); return send(ctx,b.p,b.len);
        }
        case FXP_READ: {
            int hn; const unsigned char* hs=r_str(&r,&hn); unsigned long long off=r_u64(&r); unsigned want=r_u32(&r); if(r.bad) break;
            handle_t* h=get_handle(s,hs,hn);
            if(!h||!h->f){ reply_status(&b,id,FX_FAILURE,"bad handle"); return send(ctx,b.p,b.len); }
            if(want>MAX_READ) want=MAX_READ;
            if(fseek(h->f,(long)off,SEEK_SET)!=0){ reply_status(&b,id,FX_FAILURE,"seek failed"); return send(ctx,b.p,b.len); }
            w_u8(&b,FXP_DATA); w_u32(&b,id); int lenpos=b.len; w_u32(&b,0);
            size_t got=fread(b.p+b.len,1,want,h->f);
            if(got==0){ reply_status(&b,id,FX_EOF,"end of file"); return send(ctx,b.p,b.len); }
            b.len+=(int)got; b.p[lenpos]=got>>24; b.p[lenpos+1]=got>>16; b.p[lenpos+2]=got>>8; b.p[lenpos+3]=got;
            return send(ctx,b.p,b.len);
        }
        case FXP_WRITE: {
            int hn; const unsigned char* hs=r_str(&r,&hn); unsigned long long off=r_u64(&r); int dn; const unsigned char* d=r_str(&r,&dn); if(r.bad) break;
            handle_t* h=get_handle(s,hs,hn);
            if(!h||!h->f){ reply_status(&b,id,FX_FAILURE,"bad handle"); return send(ctx,b.p,b.len); }
            if(fseek(h->f,(long)off,SEEK_SET)!=0 || fwrite(d,1,dn,h->f)!=(size_t)dn){ reply_status(&b,id,FX_FAILURE,"write failed"); return send(ctx,b.p,b.len); }
            reply_status(&b,id,FX_OK,""); return send(ctx,b.p,b.len);
        }
        case FXP_CLOSE: {
            int hn; const unsigned char* hs=r_str(&r,&hn); handle_t* h=get_handle(s,hs,hn);
            if(!h){ reply_status(&b,id,FX_FAILURE,"bad handle"); return send(ctx,b.p,b.len); }
            if(h->f) fclose(h->f);
            if(h->d) closedir(h->d);
            h->used=0;
            reply_status(&b,id,FX_OK,""); return send(ctx,b.p,b.len);
        }
        case FXP_REMOVE: case FXP_RMDIR: case FXP_MKDIR: {
            int n; const unsigned char* p=r_str(&r,&n); if(r.bad) break;
            norm_path(p,n,vp,sizeof vp); full_path(s,vp,fp,sizeof fp);
            if(strcmp(vp,"/")==0){ reply_status(&b,id,FX_PERMISSION_DENIED,"not the root"); return send(ctx,b.p,b.len); }
            int rc = type==FXP_REMOVE ? unlink(fp) : (type==FXP_RMDIR ? rmdir(fp) : mkdir(fp,0777));
            if(rc!=0) reply_status(&b,id,errno_status(),type==FXP_MKDIR?"cannot create":"cannot remove"); else reply_status(&b,id,FX_OK,"");
            return send(ctx,b.p,b.len);
        }
        case FXP_RENAME: {
            int n1,n2; const unsigned char* p1=r_str(&r,&n1); const unsigned char* p2=r_str(&r,&n2); if(r.bad) break;
            char vp2[PATH_MAX_], fp2[FULL_MAX_];
            norm_path(p1,n1,vp,sizeof vp); full_path(s,vp,fp,sizeof fp);
            norm_path(p2,n2,vp2,sizeof vp2); full_path(s,vp2,fp2,sizeof fp2);
            if(strcmp(vp,"/")==0||strcmp(vp2,"/")==0){ reply_status(&b,id,FX_PERMISSION_DENIED,"not the root"); return send(ctx,b.p,b.len); }
            struct stat st; if(stat(fp2,&st)==0){ reply_status(&b,id,FX_FAILURE,"target exists"); return send(ctx,b.p,b.len); }
            if(rename(fp,fp2)!=0) reply_status(&b,id,errno_status(),"rename failed"); else reply_status(&b,id,FX_OK,"");
            return send(ctx,b.p,b.len);
        }
        case FXP_READLINK: case FXP_SYMLINK: case FXP_EXTENDED: default:
            reply_status(&b,id,FX_OP_UNSUPPORTED,"not supported"); return send(ctx,b.p,b.len);
    }
    ESP_LOGW(TAG,"malformed request type %d",type);
    reply_status(&b,id,FX_BAD_MESSAGE,"malformed request"); return send(ctx,b.p,b.len);
    (void)base_name;
}
