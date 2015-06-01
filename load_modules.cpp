#pragma comment(lib, "shlwapi.lib")

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <sys/timeb.h>
#include <shlwapi.h>

#include "modules_def.h"
#include "tsdump.h"
#include "load_modules.h"
#include "modules.h"

#define MAX_HOOKS_NUM 256

typedef struct {
	hook_pgoutput_create_t hook_pgoutput_create;
	hook_pgoutput_t hook_pgoutput;
	hook_pgoutput_check_t hook_pgoutput_check;
	hook_pgoutput_wait_t hook_pgoutput_wait;
	hook_pgoutput_close_t hook_pgoutput_close;
	hook_pgoutput_postclose_t hook_pgoutput_postclose;
	hook_postconfig_t hook_postconfig;
	hook_close_module_t hook_close_module;
	hook_open_stream_t hook_open_stream;
	hook_encrypted_stream_t hook_encrypted_stream;
	hook_stream_t hook_stream;
	hook_close_stream_t hook_close_stream;
} module_hooks_t;

static hooks_stream_generator_t *hooks_stream_generator = NULL;
static hook_stream_decoder_t hook_stream_decoder = NULL;

typedef struct {
	module_def_t *def;
	HMODULE hdll;
	module_hooks_t hooks;
} module_load_t;

typedef struct {
	module_def_t *cmd_module;
	cmd_def_t *cmd_def;
} cmd_load_t;

#define MAX_MODULES			32
#define MAX_MODULECMDS		128

static module_load_t modules[MAX_MODULES];
static module_hooks_t *module_hooks_current;
static int n_modules = 0;

static cmd_load_t modulecmds[MAX_MODULECMDS];
static int n_modulecmds = 0;

void print_err(WCHAR* name, int err)
{
	LPWSTR pMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)(&pMsgBuf),
		0,
		NULL
	);
	fwprintf( stderr, L"%s: (0x%X) %s", name, err, pMsgBuf );
	LocalFree(pMsgBuf);
}

void register_hook_pgoutput_create(hook_pgoutput_create_t handler)
{
	module_hooks_current->hook_pgoutput_create = handler;
}

void register_hook_pgoutput(hook_pgoutput_t handler)
{
	module_hooks_current->hook_pgoutput = handler;
}

void register_hook_pgoutput_check(hook_pgoutput_check_t handler)
{
	module_hooks_current->hook_pgoutput_check = handler;
}

void register_hook_pgoutput_wait(hook_pgoutput_wait_t handler)
{
	module_hooks_current->hook_pgoutput_wait = handler;
}

void register_hook_pgoutput_close(hook_pgoutput_close_t handler)
{
	module_hooks_current->hook_pgoutput_close = handler;
}

void register_hook_pgoutput_postclose(hook_pgoutput_postclose_t handler)
{
	module_hooks_current->hook_pgoutput_postclose = handler;
}

void register_hook_postconfig(hook_postconfig_t handler)
{
	module_hooks_current->hook_postconfig = handler;
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

const WCHAR* register_hooks_stream_generator(hooks_stream_generator_t *handlers)
{
	if (hooks_stream_generator == NULL) {
		hooks_stream_generator = handlers;
	} else {
		return L"ストリームジェネレータは既に登録されています";
	}
	return NULL;
}

const WCHAR* register_hook_stream_decoder(hook_stream_decoder_t handler)
{
	if (hook_stream_decoder == NULL) {
		hook_stream_decoder = handler;
	}
	else {
		return L"ストリームデコーダは既に登録されています";
	}
	return NULL;
}

void **do_pgoutput_create(WCHAR *fname, ProgInfo *pi, ch_info_t *ch_info)
{
	int i;
	void **modulestats = (void**)malloc(sizeof(void*)*n_modules);
	for ( i = 0; i < n_modules; i++ ) {
		if (modules[i].hooks.hook_pgoutput_create) {
			modulestats[i] = modules[i].hooks.hook_pgoutput_create(fname, pi, ch_info);
		}
	}
	return modulestats;
}

void do_pgoutput(void **modulestats, unsigned char *buf, size_t size)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_pgoutput) {
			modules[i].hooks.hook_pgoutput(modulestats[i], buf, size);
		}
	}
}

