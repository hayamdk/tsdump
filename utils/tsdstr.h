#ifdef TSD_PLATFORM_MSVC
	#define TSDCHAR						wchar_t
	#define TSD_NULLCHAR				L'\0'
	#define TSD_TEXT(str)				L##str
	#define TSD_CHAR(c)					L##c
	#define tsd_printf(fmt, ...)		tsd_fprintf(stdout, fmt, __VA_ARGS__)
	int									tsd_fprintf(FILE *fp, const TSDCHAR *fmt, ...);
#else
	#define TSDCHAR						char
	#define TSD_NULLCHAR				'\0'
	#define TSD_TEXT(str)				str
	#define TSD_CHAR(c)					c
	#define tsd_printf(fmt, ...)		printf(fmt, __VA_ARGS__)
	#define tsd_fprintf(fp, fmt, ...)	fprintf(fp, fmt, __VA_ARGS__)
#endif

const TSDCHAR* tsd_strncpy(TSDCHAR *dst, const TSDCHAR *src, size_t n);
const TSDCHAR* tsd_strcpy(TSDCHAR *dst, const TSDCHAR *src);
size_t tsd_strlen(const TSDCHAR *str);
const TSDCHAR* tsd_strlcat(TSDCHAR *dst, size_t dst_buflen, const TSDCHAR *src);
int tsd_vsnprintf(TSDCHAR *str, size_t size, const TSDCHAR *format, va_list ap);
int tsd_snprintf(TSDCHAR *str, size_t size, const TSDCHAR *format, ...);
int tsd_strcmp(const TSDCHAR *s1, const TSDCHAR *s2);