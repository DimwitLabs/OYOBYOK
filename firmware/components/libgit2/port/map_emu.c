/*
 * map_emu.c — mmap emulation for the OYOBYOK ESP-IDF port.
 *
 * ESP-IDF has no <sys/mman.h>/mmap, so libgit2's unix/map.c is excluded and
 * replaced with a malloc+pread view of the file. Read-only maps (packfiles,
 * indexes, loose objects) just read the region into RAM. Write maps — used by
 * the packfile indexer (indexer.c) to patch the pack as it is received — are
 * tracked in a small side-table and written back to the fd on p_munmap.
 *
 * Single-threaded build (no GIT_THREADS), so a plain static table is safe.
 */

#include "git2_util.h"
#include "map.h"

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

int git__page_size(size_t *page_size)
{
	*page_size = 4096;
	return 0;
}

int git__mmap_alignment(size_t *alignment)
{
	*alignment = 4096;
	return 0;
}

/* Track write mappings so p_munmap can flush them back to the file. */
typedef struct {
	void     *data;
	size_t    len;
	int       fd;
	off64_t   offset;
} map_wb_t;

#define MAP_WB_MAX 8
static map_wb_t s_wb[MAP_WB_MAX];

/* ESP-IDF's FAT VFS implements lseek/read/write but not always pread/pwrite,
 * so seek-then-read. Single-threaded, and each map op is self-contained. */
static ssize_t read_full(int fd, void *buf, size_t len, off64_t offset)
{
	size_t got = 0;
	if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
		return -1;
	while (got < len) {
		ssize_t n = read(fd, (char *)buf + got, len - got);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break; /* EOF: caller zero-fills remainder */
		got += (size_t)n;
	}
	return (ssize_t)got;
}

int p_mmap(git_map *out, size_t len, int prot, int flags, int fd, off64_t offset)
{
	void *data;
	ssize_t got;

	GIT_MMAP_VALIDATE(out, len, prot, flags);

	out->data = NULL;
	out->len = 0;

	data = git__malloc(len);
	if (!data)
		return -1;

	got = read_full(fd, data, len, offset);
	if (got < 0) {
		git__free(data);
		git_error_set(GIT_ERROR_OS, "mmap emulation: read failed");
		return -1;
	}
	if ((size_t)got < len)
		memset((char *)data + got, 0, len - (size_t)got);

	if (prot & GIT_PROT_WRITE) {
		int i;
		for (i = 0; i < MAP_WB_MAX; i++) {
			if (s_wb[i].data == NULL) {
				s_wb[i].data = data;
				s_wb[i].len = len;
				s_wb[i].fd = fd;
				s_wb[i].offset = offset;
				break;
			}
		}
		if (i == MAP_WB_MAX) {
			git__free(data);
			git_error_set(GIT_ERROR_OS, "mmap emulation: too many write mappings");
			return -1;
		}
	}

	out->data = data;
	out->len = len;
	return 0;
}

int p_munmap(git_map *map)
{
	int i;

	GIT_ASSERT_ARG(map);

	for (i = 0; i < MAP_WB_MAX; i++) {
		if (s_wb[i].data && s_wb[i].data == map->data) {
			size_t off = 0;
			if (lseek(s_wb[i].fd, (off_t)s_wb[i].offset, SEEK_SET) >= 0) {
				while (off < s_wb[i].len) {
					ssize_t n = write(s_wb[i].fd, (char *)map->data + off,
							  s_wb[i].len - off);
					if (n < 0) {
						if (errno == EINTR)
							continue;
						break;
					}
					off += (size_t)n;
				}
			}
			s_wb[i].data = NULL;
			break;
		}
	}

	git__free(map->data);
	map->data = NULL;
	map->len = 0;
	return 0;
}
