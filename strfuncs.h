#define TSD_NULL_CHARACTER			L'\0'

#define TSD_TEXT(str)				L#str

const WCHAR* tsd_strncpy(WCHAR *dst, const WCHAR *src, size_t n);
const WCHAR* tsd_strcpy(WCHAR *dst, const WCHAR *src);
size_t tsd_strlen(const WCHAR *str);
const WCHAR* tsd_strlcat(WCHAR *dst, size_t dst_buflen, const WCHAR *src);