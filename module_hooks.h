typedef void (*register_hooks_t)();

typedef const WCHAR* (*cmd_handler_t)(const WCHAR*);

typedef enum {
	TSDUMP_MODULE_NONE = 0,
/*	TSDUMP_MODULE_V1 = 1,
	TSDUMP_MODULE_V2 = 2,*/
	TSDUMP_MODULE_V3 = 3,
} module_ver;

typedef struct{
	const WCHAR *cmd_name;
	const WCHAR *cmd_description;
	int have_option;
	cmd_handler_t cmd_handler;
} cmd_def_t;

typedef struct{
	module_ver mod_ver;
	const WCHAR *modname;
	register_hooks_t register_hooks;
	cmd_def_t *cmds;
} module_def_t;

typedef struct{
	int sp_num;
	int ch_num;
	int n_services;
	unsigned int *services;
	int mode_all_services;
	const WCHAR *tuner_name;
	const WCHAR *sp_str;
	const WCHAR *ch_str;
} ch_info_t;

typedef void* (*hook_pgoutput_create_t)(const WCHAR*, const proginfo_t*, const ch_info_t *ch_info);
typedef void(*hook_pgoutput_t)(void*, const unsigned char*, const size_t);
typedef const int(*hook_pgoutput_check_t)(void*);
typedef const int(*hook_pgoutput_wait_t)(void*);
typedef void(*hook_pgoutput_close_t)(void*, const proginfo_t*);
typedef void(*hook_pgoutput_postclose_t)(void*);
typedef int (*hook_postconfig_t)();
typedef void(*hook_close_module_t)();
typedef void(*hook_open_stream_t)();
typedef void(*hook_encrypted_stream_t)(const unsigned char*, const size_t);
typedef void(*hook_stream_t)(const unsigned char*, const size_t, const int);
typedef void(*hook_close_stream_t)();
typedef void(*hook_stream_generator_t)(void *, unsigned char **, int *);
typedef int (*hook_stream_generator_open_t)(void**, ch_info_t*);
typedef double(*hook_stream_generator_siglevel_t)(void *);
typedef void(*hook_stream_generator_close_t)(void *);

typedef struct {
	hook_stream_generator_open_t open_handler;
	hook_stream_generator_t handler;
	hook_stream_generator_siglevel_t siglevel_handler;
	hook_stream_generator_close_t close_handler;
} hooks_stream_generator_t;

typedef struct {
	int64_t n_input;
	int64_t n_output;
	int64_t n_dropped;
	int64_t n_scrambled;
} decoder_stats_t;

typedef int (*hook_stream_decoder_open_t)(void**, int *);
typedef void(*hook_stream_decoder_t)(void*, unsigned char **, int *, const unsigned char *, int);
typedef void(*hook_stream_decoder_stats_t)(void*, decoder_stats_t*);
typedef void(*hook_stream_decoder_close_t)(void*);

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

typedef void(*hook_message_t)(const WCHAR*, message_type_t, DWORD*, const WCHAR*);
typedef const WCHAR *(*hook_path_resolver_t)(const proginfo_t*, const ch_info_t*);

//typedef void(*hook_stream_splitter)();

#define output_message(type, fmt, ...) _output_message( __FILE__ , type, fmt, __VA_ARGS__)
MODULE_EXPORT_FUNC void _output_message(const char *fname, message_type_t msgtype, const WCHAR *fmt, ...);

MODULE_EXPORT_FUNC void register_hook_pgoutput_create(hook_pgoutput_create_t handler);
MODULE_EXPORT_FUNC void register_hook_pgoutput(hook_pgoutput_t handler);
MODULE_EXPORT_FUNC void register_hook_pgoutput_check(hook_pgoutput_check_t handler);
MODULE_EXPORT_FUNC void register_hook_pgoutput_wait(hook_pgoutput_wait_t handler);
MODULE_EXPORT_FUNC void register_hook_pgoutput_close(hook_pgoutput_close_t handler);
MODULE_EXPORT_FUNC void register_hook_pgoutput_postclose(hook_pgoutput_postclose_t handler);
MODULE_EXPORT_FUNC void register_hook_postconfig(hook_postconfig_t handler);
MODULE_EXPORT_FUNC void register_hook_close_module(hook_close_module_t handler);
MODULE_EXPORT_FUNC void register_hook_open_stream(hook_open_stream_t handler);
MODULE_EXPORT_FUNC void register_hook_encrypted_stream(hook_encrypted_stream_t handler);
MODULE_EXPORT_FUNC void register_hook_stream(hook_stream_t handler);
MODULE_EXPORT_FUNC void register_hook_close_stream(hook_close_stream_t handler);
MODULE_EXPORT_FUNC int register_hooks_stream_generator(hooks_stream_generator_t *handlers);
MODULE_EXPORT_FUNC int register_hooks_stream_decoder(hooks_stream_decoder_t *handlers);
MODULE_EXPORT_FUNC void register_hook_message(hook_message_t handler);
MODULE_EXPORT_FUNC int register_hook_path_resolver(hook_path_resolver_t handler);
