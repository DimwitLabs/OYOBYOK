// SSH transport, user authentication and one session channel carrying the SFTP subsystem.
//
// RFC 4253 (transport), 4252 (userauth), 4254 (connection), 5656 (ECDH and ECDSA), 8332 (rsa-sha2),
// 8709 (ed25519). Exactly one algorithm of each kind is offered: ecdh-sha2-nistp256,
// ecdsa-sha2-nistp256, aes128-ctr, hmac-sha2-256, none. Every OpenSSH client since 2011 accepts that
// set by default. Client keys may be ed25519, ECDSA P-256 or RSA (rsa-sha2-256/512 and ssh-rsa).
#include "oyobyok_sshd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/sha1.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/base64.h"
#include "mbedtls/bignum.h"
#include "tweetnacl.h"

static const char* TAG="oyobyok_sshd";

#define VERSION_STR   "SSH-2.0-OYOBYOK_1.1"
#define MAX_PACKET    36000          // the client is told 32768 for channel data; this leaves room around it
#define OUR_WINDOW    262144
#define BLOCK         16
#define MAC_LEN       32
#define IDLE_SECS     300            // a silent client is dropped so the next one can connect

enum { MSG_DISCONNECT=1, MSG_IGNORE=2, MSG_UNIMPLEMENTED=3, MSG_DEBUG=4, MSG_SERVICE_REQUEST=5, MSG_SERVICE_ACCEPT=6, MSG_EXT_INFO=7,
       MSG_KEXINIT=20, MSG_NEWKEYS=21, MSG_KEX_ECDH_INIT=30, MSG_KEX_ECDH_REPLY=31,
       MSG_USERAUTH_REQUEST=50, MSG_USERAUTH_FAILURE=51, MSG_USERAUTH_SUCCESS=52, MSG_USERAUTH_PK_OK=60,
       MSG_GLOBAL_REQUEST=80, MSG_REQUEST_FAILURE=82,
       MSG_CHANNEL_OPEN=90, MSG_CHANNEL_OPEN_CONFIRMATION=91, MSG_CHANNEL_OPEN_FAILURE=92, MSG_CHANNEL_WINDOW_ADJUST=93,
       MSG_CHANNEL_DATA=94, MSG_CHANNEL_EOF=96, MSG_CHANNEL_CLOSE=97, MSG_CHANNEL_REQUEST=98,
       MSG_CHANNEL_SUCCESS=99, MSG_CHANNEL_FAILURE=100 };

// TweetNaCl links against this; verification never calls it.
void randombytes(unsigned char* x,unsigned long long n){ esp_fill_random(x,(size_t)n); }
static int rng(void* p,unsigned char* out,size_t n){ (void)p; esp_fill_random(out,n); return 0; }

typedef struct { unsigned char* p; int len, cap; } buf_t;
static void b_init(buf_t* b,unsigned char* mem,int cap){ b->p=mem; b->len=0; b->cap=cap; }
static int  b_room(buf_t* b,int n){ return b->len+n<=b->cap; }
static void b_u8(buf_t* b,unsigned v){ if(b_room(b,1)) b->p[b->len++]=(unsigned char)v; }
static void b_u32(buf_t* b,unsigned v){ if(b_room(b,4)){ b->p[b->len++]=v>>24; b->p[b->len++]=v>>16; b->p[b->len++]=v>>8; b->p[b->len++]=v; } }
static void b_bytes(buf_t* b,const void* d,int n){ if(b_room(b,n)){ memcpy(b->p+b->len,d,n); b->len+=n; } }
static void b_str(buf_t* b,const void* d,int n){ b_u32(b,(unsigned)n); b_bytes(b,d,n); }
static void b_cstr(buf_t* b,const char* s){ b_str(b,s,(int)strlen(s)); }

typedef struct { const unsigned char* p; int len, pos; int bad; } rd_t;
static void r_init(rd_t* r,const unsigned char* p,int len){ r->p=p; r->len=len; r->pos=0; r->bad=0; }
static unsigned r_u8(rd_t* r){ if(r->pos+1>r->len){ r->bad=1; return 0; } return r->p[r->pos++]; }
static unsigned r_u32(rd_t* r){ if(r->pos+4>r->len){ r->bad=1; return 0; }
    unsigned v=((unsigned)r->p[r->pos]<<24)|((unsigned)r->p[r->pos+1]<<16)|((unsigned)r->p[r->pos+2]<<8)|r->p[r->pos+3]; r->pos+=4; return v; }
static const unsigned char* r_str(rd_t* r,int* n){ unsigned l=r_u32(r); if(r->bad||l>(unsigned)(r->len-r->pos)){ r->bad=1; *n=0; return (const unsigned char*)""; }
    const unsigned char* s=r->p+r->pos; r->pos+=(int)l; *n=(int)l; return s; }
static int str_is(const unsigned char* s,int n,const char* lit){ return (int)strlen(lit)==n && memcmp(s,lit,n)==0; }
static int list_has(const unsigned char* s,int n,const char* name){
    int L=(int)strlen(name);
    for(int i=0;i<=n-L;i++){
        if(memcmp(s+i,name,L)==0 && (i==0||s[i-1]==',') && (i+L==n||s[i+L]==',')) return 1;
    }
    return 0;
}

