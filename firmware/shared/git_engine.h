// Git engine interface. The device implements it with libgit2 over libssh2; the emulator drives the
// system git binary. Projects are configured in remotes.conf ([project] -> remote/branch/key).
#ifndef OYOBYOK_GIT_ENGINE_H
#define OYOBYOK_GIT_ENGINE_H

typedef struct {
    char remote[256];   // git@github.com:me/drafts.git
    char branch[64];
    char key[256];      // private key path, relative to the config dir
    int  found;
} GitRemote;

typedef struct {
    int  ok;
    int  ahead, behind; // relative to the upstream branch
    int  changed;       // uncommitted local files
    char msg[256];
} GitResult;

void       git_configure(const char* config_dir, const char* repos_root);
GitRemote  git_lookup(const char* project);
int        git_list_repos(char names[][64], int max);
GitResult  git_status(const char* project);
GitResult  git_status_fetch(const char* project);      // fetches first, so needs the network
GitResult  git_ensure_repo(const char* project);       // init + origin if the folder is not a repo yet

// Sync: commit local changes, fetch, rebase onto the remote tip, push. Either the whole round trip
// succeeds or the repo is left as it was. Same-line conflicts are kept in the file with markers and
// pushed as part of the commit, so the remote always ends up with everything the device has.
GitResult  git_sync(const char* project);

#endif
