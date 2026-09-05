#!/usr/bin/env bash
# Regenerates every screenshot in docs/static/img/screens from the emulator.
set -euo pipefail
cd "$(dirname "$0")"
make -s emu
O=../../docs/static/img/screens
mkdir -p "$O/splash"
rm -rf emu_git; mkdir -p emu_git/cfg emu_git/repos/Drafts/pieces emu_git/repos/Novel
printf '[identity]\nname = Ada\n\n[Drafts]\nremote = git@github.com:ada/drafts.git\n\n[Novel]\nremote = git@github.com:ada/novel.git\n' > emu_git/cfg/remotes.conf
printf 'It was a bright cold day in April, and the clocks were striking thirteen. Winston Smith, his chin nuzzled into his breast in an effort to escape the vile wind, slipped quickly through the glass doors of Victory Mansions.\n\nThe hallway smelt of boiled cabbage and old rag mats.' > emu_git/repos/Drafts/chapter-one.txt
printf 'notes\n' > emu_git/repos/Drafts/notes.txt
for i in $(seq 1 40); do printf 'piece %d\n' $i > emu_git/repos/Drafts/pieces/p$i.txt; done

run(){ printf "$1" | OYOBYOK_EMU_DEMO=1 ./sim_emu >/dev/null; }
ticks(){ local s=""; for _ in $(seq 1 "$1"); do s+="tick\n"; done; printf '%s' "$s"; }
sr(){ local s=""; for _ in $(seq 1 "$1"); do s+="sright\n"; done; printf '%s' "$s"; }

for i in 0 1 2 3 4; do run "splash $i\nsnap $O/splash/$i\n"; done
run "splash 0\nsnap $O/splash\n"
run "down\nup\nsnap $O/main-menu\n"
# Projects: Back, New Project, Drafts, Novel, Manage. Inside Drafts: Back, New File, New Folder, pieces, chapter-one.txt, notes.txt, Manage.
run "enter\nsnap $O/projects\ndown\ndown\nenter\nsnap $O/project\npower\nsnap $O/project-status\npower\ndown\ndown\ndown\ndown\ndown\ndown\nenter\nsnap $O/manage\ndown\ndown\ndown\ndown\nenter\nsnap $O/manage-choose\ndown\nenter\nsnap $O/manage-delete\n"
run "enter\ndown\ndown\nenter\ndown\nenter\nt chapter-two\nsnap $O/new-file\n"
run "enter\ndown\ndown\nenter\ndown\ndown\ndown\ndown\nenter\nend\nsnap $O/editor\npower\nsnap $O/editor-status\npower\nhome\n$(sr 27)snap $O/editor-select\n"
run "down\nenter\nsnap $O/synchronise\ndown\nenter\nsnap $O/git-repos\ndown\nenter\nsnap $O/git\ndown\nenter\n$(ticks 12)snap $O/git-syncing\n"
run "down\nenter\ndown\ndown\nenter\nsnap $O/wifi\n"
run "down\nenter\ndown\ndown\ndown\nenter\ndown\nsnap $O/sftp\nenter\ndown\nsnap $O/sftp-project\nenter\n$(ticks 9)snap $O/sftp-pushing\n"
run "down\ndown\ndown\nenter\nsnap $O/disk-mode\n"
run "down\ndown\ndown\ndown\nenter\nsnap $O/settings\ndown\nenter\nsnap $O/contrast\n"
rm -f "$O"/*.ppm "$O"/splash/*.ppm; rm -rf emu_frames
echo "wrote $(ls "$O"/*.png "$O"/splash/*.png | wc -l | tr -d ' ') screenshots"