typedef struct {
    int sock;
    unsigned seq_in, seq_out;
    int encrypted;
    mbedtls_aes_context aes_in, aes_out;
    unsigned char iv_in[16], iv_out[16], sb_in[16], sb_out[16]; size_t off_in, off_out;
    unsigned char mac_in[32], mac_out[32];
    unsigned char session_id[32]; int have_session_id;
    unsigned char *inbuf, *outbuf;
    char v_client[256];
    unsigned char* kexinit_client; int kexinit_client_len;
    unsigned char* kexinit_server; int kexinit_server_len;
    int authed;
    int chan_open, chan_closed, chan_client_id; unsigned client_window; unsigned client_maxpkt; unsigned our_window;
    sftpd_t* sftp; int sftp_started;
    unsigned char* sftp_in; int sftp_in_len;
    int closing;
} conn_t;

static const oyobyok_sshd_cfg_t* s_cfg;
static mbedtls_pk_context s_hostkey;
static unsigned char s_hostkey_blob[128]; static int s_hostkey_blob_len;
static volatile int s_stop=0;
static volatile int s_listen=-1, s_client=-1;
static volatile int s_connected=0;
static volatile int s_files=0;
void sftpd_note_file(void){ s_files++; }
int  oyobyok_sshd_files_touched(void){ return s_files; }
bool oyobyok_sshd_client_connected(void){ return s_connected!=0; }

static int read_full(conn_t* c,unsigned char* p,int n){
    int idle=0;
    while(n>0){
        if(s_stop) return -1;
        int r=recv(c->sock,p,n,0);
        if(r<0){ if((errno==EAGAIN||errno==EWOULDBLOCK) && ++idle<IDLE_SECS) continue; return -1; }
        if(r==0) return -1;
        idle=0; p+=r; n-=r;
    }
    return 0;
}
static int write_full(conn_t* c,const unsigned char* p,int n){
    while(n>0){
        if(s_stop) return -1;
        int r=send(c->sock,p,n,0);
        if(r<0){ if(errno==EAGAIN||errno==EWOULDBLOCK) continue; return -1; }
        p+=r; n-=r;
    }
    return 0;
}

static void mac_compute(const unsigned char* key,unsigned seq,const unsigned char* pkt,int len,unsigned char* out){
    mbedtls_md_context_t m; mbedtls_md_init(&m);
    mbedtls_md_setup(&m,mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),1);
    mbedtls_md_hmac_starts(&m,key,32);
    unsigned char s[4]={seq>>24,seq>>16,seq>>8,seq};
    mbedtls_md_hmac_update(&m,s,4); mbedtls_md_hmac_update(&m,pkt,len);
    mbedtls_md_hmac_finish(&m,out); mbedtls_md_free(&m);
}

static int send_packet(conn_t* c,int len){
    int pad=BLOCK-((5+len)%BLOCK); if(pad<4) pad+=BLOCK;
    int total=5+len+pad;
    if(total>MAX_PACKET) return -1;
    unsigned char* p=c->outbuf;
    unsigned plen=(unsigned)(1+len+pad);
    p[0]=plen>>24; p[1]=plen>>16; p[2]=plen>>8; p[3]=plen; p[4]=(unsigned char)pad;
    esp_fill_random(p+5+len,pad);
    unsigned char mac[MAC_LEN]; int maclen=0;
    if(c->encrypted){
        mac_compute(c->mac_out,c->seq_out,p,total,mac); maclen=MAC_LEN;
        mbedtls_aes_crypt_ctr(&c->aes_out,total,&c->off_out,c->iv_out,c->sb_out,p,p);
    }
    c->seq_out++;
    if(write_full(c,p,total)) return -1;
    if(maclen && write_full(c,mac,maclen)) return -1;
    return 0;
}
static void out_begin(conn_t* c,buf_t* b,int type){ b_init(b,c->outbuf+5,MAX_PACKET-5-BLOCK*2-4); b_u8(b,type); }
static int  out_send(conn_t* c,buf_t* b){ return send_packet(c,b->len); }

