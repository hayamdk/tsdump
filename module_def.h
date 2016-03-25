#define MAX_PATH_LEN		MAX_PATH

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

#ifdef _MSC_VER
	#ifndef __cplusplus
		#define inline __inline
	#endif
#endif

#define			UNREF_ARG(x)			(x)
