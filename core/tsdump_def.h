#ifdef _MSC_VER
	/* Microsoft Visual C++ */
	#define MAX_PATH_LEN				MAX_PATH
	#define	TSD_PLATFORM_MSVC

	#ifndef __cplusplus
		#define inline					__inline
	#endif

	#define TSDCHAR						wchar_t
	#define TSD_NULLCHAR				L'\0'
	#define TSD_TEXT(str)				L##str
	#define TSD_CHAR(c)					L##c
#else
	/* ‚»‚êˆÈŠO */
	#define MAX_PATH_LEN				1024
	#define	TSD_PLATFORM_OTHER

	#define TSDCHAR						char
	#define TSD_NULLCHAR				'\0'
	#define TSD_TEXT(str)				str
	#define TSD_CHAR(c)					c
#endif

#define			UNREF_ARG(x)			((void)(x))
