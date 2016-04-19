#pragma comment(lib, "shlwapi.lib")

#include <windows.h>
#include <stdio.h>
#include <inttypes.h>

#include <shlwapi.h>

#include "module_def.h"
#include "ts_proginfo.h"
#include "module_hooks.h"

#define MAX_EXECCMDS 32

static int n_execcmds = 0;
static WCHAR *execcmds[MAX_EXECCMDS];
static int cwindow_min = 0;

static void *hook_pgoutput_create(const WCHAR *fname, const proginfo_t* pi, const ch_info_t *ch_info, const int actually_start)
{
	UNREF_ARG(pi);
	UNREF_ARG(ch_info);
	UNREF_ARG(actually_start);
	WCHAR *fname_dup = _wcsdup(fname);
	return (void*)fname_dup;
}

static void hook_pgoutput_postclose(void *pstat)
{
	int i;
	WCHAR cmdarg[2048];
	WCHAR *fname = (WCHAR*)pstat;

	int show = SW_SHOWDEFAULT;
	if (cwindow_min) {
		show = SW_MINIMIZE;
	}

	for (i = 0; i < n_execcmds; i++) {
		swprintf(cmdarg, 2048 - 1, L"\"%s\"", fname);
		ShellExecute(NULL, NULL, execcmds[i], cmdarg, NULL, show);
	}
	free(fname);
}

static const WCHAR* set_cmd(const WCHAR *param)
{
	if (n_execcmds >= MAX_EXECCMDS) {
		return L"指定する終了時実行コマンドの数が多すぎます";
	}
	execcmds[n_execcmds] = _wcsdup(param);
	n_execcmds++;
	return NULL;
}

static const WCHAR *set_min(const WCHAR* param)
{
	cwindow_min = 1;
	UNREF_ARG(param);
	return NULL;
}

static void register_hooks()
{
	register_hook_pgoutput_create(hook_pgoutput_create);
	register_hook_pgoutput_postclose(hook_pgoutput_postclose);
}

static cmd_def_t cmds[] = {
	{ L"--cmd", L"番組終了時実行コマンド (複数指定可)", 1, set_cmd },
	{ L"--cwmin", L"終了時実行コマンドのウィンドウを最小化する", 0, set_min },
	NULL,
};

MODULE_DEF module_def_t mod_cmdexec_win = {
	TSDUMP_MODULE_V4,
	L"mod_cmdexec_win",
	register_hooks,
	cmds
};