static int read_packet(conn_t* c,int* len){
    unsigned char* p=c->inbuf;
    int blk=c->encrypted?BLOCK:8;           // RFC 4253 section 6: the "none" cipher pads to 8
    if(read_full(c,p,blk)) return -1;
    if(c->encrypted) mbedtls_aes_crypt_ctr(&c->aes_in,BLOCK,&c->off_in,c->iv_in,c->sb_in,p,p);
    unsigned plen=((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];
    if(plen<5 || plen+4>(unsigned)MAX_PACKET || ((plen+4)%blk)!=0) { ESP_LOGW(TAG,"bad packet length %u",plen); return -1; }
    int rest=(int)plen+4-blk;
    if(read_full(c,p+blk,rest)) return -1;
    if(c->encrypted){
        mbedtls_aes_crypt_ctr(&c->aes_in,rest,&c->off_in,c->iv_in,c->sb_in,p+blk,p+blk);
        unsigned char mac[MAC_LEN], want[MAC_LEN];
        if(read_full(c,mac,MAC_LEN)) return -1;
        mac_compute(c->mac_in,c->seq_in,p,(int)plen+4,want);
        if(memcmp(mac,want,MAC_LEN)!=0){ ESP_LOGW(TAG,"bad MAC"); return -1; }
    }
    c->seq_in++;
    int pad=p[4];
    *len=(int)plen-1-pad;
    if(*len<1) return -1;
    memmove(p,p+5,*len);
    return 0;
}

static int send_disconnect(conn_t* c,const char* why){
    buf_t b; out_begin(c,&b,MSG_DISCONNECT); b_u32(&b,2); b_cstr(&b,why); b_cstr(&b,""); return out_send(c,&b);
}

static int hostkey_load_or_make(const char* path,char* err,int errlen){
    mbedtls_pk_init(&s_hostkey);
    int rc=mbedtls_pk_parse_keyfile(&s_hostkey,path,NULL,rng,NULL);
    if(rc!=0 || mbedtls_pk_get_type(&s_hostkey)!=MBEDTLS_PK_ECKEY){
        mbedtls_pk_free(&s_hostkey); mbedtls_pk_init(&s_hostkey);
        ESP_LOGI(TAG,"making a new host key at %s",path);
        if(mbedtls_pk_setup(&s_hostkey,mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY))!=0 ||
           mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,mbedtls_pk_ec(s_hostkey),rng,NULL)!=0){
            snprintf(err,errlen,"could not make a host key"); return -1; }
        unsigned char pem[512];
        if(mbedtls_pk_write_key_pem(&s_hostkey,pem,sizeof pem)!=0){ snprintf(err,errlen,"could not encode the host key"); return -1; }
        FILE* f=fopen(path,"wb");
        if(!f){ snprintf(err,errlen,"could not write %s",path); return -1; }
        fputs((char*)pem,f); fclose(f);
    }
    // mbedTLS writes DER at the end of the buffer; the uncompressed point is its last 65 bytes.
    unsigned char der[160]; int n=mbedtls_pk_write_pubkey_der(&s_hostkey,der,sizeof der);
    if(n<65){ snprintf(err,errlen,"bad host key"); return -1; }
    const unsigned char* q=der+sizeof der-65;
    buf_t b; b_init(&b,s_hostkey_blob,sizeof s_hostkey_blob);
    b_cstr(&b,"ecdsa-sha2-nistp256"); b_cstr(&b,"nistp256"); b_str(&b,q,65);
    s_hostkey_blob_len=b.len;
    return 0;
}

// DER INTEGER contents are already valid SSH mpints, so r and s are passed through as-is.
static int der_sig_split(const unsigned char* d,int n,const unsigned char** r,int* rl,const unsigned char** s,int* sl){
    int i=0; if(n<8||d[i++]!=0x30) return -1;
    int L=d[i++]; if(L&0x80){ int nb=L&0x7f; L=0; while(nb--){ L=(L<<8)|d[i++]; } }
    if(d[i++]!=0x02) return -1;
    *rl=d[i++]; *r=d+i; i+=*rl;
    if(i>=n||d[i++]!=0x02) return -1;
    *sl=d[i++]; *s=d+i; i+=*sl;
    return i<=n?0:-1;
}

static void hash_str(mbedtls_sha256_context* h,const unsigned char* d,int n){
    unsigned char l[4]={(unsigned)n>>24,(unsigned)n>>16,(unsigned)n>>8,(unsigned)n}; mbedtls_sha256_update(h,l,4); mbedtls_sha256_update(h,d,n);
}
static void derive_key(const unsigned char* K,int Klen,const unsigned char* H,char X,const unsigned char* sid,unsigned char* out32){
    mbedtls_sha256_context h; mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h,0);
    hash_str(&h,K,Klen); mbedtls_sha256_update(&h,H,32); mbedtls_sha256_update(&h,(unsigned char*)&X,1); mbedtls_sha256_update(&h,sid,32);
    mbedtls_sha256_finish(&h,out32); mbedtls_sha256_free(&h);
}

static int send_kexinit(conn_t* c){
    buf_t b; out_begin(c,&b,MSG_KEXINIT);
    unsigned char cookie[16]; esp_fill_random(cookie,16); b_bytes(&b,cookie,16);
    b_cstr(&b,"ecdh-sha2-nistp256,ext-info-s"); b_cstr(&b,"ecdsa-sha2-nistp256");
    b_cstr(&b,"aes128-ctr"); b_cstr(&b,"aes128-ctr"); b_cstr(&b,"hmac-sha2-256"); b_cstr(&b,"hmac-sha2-256");
    b_cstr(&b,"none"); b_cstr(&b,"none"); b_cstr(&b,""); b_cstr(&b,""); b_u8(&b,0); b_u32(&b,0);
    free(c->kexinit_server); c->kexinit_server=malloc(b.len); if(!c->kexinit_server) return -1;
    memcpy(c->kexinit_server,b.p,b.len); c->kexinit_server_len=b.len;
    return out_send(c,&b);
}

