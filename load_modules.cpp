#define _CRT_SECURE_NO_WARNINGS

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
	module_def_t *module_def;
	HMODULE hdll;
	hook_pgoutput_create_t hook_pgoutput_create;
	hook_pgoutput_t hook_pgoutput;
	hook_pgoutput_check_t hook_pgoutput_check;
	hook_pgoutput_wait_t hook_pgoutput_wait;
	hook_pgoutput_close_t hook_pgoutput_close;
	hook_pgoutput_postclose_t hook_pgoutput_postclose;
	hook_postconfig_t hook_postconfig;
	hook_open_stream_t hook_open_stream;
	hook_encrypted_stream_t hook_encrypted_stream;
	hook_stream_t hook_stream;
	hook_close_stream_t hook_close_stream;
} module_hooks_t;

typedef struct {
	module_def_t *cmd_module;
	cmd_def_t *cmd_def;
} cmd_load_t;

#define MAX_MODULES			32
#define MAX_MODULECMDS		128

static module_hooks_t module_hooks[MAX_MODULES];
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

void **do_pgoutput_create(WCHAR *fname, ProgInfo *pi, ch_info_t *ch_info)
{
	int i;
	void **modulestats = (void**)malloc(sizeof(void*)*n_modules);
	for ( i = 0; i < n_modules; i++ ) {
		if (module_hooks[i].hook_pgoutput_create) {
			modulestats[i] = module_hooks[i].hook_pgoutput_create(fname, pi, ch_info);
		}
	}
	return modulestats;
}

void do_pgoutput(void **modulestats, unsigned char *buf, size_t size)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_pgoutput) {
			module_hooks[i].hook_pgoutput(modulestats[i], buf, size);
		}
	}
}

int do_pgoutput_check(void **modulestats)
{
	int i;
	int write_busy = 0;
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_pgoutput_check) {
			write_busy |= module_hooks[i].hook_pgoutput_check(modulestats[i]);
		}
	}
	return write_busy;
}

int do_pgoutput_wait(void **modulestats)
{
	int i;
	int err = 0;
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_pgoutput_wait) {
			err |= module_hooks[i].hook_pgoutput_wait(modulestats[i]);
		}
	}
	return err;
}

void do_pgoutput_close(void **modulestats, ProgInfo *pi)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_pgoutput_close) {
			module_hooks[i].hook_pgoutput_close(modulestats[i], pi);
		}
	}
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_pgoutput_postclose) {
			module_hooks[i].hook_pgoutput_postclose(modulestats[i]);
		}
	}
	free(modulestats);
}

int do_postconfig()
{
	int i;
	const WCHAR *msg;
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_postconfig) {
			msg = module_hooks[i].hook_postconfig();
			if ( msg ) {
				fwprintf(stderr, L"%s\n", msg );
				return 0;
			}
		}
	}
	return 1;
}

void do_open_stream()
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_open_stream) {
			module_hooks[i].hook_open_stream();
		}
	}
}

void do_encrypted_stream(unsigned char *buf, size_t size)
{
	int i;
	if (size == 0) {
		return;
	}
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_encrypted_stream) {
			module_hooks[i].hook_encrypted_stream(buf, size);
		}
	}
}

void do_stream(unsigned char *buf, size_t size, int encrypted)
{
	int i;
	if (size == 0) {
		return;
	}
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_stream) {
			module_hooks[i].hook_stream(buf, size, encrypted);
		}
	}
}

void do_close_stream()
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (module_hooks[i].hook_close_stream) {
			module_hooks[i].hook_close_stream();
		}
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
	cmd_def_t *cmd;

	if ( mod->mod_ver != TSDUMP_MODULE_V1 ) {
		fwprintf(stderr, L"Invalid module version: %s\n", mod->modname);
		return 0;
	}

	if (n_modules >= MAX_MODULES) {
		fwprintf(stderr, L"Too many modules ( > %d) !\n", MAX_MODULES);
		return 0;
	}

	/* hooks */
	module_hooks_current = &module_hooks[n_modules];
	memset(module_hooks_current, 0, sizeof(module_hooks_t));
	mod->register_hooks();

	/* cmds */
	if ( mod->cmds ) {
		for ( cmd = mod->cmds; cmd->cmd_name != NULL; cmd++ ) {
			if ( ! load_module_cmd(mod, cmd) ) {
				return  0;
			}
		}
	}

	if (hdll) {
		wprintf(L"Module loaded(dll): %s\n", mod->modname);
	} else {
		wprintf(L"Module loaded: %s\n", mod->modname);
	}

	module_hooks_current->hdll = hdll;
	n_modules++;
	return 1;
}

static int load_dll_modules()
{
	FILE *fp;
	WCHAR exepath[MAX_PATH_LEN];
	WCHAR confpath[MAX_PATH_LEN];
	WCHAR dllname[MAX_PATH_LEN];
	char modname[MAX_PATH_LEN];
	module_def_t *mod;
	HMODULE hdll;

	GetModuleFileName(NULL, exepath, MAX_PATH);
	PathRemoveFileSpec(exepath);
	swprintf(confpath, MAX_PATH-1, L"%s\\modules.conf", exepath);
	fp = _wfopen(confpath, L"r");
	if (fp != NULL) {
		while( fgetws(dllname, MAX_PATH_LEN-1, fp) != NULL ) {
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
			wcstombs(modname, dllname, MAX_PATH_LEN);
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
		if (module_hooks[i].hdll) {
			FreeLibrary(module_hooks[i].hdll);
		}
	}
}

int get_cmd_params( int argc, WCHAR* argv[] )
{
	int i;
	cmd_load_t *cmd_load;
	cmd_def_t *cmd_def;
	for ( i = 1; i < argc; i++ ) {
		cmd_load = get_cmddef(argv[i]);
		if ( ! cmd_load ) {
			fwprintf(stderr, L"不明なコマンドオプション %s が指定されました\n", argv[i]);
			return 1;
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

void print_cmd_usage()
{
	int i;
	wprintf(L"\n<使用法>\n");
	for (i = 0; i < n_modulecmds; i++) {
		wprintf(L"%s: %s\n",
			modulecmds[i].cmd_def->cmd_name,
			modulecmds[i].cmd_def->cmd_description
		);
	}
	wprintf(L"* は必須オプション\n");
}