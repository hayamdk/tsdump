#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <wtypes.h>

#include "utils/tsdstr.h"

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

/* WindowsƒRƒ“ƒ\[ƒ‹‚ÅWCHAR‚Ì“ú–{Œê‚ðprintf‚·‚é‚Æ‚È‚º‚©–Ò—ó‚É’x‚¢(”\ms`”•Sms)‚Ì‚ÅWriteConsole‚ðŽg‚¤ */
int tsd_fprintf(FILE *fp, const WCHAR *fmt, ...)
{
	int ret;
	DWORD written;
	WCHAR str[2048];
	HANDLE hc = INVALID_HANDLE_VALUE;
	va_list list;

	va_start(list, fmt);
	ret = vswprintf(str, 2048-1, fmt, list);
	va_end(list);

	if (fp == stderr) {
		hc = GetStdHandle(STD_ERROR_HANDLE);
	} else if( fp == stdout ) {
		hc = GetStdHandle(STD_OUTPUT_HANDLE);
	} else {
		return _putws(str);
	}

	if (hc != INVALID_HANDLE_VALUE && ret >= 0) {
		WriteConsole(hc, str, ret, &written, NULL);
		return written;
	} else {
		return -1;
	}
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