static int do_kex(conn_t* c,int len){
    free(c->kexinit_client); c->kexinit_client=malloc(len); if(!c->kexinit_client) return -1;
    memcpy(c->kexinit_client,c->inbuf,len); c->kexinit_client_len=len;
    { rd_t r; r_init(&r,c->inbuf,len); r_u8(&r); r.pos+=16; int n; const unsigned char* kex=r_str(&r,&n);
      const unsigned char* hk; int hn; hk=r_str(&r,&hn); int en; const unsigned char* enc=r_str(&r,&en);
      if(r.bad||!list_has(kex,n,"ecdh-sha2-nistp256")||!list_has(hk,hn,"ecdsa-sha2-nistp256")||!list_has(enc,en,"aes128-ctr")){
          char why[160]; snprintf(why,sizeof why,"no common algorithm (len=%d bad=%d kex=%d/%d hk=%d/%d enc=%d/%d)",len,r.bad,
              n,list_has(kex,n,"ecdh-sha2-nistp256"),hn,list_has(hk,hn,"ecdsa-sha2-nistp256"),en,list_has(enc,en,"aes128-ctr"));
          ESP_LOGW(TAG,"%s",why); send_disconnect(c,why); return -1; } }
    if(!c->kexinit_server && send_kexinit(c)) return -1;

    int plen;
    if(read_packet(c,&plen)) return -1;
    if(c->inbuf[0]!=MSG_KEX_ECDH_INIT){ ESP_LOGW(TAG,"expected ECDH_INIT, got %d",c->inbuf[0]); return -1; }
    rd_t r; r_init(&r,c->inbuf,plen); r_u8(&r); int qclen; const unsigned char* qc=r_str(&r,&qclen);
    if(r.bad||qclen!=65) return -1;

    mbedtls_ecp_group grp; mbedtls_mpi d, z; mbedtls_ecp_point Q, Qc;
    mbedtls_ecp_group_init(&grp); mbedtls_mpi_init(&d); mbedtls_mpi_init(&z); mbedtls_ecp_point_init(&Q); mbedtls_ecp_point_init(&Qc);
    unsigned char qs[65], K[80], H[32], qc_copy[65]; size_t qslen=0; int Klen=0; int rc=-1;
    memcpy(qc_copy,qc,65);
    do{
        if(mbedtls_ecp_group_load(&grp,MBEDTLS_ECP_DP_SECP256R1)) break;
        if(mbedtls_ecp_gen_keypair(&grp,&d,&Q,rng,NULL)) break;
        if(mbedtls_ecp_point_write_binary(&grp,&Q,MBEDTLS_ECP_PF_UNCOMPRESSED,&qslen,qs,sizeof qs)||qslen!=65) break;
        if(mbedtls_ecp_point_read_binary(&grp,&Qc,qc_copy,65)||mbedtls_ecp_check_pubkey(&grp,&Qc)) break;
        if(mbedtls_ecdh_compute_shared(&grp,&z,&Qc,&d,rng,NULL)) break;
        Klen=(int)mbedtls_mpi_size(&z); if(Klen>(int)sizeof K-1) break;
        if(mbedtls_mpi_write_binary(&z,K+1,Klen)) break;
        if(K[1]&0x80){ K[0]=0; Klen+=1; } else memmove(K,K+1,Klen);
        rc=0;
    }while(0);
    mbedtls_mpi_free(&d); mbedtls_mpi_free(&z); mbedtls_ecp_point_free(&Q); mbedtls_ecp_point_free(&Qc); mbedtls_ecp_group_free(&grp);
    if(rc){ ESP_LOGW(TAG,"ECDH failed"); return -1; }

    // RFC 5656 section 4: H = hash(V_C, V_S, I_C, I_S, K_S, Q_C, Q_S, K)
    mbedtls_sha256_context h; mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h,0);
    hash_str(&h,(unsigned char*)c->v_client,(int)strlen(c->v_client));
    hash_str(&h,(unsigned char*)VERSION_STR,(int)strlen(VERSION_STR));
    hash_str(&h,c->kexinit_client,c->kexinit_client_len);
    hash_str(&h,c->kexinit_server,c->kexinit_server_len);
    hash_str(&h,s_hostkey_blob,s_hostkey_blob_len);
    hash_str(&h,qc_copy,65); hash_str(&h,qs,65);
    hash_str(&h,K,Klen);
    mbedtls_sha256_finish(&h,H); mbedtls_sha256_free(&h);
    if(!c->have_session_id){ memcpy(c->session_id,H,32); c->have_session_id=1; }

    // The signature is over SHA-256 of H, not H itself.
    unsigned char HH[32]; mbedtls_sha256(H,32,HH,0);
    unsigned char der[80]; size_t derlen=0;
    if(mbedtls_pk_sign(&s_hostkey,MBEDTLS_MD_SHA256,HH,32,der,sizeof der,&derlen,rng,NULL)){ ESP_LOGW(TAG,"host key sign failed"); return -1; }
    const unsigned char *sr,*ss; int srl,ssl;
    if(der_sig_split(der,(int)derlen,&sr,&srl,&ss,&ssl)) return -1;
    unsigned char sigmem[96]; buf_t sig; b_init(&sig,sigmem,sizeof sigmem); b_str(&sig,sr,srl); b_str(&sig,ss,ssl);

    buf_t b; out_begin(c,&b,MSG_KEX_ECDH_REPLY);
    b_str(&b,s_hostkey_blob,s_hostkey_blob_len); b_str(&b,qs,65);
    unsigned char sigblob[128]; buf_t sb; b_init(&sb,sigblob,sizeof sigblob); b_cstr(&sb,"ecdsa-sha2-nistp256"); b_str(&sb,sig.p,sig.len);
    b_str(&b,sb.p,sb.len);
    if(out_send(c,&b)) return -1;
    out_begin(c,&b,MSG_NEWKEYS); if(out_send(c,&b)) return -1;

    if(read_packet(c,&plen)) return -1;
    if(c->inbuf[0]!=MSG_NEWKEYS){ ESP_LOGW(TAG,"expected NEWKEYS"); return -1; }

    unsigned char k[32];
    derive_key(K,Klen,H,'A',c->session_id,k); memcpy(c->iv_in,k,16);
    derive_key(K,Klen,H,'B',c->session_id,k); memcpy(c->iv_out,k,16);
    derive_key(K,Klen,H,'C',c->session_id,k); mbedtls_aes_init(&c->aes_in);  mbedtls_aes_setkey_enc(&c->aes_in,k,128);
    derive_key(K,Klen,H,'D',c->session_id,k); mbedtls_aes_init(&c->aes_out); mbedtls_aes_setkey_enc(&c->aes_out,k,128);
    derive_key(K,Klen,H,'E',c->session_id,c->mac_in);
    derive_key(K,Klen,H,'F',c->session_id,c->mac_out);
    c->off_in=c->off_out=0;
    int first=!c->encrypted; c->encrypted=1;
    free(c->kexinit_client); c->kexinit_client=NULL; free(c->kexinit_server); c->kexinit_server=NULL;
    if(first){
        // RFC 8308 server-sig-algs: without it OpenSSH refuses to sign with RSA keys.
        buf_t b; out_begin(c,&b,MSG_EXT_INFO); b_u32(&b,1); b_cstr(&b,"server-sig-algs");
        b_cstr(&b,"ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-512,rsa-sha2-256,ssh-rsa");
        if(out_send(c,&b)) return -1;
    }
    return 0;
}

