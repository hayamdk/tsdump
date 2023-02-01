#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#pragma comment(lib, "shlwapi.lib")
#include <windows.h>
#include <shlwapi.h>
#include <process.h>
#else
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/timeb.h>
#include <inttypes.h>

#define TSD_MODULES_HOOKS_API_SET

#include "utils/advanced_buffer.h"
#include "utils/tsdstr.h"
#include "utils/arib_proginfo.h"
#include "core/module_api.h"
#include "utils/arib_parser.h"
#include "utils/path.h"

#include "core/tsdump.h"
#include "core/load_modules.h"
#include "core/default_decoder.h"
#include "modules/modules.h"

static hooks_stream_generator_t *hooks_stream_generator = NULL;
static hooks_stream_decoder_t *hooks_stream_decoder = NULL;
static hook_path_resolver_t hook_path_resolver = NULL;

typedef struct {
	module_def_t *cmd_module;
	cmd_def_t *cmd_def;
} cmd_load_t;

#define MAX_MODULECMDS		128

module_load_t modules[MAX_MODULES];
static module_hooks_t *module_hooks_current;
int n_modules = 0;

static cmd_load_t modulecmds[MAX_MODULECMDS];
static int n_modulecmds = 0;

static stream_stats_t stream_stats;

void register_hook_pgoutput_precreate(hook_pgoutput_precreate_t handler)
{
	module_hooks_current->hook_pgoutput_precreate = handler;
}

void register_hook_pgoutput_changed(hook_pgoutput_changed_t handler)
{
	module_hooks_current->hook_pgoutput_changed = handler;
}

void register_hook_pgoutput_end(hook_pgoutput_end_t handler)
{
	module_hooks_current->hook_pgoutput_end = handler;
}

void register_hook_pgoutput_postclose(hook_pgoutput_postclose_t handler)
{
	module_hooks_current->hook_pgoutput_postclose = handler;
}

void register_hook_pgoutput_create(hook_pgoutput_create_t handler)
{
	module_hooks_current->hook_pgoutput_create = handler;
}

void register_hook_pgoutput(hook_pgoutput_t handler, int block_size)
{
	module_hooks_current->hook_pgoutput = handler;
	module_hooks_current->output_block_size = block_size;
}

void set_use_retval_pgoutput()
{
	module_hooks_current->pgoutput_use_retval = 1;
}

void register_hook_pgoutput_check(hook_pgoutput_check_t handler)
{
	module_hooks_current->hook_pgoutput_check = handler;
}

void register_hook_pgoutput_forceclose(hook_pgoutput_forceclose_t handler)
{
	module_hooks_current->hook_pgoutput_forceclose = handler;
}

void register_hook_pgoutput_close(hook_pgoutput_close_t handler)
{
	module_hooks_current->hook_pgoutput_close = handler;
}

void register_hook_postconfig(hook_postconfig_t handler)
{
	module_hooks_current->hook_postconfig = handler;
}

void register_hook_preclose_module(hook_preclose_module_t handler)
{
	module_hooks_current->hook_preclose_module = handler;
}

void register_hook_close_module(hook_close_module_t handler)
{
	module_hooks_current->hook_close_module = handler;
}

void register_hook_open_stream(hook_open_stream_t handler)
{
	module_hooks_current->hook_open_stream = handler;
}

void register_hook_encrypted_stream(hook_encrypted_stream_t handler)
{
	module_hooks_current->hook_encrypted_stream = handler;
}

void register_hook_stream(hook_stream_t handler)
{
	module_hooks_current->hook_stream = handler;
}

void register_hook_close_stream(hook_close_stream_t handler)
{
	module_hooks_current->hook_close_stream = handler;
}

void register_hook_tick(hook_tick_t handler)
{
	module_hooks_current->hook_tick = handler;
}

int register_hooks_stream_generator(hooks_stream_generator_t *handlers)
{
	if (hooks_stream_generator == NULL) {
		hooks_stream_generator = handlers;
	} else {
		output_message(MSG_ERROR, TSD_TEXT("ストリームジェネレータは既に登録されています"));
		return 0;
	}
	return 1;
}

