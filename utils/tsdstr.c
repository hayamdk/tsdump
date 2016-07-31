#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "utils/tsdstr.h"

#ifdef TSD_PLATFORM_MSVC
#include <wtypes.h>
#else
#include <string.h>
#endif

/* strncpyと異なりコピー先の文字列は必ず終端される。
領域が重なっていてもかまわない。戻り値は終端文字を除くコピーした要素数。 */
size_t tsd_strlcpy(TSDCHAR *dst, const TSDCHAR *src, size_t n)
{
	size_t len = tsd_strlen(src);
	if (len > n) {
		len = n;
	}
	if (src <= &dst[len] && dst <= &src[len]) {
		/* 領域が重複している */
		memmove(dst, src, sizeof(TSDCHAR)*len);
	} else {
		memcpy(dst, src, sizeof(TSDCHAR)*len);
	}
	dst[len] = TSD_NULLCHAR;
	return len;
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

int tsd_strncmp(const TSDCHAR *s1, const TSDCHAR *s2, size_t n)
{
#ifdef TSD_PLATFORM_MSVC
	return wcsncmp(s1, s2, n);
#else
	return strncmp(s1, s2, n);
#endif
}

/* WindowsコンソールでWCHARの日本語をprintfするとなぜか猛烈に遅い(数十ms～数百ms)のでWriteConsoleを使う */
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
		return fputws(str, fp);
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
		tsd_strlcpy(&dst[dstlen], src, dst_buflen - dstlen - 1 );
	}
	return dst;
}

int tsd_atoi(const TSDCHAR *str)
{
#ifdef TSD_PLATFORM_MSVC
	return _wtoi(str);
#else
	return atoi(str);
#endif
}

double tsd_atof(const TSDCHAR *str)
{
#ifdef TSD_PLATFORM_MSVC
	return _wtof(str);
#else
	return atof(str);
#endif
}

static int get_old_len(tsdstr_replace_set_t *sets, size_t idx)
{
	if (sets[idx].old_len == 0) {
		sets[idx].old_len = tsd_strlen(sets[idx].old) + 1;
	}
	return sets[idx].old_len - 1;
}

static int get_new_len(tsdstr_replace_set_t *sets, size_t idx)
{
	if (!sets[idx].new) {
		return 0;
	}
	if (sets[idx].new_len == 0) {
		sets[idx].new_len = tsd_strlen(sets[idx].new) + 1;
	}
	return sets[idx].new_len - 1;
}

static int search_sets(const TSDCHAR *str, tsdstr_replace_set_t *sets, size_t n_sets, int longest_match)
{
	size_t i, match_len=0, len;
	int ret, found = -1;

	for (i = 0; i < n_sets; i++) {
		len = get_old_len(sets, i);
		ret = tsd_strncmp(str, sets[i].old, len);
		if (ret == 0) {
			if (found == -1) {
				found = i;
				match_len = len;
			} else if (longest_match) {
				if (match_len < len) {
					found = i;
					match_len = len;
				}
			} else {
				if (match_len > len) {
					found = i;
					match_len = len;
				}
			}
		}
	}

	return found;
}

void tsd_replace_sets(TSDCHAR *str, size_t str_maxlen, tsdstr_replace_set_t *sets, size_t n_sets, int longest_match)
{
	size_t i;
	size_t old_len, new_len, str_len = tsd_strlen(str);
	int ret;

	for (i = 0; i < str_len; ) {
		ret = search_sets(&str[i], sets, n_sets, longest_match);
		if (ret >= 0) {
			old_len = get_old_len(sets, ret);
			new_len = get_new_len(sets, ret);

			if (i + new_len >= str_maxlen) {
				/* オーバーその1 */
				old_len = 0;
				new_len = str_maxlen - i - 1;
				str[str_maxlen] = TSD_NULLCHAR;
				str_len = str_maxlen - 1;
			} else if (str_len + new_len - old_len >= str_maxlen) {
				/* オーバーその2 */
				memmove(&str[i + new_len], &str[i + old_len], (str_maxlen - 1 - i - new_len) * sizeof(TSDCHAR));
				str[str_maxlen] = TSD_NULLCHAR;
				str_len = str_maxlen - 1;
			} else {
				/* 通常 */
				memmove(&str[i + new_len], &str[i + old_len], (str_len + 1 - i - old_len) * sizeof(TSDCHAR));
				str_len = str_len + new_len - old_len;
			}
			if (new_len > 0) {
				memcpy(&str[i], sets[ret].new, new_len * sizeof(TSDCHAR));
			}
			i = i + new_len;
		} else {
			i++;
		}
	}
}

void tsd_rstrip(TSDCHAR *str)
{
	int i;
	size_t len = tsd_strlen(str);
	for (i = len - 1; i > 0; i--) {
		switch (str[i]) {
		case TSD_CHAR(' '):
		case TSD_CHAR('\t'):
		case TSD_CHAR('\n'):
		case TSD_CHAR('\r'):
			str[i] = TSD_NULLCHAR;
			break;
		default:
			i = 0;
			break;
		}
	}
}