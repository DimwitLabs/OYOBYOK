// Sync against a local bare remote: first push, a diverging edit from a second clone, and a
// same-line conflict that Sync carries through with markers.
#include "git_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
static void sh(const char*c){ if(system(c)!=0) fprintf(stderr,"(cmd failed: %s)\n",c); }
static int file_has(const char* path,const char* needle){
    FILE* f=fopen(path,"r"); if(!f) return 0; char buf[4096]; size_t n=fread(buf,1,sizeof buf-1,f); fclose(f);
    buf[n]=0; return strstr(buf,needle)!=NULL; }
int main(void){
    sh("rm -rf st_cfg st_repos st_remote st_other");
    sh("mkdir -p st_cfg st_repos st_remote");
    sh("git init -q --bare -b main st_remote/p.git");
    char cwd[512]; if(!getcwd(cwd,sizeof cwd)) return 1;
    char conf[700]; snprintf(conf,sizeof conf,"printf '[Proj]\\nremote = %s/st_remote/p.git\\nbranch = main\\n' > st_cfg/remotes.conf",cwd);
    sh(conf);
    git_configure("st_cfg","st_repos");

    sh("mkdir -p st_repos/Proj && printf 'line one\\nline two\\n' > st_repos/Proj/a.txt");
    GitResult g=git_sync("Proj"); printf("first sync : ok=%d %s\n",g.ok,g.msg); CHECK(g.ok);

    sh("git clone -q st_remote/p.git st_other");
    sh("printf 'line one\\nline two\\nline three from the other side\\n' > st_other/a.txt && git -C st_other commit -qam other && git -C st_other push -q");
    sh("printf 'a new file\\n' > st_repos/Proj/b.txt");
    g=git_sync("Proj"); printf("merge sync : ok=%d %s\n",g.ok,g.msg); CHECK(g.ok);
    CHECK(file_has("st_repos/Proj/a.txt","other side"));
    g=git_status("Proj"); CHECK(g.ahead==0 && g.behind==0 && g.changed==0);

    sh("git -C st_other pull -q --rebase && printf 'line one\\nline TWO-from-other\\n' > st_other/a.txt && git -C st_other commit -qam other2 && git -C st_other push -q");
    sh("printf 'line one\\nline TWO-from-device\\n' > st_repos/Proj/a.txt");
    g=git_sync("Proj"); printf("conflict   : ok=%d %s\n",g.ok,g.msg); CHECK(g.ok);
    CHECK(file_has("st_repos/Proj/a.txt","<<<<<<<"));
    g=git_status("Proj"); CHECK(g.ahead==0 && g.behind==0);

    printf(fails?"FAILED (%d)\n":"OK\n",fails);
    return fails?1:0;
}
