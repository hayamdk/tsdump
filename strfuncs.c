#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <wtypes.h>

#include "strfuncs.h"

const WCHAR* tsd_strncpy(WCHAR *dst, const WCHAR *src, size_t n)
{
	return wcsncpy(dst, src, n);
}

const WCHAR* tsd_strcpy(WCHAR *dst, const WCHAR *src)
{
	return wcscpy(dst, src);
}

size_t tsd_strlen(const WCHAR *str)
{
	return wcslen(str);
}

const WCHAR* tsd_strlcat(WCHAR *dst, size_t dst_buflen, const WCHAR *src)
{
	size_t dstlen = tsd_strlen(dst);
	size_t srclen = tsd_strlen(src);
	if (dstlen + srclen < dst_buflen) {
		tsd_strcpy(&dst[dstlen], src);
	} else if(dstlen + 1 < dst_buflen) {
		tsd_strncpy(&dst[dstlen], src, dst_buflen - dstlen - 1 );
		dst[dst_buflen - 1] = TSD_NULL_CHARACTER;
	}
	return dst;
}