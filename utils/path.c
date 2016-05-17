#include <stdio.h>

#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <Windows.h>
#include <shlwapi.h>
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

TSDCHAR* path_extension(const TSDCHAR *path)
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

#endif