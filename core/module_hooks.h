#ifndef __TSD_MODULES_HOOKS
#define __TSD_MODULES_HOOKS

typedef void (*register_hooks_t)();

typedef const TSDCHAR* (*cmd_handler_t)(const TSDCHAR*);

#define TSDUMP_MODULE_API_VER 5

typedef enum {
	TSDUMP_SCALE_NONE,
	TSDUMP_SCALE_COUNTER,
	TSDUMP_SCALE_DECIBEL,
	TSDUMP_SCALE_RELATIVE,
} signal_value_scale_t;

typedef struct{
	const TSDCHAR *cmd_name;
	const TSDCHAR *cmd_description;
	int have_option;
	cmd_handler_t cmd_handler;
} cmd_def_t;

typedef struct{
	int mod_ver;
	const TSDCHAR *modname;
	register_hooks_t register_hooks;
	cmd_def_t *cmds;
	void(*init_handler)();
	void(*api_init_handler)(void*);
} module_def_t;

typedef struct{
	int sp_num;
	int ch_num;
	int n_services;
	unsigned int *services;
	int mode_all_services;
	const TSDCHAR *tuner_name;
	const TSDCHAR *sp_str;
	const TSDCHAR *ch_str;
} ch_info_t;

typedef void* (*hook_pgoutput_create_t)(const TSDCHAR*, const proginfo_t*, const ch_info_t*, const int);
typedef void (*hook_pgoutput_t)(void*, const unsigned char*, const size_t);
typedef const int (*hook_pgoutput_check_t)(void*);
typedef const int (*hook_pgoutput_wait_t)(void*);
typedef void (*hook_pgoutput_changed_t)(void*, const proginfo_t*, const proginfo_t*);
typedef void (*hook_pgoutput_end_t)(void*, const proginfo_t*);
typedef void (*hook_pgoutput_close_t)(void*, const proginfo_t*);
typedef void (*hook_pgoutput_postclose_t)(void*);
typedef int (*hook_postconfig_t)();
typedef void (*hook_close_module_t)();
typedef void (*hook_open_stream_t)();
typedef void (*hook_encrypted_stream_t)(const unsigned char*, const size_t);
typedef void (*hook_stream_t)(const unsigned char*, const size_t, const int);
typedef void (*hook_close_stream_t)();
typedef void (*hook_stream_generator_t)(void *, unsigned char **, int *);
typedef int (*hook_stream_generator_open_t)(void**, ch_info_t*);
typedef int (*hook_stream_generator_wait_t)(void *, int);
typedef void (*hook_stream_generator_siglevel_t)(void*, double*, signal_value_scale_t*);
typedef void (*hook_stream_generator_cnr_t)(void*, double*, signal_value_scale_t*);
typedef void (*hook_stream_generator_close_t)(void*);
typedef void (*hook_tick_t)(int64_t);

typedef struct {
	hook_stream_generator_open_t open_handler;
	hook_stream_generator_t handler;
	hook_stream_generator_wait_t wait_handler;
	hook_stream_generator_siglevel_t siglevel_handler;
	hook_stream_generator_cnr_t cnr_handler;
	hook_stream_generator_close_t close_handler;
} hooks_stream_generator_t;

typedef struct {
	int64_t n_input;
	int64_t n_output;
	int64_t n_dropped;
	int64_t n_scrambled;
} decoder_stats_t;

typedef struct {
	signal_value_scale_t cnr_scale;
	double cnr;
	signal_value_scale_t level_scale;
	double level;
} signal_stats_t;

typedef struct {
	int64_t total_bytes;
	decoder_stats_t s_decoder;
	signal_stats_t s_signal;
	double mbps;
	int buf_all;
	int buf_used;
	int buf_dirty;
	int buf_cached;
} stream_stats_t;

typedef int (*hook_stream_decoder_open_t)(void**, int *);
typedef void (*hook_stream_decoder_t)(void*, unsigned char **, int *, const unsigned char *, int);
typedef void (*hook_stream_decoder_stats_t)(void*, decoder_stats_t*);
typedef void (*hook_stream_decoder_close_t)(void*);

typedef struct {
	hook_stream_decoder_open_t open_handler;
	hook_stream_decoder_t handler;
	hook_stream_decoder_stats_t stats_handler;
	hook_stream_decoder_close_t close_handler;
} hooks_stream_decoder_t;

typedef enum {
	MSG_NONE = 0,
	MSG_WARNING = 1,
	MSG_ERROR = 2,
	MSG_SYSERROR = 3,
	MSG_WINSOCKERROR = 4,
	MSG_NOTIFY = 5,
	MSG_DISP = 6,
	MSG_PACKETERROR = 7,
	MSG_DEBUG = 8,
} message_type_t;

#ifdef TSD_PLATFORM_MSVC
	typedef DWORD tsd_syserr_t;
#else
	typedef int tsd_syserr_t;
#endif

