// Desktop git engine: drives the system git binary. Same behaviour as the device engine, used by the
// emulator and the tests.
#include "git_engine.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static char CFG[512]="../device-files/git";
static char ROOT[512]="files";

void git_configure(const char* config_dir, const char* repos_root){
    if(config_dir) snprintf(CFG,sizeof CFG,"%s",config_dir);
    if(repos_root) snprintf(ROOT,sizeof ROOT,"%s",repos_root);
}

static char* trim(char* s){ while(*s==' '||*s=='\t')s++;
    char* e=s+strlen(s); while(e>s&&(e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n'))*--e=0; return s; }

GitRemote git_lookup(const char* project){
    GitRemote r; memset(&r,0,sizeof r); snprintf(r.branch,sizeof r.branch,"main");
    char path[600]; snprintf(path,sizeof path,"%s/remotes.conf",CFG);
    FILE* f=fopen(path,"r"); if(!f){ return r; }
    char line[512]; int in=0;
    while(fgets(line,sizeof line,f)){
        char* s=trim(line); if(!*s||*s=='#'||*s==';') continue;
        if(*s=='['){ char* e=strchr(s,']'); if(e){*e=0; in=!strcmp(s+1,project);} continue; }
        if(!in) continue;
        char* sep=s; while(*sep && *sep!=':' && *sep!='=') sep++;
        if(!*sep) continue; *sep=0; char* k=trim(s); char* v=trim(sep+1);
        if(!strcmp(k,"remote")) snprintf(r.remote,sizeof r.remote,"%s",v);
        else if(!strcmp(k,"branch")) snprintf(r.branch,sizeof r.branch,"%s",v);
        else if(!strcmp(k,"key")) snprintf(r.key,sizeof r.key,"%s",v);
    }
    fclose(f);
    r.found = r.remote[0]!=0;
    return r;
}

int git_list_repos(char names[][64], int max){
    char path[600]; snprintf(path,sizeof path,"%s/remotes.conf",CFG);
    FILE* f=fopen(path,"r"); if(!f) return 0;
    char line[512]; int n=0;
    while(n<max && fgets(line,sizeof line,f)){
        char* s=trim(line); if(!*s||*s=='#'||*s==';') continue;
        if(*s=='['){ char* e=strchr(s,']'); if(e){ *e=0; if(strcmp(s+1,"identity")){ snprintf(names[n],64,"%.63s",s+1); n++; } } }
    }
    fclose(f); return n;
}

static void base_cmd(char* out,size_t n,const char* project,const GitRemote* r){
    if(r->key[0]) snprintf(out,n,
        "GIT_SSH_COMMAND='ssh -i %s/%s -o StrictHostKeyChecking=accept-new' "
        "git -C '%s/%s'", CFG, r->key, ROOT, project);
    else snprintf(out,n,"git -C '%s/%s'", ROOT, project);
}

static int run(const char* cmd){ return system(cmd); }
static int run_out(const char* cmd,char* buf,size_t n){
    FILE* p=popen(cmd,"r"); if(!p){ if(n)buf[0]=0; return -1; }
    if(!fgets(buf,(int)n,p)){ buf[0]=0; } else { char*nl=strchr(buf,'\n'); if(nl)*nl=0; }
    return pclose(p);
}

GitResult git_ensure_repo(const char* project){
    GitResult g; memset(&g,0,sizeof g);
    GitRemote r=git_lookup(project);
    if(!r.found){ snprintf(g.msg,sizeof g.msg,"No remote for %s (see remotes.conf)",project); return g; }
    char dir[600]; snprintf(dir,sizeof dir,"%s/%s",ROOT,project);
    char cmd[1400];
    snprintf(cmd,sizeof cmd,"mkdir -p '%s'",dir); run(cmd);
    char base[900]; base_cmd(base,sizeof base,project,&r);
    snprintf(cmd,sizeof cmd,"test -d '%s/.git' || %s init -q -b %s",dir,base,r.branch); run(cmd);
    snprintf(cmd,sizeof cmd,"%s remote remove origin 2>/dev/null; %s remote add origin '%s'",base,base,r.remote); run(cmd);
    g.ok=1; snprintf(g.msg,sizeof g.msg,"repo ready (%s)",r.branch);
    return g;
}

GitResult git_sync(const char* project){
    GitResult g; memset(&g,0,sizeof g);
    GitRemote r=git_lookup(project);
    if(!r.found){ snprintf(g.msg,sizeof g.msg,"No remote configured"); return g; }
    git_ensure_repo(project);
    char base[900]; base_cmd(base,sizeof base,project,&r); char cmd[1600];
    time_t t=time(NULL); struct tm tmv; localtime_r(&t,&tmv); char ds[32]; strftime(ds,sizeof ds,"%Y-%m-%d",&tmv);
    snprintf(cmd,sizeof cmd,"%s add -A && (%s diff --cached --quiet || %s commit -q -m 'oyobyok: update from device on %s')",base,base,base,ds);
    if(run(cmd)!=0){ snprintf(g.msg,sizeof g.msg,"commit failed"); return g; }
    snprintf(cmd,sizeof cmd,"%s fetch -q origin 2>&1",base);
    if(run(cmd)!=0){ snprintf(g.msg,sizeof g.msg,"Fetch failed (offline?)"); return g; }
    // Rebase onto the remote tip, if the remote has one. Conflicted files are kept with their
    // markers and staged.
    snprintf(cmd,sizeof cmd,"%s rev-parse -q --verify origin/%s >/dev/null 2>&1",base,r.branch);
    int rr=0;
    if(run(cmd)==0){ snprintf(cmd,sizeof cmd,"%s rebase origin/%s 2>/dev/null",base,r.branch); rr=run(cmd); }
    for(int i=0;rr!=0 && i<20;i++){
        char c2[1200];
        snprintf(c2,sizeof c2,"%s add -A 2>/dev/null && GIT_EDITOR=true %s rebase --continue 2>/dev/null",base,base);
        rr=run(c2);
    }
    if(rr!=0){ char ab[1000]; snprintf(ab,sizeof ab,"%s rebase --abort 2>/dev/null",base); run(ab);
        snprintf(g.msg,sizeof g.msg,"Reconcile failed"); return g; }
    snprintf(cmd,sizeof cmd,"%s push -q -u origin %s 2>&1",base,r.branch);
    if(run(cmd)!=0){ snprintf(g.msg,sizeof g.msg,"Push failed"); return g; }
    g.ok=1; snprintf(g.msg,sizeof g.msg,"Synced %s",r.branch);
    return g;
}

GitResult git_status(const char* project){
    GitResult g; memset(&g,0,sizeof g);
    GitRemote r=git_lookup(project);
    char dir[600]; snprintf(dir,sizeof dir,"%s/%s",ROOT,project);
    char cmd[1000], out[256];
    snprintf(cmd,sizeof cmd,"test -d '%s/.git' && echo y",dir);
    if(run_out(cmd,out,sizeof out)!=0||out[0]!='y'){ snprintf(g.msg,sizeof g.msg,"not a repo"); return g; }
    char base[900]; base_cmd(base,sizeof base,project,&r);
    snprintf(cmd,sizeof cmd,"%s status --porcelain | wc -l | tr -d ' '",base);
    if(run_out(cmd,out,sizeof out)==0) g.changed=atoi(out);
    snprintf(cmd,sizeof cmd,"%s rev-list --left-right --count %s...origin/%s 2>/dev/null",base,r.branch,r.branch);
    if(run_out(cmd,out,sizeof out)==0 && out[0]){ sscanf(out,"%d %d",&g.ahead,&g.behind); }
    g.ok=1; snprintf(g.msg,sizeof g.msg,"%s  %d ahead  %d behind  %d local",r.branch,g.ahead,g.behind,g.changed);
    return g;
}

GitResult git_status_fetch(const char* project){
    GitRemote r=git_lookup(project);
    if(r.found){ char base[900]; base_cmd(base,sizeof base,project,&r); char cmd[1400];
        snprintf(cmd,sizeof cmd,"%s fetch -q origin 2>/dev/null",base); run(cmd); }
    return git_status(project);
}

