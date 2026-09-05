// Git engine on libgit2 with the libssh2 transport. Implements shared/git_engine.h.
// Configuration comes from <cfg>/remotes.conf; repositories live under <root>/<project>.
#include "git_engine.h"
#include <git2.h>
#include <git2/sys/alloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG="oyobyok_git";

// libgit2's working memory goes to PSRAM so internal RAM stays free for WiFi and LWIP buffers.
static void* psram_gmalloc(size_t n,const char* file,int line){ (void)file;(void)line;
    void* p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); return p?p:malloc(n); }
static void* psram_grealloc(void* ptr,size_t n,const char* file,int line){ (void)file;(void)line;
    void* p=heap_caps_realloc(ptr,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); return p?p:realloc(ptr,n); }
static void  psram_gfree(void* ptr){ free(ptr); }

// Pack indexing and checkout are CPU bound; yield now and then so the idle task keeps the watchdog fed.
static int  git_xfer_cb(const git_indexer_progress* s,void* p){ (void)s;(void)p; static unsigned n; if((++n & 15)==0) vTaskDelay(1); return 0; }
static void git_ckout_cb(const char* path,size_t c,size_t t,void* p){ (void)path;(void)c;(void)t;(void)p; static unsigned n; if((++n & 7)==0) vTaskDelay(1); }
static int  git_pushxfer_cb(unsigned cur,unsigned tot,size_t b,void* p){ (void)cur;(void)tot;(void)b;(void)p; static unsigned n; if((++n & 15)==0) vTaskDelay(1); return 0; }

static char CFG[256]="/sdcard/git";
static char ROOT[256]="/sdcard/projects";
static char ID_NAME[64]="OYOBYOK";
static char ID_EMAIL[96]="oyobyok@localhost";

static GitResult fail(const char* m){ GitResult g; memset(&g,0,sizeof g); snprintf(g.msg,sizeof g.msg,"%s",m); return g; }
static GitResult fail_git(const char* m){
    GitResult g; memset(&g,0,sizeof g);
    const git_error* e=git_error_last();
    if(e && e->message && e->message[0]){
        snprintf(g.msg,sizeof g.msg,"%s: %.200s",m,e->message);
        ESP_LOGE(TAG,"%s: %s",m,e->message);
    } else { snprintf(g.msg,sizeof g.msg,"%s",m); ESP_LOGE(TAG,"%s",m); }
    return g;
}

static char* trim(char* s){ while(*s==' '||*s=='\t')s++; char* e=s+strlen(s);
    while(e>s&&(e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n')) *--e=0;
    return s; }

// Read the [identity] section of remotes.conf, if present.
static void load_identity(void){
    char path[320]; snprintf(path,sizeof path,"%s/remotes.conf",CFG);
    FILE* f=fopen(path,"r"); if(!f) return;
    char line[512]; int in=0;
    while(fgets(line,sizeof line,f)){
        char* s=trim(line); if(!*s||*s=='#'||*s==';') continue;
        if(*s=='['){ char* e=strchr(s,']'); if(e){*e=0; in=!strcmp(s+1,"identity");} continue; }
        if(!in) continue;
        char* sep=s; while(*sep && *sep!=':' && *sep!='=') sep++;
        if(!*sep) continue;
        *sep=0; char* k=trim(s); char* v=trim(sep+1);
        if(!strcmp(k,"name") && *v) snprintf(ID_NAME,sizeof ID_NAME,"%s",v);
        else if(!strcmp(k,"email") && *v) snprintf(ID_EMAIL,sizeof ID_EMAIL,"%s",v);
    }
    fclose(f);
}

void git_configure(const char* cfg,const char* root){
    if(cfg) snprintf(CFG,sizeof CFG,"%s",cfg);
    if(root) snprintf(ROOT,sizeof ROOT,"%s",root);
    load_identity();
    static int inited=0; if(!inited){
        git_libgit2_init();
        static const git_allocator psram_alloc = { psram_gmalloc, psram_grealloc, psram_gfree };
        git_libgit2_opts(GIT_OPT_SET_ALLOCATOR, &psram_alloc);
        git_libgit2_opts(GIT_OPT_SET_CACHE_MAX_SIZE, (ssize_t)(256*1024));
        git_libgit2_opts(GIT_OPT_SET_MWINDOW_SIZE, (size_t)(128*1024));
        git_libgit2_opts(GIT_OPT_SET_MWINDOW_MAPPED_LIMIT, (size_t)(512*1024));
        git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);    // no users on the device
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_SYSTEM, "");
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_GLOBAL, "");
        git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, GIT_CONFIG_LEVEL_XDG, "");
        git_libgit2_opts(GIT_OPT_SET_SERVER_CONNECT_TIMEOUT, 15000);
        git_libgit2_opts(GIT_OPT_SET_SERVER_TIMEOUT, 30000);
        inited=1;
    }
}