static int key_in_file(const char* path,const unsigned char* blob,int bloblen){
    if(!path) return 0;
    FILE* f=fopen(path,"rb");
    if(!f) return 0;
    char line[1200]; int ok=0; unsigned char* dec=malloc(900);
    if(!dec){ fclose(f); return 0; }
    while(!ok && fgets(line,sizeof line,f)){
        char* p=line; while(*p==' '||*p=='\t') p++;
        if(*p=='#'||*p=='\n'||*p=='\r'||*p==0) continue;
        char* sp=strpbrk(p," \t"); if(!sp) continue;
        char* b64=sp+1; while(*b64==' '||*b64=='\t') b64++;
        char* e=b64; while(*e && *e!=' '&&*e!='\t'&&*e!='\n'&&*e!='\r') e++;
        size_t n=0;
        if(mbedtls_base64_decode(dec,900,&n,(unsigned char*)b64,(size_t)(e-b64))==0 && (int)n==bloblen && memcmp(dec,blob,n)==0) ok=1;
    }
    free(dec); fclose(f);
    return ok;
}
static int key_is_authorized(const unsigned char* blob,int bloblen){
    return key_in_file(s_cfg->own_pubkey,blob,bloblen) || key_in_file(s_cfg->authorized_keys,blob,bloblen);
}

static int verify_signature(const unsigned char* blob,int bloblen,const unsigned char* sig,int siglen,const unsigned char* data,int datalen){
    rd_t kb; r_init(&kb,blob,bloblen); int tn; const unsigned char* type=r_str(&kb,&tn);
    rd_t sb; r_init(&sb,sig,siglen); int an; const unsigned char* alg=r_str(&sb,&an); int sn; const unsigned char* s=r_str(&sb,&sn);
    if(kb.bad||sb.bad) return 0;
    if(str_is(type,tn,"ssh-ed25519")){
        if(!str_is(alg,an,"ssh-ed25519")||sn!=64) return 0;
        int pn; const unsigned char* pk=r_str(&kb,&pn); if(kb.bad||pn!=32) return 0;
        unsigned char* sm=malloc(64+datalen); unsigned char* m=malloc(64+datalen);
        if(!sm||!m){ free(sm); free(m); return 0; }
        memcpy(sm,s,64); memcpy(sm+64,data,datalen);
        unsigned long long mlen=0;
        int rc=crypto_sign_open(m,&mlen,sm,64+(unsigned long long)datalen,pk);
        free(sm); free(m);
        return rc==0;
    }
    if(str_is(type,tn,"ecdsa-sha2-nistp256")){
        if(!str_is(alg,an,"ecdsa-sha2-nistp256")) return 0;
        int cn; const unsigned char* curve=r_str(&kb,&cn); int qn; const unsigned char* q=r_str(&kb,&qn);
        if(kb.bad||!str_is(curve,cn,"nistp256")||qn!=65) return 0;
        rd_t rs; r_init(&rs,s,sn); int rl,sl; const unsigned char* rb=r_str(&rs,&rl); const unsigned char* sbb=r_str(&rs,&sl);
        if(rs.bad) return 0;
        unsigned char hash[32]; mbedtls_sha256(data,datalen,hash,0);
        mbedtls_ecp_group grp; mbedtls_ecp_point Q; mbedtls_mpi r,ss;
        mbedtls_ecp_group_init(&grp); mbedtls_ecp_point_init(&Q); mbedtls_mpi_init(&r); mbedtls_mpi_init(&ss);
        int ok=0;
        if(mbedtls_ecp_group_load(&grp,MBEDTLS_ECP_DP_SECP256R1)==0 && mbedtls_ecp_point_read_binary(&grp,&Q,q,65)==0 &&
           mbedtls_mpi_read_binary(&r,rb,rl)==0 && mbedtls_mpi_read_binary(&ss,sbb,sl)==0)
            ok = mbedtls_ecdsa_verify(&grp,hash,32,&Q,&r,&ss)==0;
        mbedtls_ecp_group_free(&grp); mbedtls_ecp_point_free(&Q); mbedtls_mpi_free(&r); mbedtls_mpi_free(&ss);
        return ok;
    }
    if(str_is(type,tn,"ssh-rsa")){
        mbedtls_md_type_t md; unsigned hl; unsigned char hash[64];
        if(str_is(alg,an,"rsa-sha2-256")){ md=MBEDTLS_MD_SHA256; hl=32; mbedtls_sha256(data,datalen,hash,0); }
        else if(str_is(alg,an,"rsa-sha2-512")){ md=MBEDTLS_MD_SHA512; hl=64; mbedtls_sha512(data,datalen,hash,0); }
        else if(str_is(alg,an,"ssh-rsa")){ md=MBEDTLS_MD_SHA1; hl=20; mbedtls_sha1(data,datalen,hash); }
        else return 0;
        int en,nn; const unsigned char* e=r_str(&kb,&en); const unsigned char* n=r_str(&kb,&nn);
        if(kb.bad) return 0;
        while(nn>0&&n[0]==0){ n++; nn--; }
        mbedtls_rsa_context rsa; mbedtls_rsa_init(&rsa);
        int ok=0;
        if(mbedtls_rsa_import_raw(&rsa,n,nn,NULL,0,NULL,0,NULL,0,e,en)==0 && mbedtls_rsa_complete(&rsa)==0){
            int klen=(int)mbedtls_rsa_get_len(&rsa);
            unsigned char* sigp=calloc(1,klen);
            if(sigp && sn<=klen){ memcpy(sigp+(klen-sn),s,sn); ok = mbedtls_rsa_pkcs1_verify(&rsa,md,hl,hash,sigp)==0; }
            free(sigp);
        }
        mbedtls_rsa_free(&rsa);
        return ok;
    }
    return 0;
}

