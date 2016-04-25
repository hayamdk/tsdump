#include <stdio.h>

#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <Windows.h>
#endif

#include "utils/tsdstr.h"
#include "utils/path.h"

int path_join(TSDCHAR *dst, const TSDCHAR *base, const TSDCHAR *addition)
{
	return 0;
}

void path_split(TSDCHAR **root, int *root_len, TSDCHAR **dir, int *dir_len, TSDCHAR **fname,
	int *fname_len, TSDCHAR **ext, int *ext_len, TSDCHAR *src)
{
}

void path_dirname(TSDCHAR *dst, const TSDCHAR *path)
{
}

void path_filename(TSDCHAR *dst, const TSDCHAR *path)
{
}

void path_extension(TSDCHAR *dst, const TSDCHAR *path)
{
}

void path_isexist(const TSDCHAR *path)
{
}