int register_hooks_stream_decoder(hooks_stream_decoder_t *handlers)
{
	if (hooks_stream_decoder == NULL) {
		hooks_stream_decoder = handlers;
	} else {
		output_message(MSG_ERROR, TSD_TEXT("ストリームデコーダは既に登録されています"));
		return 0;
	}
	return 1;
}

void register_hook_message(hook_message_t handler)
{
	module_hooks_current->hook_message = handler;
}

int register_hook_path_resolver(hook_path_resolver_t handler)
{
	if (hook_path_resolver == NULL) {
		hook_path_resolver = handler;
	} else {
		output_message(MSG_ERROR, TSD_TEXT("パスリゾルバは既に登録されています"));
		return 0;
	}
	return 1;
}

void get_stream_stats(const stream_stats_t **s)
{
	*s = &stream_stats;
}

void set_stream_stats_mbps(const double mbps)
{
	stream_stats.mbps = mbps;
}

void add_stream_stats_total_bytes(const int bytes)
{
	stream_stats.total_bytes += bytes;
}

static void init_stream_stats()
{
	memset(&stream_stats, 0, sizeof(stream_stats_t));
	stream_stats.mbps = 0.0;
}

int do_postconfig()
{
	int i;
	int ret;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_postconfig) {
			ret = modules[i].hooks.hook_postconfig();
			if (!ret) {
				return 0;
			}
		}
	}
	return 1;
}

void do_close_module()
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_close_module) {
			modules[i].hooks.hook_close_module();
		}
	}
}

void do_preclose_module()
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_preclose_module) {
			modules[i].hooks.hook_preclose_module();
		}
	}
}

void do_open_stream()
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_open_stream) {
			modules[i].hooks.hook_open_stream();
		}
	}
}

void do_encrypted_stream(unsigned char *buf, size_t size)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_encrypted_stream) {
			modules[i].hooks.hook_encrypted_stream(buf, size);
		}
	}
}

void do_stream(unsigned char *buf, size_t size, int encrypted)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_stream) {
			modules[i].hooks.hook_stream(buf, size, encrypted);
		}
	}
}

void do_close_stream()
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_close_stream) {
			modules[i].hooks.hook_close_stream();
		}
	}
}

void do_tick(int64_t time_ms)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_tick) {
			modules[i].hooks.hook_tick(time_ms);
		}
	}
}

int do_stream_generator_open(void **param, ch_info_t *chinfo)
{
	if (hooks_stream_generator) {
		return hooks_stream_generator->open_handler(param, chinfo);
	}
	output_message(MSG_ERROR, TSD_TEXT("ストリームジェネレータが一つも登録されていません"));
	return 0;
}

void do_stream_generator(void *param, unsigned char **buf, int *size)
{
	if (hooks_stream_generator) {
		hooks_stream_generator->handler(param, buf, size);
	}
}

int do_stream_generator_wait(void *param, int timeout_ms)
{
	if (hooks_stream_generator) {
		if (hooks_stream_generator->wait_handler) {
			return hooks_stream_generator->wait_handler(param, timeout_ms);
		}
	}
	return -1;
}

void do_stream_generator_cnr(void *param)
{
	if (hooks_stream_generator) {
		if (hooks_stream_generator->cnr_handler) {
			hooks_stream_generator->cnr_handler(param, &stream_stats.s_signal.cnr, &stream_stats.s_signal.cnr_scale);
		} else {
			stream_stats.s_signal.cnr = 0.0;
			stream_stats.s_signal.cnr_scale = TSDUMP_SCALE_NONE;
		}
	}
}

void do_stream_generator_siglevel(void *param)
{
	if (hooks_stream_generator) {
		if (hooks_stream_generator->siglevel_handler) {
			hooks_stream_generator->siglevel_handler(param, &stream_stats.s_signal.level, &stream_stats.s_signal.level_scale);
		} else {
			stream_stats.s_signal.level = 0.0;
			stream_stats.s_signal.level_scale = TSDUMP_SCALE_NONE;
		}
	}
}

void do_stream_generator_close(void *param)
{
	if (hooks_stream_generator) {
		hooks_stream_generator->close_handler(param);
	}
}

int do_stream_decoder_open(void **param, int *encrypted)
{
	if (hooks_stream_decoder) {
		return hooks_stream_decoder->open_handler(param, encrypted);
	} else {
		*encrypted = 1;
	}
	return 1;
}