GitRemote git_lookup(const char* project){
    GitRemote r; memset(&r,0,sizeof r); snprintf(r.branch,sizeof r.branch,"main");
    snprintf(r.key,sizeof r.key,"keys/oyobyok_rsa");
    char path[320]; snprintf(path,sizeof path,"%s/remotes.conf",CFG);
    FILE* f=fopen(path,"r"); if(!f) return r;
    char line[512]; int in=0;
    while(fgets(line,sizeof line,f)){
        char* s=trim(line); if(!*s||*s=='#'||*s==';') continue;
        if(*s=='['){ char* e=strchr(s,']'); if(e){*e=0; in=!strcmp(s+1,project);} continue; }
        if(!in) continue;
        // "key: value" or "key = value"; split on the first separator so URLs keep their colons.
        char* sep=s; while(*sep && *sep!=':' && *sep!='=') sep++;
        if(!*sep) continue;
        *sep=0; char* k=trim(s); char* v=trim(sep+1);
        if(!strcmp(k,"remote")) snprintf(r.remote,sizeof r.remote,"%s",v);
        else if(!strcmp(k,"branch")) snprintf(r.branch,sizeof r.branch,"%s",v);
        else if(!strcmp(k,"key")) snprintf(r.key,sizeof r.key,"%s",v);
    }
    fclose(f); r.found=r.remote[0]!=0; return r;
}

int git_list_repos(char names[][64], int max){
    char path[320]; snprintf(path,sizeof path,"%s/remotes.conf",CFG);
    FILE* f=fopen(path,"r"); if(!f) return 0;
    char line[512]; int n=0;
    while(n<max && fgets(line,sizeof line,f)){
        char* s=trim(line); if(!*s||*s=='#'||*s==';') continue;
        if(*s=='['){ char* e=strchr(s,']'); if(e){ *e=0; if(strcmp(s+1,"identity")){ snprintf(names[n],64,"%.63s",s+1); n++; } } }
    }
    fclose(f); return n;
}

typedef struct { char pub[520], prv[520]; } Cred;
static int cred_cb(git_credential** out,const char* url,const char* user,unsigned int allowed,void* payload){
    (void)url; Cred* c=payload;
    if(allowed & GIT_CREDENTIAL_SSH_KEY)
        return git_credential_ssh_key_new(out, user?user:"git", c->pub, c->prv, "");
    return GIT_PASSTHROUGH;
}
static void keypaths(const GitRemote* r,Cred* c){
    snprintf(c->prv,sizeof c->prv,"%s/%s",CFG,r->key);
    snprintf(c->pub,sizeof c->pub,"%s/%s.pub",CFG,r->key);
}
// Trust the server host key on first use; there is no known_hosts for libgit2 on the device.
static int cert_check_cb(git_cert* cert,int valid,const char* host,void* payload){
    (void)cert;(void)valid;(void)host;(void)payload; return 0;
}
static git_signature* signature(void){ git_signature* s=NULL; git_signature_now(&s,ID_NAME,ID_EMAIL); return s; }

static int open_or_init(const char* project,const GitRemote* r,git_repository** repo){
    char dir[320]; snprintf(dir,sizeof dir,"%s/%s",ROOT,project);
    if(git_repository_open(repo,dir)==0) return 0;
    git_repository_init_options o=GIT_REPOSITORY_INIT_OPTIONS_INIT;
    o.flags=GIT_REPOSITORY_INIT_MKPATH; o.initial_head=r->branch;
    if(git_repository_init_ext(repo,dir,&o)!=0) return -1;
    git_remote* rem=NULL;
    if(git_remote_create(&rem,*repo,"origin",r->remote)==0) git_remote_free(rem);
    return 0;
}

GitResult git_ensure_repo(const char* project){
    GitRemote r=git_lookup(project); if(!r.found) return fail("No remote for project");
    git_repository* repo=NULL; if(open_or_init(project,&r,&repo)) return fail_git("init failed");
    git_repository_free(repo);
    GitResult g; memset(&g,0,sizeof g); g.ok=1; snprintf(g.msg,sizeof g.msg,"repo ready (%s)",r.branch); return g;
}

