#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <Windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>

#include "utils/arib_proginfo.h"
#include "core/module_api.h"
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

static void get_fname(TSDCHAR* fname, const proginfo_t *pi, const ch_info_t *ch_info, TSDCHAR *ext)
{
	int64_t tn;
	int i, isok = 0;
	const TSDCHAR *chname, *pname;
	time_mjd_t time_mjd;

	TSDCHAR pname_n[256], chname_n[256];
	TSDCHAR filename[MAX_PATH_LEN + 1];

	pname = TSD_TEXT("番組情報なし");

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
			tn = timenum_timemjd(&time_mjd);
		} else {
			tn = timenumnow();
		}
	}

	tsd_strlcpy(pname_n, pname, 256 - 1);
	tsd_strlcpy(chname_n, chname, 256 - 1);

	normalize_fname(pname_n, 256-1);
	normalize_fname(chname_n, 256-1);

	/* tnは番組情報の開始時刻 */
	if (!isok && ch_info->n_services > 1) {
		tsd_snprintf(filename, MAX_PATH_LEN - 1, TSD_TEXT("%"PRId64"_%s(sv=%d)_%s%s"), tn, chname_n, pi->service_id, pname_n, ext);
	} else {
		tsd_snprintf(filename, MAX_PATH_LEN - 1, TSD_TEXT("%"PRId64"_%s_%s%s"), tn, chname_n, pname_n, ext);
	}
	path_join(fname, param_base_dir, filename);
	if (!path_isexist(fname)) {
		goto END;
	}

	/* ファイルが既に存在したらtnを現在時刻に */
	tn = timenumnow();
	if (!isok && ch_info->n_services > 1) {
		tsd_snprintf(filename, MAX_PATH_LEN - 1, TSD_TEXT("%"PRId64"_%s(sv=%d)_%s%s"), tn, chname_n, pi->service_id, pname_n, ext);
	} else {
		tsd_snprintf(filename, MAX_PATH_LEN - 1, TSD_TEXT("%"PRId64"_%s_%s%s"), tn, chname_n, pname_n, ext);
	}
	path_join(fname, param_base_dir, filename);
	if (!path_isexist(fname)) {
		goto END;
	}

	/* それでも存在したらsuffixをつける */
	for (i = 2;; i++) {
		if (!isok && ch_info->n_services > 1) {
			tsd_snprintf(filename, MAX_PATH_LEN - 1, TSD_TEXT("%"PRId64"_%s(sv=%d)_%s_%d%s"), tn, chname_n, pi->service_id, pname_n, i, ext);
		} else {
			tsd_snprintf(filename, MAX_PATH_LEN - 1, TSD_TEXT("%"PRId64"_%s_%s_%d%s"), tn, chname_n, pname_n, i, ext);
		}
		path_join(fname, param_base_dir, filename);
		if (!path_isexist(fname)) {
			goto END;
		}
	}

END:
	return;
}

static void hook_path_resolver(const proginfo_t *pi, const ch_info_t *ch_info, TSDCHAR *fn)
{
	get_fname(fn, pi, ch_info, TSD_TEXT(".ts"));
}

static const TSDCHAR* set_dir(const TSDCHAR *param)
{
	tsd_strlcpy(param_base_dir, param, MAX_PATH_LEN - 1);
	return NULL;
}

static int hook_postconfig()
{
	if (param_base_dir[0] == L'\0') {
		output_message(MSG_ERROR, TSD_TEXT("出力ディレクトリが指定されていないか、または不正です"));
		return 0;
	}
	return 1;
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--dir"), TSD_TEXT("出力先ディレクトリ *"), 1, set_dir },
	{ NULL },
};

static void register_hooks()
{
	register_hook_postconfig(hook_postconfig);
	register_hook_path_resolver(hook_path_resolver);
}

TSD_MODULE_DEF(
	mod_path_resolver,
	register_hooks,
	cmds,
	NULL
);