void do_stream_decoder(void *param, unsigned char **dst_buf, int *dst_size, const unsigned char *src_buf, int src_size)
{
	if (hooks_stream_decoder) {
		hooks_stream_decoder->handler(param, dst_buf, dst_size, src_buf, src_size);
	}else {
		default_decoder(dst_buf, dst_size, src_buf, src_size);
	}
}

int is_implemented_stream_decoder_stats()
{
	if (hooks_stream_decoder && hooks_stream_decoder->stats_handler) {
		return 1;
	}
	return 0;
}

void do_stream_decoder_stats(void *param)
{
	if ( is_implemented_stream_decoder_stats() ) {
		hooks_stream_decoder->stats_handler(param, &stream_stats.s_decoder);
	} else {
		stream_stats.s_decoder.n_dropped = ts_n_drops;
		stream_stats.s_decoder.n_input = ts_n_total;
		stream_stats.s_decoder.n_scrambled = ts_n_scrambled;
		stream_stats.s_decoder.n_output = ts_n_total;
	}
}

void do_stream_decoder_close(void *param)
{
	if (hooks_stream_decoder) {
		hooks_stream_decoder->close_handler(param);
	}
}

void do_message(const TSDCHAR *modname, message_type_t msgtype, tsd_syserr_t *err, const TSDCHAR *msg)
{
	int i;

	/* モジュールロード前でもこのフックだけは呼ぶ */
	ghook_message(modname, msgtype, err, msg);

	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_message) {
			modules[i].hooks.hook_message(modname, msgtype, err, msg);
		}
	}
}

void default_path_resolver(const proginfo_t *pi, const ch_info_t *ch_info, TSDCHAR *fn)
{
	int pid;
	UNREF_ARG(pi);
	UNREF_ARG(ch_info);

#ifdef TSD_PLATFORM_MSVC
	pid = _getpid();
#else
	pid = getpid();
#endif

	tsd_snprintf(fn, MAX_PATH_LEN-1, TSD_TEXT("%"PRId64"_%s_%s_%d.ts"), gettime(), ch_info->tuner_name, ch_info->ch_str, pid);
}

void do_path_resolver(const proginfo_t *proginfo, const ch_info_t *ch_info, TSDCHAR *fn)
{
	if (hook_path_resolver) {
		hook_path_resolver(proginfo, ch_info, fn);
	} else {
		default_path_resolver(proginfo, ch_info, fn);
	}
}

static cmd_load_t *get_cmddef(const TSDCHAR *cmdname)
{
	int i;
	for ( i = 0; i < n_modulecmds; i++ ) {
		if ( tsd_strcmp(cmdname, modulecmds[i].cmd_def->cmd_name) == 0 ) {
			return &modulecmds[i];
		}
	}
	return NULL;
}

static int load_module_cmd(module_def_t *mod, cmd_def_t *cmd)
{
	cmd_load_t *cmd_load = get_cmddef(cmd->cmd_name);
	if ( cmd_load != NULL ) {
		output_message(MSG_ERROR, TSD_TEXT("%s: コマンドオプション %s は既にモジュール %s によって登録されています"),
			mod->modname, cmd->cmd_name, cmd_load->cmd_module->modname );
		return 0;
	}
	modulecmds[n_modulecmds].cmd_def = cmd;
	modulecmds[n_modulecmds].cmd_module = mod;
	n_modulecmds++;
	return 1;
}

#ifdef TSD_PLATFORM_MSVC
static int load_module(module_def_t *mod, HMODULE handle)
#else
static int load_module(module_def_t *mod, void *handle)
#endif
{
	if ( mod->mod_ver != TSDUMP_MODULE_API_VER ) {
		output_message(MSG_ERROR, TSD_TEXT("互換性の無いモジュールです: %s"), mod->modname);
		return 0;
	}

	if (n_modules >= MAX_MODULES) {
		output_message(MSG_ERROR, TSD_TEXT("これ以上モジュールをロードできません(最大数:%d)"), MAX_MODULES);
		return 0;
	}

	modules[n_modules].def = mod;
	memset(&modules[n_modules].hooks, 0, sizeof(module_hooks_t));
	modules[n_modules].handle = handle;
	n_modules++;
	return 1;
}