// Rebase the branch at bref onto `onto`, one commit per replayed patch. Conflicted files are kept as
// checked out, markers and all, and staged as the resolution: the device never blocks on a conflict,
// the markers get sorted out on a computer later. Returns 0 on success, -1 on error (rebase aborted).
static int do_rebase_onto(git_repository* repo,const git_oid* onto,const char* bref){
    git_reference* rf=NULL; if(git_reference_lookup(&rf,repo,bref)) return -1;
    git_annotated_commit *head_ac=NULL,*onto_ac=NULL;
    int arc=git_annotated_commit_from_ref(&head_ac,repo,rf); git_reference_free(rf);
    if(arc) return -1;
    if(git_annotated_commit_lookup(&onto_ac,repo,onto)){ git_annotated_commit_free(head_ac); return -1; }
    git_rebase* rb=NULL; git_rebase_options ro=GIT_REBASE_OPTIONS_INIT;
    // FATFS cannot remove a non-empty directory; skip such directories instead of failing the rebase.
    ro.checkout_options.checkout_strategy=GIT_CHECKOUT_SAFE|GIT_CHECKOUT_SKIP_LOCKED_DIRECTORIES;
    int rc=git_rebase_init(&rb,repo,head_ac,onto_ac,onto_ac,&ro);
    git_annotated_commit_free(head_ac); git_annotated_commit_free(onto_ac);
    if(rc||!rb) return -1;
    git_signature* sig=signature();
    int result=0; git_rebase_operation* op=NULL;
    while((rc=git_rebase_next(&op,rb))==0){
        git_index* idx=NULL; git_repository_index(&idx,repo);
        if(idx && git_index_has_conflicts(idx)){
            git_index_conflict_iterator* it=NULL;
            if(git_index_conflict_iterator_new(&it,idx)==0){
                const git_index_entry *anc,*our,*their; char paths[16][256]; int np=0;
                while(git_index_conflict_next(&anc,&our,&their,it)==0 && np<16){
                    const git_index_entry* e=our?our:(their?their:anc);
                    if(e) snprintf(paths[np++],256,"%s",e->path);
                }
                git_index_conflict_iterator_free(it);
                for(int i=0;i<np;i++){
                    char full[400]; snprintf(full,sizeof full,"%s/%s",git_repository_workdir(repo),paths[i]);
                    git_index_conflict_remove(idx,paths[i]);
                    if(access(full,F_OK)==0) git_index_add_bypath(idx,paths[i]); else git_index_remove_bypath(idx,paths[i]);
                    ESP_LOGW(TAG,"conflict in %s, kept with markers",paths[i]);
                }
                git_index_write(idx);
            }
            if(git_index_has_conflicts(idx)){ git_index_free(idx); result=-1; break; }
        }
        if(idx) git_index_free(idx);
        git_oid cid; rc=git_rebase_commit(&cid,rb,NULL,sig,NULL,NULL);
        if(rc==GIT_EAPPLIED) continue;
        if(rc){ result=-1; break; }
    }
    if(result==0 && rc!=GIT_ITEROVER) result=-1;
    if(result==0){ if(git_rebase_finish(rb,sig)) result=-1; }
    else          { git_rebase_abort(rb); }
    git_signature_free(sig); git_rebase_free(rb);
    return result;
}

