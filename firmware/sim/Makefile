# Desktop emulator and tests. No ESP-IDF needed.
#   make emu     build ./sim_emu (the shared app core with stdin keys and PNG frames)
#   make test    run the git engine and LCD packing tests
CC ?= clang
CFLAGS ?= -O2 -Wall -I../shared

emu: sim_emu.c git_engine.c ../shared/oyobyok_app.inc ../shared/render.h ../shared/oyobyok_menus.h
	$(CC) $(CFLAGS) -o sim_emu sim_emu.c git_engine.c -lm

git_sync_test: git_sync_test.c git_engine.c
	$(CC) $(CFLAGS) -o git_sync_test git_sync_test.c git_engine.c
test_lcdpack: test_lcdpack.c ../shared/oyobyok_lcd_pack.h
	$(CC) $(CFLAGS) -o test_lcdpack test_lcdpack.c

test: git_sync_test test_lcdpack
	./git_sync_test
	./test_lcdpack

clean:
	rm -f sim_emu git_sync_test test_lcdpack *.ppm *.png
	rm -rf emu_frames emu_git st_*

.PHONY: emu test clean