static int send_auth_failure(conn_t* c){ buf_t b; out_begin(c,&b,MSG_USERAUTH_FAILURE); b_cstr(&b,"publickey"); b_u8(&b,0); return out_send(c,&b); }

static int handle_userauth(conn_t* c,int len){
    rd_t r; r_init(&r,c->inbuf,len); r_u8(&r);
    int un,sn,mn; const unsigned char* user=r_str(&r,&un); const unsigned char* svc=r_str(&r,&sn); const unsigned char* method=r_str(&r,&mn);
    if(r.bad||!str_is(svc,sn,"ssh-connection")) return send_auth_failure(c);
    if(!str_is(method,mn,"publickey")) return send_auth_failure(c);
    int has_sig=r_u8(&r); int an,bn; const unsigned char* alg=r_str(&r,&an); const unsigned char* blob=r_str(&r,&bn);
    if(r.bad) return send_auth_failure(c);
    if(!key_is_authorized(blob,bn)){ ESP_LOGI(TAG,"key not in authorized_keys (%.*s)",an,(const char*)alg); return send_auth_failure(c); }
    if(!has_sig){
        buf_t b; out_begin(c,&b,MSG_USERAUTH_PK_OK); b_str(&b,alg,an); b_str(&b,blob,bn); return out_send(c,&b);
    }
    int gn; const unsigned char* sig=r_str(&r,&gn); if(r.bad) return send_auth_failure(c);
    // RFC 4252 section 7: the signature covers the session id and the request up to the key blob.
    int dlen=4+32+(r.pos-gn-4);
    unsigned char* data=malloc(dlen); if(!data) return -1;
    buf_t d; b_init(&d,data,dlen); b_str(&d,c->session_id,32); b_bytes(&d,c->inbuf,r.pos-gn-4);
    int ok=verify_signature(blob,bn,sig,gn,data,d.len);
    free(data);
    if(!ok){ ESP_LOGW(TAG,"bad signature for %.*s",un,(const char*)user); return send_auth_failure(c); }
    ESP_LOGI(TAG,"%.*s logged in with %.*s",un,(const char*)user,an,(const char*)alg);
    c->authed=1;
    buf_t b; out_begin(c,&b,MSG_USERAUTH_SUCCESS); return out_send(c,&b);
}

static int chan_send_data(conn_t* c,const unsigned char* d,int n);

static int sftp_send_cb(void* ctx,const unsigned char* pkt,int len){
    conn_t* c=ctx;
    unsigned char hdr[4]={(unsigned)len>>24,(unsigned)len>>16,(unsigned)len>>8,(unsigned)len};
    if(len+4<=(int)c->client_maxpkt-64){
        unsigned char* tmp=malloc(len+4); if(!tmp) return -1;
        memcpy(tmp,hdr,4); memcpy(tmp+4,pkt,len);
        int rc=chan_send_data(c,tmp,len+4); free(tmp); return rc;
    }
    if(chan_send_data(c,hdr,4)) return -1;
    return chan_send_data(c,pkt,len);
}