GitResult git_sync(const char* project){
    GitRemote r=git_lookup(project); if(!r.found) return fail("No remote configured");
    git_repository* repo=NULL; if(open_or_init(project,&r,&repo)) return fail_git("open failed");
    GitResult g=fail("Sync failed"); Cred cred; keypaths(&r,&cred);
    char bref[96];  snprintf(bref,sizeof bref,"refs/heads/%s",r.branch);
    char rref[160]; snprintf(rref,sizeof rref,"refs/remotes/origin/%s",r.branch);

    // Recover from an interrupted earlier sync: a leftover rebase state or a detached HEAD.
    if(git_repository_state(repo)!=GIT_REPOSITORY_STATE_NONE) git_repository_state_cleanup(repo);
    if(git_repository_head_detached(repo)){
        git_oid h; if(!git_reference_name_to_id(&h,repo,"HEAD")){
            git_reference* nb=NULL; if(git_reference_create(&nb,repo,bref,&h,1,"reattach")==0) git_reference_free(nb);
            git_repository_set_head(repo,bref);
        }
    }

    // 1. Commit the working tree if it differs from HEAD.
    git_oid head_before; int has_head_before=!git_reference_name_to_id(&head_before,repo,"HEAD");
    int made_commit=0;
    {
        git_index* idx=NULL; if(git_repository_index(&idx,repo)){ g=fail_git("index open failed"); goto done; }
        if(git_index_add_all(idx,NULL,GIT_INDEX_ADD_DEFAULT,NULL,NULL)){ git_index_free(idx); g=fail_git("add_all failed"); goto done; }
        extern void git_index__drop_tree_cache(git_index*); git_index__drop_tree_cache(idx);
        git_oid tree_oid;
        if(git_index_write(idx)||git_index_write_tree(&tree_oid,idx)){ git_index_free(idx); g=fail_git("write tree failed"); goto done; }
        git_index_free(idx);
        int differs=1;
        if(has_head_before){ git_commit* hc=NULL; if(git_commit_lookup(&hc,repo,&head_before)==0){
            differs=!!git_oid_cmp(git_commit_tree_id(hc),&tree_oid); git_commit_free(hc); } }
        if(differs){
            git_tree* tree=NULL; if(git_tree_lookup(&tree,repo,&tree_oid)){ g=fail_git("tree lookup failed"); goto done; }
            for(int i=0;i<50 && time(NULL)<1700000000;i++) vTaskDelay(pdMS_TO_TICKS(100));   // wait for SNTP
            char msg[96]; { time_t t=time(NULL); struct tm tmv; localtime_r(&t,&tmv); char ds[32];
                strftime(ds,sizeof ds,"%Y-%m-%d",&tmv); snprintf(msg,sizeof msg,"oyobyok: update from device on %s",ds); }
            git_signature* sig=signature();
            git_commit* parent=NULL; const git_commit* parents[1]; int np=0;
            if(has_head_before && git_commit_lookup(&parent,repo,&head_before)==0){ parents[0]=parent; np=1; }
            git_oid cid; int rc=git_commit_create(&cid,repo,bref,sig,sig,NULL,msg,tree,np,parents);
            if(parent) git_commit_free(parent);
            git_signature_free(sig); git_tree_free(tree);
            if(rc){ g=fail_git("commit failed"); goto done; }
            made_commit=1;
        }
    }

    // 2. Fetch the full history; the rebase needs the real merge base.
    {
        git_remote* rem=NULL; if(git_remote_lookup(&rem,repo,"origin")){ g=fail_git("no origin"); goto rollback; }
        git_fetch_options fo=GIT_FETCH_OPTIONS_INIT;
        fo.callbacks.credentials=cred_cb; fo.callbacks.payload=&cred;
        fo.callbacks.transfer_progress=git_xfer_cb; fo.callbacks.certificate_check=cert_check_cb;
        int rc=git_remote_fetch(rem,NULL,&fo,NULL); git_remote_free(rem);
        if(rc){ g=fail_git("Fetch failed (offline?)"); goto rollback; }
    }

    // 3. Reconcile local onto the remote tip so the push is a fast-forward.
    git_oid rtip; int has_rtip=!git_reference_name_to_id(&rtip,repo,rref); int pulled=0;
    if(has_rtip){
        git_oid cur; if(!git_reference_name_to_id(&cur,repo,"HEAD")){
            git_oid base; int has_base=!git_merge_base(&base,repo,&cur,&rtip);
            if(git_oid_equal(&cur,&rtip)){ }
            else if(has_base && git_oid_equal(&base,&rtip)){ }             // strictly ahead
            else if(has_base && git_oid_equal(&base,&cur)){                 // strictly behind: fast-forward
                git_checkout_options co=GIT_CHECKOUT_OPTIONS_INIT; co.checkout_strategy=GIT_CHECKOUT_SAFE|GIT_CHECKOUT_SKIP_LOCKED_DIRECTORIES; co.progress_cb=git_ckout_cb;
                git_object* tgt=NULL; if(git_object_lookup(&tgt,repo,&rtip,GIT_OBJECT_COMMIT)){ g=fail_git("Pull failed"); goto rollback; }
                int crc=git_checkout_tree(repo,tgt,&co); git_object_free(tgt);
                if(crc){ g=fail_git("Pull failed"); goto rollback; }
                git_reference* rf=NULL; if(git_reference_lookup(&rf,repo,bref)==0){ git_reference* nrf=NULL;
                    git_reference_set_target(&nrf,rf,&rtip,"ff"); if(nrf) git_reference_free(nrf); git_reference_free(rf); }
                git_repository_set_head(repo,bref);
                pulled=1;
            } else {                                                        // diverged: rebase
                if(do_rebase_onto(repo,&rtip,bref)){ g=fail_git("Reconcile failed"); goto rollback; }
            }
        }
    }

    // 4. Push, unless there is nothing to send.
    {
        git_oid cur; int has_cur=!git_reference_name_to_id(&cur,repo,"HEAD");
        if(has_cur && has_rtip && git_oid_equal(&cur,&rtip)){ g.ok=1; if(pulled) snprintf(g.msg,sizeof g.msg,"Pulled %s",r.branch); else snprintf(g.msg,sizeof g.msg,"Already in sync"); goto done; }
        git_remote* rem=NULL; if(git_remote_lookup(&rem,repo,"origin")){ g=fail_git("no origin"); goto done; }
        char refspec[160]; snprintf(refspec,sizeof refspec,"refs/heads/%s:refs/heads/%s",r.branch,r.branch);
        char* specs[1]={refspec}; git_strarray arr={specs,1};
        git_push_options po=GIT_PUSH_OPTIONS_INIT;
        po.callbacks.credentials=cred_cb; po.callbacks.payload=&cred; po.callbacks.push_transfer_progress=git_pushxfer_cb; po.callbacks.certificate_check=cert_check_cb;
        int rc=git_remote_push(rem,&arr,&po); git_remote_free(rem);
        if(rc){ g=fail_git("Push failed"); goto done; }
        g.ok=1; snprintf(g.msg,sizeof g.msg,"Synced %s",r.branch);
    }
    goto done;

rollback:
    // Undo the commit from step 1 so a failed sync leaves the repository exactly as it was.
    if(made_commit){ git_reference* rf=NULL; if(git_reference_lookup(&rf,repo,bref)==0){
        if(has_head_before){ git_reference* nrf=NULL; git_reference_set_target(&nrf,rf,&head_before,"rollback sync"); if(nrf) git_reference_free(nrf); }
        else git_reference_delete(rf);
        git_reference_free(rf); } }
done:
    git_repository_free(repo); return g;
}

