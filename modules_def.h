#define MAX_PATH_LEN		MAX_PATH

#ifdef __cplusplus
#define MODULE_EXPORT		extern "C" __declspec(dllexport)
#elif
#define MODULE_EXPORT		__declspec(dllexport)
#endif

// î‘ëgèÓïÒç\ë¢ëÃ

#define		CONVBUFSIZE				65536

typedef struct {
	int		recyear;
	int		recmonth;
	int		recday;
	int		rechour;
	int		recmin;
	int		recsec;
	int		durhour;
	int		durmin;
	int		dursec;
	int		rectimezone;
	int		makerid;
	int		modelcode;
	int		recsrc;
	int		chnum;
	WCHAR	chname[CONVBUFSIZE];
	int		chnamelen;
	WCHAR	pname[CONVBUFSIZE];
	int		pnamelen;
	WCHAR	pdetail[CONVBUFSIZE];
	int		pdetaillen;
	WCHAR	pextend[CONVBUFSIZE];
	int		pextendlen;
	int		genre[3];
	int		genretype[3];
	BOOL	bSonyRpls;
	BOOL	bPanaRpls;

	/* í«â¡ */
	BOOL	isok;
} ProgInfo;

typedef void (*register_hooks_t)();

typedef const WCHAR* (*cmd_handler_t)(const WCHAR*);

typedef enum {
	TSDUMP_MODULE_NONE = 0,
	TSDUMP_MODULE_V1 = 1,
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
	int service_id;
	const WCHAR *tuner_name;
	const WCHAR *sp_str;
	const WCHAR *ch_str;
} ch_info_t;

typedef void* (*hook_pgoutput_create_t)(const WCHAR*, const ProgInfo*, const ch_info_t *ch_info);
typedef void(*hook_pgoutput_t)(void*, const unsigned char*, const size_t);
typedef const int(*hook_pgoutput_check_t)(void*);
typedef const int(*hook_pgoutput_wait_t)(void*);
typedef void(*hook_pgoutput_close_t)(void*, const ProgInfo*);
typedef void(*hook_pgoutput_postclose_t)(void*);
typedef const WCHAR* (*hook_postconfig_t)();
typedef void(*hook_open_stream_t)();
typedef void(*hook_encrypted_stream_t)(const unsigned char*, const size_t);
typedef void(*hook_stream_t)(const unsigned char*, const size_t, const int);
typedef void(*hook_close_stream_t)();

MODULE_EXPORT void print_err(WCHAR* name, int err);
MODULE_EXPORT int putGenreStr(WCHAR*, const int, const int*, const int*);

MODULE_EXPORT void register_hook_pgoutput_create(hook_pgoutput_create_t handler);
MODULE_EXPORT void register_hook_pgoutput(hook_pgoutput_t handler);
MODULE_EXPORT void register_hook_pgoutput_check(hook_pgoutput_check_t handler);
MODULE_EXPORT void register_hook_pgoutput_wait(hook_pgoutput_wait_t handler);
MODULE_EXPORT void register_hook_pgoutput_close(hook_pgoutput_close_t handler);
MODULE_EXPORT void register_hook_pgoutput_postclose(hook_pgoutput_postclose_t handler);
MODULE_EXPORT void register_hook_postconfig(hook_postconfig_t handler);
MODULE_EXPORT void register_hook_open_stream(hook_open_stream_t handler);
MODULE_EXPORT void register_hook_crypted_stream(hook_encrypted_stream_t handler);
MODULE_EXPORT void register_hook_stream(hook_stream_t handler);
MODULE_EXPORT void register_hook_close_stream(hook_close_stream_t handler);