static int sftp_drain(conn_t* c){
    for(;;){
        if(c->sftp_in_len<4) return 0;
        unsigned char* p=c->sftp_in;
        unsigned L=((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];
        if(L>(unsigned)MAX_PACKET){ ESP_LOGW(TAG,"sftp packet too long"); return -1; }
        if(c->sftp_in_len<(int)L+4) return 0;
        if(sftpd_handle(c->sftp,p+4,(int)L,sftp_send_cb,c)) return -1;
        memmove(p,p+4+L,c->sftp_in_len-4-(int)L); c->sftp_in_len-=4+(int)L;
    }
}

static int handle_connection_packet(conn_t* c,int len,int allow_sftp);

static int chan_send_data(conn_t* c,const unsigned char* d,int n){
    while(n>0){
        while(c->client_window==0){
            int plen; if(read_packet(c,&plen)) return -1;
            if(handle_connection_packet(c,plen,0)) return -1;
            if(c->closing) return -1;
        }
        int chunk=n; if((unsigned)chunk>c->client_window) chunk=(int)c->client_window;
        if((unsigned)chunk>c->client_maxpkt-16) chunk=(int)c->client_maxpkt-16;
        buf_t b; out_begin(c,&b,MSG_CHANNEL_DATA); b_u32(&b,(unsigned)c->chan_client_id); b_str(&b,d,chunk);
        if(out_send(c,&b)) return -1;
        c->client_window-=(unsigned)chunk; d+=chunk; n-=chunk;
    }
    return 0;
}

static int handle_connection_packet(conn_t* c,int len,int allow_sftp){
    rd_t r; r_init(&r,c->inbuf,len); int t=r_u8(&r);
    buf_t b;
    switch(t){
        case MSG_IGNORE: case MSG_DEBUG: case MSG_UNIMPLEMENTED: case MSG_EXT_INFO: return 0;
        case MSG_DISCONNECT: c->closing=1; return 0;
        case MSG_KEXINIT: return do_kex(c,len);
        case MSG_GLOBAL_REQUEST: { int n; r_str(&r,&n); int want=r_u8(&r);
            if(want){ out_begin(c,&b,MSG_REQUEST_FAILURE); return out_send(c,&b); } return 0; }
        case MSG_CHANNEL_OPEN: {
            int n; const unsigned char* type=r_str(&r,&n); unsigned sender=r_u32(&r), win=r_u32(&r), maxp=r_u32(&r);
            if(r.bad) return -1;
            if(c->chan_open || !str_is(type,n,"session")){
                out_begin(c,&b,MSG_CHANNEL_OPEN_FAILURE); b_u32(&b,sender); b_u32(&b,1); b_cstr(&b,"one session only"); b_cstr(&b,""); return out_send(c,&b);
            }
            c->chan_open=1; c->chan_client_id=(int)sender; c->client_window=win; c->client_maxpkt=maxp<1024?1024:maxp; c->our_window=OUR_WINDOW;
            out_begin(c,&b,MSG_CHANNEL_OPEN_CONFIRMATION); b_u32(&b,sender); b_u32(&b,0); b_u32(&b,OUR_WINDOW); b_u32(&b,32768);
            return out_send(c,&b);
        }
        case MSG_CHANNEL_REQUEST: {
            r_u32(&r); int n; const unsigned char* type=r_str(&r,&n); int want=r_u8(&r);
            int ok=0;
            if(str_is(type,n,"subsystem")){ int sn; const unsigned char* sub=r_str(&r,&sn);
                if(!r.bad && str_is(sub,sn,"sftp") && !c->sftp_started){ c->sftp=sftpd_new(s_cfg->root); c->sftp_started=(c->sftp!=NULL); ok=c->sftp_started; } }
            if(want){ out_begin(c,&b,ok?MSG_CHANNEL_SUCCESS:MSG_CHANNEL_FAILURE); b_u32(&b,(unsigned)c->chan_client_id); return out_send(c,&b); }
            return 0;
        }
        case MSG_CHANNEL_WINDOW_ADJUST: { r_u32(&r); unsigned add=r_u32(&r); c->client_window+=add; return 0; }
        case MSG_CHANNEL_DATA: {
            r_u32(&r); int n; const unsigned char* d=r_str(&r,&n); if(r.bad) return -1;
            if(!c->sftp_started) return 0;
            if(c->sftp_in_len+n>MAX_PACKET*2){ ESP_LOGW(TAG,"sftp input overflow"); return -1; }
            memcpy(c->sftp_in+c->sftp_in_len,d,n); c->sftp_in_len+=n;
            c->our_window-=(unsigned)n;
            if(c->our_window<OUR_WINDOW/2){ out_begin(c,&b,MSG_CHANNEL_WINDOW_ADJUST); b_u32(&b,(unsigned)c->chan_client_id); b_u32(&b,OUR_WINDOW); if(out_send(c,&b)) return -1; c->our_window+=OUR_WINDOW; }
            return allow_sftp ? sftp_drain(c) : 0;
        }
        case MSG_CHANNEL_EOF: case MSG_CHANNEL_CLOSE: {
            // ssh waits for the server's own EOF and CLOSE before it exits.
            if(!c->chan_closed){
                c->chan_closed=1;
                out_begin(c,&b,MSG_CHANNEL_EOF);   b_u32(&b,(unsigned)c->chan_client_id); if(out_send(c,&b)) return -1;
                out_begin(c,&b,MSG_CHANNEL_CLOSE); b_u32(&b,(unsigned)c->chan_client_id); if(out_send(c,&b)) return -1;
            }
            if(t==MSG_CHANNEL_CLOSE) c->closing=1;
            return 0;
        }
        default:
            out_begin(c,&b,MSG_UNIMPLEMENTED); b_u32(&b,c->seq_in-1); return out_send(c,&b);
    }
}

static void serve_client(int sock){
    conn_t* c=calloc(1,sizeof *c); if(!c) return;
    c->sock=sock;
    c->inbuf=malloc(MAX_PACKET); c->outbuf=malloc(MAX_PACKET); c->sftp_in=malloc(MAX_PACKET*2);
    if(!c->inbuf||!c->outbuf||!c->sftp_in){ ESP_LOGE(TAG,"out of memory for a session"); goto out; }
    struct timeval tv={ .tv_sec=1, .tv_usec=0 }; setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
    int one=1; setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);

    { char v[64]; int n=snprintf(v,sizeof v,"%s\r\n",VERSION_STR); if(write_full(c,(unsigned char*)v,n)) goto out; }
    for(int lines=0;lines<20;lines++){
        int i=0; char ch;
        while(i<(int)sizeof c->v_client-1){ if(read_full(c,(unsigned char*)&ch,1)) goto out; if(ch=='\n') break; c->v_client[i++]=ch; }
        c->v_client[i]=0; while(i>0&&(c->v_client[i-1]=='\r')) c->v_client[--i]=0;
        if(strncmp(c->v_client,"SSH-",4)==0) break;
    }
    if(strncmp(c->v_client,"SSH-2.0",7)!=0){ ESP_LOGW(TAG,"not an SSH 2 client: %s",c->v_client); goto out; }
    ESP_LOGI(TAG,"client %s",c->v_client);

    if(send_kexinit(c)) goto out;
    int plen;
    if(read_packet(c,&plen)) goto out;
    if(c->inbuf[0]!=MSG_KEXINIT || do_kex(c,plen)) goto out;

    if(read_packet(c,&plen)) goto out;
    { rd_t r; r_init(&r,c->inbuf,plen); int t=r_u8(&r); int n; const unsigned char* s=r_str(&r,&n);
      if(t==MSG_EXT_INFO){ if(read_packet(c,&plen)) goto out; r_init(&r,c->inbuf,plen); t=r_u8(&r); s=r_str(&r,&n); }
      if(t!=MSG_SERVICE_REQUEST||!str_is(s,n,"ssh-userauth")) goto out;
      buf_t b; out_begin(c,&b,MSG_SERVICE_ACCEPT); b_cstr(&b,"ssh-userauth"); if(out_send(c,&b)) goto out; }
    for(int tries=0;!c->authed && tries<12;tries++){
        if(read_packet(c,&plen)) goto out;
        int t=c->inbuf[0];
        if(t==MSG_USERAUTH_REQUEST){ if(handle_userauth(c,plen)) goto out; }
        else if(t==MSG_IGNORE||t==MSG_DEBUG||t==MSG_EXT_INFO) tries--;
        else if(t==MSG_DISCONNECT) goto out;
        else if(t==MSG_KEXINIT){ if(do_kex(c,plen)) goto out; }
        else goto out;
    }
    if(!c->authed){ send_disconnect(c,"too many authentication attempts"); goto out; }

    s_connected=1;
    while(!c->closing && !s_stop){
        if(read_packet(c,&plen)) break;
        if(handle_connection_packet(c,plen,1)) break;
    }
    if(s_stop && !c->closing) send_disconnect(c,"the device stopped serving");
out:
    s_connected=0;
    if(c->sftp) sftpd_free(c->sftp);
    if(c->encrypted){ mbedtls_aes_free(&c->aes_in); mbedtls_aes_free(&c->aes_out); }
    free(c->kexinit_client); free(c->kexinit_server);
    free(c->inbuf); free(c->outbuf); free(c->sftp_in); free(c);
}

