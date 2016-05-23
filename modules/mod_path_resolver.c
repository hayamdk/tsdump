#include <Windows.h>
#include <inttypes.h>
#include <wchar.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>

#include "core/tsdump_def.h"
#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"
#include "core/tsdump.h"
#include "utils/tsdstr.h"
#include "utils/path.h"

TSDCHAR param_base_dir[MAX_PATH_LEN] = {TSD_CHAR('\0')};

static void normalize_fname(TSDCHAR *fname, size_t fname_max)
{
	tsdstr_replace_set_t replace_sets[] = {
		{ TSD_TEXT("\\"), TSD_TEXT("￥") },
		{ TSD_TEXT("/"), TSD_TEXT("／") },
		{ TSD_TEXT("*"), TSD_TEXT("＊") },
		{ TSD_TEXT("?"), TSD_TEXT("？") },
		{ TSD_TEXT("\""), TSD_TEXT("”") },
		{ TSD_TEXT("<"), TSD_TEXT("＜") },
		{ TSD_TEXT(">"), TSD_TEXT("＞") },
		{ TSD_TEXT("|"), TSD_TEXT("｜") },
		{ TSD_TEXT(":"), TSD_TEXT("：") },
	};

	tsd_replace_sets(fname, fname_max, replace_sets, sizeof(replace_sets) / sizeof(tsdstr_replace_set_t), 0);
}

static void get_fname(WCHAR* fname, const proginfo_t *pi, const ch_info_t *ch_info, WCHAR *ext)
{
	int64_t tn;
	int i, isok = 0;
	const WCHAR *chname, *pname;
	time_mjd_t time_mjd;

	WCHAR pname_n[256], chname_n[256];
	WCHAR filepath[MAX_PATH_LEN + 1];

	pname = L"番組情報なし";

	if (PGINFO_READY(pi->status)) {
		tn = timenum_start(pi);
		chname = pi->service_name.str;
		pname = pi->event_name.str;
		isok = 1;
	} else {
		if ((pi->status&PGINFO_GET_SERVICE_INFO)) {
			chname = pi->service_name.str;
			isok = 1;
		} else {
			chname = ch_info->ch_str;
		}

		if (get_stream_timestamp_rough(pi, &time_mjd)) {
			tn = timenum_timemjd(time_mjd);
		} else {
			tn = timenumnow();
		}
	}

	tsd_strncpy(pname_n, pname, 256 - 1);
	tsd_strncpy(chname_n, chname, 256 - 1);

	normalize_fname(pname_n, 256-1);
	normalize_fname(chname_n, 256-1);

	/* tnは番組情報の開始時刻 */
	if (!isok && ch_info->n_services > 1) {
		swprintf(fname, MAX_PATH_LEN - 1, L"%I64d_%s(sv=%d)_%s%s", tn, chname_n, pi->service_id, pname_n, ext);
	} else {
		swprintf(fname, MAX_PATH_LEN - 1, L"%I64d_%s_%s%s", tn, chname_n, pname_n, ext);
	}
	path_join(filepath, param_base_dir, fname);
	if (!path_isexist(filepath)) {
		goto END;
	}

	/* ファイルが既に存在したらtnを現在時刻に */
	tn = timenumnow();
	if (!isok && ch_info->n_services > 1) {
		swprintf(fname, MAX_PATH_LEN - 1, L"%I64d_%s(sv=%d)_%s%s", tn, chname_n, pi->service_id, pname_n, ext);
	} else {
		swprintf(fname, MAX_PATH_LEN - 1, L"%I64d_%s_%s%s", tn, chname_n, pname_n, ext);
	}
	path_join(filepath, param_base_dir, fname);
	if (!path_isexist(filepath)) {
		goto END;
	}

	/* それでも存在したらsuffixをつける */
	for (i = 2;; i++) {
		if (!isok && ch_info->n_services > 1) {
			swprintf(fname, MAX_PATH_LEN - 1, L"%I64d_%s(sv=%d)_%s_%d%s", tn, chname_n, pi->service_id, pname_n, i, ext);
		} else {
			swprintf(fname, MAX_PATH_LEN - 1, L"%I64d_%s_%s_%d%s", tn, chname_n, pname_n, i, ext);
		}
		path_join(filepath, param_base_dir, fname);
		if (!path_isexist(filepath)) {
			goto END;
		}
	}

END:
	free(pname_n);
	free(chname_n);
	return;
}

static const WCHAR* hook_path_resolver(const proginfo_t *pi, const ch_info_t *ch_info)
{
	WCHAR *fname = (WCHAR*)malloc(sizeof(WCHAR)*MAX_PATH_LEN);
	get_fname(fname, pi, ch_info, L".ts");
	return fname;
}

static const WCHAR* set_dir(const WCHAR *param)
{
	tsd_strncpy(param_base_dir, param, MAX_PATH_LEN);
	//PathAddBackslash(param_base_dir);
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
	TSDUMP_MODULE_V4,
	L"mod_path_resolver",
	register_hooks,
	cmds,
};