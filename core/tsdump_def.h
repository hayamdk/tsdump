#ifdef _MSC_VER
	/* Microsoft Visual C++ */
	#define MAX_PATH_LEN					MAX_PATH
	#define	TSD_PLATFORM_MSVC

	#ifdef IN_SHARED_MODULE
		#ifdef __cplusplus
			#define MODULE_EXPORT_FUNC		extern "C" __declspec(dllimport)
			#define MODULE_EXPORT_VAR		extern "C" __declspec(dllimport)
			#define MODULE_DEF				extern "C" __declspec(dllexport)
		#else
			#define MODULE_EXPORT_FUNC		__declspec(dllimport)
			#define MODULE_EXPORT_VAR		__declspec(dllimport)
			#define MODULE_DEF				__declspec(dllexport)
		#endif
	#else
		#ifdef __cplusplus
			#define MODULE_EXPORT_FUNC		extern "C" __declspec(dllexport)
			#define MODULE_EXPORT_VAR		extern "C" __declspec(dllexport)
			#define MODULE_DEF				extern "C"
		#else
			#define MODULE_EXPORT_FUNC		__declspec(dllexport)
			#define MODULE_EXPORT_VAR		__declspec(dllexport)
			#define MODULE_DEF
		#endif
	#endif

	#ifndef __cplusplus
		#define inline						__inline
	#endif
#else
	/* ‚»‚êˆÈŠO */
	#define MAX_PATH_LEN					1024
	#define	TSD_PLATFORM_OTHER
#endif

#define			UNREF_ARG(x)			(x)