int oyobyok_sshd_run(const oyobyok_sshd_cfg_t* cfg,char* err,int errlen){
    if(err&&errlen) err[0]=0;
    s_cfg=cfg; s_stop=0; s_files=0; s_connected=0;
    if(hostkey_load_or_make(cfg->host_key_path,err,errlen)) return -1;
    int ls=socket(AF_INET,SOCK_STREAM,0);
    if(ls<0){ snprintf(err,errlen,"no socket"); mbedtls_pk_free(&s_hostkey); return -1; }
    int one=1; setsockopt(ls,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY); a.sin_port=htons(cfg->port?cfg->port:22);
    if(bind(ls,(struct sockaddr*)&a,sizeof a)<0 || listen(ls,1)<0){ snprintf(err,errlen,"port %d is busy",cfg->port?cfg->port:22); close(ls); mbedtls_pk_free(&s_hostkey); return -1; }
    struct timeval tv={ .tv_sec=1, .tv_usec=0 }; setsockopt(ls,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
    s_listen=ls;
    ESP_LOGI(TAG,"serving %s on port %d",cfg->root,cfg->port?cfg->port:22);
    while(!s_stop){
        struct sockaddr_in peer; socklen_t pl=sizeof peer;
        int cs=accept(ls,(struct sockaddr*)&peer,&pl);
        if(cs<0){ if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR) continue; break; }
        ESP_LOGI(TAG,"client from %s",inet_ntoa(peer.sin_addr));
        s_client=cs;
        serve_client(cs);
        s_client=-1;
        close(cs);
        ESP_LOGI(TAG,"client gone");
    }
    s_listen=-1; close(ls);
    mbedtls_pk_free(&s_hostkey);
    return 0;
}

void oyobyok_sshd_stop(void){
    s_stop=1;
    int cs=s_client; if(cs>=0) shutdown(cs,SHUT_RDWR);
    int ls=s_listen; if(ls>=0) shutdown(ls,SHUT_RDWR);
}