typedef void(*hook_message_t)(const TSDCHAR*, message_type_t, tsd_syserr_t*, const TSDCHAR*);
typedef const TSDCHAR *(*hook_path_resolver_t)(const proginfo_t*, const ch_info_t*);

//typedef void(*hook_stream_splitter)();

#define output_message(type, ...) _output_message( __FILE__ , type, __VA_ARGS__ )

#define TSD_API_DEF(type, name, args) type (* name) args
#define __TSD_MODULES_HOOKS2
typedef struct {
#include "core/module_hooks.h"
} tsd_api_set_t;
#undef __TSD_MODULES_HOOKS2

#undef TSD_API_DEF


#ifdef IN_SHARED_MODULE

#ifdef TSD_PLATFORM_MSVC
#ifdef __cplusplus
#define MODULE_DEF				extern "C" __declspec(dllexport)
#else
#define MODULE_DEF				__declspec(dllexport)
#endif
#else
#ifdef __cplusplus
#define MODULE_DEF				extern "C"
#else
#define MODULE_DEF
#endif
#endif

#ifdef IS_SHARED_MODULE
#define TSD_API_DEF(type, name, args) type (*name) args
#else
#define TSD_API_DEF(type, name, args) extern type (*name) args
#endif

#else /* IN_SHARED_MODULE */

#ifdef __cplusplus
#define MODULE_DEF				extern "C"
#define MODULE_API_FUNC			extern "C"
#else
#define MODULE_DEF
#define MODULE_API_FUNC
#endif

#define TSD_API_DEF(type, name, args) MODULE_API_FUNC type name args

#endif /* IN_SHARED_MODULE */

#endif /* __TSD_MODULES_HOOKS */

TSD_API_DEF(void, _output_message, (const char *fname, message_type_t msgtype, const TSDCHAR *fmt, ...));
TSD_API_DEF(void, get_stream_stats, (const stream_stats_t **s));
TSD_API_DEF(void, request_shutdown, (int));

TSD_API_DEF(void, register_hook_pgoutput_create, (hook_pgoutput_create_t));
TSD_API_DEF(void, register_hook_pgoutput, (hook_pgoutput_t));
TSD_API_DEF(void, register_hook_pgoutput_check, (hook_pgoutput_check_t));
TSD_API_DEF(void, register_hook_pgoutput_wait, (hook_pgoutput_wait_t));
TSD_API_DEF(void, register_hook_pgoutput_changed, (hook_pgoutput_changed_t));
TSD_API_DEF(void, register_hook_pgoutput_end, (hook_pgoutput_end_t));
TSD_API_DEF(void, register_hook_pgoutput_close, (hook_pgoutput_close_t));
TSD_API_DEF(void, register_hook_pgoutput_postclose, (hook_pgoutput_postclose_t));
TSD_API_DEF(void, register_hook_postconfig, (hook_postconfig_t));
TSD_API_DEF(void, register_hook_close_module, (hook_close_module_t));
TSD_API_DEF(void, register_hook_open_stream, (hook_open_stream_t));
TSD_API_DEF(void, register_hook_encrypted_stream, (hook_encrypted_stream_t));
TSD_API_DEF(void, register_hook_stream, (hook_stream_t));
TSD_API_DEF(void, register_hook_close_stream, (hook_close_stream_t));
TSD_API_DEF(int, register_hooks_stream_generator, (hooks_stream_generator_t*));
TSD_API_DEF(int, register_hooks_stream_decoder, (hooks_stream_decoder_t*));
TSD_API_DEF(void, register_hook_message, (hook_message_t));
TSD_API_DEF(int, register_hook_path_resolver, (hook_path_resolver_t));
TSD_API_DEF(void, register_hook_tick, (hook_tick_t));


#ifndef __TSD_MODULES_HOOKS2
#define __TSD_MODULES_HOOKS2

#ifdef IN_SHARED_MODULE

#ifdef IS_SHARED_MODULE

#undef TSD_API_DEF
#define TSD_API_DEF(type, name, args) name = set->name
static void __tsd_api_init(void *p)
{
	tsd_api_set_t *set = (tsd_api_set_t*)p;
#include "core/module_hooks.h"
}

#endif

#else /* IN_SHARED_MODULE */

#define __tsd_api_init NULL

#ifdef TSD_MODULES_HOOKS_API_SET
#undef TSD_API_DEF
#define TSD_API_DEF(type, name, args) set->name = name
static void tsd_api_init_set(void *p)
{
	tsd_api_set_t *set =(tsd_api_set_t*)p;
#include "core/module_hooks.h"
}
#endif

#endif

#define TSD_MODULE_DEF(name, reg_hooks, cmds, init_handler) \
MODULE_DEF module_def_t name = { \
	TSDUMP_MODULE_API_VER, \
	TSD_TEXT(#name), \
	reg_hooks, \
	cmds, \
	init_handler, \
	__tsd_api_init \
}

#endif /* __TSD_MODULES_HOOKS2 */
