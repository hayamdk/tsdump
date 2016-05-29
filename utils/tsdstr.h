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

typedef struct {
	const TSDCHAR *old;
	const TSDCHAR *new;
	size_t old_len;
	size_t new_len;
} tsdstr_replace_set_t;

const TSDCHAR* tsd_strncpy(TSDCHAR *dst, const TSDCHAR *src, size_t n);
const TSDCHAR* tsd_strcpy(TSDCHAR *dst, const TSDCHAR *src);
size_t tsd_strlen(const TSDCHAR *str);
const TSDCHAR* tsd_strlcat(TSDCHAR *dst, size_t dst_buflen, const TSDCHAR *src);
int tsd_vsnprintf(TSDCHAR *str, size_t size, const TSDCHAR *format, va_list ap);
int tsd_snprintf(TSDCHAR *str, size_t size, const TSDCHAR *format, ...);
int tsd_strcmp(const TSDCHAR *s1, const TSDCHAR *s2);
int tsd_strncmp(const TSDCHAR *s1, const TSDCHAR *s2, size_t n);
void tsd_replace_sets(TSDCHAR *str, size_t str_maxlen, tsdstr_replace_set_t *sets, size_t n_sets, int longest_match);