int do_pgoutput_check(void **modulestats)
{
	int i;
	int write_busy = 0;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_pgoutput_check) {
			write_busy |= modules[i].hooks.hook_pgoutput_check(modulestats[i]);
		}
	}
	return write_busy;
}

int do_pgoutput_wait(void **modulestats)
{
	int i;
	int err = 0;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_pgoutput_wait) {
			err |= modules[i].hooks.hook_pgoutput_wait(modulestats[i]);
		}
	}
	return err;
}

void do_pgoutput_close(void **modulestats, ProgInfo *pi)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_pgoutput_close) {
			modules[i].hooks.hook_pgoutput_close(modulestats[i], pi);
		}
	}
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_pgoutput_postclose) {
			modules[i].hooks.hook_pgoutput_postclose(modulestats[i]);
		}
	}
	free(modulestats);
}

int do_postconfig()
{
	int i;
	const WCHAR *msg;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_postconfig) {
			msg = modules[i].hooks.hook_postconfig();
			if ( msg ) {
				fwprintf(stderr, L"%s\n", msg );
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

void* do_stream_generator_open(ch_info_t *chinfo)
{
	if (hooks_stream_generator) {
		return hooks_stream_generator->open_handler(chinfo);
	}
	return NULL;
}

void do_stream_generator(void *param, unsigned char **buf, int *size)
{
	if (hooks_stream_generator) {
		hooks_stream_generator->handler(param, buf, size);
	}
}

void do_stream_generator_close(void *param)
{
	if (hooks_stream_generator) {
		hooks_stream_generator->close_handler(param);
	}
}

void do_stream_decoder(unsigned char **dst_buf, int *dst_size, unsigned char *src_buf, int src_size)
{
	if (hook_stream_decoder) {
		hook_stream_decoder(dst_buf, dst_size, src_buf, src_size);
	}
}

static cmd_load_t *get_cmddef(const WCHAR *cmdname)
{
	int i;
	for ( i = 0; i < n_modulecmds; i++ ) {
		if ( wcscmp(cmdname, modulecmds[i].cmd_def->cmd_name) == 0 ) {
			return &modulecmds[i];
		}
	}
	return NULL;
}

static int load_module_cmd(module_def_t *mod, cmd_def_t *cmd)
{
	cmd_load_t *cmd_load = get_cmddef(cmd->cmd_name);
	if ( cmd_load != NULL ) {
		fwprintf(stderr, L"%s: コマンドオプション %s は既にモジュール %s によって登録されています\n",
			mod->modname, cmd->cmd_name, cmd_load->cmd_module->modname );
		return 0;
	}
	modulecmds[n_modulecmds].cmd_def = cmd;
	modulecmds[n_modulecmds].cmd_module = mod;
	n_modulecmds++;
	return 1;
}

static int load_module(module_def_t *mod, HMODULE hdll)
{
	if ( mod->mod_ver > TSDUMP_MODULE_V2 ) {
		fwprintf(stderr, L"Invalid module version: %s\n", mod->modname);
		return 0;
	}

	if (n_modules >= MAX_MODULES) {
		fwprintf(stderr, L"Too many modules ( > %d) !\n", MAX_MODULES);
		return 0;
	}

	/* cmds */
	/*if ( mod->cmds ) {
		for ( cmd = mod->cmds; cmd->cmd_name != NULL; cmd++ ) {
			if ( ! load_module_cmd(mod, cmd) ) {
				return  0;
			}
		}
	}*/

	/* hooks */
	//module_hooks_current = &module_hooks[n_modules];
	//memset(module_hooks_current, 0, sizeof(module_hooks_t));
	//mod->register_hooks();

	if (hdll) {
		wprintf(L"Module loaded(dll): %s\n", mod->modname);
	} else {
		wprintf(L"Module loaded: %s\n", mod->modname);
	}

	modules[n_modules].def = mod;
	memset(&modules[n_modules].hooks, 0, sizeof(module_hooks_t));
	modules[n_modules].hdll = hdll;
	n_modules++;
	return 1;
}

static int load_dll_modules()
{
	FILE *fp = NULL;
	WCHAR exepath[MAX_PATH_LEN];
	WCHAR confpath[MAX_PATH_LEN];
	WCHAR dllname[MAX_PATH_LEN];
	char modname[MAX_PATH_LEN];
	module_def_t *mod;
	HMODULE hdll;
	size_t len;
	errno_t err;

	GetModuleFileName(NULL, exepath, MAX_PATH);
	PathRemoveFileSpec(exepath);
	swprintf(confpath, MAX_PATH-1, L"%s\\modules.conf", exepath);
	err = _wfopen_s(&fp, confpath, L"r");
	if (err == 0) {
		while( fgetws(dllname, MAX_PATH_LEN-1, fp) != NULL ) {
			if ( (len = wcslen(dllname)) > 0 ) {
				if (dllname[len - 1] == L'\n') {
					dllname[len - 1] = L'\0'; /* 末尾の改行を削除 */
				}
			}
			if (dllname[0] == L'#') { /* #から始まる行は無視する */
				continue;
			}
			hdll = LoadLibrary(dllname);
			if (hdll == NULL) {
				print_err( L"LoadLibrary()", GetLastError() );
				fwprintf(stderr, L"DLLモジュールをロードできませんでした: %s\n", dllname);
				fclose(fp);
				return 0;
			}
			wcstombs_s(&len, modname, MAX_PATH_LEN-1, dllname, MAX_PATH_LEN);
			PathRemoveExtensionA(modname);
			mod = (module_def_t*)GetProcAddress(hdll, modname);
			if (mod == NULL) {
				print_err(L"GetProcAddress", GetLastError());
				fprintf(stderr, "モジュールポインタを取得できませんでした: %s\n", modname);
				FreeLibrary(hdll);
				fclose(fp);
				return 0;
			}
			if ( ! load_module(mod, hdll) ) {
				FreeLibrary(hdll);
				fclose(fp);
				return 0;
			}
		}
		fclose(fp);
	} else {
		fwprintf(stderr, L"modules.confを開けないのでDLLモジュールをロードしません\n");
	}
	return 1;
}

static int get_cmd_params( int argc, WCHAR* argv[] )
{
	int i;
	cmd_load_t *cmd_load;
	cmd_def_t *cmd_def;
	for ( i = 1; i < argc; i++ ) {
		cmd_load = get_cmddef(argv[i]);
		if ( ! cmd_load ) {
			fwprintf(stderr, L"不明なコマンドオプション %s が指定されました\n", argv[i]);
			return 0;
		}
		cmd_def = cmd_load->cmd_def;
		if ( cmd_def->have_option ) {
			if ( i == argc-1 ) {
				fwprintf(stderr, L"コマンドオプション %s に値を指定してください\n", argv[i]);
				return 1;
			} else {
				cmd_def->cmd_handler(argv[i+1]);
				i++;
			}
		} else {
			cmd_def->cmd_handler(NULL);
		}
	}
	return 1;
}

int init_modules(int argc, WCHAR* argv[])
{
	int i;
	cmd_def_t *cmd;

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
		if (modules[i].hdll) {
			FreeLibrary(modules[i].hdll);
		}
	}
}

void print_cmd_usage()
{
	int i;
	wprintf(L"\n----------------------\n<使用法>\n");
	for (i = 0; i < n_modulecmds; i++) {
		wprintf(L"%s: %s\n",
			modulecmds[i].cmd_def->cmd_name,
			modulecmds[i].cmd_def->cmd_description
		);
	}
	wprintf(L"* は必須オプション\n");
}