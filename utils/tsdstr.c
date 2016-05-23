#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#endif

#include "utils/tsdstr.h"

#ifdef TSD_PLATFORM_MSVC
#include <wtypes.h>
#else
#include <string.h>
#endif

const TSDCHAR* tsd_strncpy(TSDCHAR *dst, const TSDCHAR *src, size_t n)
{
#ifdef TSD_PLATFORM_MSVC
	return wcsncpy(dst, src, n);
#else
	return strncpy(dst, src, n);
#endif
}

const TSDCHAR* tsd_strcpy(TSDCHAR *dst, const TSDCHAR *src)
{
#ifdef TSD_PLATFORM_MSVC
	return wcscpy(dst, src);
#else
	return strcpy(dst, src);
#endif
}

size_t tsd_strlen(const TSDCHAR *str)
{
#ifdef TSD_PLATFORM_MSVC
	return wcslen(str);
#else
	return strlen(str);
#endif
}

int tsd_vsnprintf(TSDCHAR *str, size_t size, const TSDCHAR *format, va_list ap)
{
#ifdef TSD_PLATFORM_MSVC
	return vswprintf(str, size, format, ap);
#else
	return vsnprintf(str, size, format, ap);
#endif
}

int tsd_snprintf(TSDCHAR *str, size_t size, const TSDCHAR *format, ...)
{
	int ret;
	va_list list;

	va_start(list, format);
	ret = tsd_vsnprintf(str, size, format, list);
	va_end(list);

	return ret;
}

int tsd_strcmp(const TSDCHAR *s1, const TSDCHAR *s2)
{
#ifdef TSD_PLATFORM_MSVC
	return wcscmp(s1, s2);
#else
	return strcmp(s1, s2);
#endif
}

/* WindowsƒRƒ“ƒ\[ƒ‹‚ÅWCHAR‚Ì“ú–{Œê‚ðprintf‚·‚é‚Æ‚È‚º‚©–Ò—ó‚É’x‚¢(”\ms`”•Sms)‚Ì‚ÅWriteConsole‚ðŽg‚¤ */
#ifdef TSD_PLATFORM_MSVC
int tsd_fprintf(FILE *fp, const TSDCHAR *fmt, ...)
{
	int ret;
	DWORD written;
	TSDCHAR str[2048];
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
#endif

const TSDCHAR* tsd_strlcat(TSDCHAR *dst, size_t dst_buflen, const TSDCHAR *src)
{
	size_t dstlen = tsd_strlen(dst);
	size_t srclen = tsd_strlen(src);
	if (dstlen + srclen < dst_buflen) {
		tsd_strcpy(&dst[dstlen], src);
	} else if(dstlen + 1 < dst_buflen) {
		tsd_strncpy(&dst[dstlen], src, dst_buflen - dstlen - 1 );
		dst[dst_buflen - 1] = TSD_NULLCHAR;
	}
	return dst;
}
