#include <stdio.h>

#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#pragma comment(lib, "shlwapi.lib")
#include <Windows.h>
#include <shlwapi.h>
#else
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <sys/stat.h>

#include "utils/tsdstr.h"
#include "utils/path.h"

#ifdef TSD_PLATFORM_MSVC

int path_join(TSDCHAR *dst, const TSDCHAR *base, const TSDCHAR *addition)
{
	if (!PathCombine(dst, base, addition)) {
		return 0;
	}
	return 1;
}

int path_getdir(TSDCHAR *dst, const TSDCHAR *path)
{
	tsd_strcpy(dst, path);
	if (!PathRemoveFileSpec(dst)) {
		return 0;
	}
	return 1;
}

TSDCHAR* path_getfile(const TSDCHAR *path)
{
	return PathFindFileName(path);
}

TSDCHAR* path_getext(const TSDCHAR *path)
{
	return PathFindExtension(path);
}

int path_isexist(const TSDCHAR *path)
{
	int ret;
	struct _stat buf;

	ret = _wstat(path, &buf);
	if (ret) {
		if (errno == ENOENT) {
			return TSD_PATH_NOTEXIST;
		}
		return TSD_PATH_ERROR;
	}
	if ((buf.st_mode & _S_IFMT) == _S_IFDIR) {
		return TSD_PATH_ISDIR;
	}
	if ((buf.st_mode & _S_IFMT) == _S_IFREG) {
		return TSD_PATH_ISFILE;
	}
	return TSD_PATH_OTHER;
}

int path_addext(TSDCHAR *path, const TSDCHAR *ext)
{
	return PathAddExtension(path, ext);
}

int path_self(TSDCHAR *path)
{
	return GetModuleFileName(NULL, path, MAX_PATH_LEN);
}

#else

static void path_split(const TSDCHAR **dir, int *dir_len, TSDCHAR **file, int *file_len, TSDCHAR **ext, int *ext_len, const TSDCHAR *path)
{
	const TSDCHAR *d, *f, *e, *p;
	int d_len, f_len, e_len;
	size_t path_len;

	path_len = tsd_strlen(path);

	d = path;
	if (path_len == 0) {
		d_len = 0;
		f = e = path;
		f_len = e_len = 0;
	} else {
		for (e_len = 0, e = p = &path[path_len]; *p != PATH_DELIMITER && p > path; p--) {
			if (e_len == 0 && *p == TSD_CHAR('.')) {
				e = p;
				e_len = &path[path_len] - p;
			}
		}
		if(p[0] == PATH_DELIMITER) {
			f = &p[1];
			f_len = &path[path_len] - p - 1;
		} else {
			f = &p[0];
			f_len = &path[path_len] - p;
		}
		d_len = p - path;
	}

	if (dir) { *dir = (char*)d; }
	if (dir_len) { *dir_len = d_len; }
	if (file) { *file = (char*)f; }
	if (file_len) { *file_len = f_len; }
	if (ext) { *ext = (char*)e; }
	if (ext_len) { *ext_len = e_len; }
	return;
}

int path_join(TSDCHAR *dst, const TSDCHAR *base, const TSDCHAR *addition)
{
	int add_delimiter = 0;
	int base_len = tsd_strlen(base);
	int addition_len = tsd_strlen(addition);

	if (addition[0] == PATH_DELIMITER) {
		if (base[base_len - 1] == PATH_DELIMITER) {
			addition++;
			addition_len--;
		}
	} else {
		if (base[base_len-1] != PATH_DELIMITER) {
			add_delimiter = 1;
		}
	}

	if (base_len + add_delimiter >= MAX_PATH_LEN) {
		tsd_strlcpy(dst, base, MAX_PATH_LEN - 1);
		return 0;
	}
	tsd_strcpy(dst, base);
	if (add_delimiter) {
		dst[base_len] = PATH_DELIMITER;
		base_len++;
	}

	if (base_len + addition_len >= MAX_PATH_LEN) {
		tsd_strlcpy(&dst[base_len], addition, MAX_PATH_LEN - 1 - base_len);
		return 0;
	}
	tsd_strcpy(&dst[base_len], addition);
	dst[base_len + addition_len] = TSD_NULLCHAR;

	return 1;
}

int path_getdir(TSDCHAR *dst, const TSDCHAR *path)
{
	int ret = 1;
	const TSDCHAR *dir;
	int dir_len;

	path_split(&dir, &dir_len, NULL, NULL, NULL, NULL, path);

	if (dir_len > MAX_PATH_LEN - 1) {
		dir_len = MAX_PATH_LEN - 1;
		ret = 0;
	} else if (dir_len == 0) {
		dst[0] = TSD_NULLCHAR;
	}

	memcpy(dst, dir, sizeof(TSDCHAR)*dir_len);
	dst[dir_len] = TSD_NULLCHAR;

	return ret;
}

TSDCHAR* path_getfile(const TSDCHAR *path)
{
	TSDCHAR *file;
	path_split(NULL, NULL, &file, NULL, NULL, NULL, path);
	return file;
}

TSDCHAR* path_getext(const TSDCHAR *path)
{
	TSDCHAR *ext;
	path_split(NULL, NULL, NULL, NULL, &ext, NULL, path);
	return ext;
}

int path_addext(TSDCHAR *path, const TSDCHAR *ext)
{
	int maxlen, ret=1;
	TSDCHAR *pext = path_getext(path);
	maxlen = &path[MAX_PATH_LEN-1] - pext;
	if (maxlen < strlen(pext)) {
		ret = 0;
	}
	strncpy(pext, ext, maxlen);
	return ret;
}

int path_isexist(const TSDCHAR *path)
{
	int ret;
	struct stat buf;

	ret = stat(path, &buf);
	if (ret) {
		if (errno == ENOENT) {
			return TSD_PATH_NOTEXIST;
		}
		return TSD_PATH_ERROR;
	}
	if ((buf.st_mode & S_IFMT) == S_IFDIR) {
		return TSD_PATH_ISDIR;
	}
	if ((buf.st_mode & S_IFMT) == S_IFREG) {
		return TSD_PATH_ISFILE;
	}
	return TSD_PATH_OTHER;
}

int path_self(TSDCHAR *path)
{
	ssize_t ret;
	ret = readlink("/proc/self/exe", path, MAX_PATH_LEN-1);
	if (ret <= 0) {
		return 0;
	}
	path[ret] = TSD_NULLCHAR;
	return 1;
}

#endif

int path_isdir(const TSDCHAR *path)
{
	if (path_isexist(path) == TSD_PATH_ISDIR) {
		return 1;
	}
	return 0;
}

int path_isfile(const TSDCHAR *path)
{
	if (path_isexist(path) == TSD_PATH_ISFILE) {
		return 1;
	}
	return 0;
}

void path_removeext(TSDCHAR *path)
{
	TSDCHAR *c = path_getext(path);
	*c = TSD_NULLCHAR;
}