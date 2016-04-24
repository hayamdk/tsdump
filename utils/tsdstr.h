#ifdef TSD_PLATFORM_MSVC
	#define TSDCHAR					wchar_t
	#define TSD_NULLCHAR			L'\0'
	#define TSD_TEXT(str)			L##str
#else
	#define TSDCHAR				char
	#define TSD_NULLCHAR			'\0'
	#define TSD_TEXT(str)			str
#endif

#define tsd_printf(fmt, ...)		tsd_fprintf(stdout, fmt, __VA_ARGS__)

const TSDCHAR* tsd_strncpy(TSDCHAR *dst, const TSDCHAR *src, size_t n);
const TSDCHAR* tsd_strcpy(TSDCHAR *dst, const TSDCHAR *src);
size_t tsd_strlen(const TSDCHAR *str);
int tsd_fprintf(FILE *fp, const TSDCHAR *fmt, ...);
const TSDCHAR* tsd_strlcat(TSDCHAR *dst, size_t dst_buflen, const TSDCHAR *src);