static int unload_module(const TSDCHAR *modname)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if ( tsd_strcmp(modules[i].def->modname, modname) == 0 ) {
			memmove( &modules[i], &modules[i+1], sizeof(module_load_t)*(n_modules-i-1) );
			n_modules--;
			return 1;
		}
	}
	output_message(MSG_ERROR, TSD_TEXT("モジュール%sはロードされていません"), modname);
	return 0;
}

static int load_dll_modules()
{
	FILE *fp = NULL;
	TSDCHAR exepath[MAX_PATH_LEN];
	TSDCHAR exedir[MAX_PATH_LEN];
	TSDCHAR confpath[MAX_PATH_LEN];
	TSDCHAR modfile[MAX_PATH_LEN];
	char modname[MAX_PATH_LEN];
	module_def_t *mod;
#ifdef TSD_PLATFORM_MSVC
	HMODULE handle;
	errno_t err;
	size_t len;
#else
	void *handle;
#endif

	if (!path_self(exepath)) {
		output_message(MSG_SYSERROR, TSD_TEXT("実行ファイルのパスを取得できません"));
		return 0;
	}
	path_getdir(exedir, exepath);
	path_join(confpath, exedir, TSD_TEXT("modules.conf"));
#ifdef TSD_PLATFORM_MSVC
	err = _wfopen_s(&fp, confpath, L"r");
	if (err == 0) {
		while ( fgetws(modfile, MAX_PATH_LEN - 1, fp) != NULL ) {
#else
	fp = fopen(confpath, "r");
	if (fp) {
		while ( fgets(modfile, MAX_PATH_LEN - 1, fp) != NULL ) {
#endif
			/* 末尾の改行、空文字を削除 */
			tsd_rstrip(modfile);

			if (modfile[0] == TSD_CHAR('#')) {
				/* #から始まる行は無視する */
				continue;
			} else if (modfile[0] == TSD_CHAR('!')) {
				/* !から始まる行はモジュールを取り消す */
				if (!unload_module(&modfile[1])) {
					fclose(fp);
					return 0;
				}
				continue;
			}

#ifdef TSD_PLATFORM_MSVC
			handle = LoadLibrary(modfile);
			if (handle == NULL) {
				output_message(MSG_SYSERROR, TSD_TEXT("DLLモジュールをロードできませんでした: %s (LoadLibrary)"), modfile);
				fclose(fp);
				return 0;
			}
			wcstombs_s(&len, modname, MAX_PATH_LEN-1, modfile, MAX_PATH_LEN);
			PathRemoveExtensionA(modname);
#pragma warning(push)
#pragma warning(disable:4054)
			mod = (module_def_t*)GetProcAddress(handle, modname);
#pragma warning(pop)
#else
			handle = dlopen(modfile, RTLD_LAZY);
			if (!handle) {
				output_message(MSG_ERROR, "動的モジュールをロードできませんでした(dlopen): %s", dlerror());
				fclose(fp);
				return 0;
			}
			tsd_strlcpy(modname, modfile, MAX_PATH_LEN - 1);
			*(path_getext(modname)) = '\0';
			mod = dlsym(handle, path_getfile(modname));
#endif
			if (mod == NULL) {
#ifdef TSD_PLATFORM_MSVC
				output_message(MSG_SYSERROR, TSD_TEXT("モジュールポインタを取得できませんでした: %s (GetProcAddress)"), modfile);
				FreeLibrary(handle);
#else
				output_message(MSG_SYSERROR, TSD_TEXT("モジュールポインタを取得できませんでした(dlsym): %s"), dlerror());
				dlclose(handle);
#endif
				
				fclose(fp);
				return 0;
			}
			if (!load_module(mod, handle)) {
#ifdef TSD_PLATFORM_MSVC
				FreeLibrary(handle);
#else
				dlclose(handle);
#endif
				fclose(fp);
				return 0;
			}
		}
		fclose(fp);
	} else {
#ifdef TSD_PLATFORM_MSVC
		output_message(MSG_NOTIFY, TSD_TEXT("modules.confを開けないのでDLLモジュールをロードしません"));
#else
		output_message(MSG_NOTIFY, TSD_TEXT("modules.confを開けないので動的モジュールをロードしません"));
#endif
	}
	return 1;
}

static int get_cmd_params( int argc, const TSDCHAR* argv[] )
{
	int i;
	cmd_load_t *cmd_load;
	cmd_def_t *cmd_def;
	const TSDCHAR *ret = NULL;
	for ( i = 1; i < argc; i++ ) {
		cmd_load = get_cmddef(argv[i]);
		if ( ! cmd_load ) {
			output_message(MSG_ERROR, TSD_TEXT("不明なコマンドオプション %s が指定されました"), argv[i]);
			return 0;
		}
		cmd_def = cmd_load->cmd_def;
		if ( cmd_def->have_option ) {
			if ( i == argc-1 ) {
				output_message(MSG_ERROR, TSD_TEXT("コマンドオプション %s に値を指定してください"), argv[i]);
				return 0;
			} else {
				ret = cmd_def->cmd_handler(argv[i+1]);
				i++;
			}
		} else {
			ret = cmd_def->cmd_handler(NULL);
		}
		if (ret) {
			output_message(MSG_ERROR, ret);
			return 0;
		}
	}
	return 1;
}

int init_modules(int argc, const TSDCHAR* argv[])
{
	int i;
	cmd_def_t *cmd;
	tsd_api_set_t api_set;

	init_stream_stats();

	/* list */
	for (i = 0; i < n_modules; i++) {
		if (modules[i].handle) {
#ifdef TSD_PLATFORM_MSVC
			output_message(MSG_NOTIFY, TSD_TEXT("module(dll): %s"), modules[i].def->modname);
#else
			output_message(MSG_NOTIFY, TSD_TEXT("module(dynamic): %s"), modules[i].def->modname);
#endif
		} else {
			output_message(MSG_NOTIFY, TSD_TEXT("module: %s"), modules[i].def->modname);
		}
	}

	/* init */
	tsd_api_init_set(&api_set);

	for (i = 0; i < n_modules; i++) {
		if (modules[i].def->api_init_handler) {
			modules[i].def->api_init_handler(&api_set);
		}
		if( modules[i].def->init_handler ) {
			modules[i].def->init_handler();
		}
	}

	/* cmds */
	for (i = 0; i < n_modules; i++) {
		if ( modules[i].def->cmds ) {
			for ( cmd = modules[i].def->cmds; cmd->cmd_name != NULL; cmd++ ) {
				if ( !load_module_cmd(modules[i].def, cmd) ) {
					return  0;
				}
			}
		}
	}

	/* モジュールの引数を処理 */
	if (!get_cmd_params(argc, argv)) {
		return 0;
	}

	/* hooks */
	for (i = 0; i < n_modules; i++) {
		/* hooks */
		module_hooks_current = &modules[i].hooks;
		modules[i].def->register_hooks();
	}

	/* postconfigフックを呼び出し */
	if (!do_postconfig()) {
		return 0;
	}

	return 1;
}

int load_modules()
{
	int i;
	int n = sizeof(static_modules) / sizeof(module_def_t*);
	for ( i = 0; i < n; i++ ) {
		if( ! load_module(static_modules[i], NULL) ) {
			return -1;
		}
	}
	if ( ! load_dll_modules() ) {
		return -1;
	}
	return n_modules;
}

void free_modules()
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].handle) {
#ifdef TSD_PLATFORM_MSVC
			FreeLibrary(modules[i].handle);
#else
			dlclose(modules[i].handle);
#endif
		}
	}
	n_modules = 0;
}

void print_cmd_usage()
{
	int i;
	tsd_printf(TSD_TEXT("\n----------------------\n<コマンドオプション>\n"));
	for (i = 0; i < n_modulecmds; i++) {
		if (modulecmds[i].cmd_def->have_option) {
			tsd_printf(TSD_TEXT("%s [option]: %s\n"),
				modulecmds[i].cmd_def->cmd_name,
				modulecmds[i].cmd_def->cmd_description
				);
		} else {
			tsd_printf(TSD_TEXT("%s: %s\n"),
				modulecmds[i].cmd_def->cmd_name,
				modulecmds[i].cmd_def->cmd_description
				);
		}
	}
	tsd_printf(TSD_TEXT("* は必須オプション\n"));
}
