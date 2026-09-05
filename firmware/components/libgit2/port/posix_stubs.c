/*
 * posix_stubs.c — POSIX symbols libgit2 references that ESP-IDF newlib/lwIP
 * do not provide. None are exercised by the OYOBYOK SSH git flow:
 *   - symlinks: FAT has none (core.symlinks=false); readlink/symlink/lstat
 *     for links just fail cleanly, lstat falls back to stat.
 *   - user/process identity: no multi-user OS; return 0 / "no home dir" so
 *     libgit2 skips global/XDG config discovery and owner checks.
 *   - utimes: FAT mtime is coarse; touching times is a harmless no-op.
 *   - gai_strerror: lwIP declares it but ships no definition.
 *
 * Defining these is safe precisely because the linker reported them undefined
 * (nothing else in the image provides them).
 */

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <errno.h>

int lstat(const char *path, struct stat *buf)
{
	return stat(path, buf); /* FAT: no symlinks, so lstat == stat */
}

/* FAT has no permission bits. Defining chmod here (rather than leaving it
 * undefined) stops the linker pulling libnosys' dir.o, whose mkdir/getcwd/
 * readdir/... would then clash with ESP-IDF's vfs + newlib. */
int chmod(const char *path, mode_t mode)
{
	(void)path; (void)mode;
	return 0;
}

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
	(void)path; (void)buf; (void)bufsiz;
	errno = EINVAL; /* not a symbolic link */
	return -1;
}

int symlink(const char *target, const char *linkpath)
{
	(void)target; (void)linkpath;
	errno = ENOSYS; /* unsupported on FAT */
	return -1;
}

int utimes(const char *filename, const struct timeval times[2])
{
	struct stat st;
	(void)times;
	/* FAT mtime is coarse, so we don't actually set the time — BUT we must still
	 * report failure for a missing file. libgit2's loose-ODB "freshen" touches an
	 * object file and treats success as "object already exists, skip the write"
	 * (odb.c odb_freshen_1). A blanket success here made every new tree/commit
	 * object look already-present, so git_odb_write silently wrote nothing and
	 * push failed with "invalid object specified". Mirror real utimes: ENOENT if
	 * the path doesn't exist, no-op success if it does. */
	if (stat(filename, &st) != 0) {
		errno = ENOENT;
		return -1;
	}
	return 0;
}

uid_t getuid(void)  { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void)  { return 0; }

pid_t getppid(void)        { return 0; }
pid_t getpgid(pid_t pid)   { (void)pid; return 0; }
pid_t getsid(pid_t pid)    { (void)pid; return 0; }

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
	       struct passwd **result)
{
	(void)uid; (void)pwd; (void)buf; (void)buflen;
	*result = NULL; /* no such user / no home directory */
	return 0;
}

const char *gai_strerror(int ecode)
{
	(void)ecode;
	return "getaddrinfo error";
}
