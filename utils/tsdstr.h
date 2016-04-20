#define TSD_NULL_CHARACTER			L'\0'

#define TSD_TEXT(str)				L##str
#define tsd_printf(fmt, ...) tsd_fprintf(stdout, fmt, __VA_ARGS__)

const WCHAR* tsd_strncpy(WCHAR *dst, const WCHAR *src, size_t n);
const WCHAR* tsd_strcpy(WCHAR *dst, const WCHAR *src);
size_t tsd_strlen(const WCHAR *str);
int tsd_fprintf(FILE *fp, const WCHAR *fmt, ...);
const WCHAR* tsd_strlcat(WCHAR *dst, size_t dst_buflen, const WCHAR *src);