GitResult git_status(const char* project){
    GitRemote r=git_lookup(project); git_repository* repo=NULL;
    char dir[320]; snprintf(dir,sizeof dir,"%s/%s",ROOT,project);
    if(git_repository_open(&repo,dir)) return fail("not a repo");
    GitResult g; memset(&g,0,sizeof g);
    git_status_list* sl=NULL; git_status_options so=GIT_STATUS_OPTIONS_INIT;
    so.flags=GIT_STATUS_OPT_INCLUDE_UNTRACKED;
    if(git_status_list_new(&sl,repo,&so)==0){ g.changed=(int)git_status_list_entrycount(sl); git_status_list_free(sl); }
    git_oid local,upstream; char ref[128]; snprintf(ref,sizeof ref,"refs/remotes/origin/%s",r.branch);
    if(!git_reference_name_to_id(&local,repo,"HEAD") && !git_reference_name_to_id(&upstream,repo,ref)){
        size_t a=0,b=0; if(git_graph_ahead_behind(&a,&b,repo,&local,&upstream)==0){ g.ahead=(int)a; g.behind=(int)b; }
    }
    g.ok=1; snprintf(g.msg,sizeof g.msg,"%s  %d ahead  %d behind  %d local",r.branch,g.ahead,g.behind,g.changed);
    git_repository_free(repo); return g;
}

GitResult git_status_fetch(const char* project){
    GitRemote r=git_lookup(project); if(!r.found) return fail("No remote configured");
    git_repository* repo=NULL; char dir[320]; snprintf(dir,sizeof dir,"%s/%s",ROOT,project);
    if(git_repository_open(&repo,dir)==0){
        Cred cred; keypaths(&r,&cred); git_remote* rem=NULL;
        if(git_remote_lookup(&rem,repo,"origin")==0){
            git_fetch_options fo=GIT_FETCH_OPTIONS_INIT;
            fo.callbacks.credentials=cred_cb; fo.callbacks.payload=&cred;
            fo.callbacks.transfer_progress=git_xfer_cb; fo.callbacks.certificate_check=cert_check_cb;
            if(git_remote_fetch(rem,NULL,&fo,NULL)) ESP_LOGW(TAG,"status: fetch failed, showing the last known remote");
            git_remote_free(rem);
        }
        git_repository_free(repo);
    }
    return git_status(project);
}

