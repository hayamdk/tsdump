#include <Windows.h>
#include <inttypes.h>
#include <shlwapi.h>
#include <wchar.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>

#include "modules_def.h"
#include "tsdump.h"
#include "strfuncs.h"

WCHAR param_base_dir[MAX_PATH_LEN] = {L'\0'};

static void normalize_fname(WCHAR *fname)
{
	WCHAR *p = fname;
	WCHAR c;

	while ((c = *p) != L'\0') {
		if (c == L'\\') {
			*p = L'￥';
		} else if (c == L'/') {
			*p = L'／';
		} else if (c == L'*') {
			*p = L'*';
		} else if (c == L'?') {
			*p = L'？';
		} else if (c == L'"') {
			*p = L'”';
		} else if (c == L'<') {
			*p = L'＜';
		} else if (c == L'>') {
			*p = L'＞';
		} else if (c == L'|') {
			*p = L'｜';
		} else if (c == L':') {
			*p = L'：';
		}
		p++;
	}
}

static void get_fname(WCHAR* fname, const ProgInfo *pi, const ch_info_t *ch_info, WCHAR *ext)
{
	int64_t tn;
	int i;

	//ProgInfo *pi = &(tos->pi);
	const WCHAR *chname, *pname;
	if (pi->isok) {
		tn = timenum_start(pi);
		chname = pi->chname;
		pname = pi->pname;
	} else {
		tn = timenumnow();
		chname = ch_info->ch_str;
		pname = L"番組情報なし";
	}

	WCHAR *pname_n = _wcsdup(pname);
	WCHAR *chname_n = _wcsdup(chname);

	normalize_fname(pname_n);
	normalize_fname(chname_n);

	/* tnは番組情報の開始時刻 */
	if (!pi->isok && ch_info->n_services > 1) {
		swprintf(fname, MAX_PATH_LEN - 1, L"%s%I64d_%s(sv=%d)_%s%s", param_base_dir, tn, chname_n, ch_info->service_id, pname_n, ext);
	} else {
		swprintf(fname, MAX_PATH_LEN - 1, L"%s%I64d_%s_%s%s", param_base_dir, tn, chname_n, pname_n, ext);
	}
	if (!PathFileExists(fname)) {
		goto END;
	}
	/* ファイルが既に存在したらtnを現在時刻に */
	tn = timenumnow();
	if (!pi->isok && ch_info->n_services > 1) {
		swprintf(fname, MAX_PATH_LEN - 1, L"%s%I64d_%s(sv=%d)_%s%s", param_base_dir, tn, chname_n, ch_info->service_id, pname_n, ext);
	} else {
		swprintf(fname, MAX_PATH_LEN - 1, L"%s%I64d_%s_%s%s", param_base_dir, tn, chname_n, pname_n, ext);
	}
	if (!PathFileExists(fname)) {
		goto END;
	}

	/* それでも存在したらsuffixをつける */
	for (i = 2;; i++) {
		if (!pi->isok && ch_info->n_services > 1) {
			swprintf(fname, MAX_PATH_LEN - 1, L"%s%I64d_%s(sv=%d)_%s_%d%s", param_base_dir, tn, chname_n, ch_info->service_id, pname_n, i, ext);
		} else {
			swprintf(fname, MAX_PATH_LEN - 1, L"%s%I64d_%s_%s_%d%s", param_base_dir, tn, chname_n, pname_n, i, ext);
		}
		if (!PathFileExists(fname)) {
			goto END;
		}
	}

END:
	free(pname_n);
	free(chname_n);
	return;
}

static const WCHAR* hook_path_resolver(const ProgInfo *pi, const ch_info_t *ch_info)
{
	WCHAR *fname = (WCHAR*)malloc(sizeof(WCHAR)*MAX_PATH_LEN);
	get_fname(fname, pi, ch_info, L".ts");
	return fname;
}

static const WCHAR* set_dir(const WCHAR *param)
{
	//wcsncpy_s(param_base_dir, param, MAX_PATH_LEN);
	tsd_strncpy(param_base_dir, param, MAX_PATH_LEN);
	PathAddBackslash(param_base_dir);
	return NULL;
}

static int hook_postconfig()
{
	if (param_base_dir[0] == L'\0') {
		output_message(MSG_ERROR, L"出力ディレクトリが指定されていないか、または不正です");
		return 0;
	}
	return 1;
}

static cmd_def_t cmds[] = {
	{ L"--dir", L"出力先ディレクトリ *", 1, set_dir },
	NULL,
};

static void register_hooks()
{
	register_hook_postconfig(hook_postconfig);
	register_hook_path_resolver(hook_path_resolver);
}

MODULE_DEF module_def_t mod_path_resolver = {
	TSDUMP_MODULE_V2,
	L"mod_path_resolver",
	register_hooks,
	cmds,
};