#ifdef TSD_PLATFORM_MSVC
	#define tsd_printf(...)				tsd_fprintf(stdout, __VA_ARGS__)
	int									tsd_fprintf(FILE *fp, const TSDCHAR *fmt, ...);
#else
	#define tsd_printf(...)				printf( __VA_ARGS__)
	#define tsd_fprintf(fp, ...)		fprintf(fp, __VA_ARGS__)
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

int tsd_atoi(const TSDCHAR *str);
double tsd_atof(const TSDCHAR *str);

int tsd_snprintf(TSDCHAR *str, size_t size, const TSDCHAR *format, ...);
int tsd_strcmp(const TSDCHAR *s1, const TSDCHAR *s2);
int tsd_strncmp(const TSDCHAR *s1, const TSDCHAR *s2, size_t n);
void tsd_replace_sets(TSDCHAR *str, size_t str_maxlen, tsdstr_replace_set_t *sets, size_t n_sets, int longest_match);

#define TSD_REPLACE_ADD_SET(sets, n, _old, _new) \
	do { \
		(sets)[(n)].old = (_old); \
		(sets)[(n)].new = (_new); \
		(sets)[(n)].old_len = 0; \
		(sets)[(n)].new_len = 0; \
		(n)++; \
	} while(0)

#ifdef va_start
int tsd_vsnprintf(TSDCHAR *str, size_t size, const TSDCHAR *format, va_list ap);
#endif

void tsd_rstrip(TSDCHAR *str);