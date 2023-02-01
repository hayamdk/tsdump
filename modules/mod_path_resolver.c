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
#include "utils/tsdstr.h"
#include "utils/path.h"

#include "core/tsdump.h"

TSDCHAR param_base_dir[MAX_PATH_LEN] = {TSD_NULLCHAR};
TSDCHAR param_filename_format[MAX_PATH_LEN] = TSD_TEXT("{Y}{MM}{DD}{hh}{mm}_{CH2}_{PROG}");
TSDCHAR param_filename_format_alt[MAX_PATH_LEN] = TSD_TEXT("{YC}{MMC}{DDC}{hhC}{mmC}_{CH2}_{PROG}");
TSDCHAR param_filename_format_noinfo[MAX_PATH_LEN] = TSD_TEXT("{YC}{MMC}{DDC}{hhC}{mmC}_{CH2}_番組情報なし");


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

static void gen_filename_base(TSDCHAR *filename, const TSDCHAR* format, tsdstr_replace_set_t *sets, const size_t n_sets)
{
	tsd_strlcpy(filename, format, MAX_PATH_LEN);
	tsd_replace_sets(filename, MAX_PATH_LEN, sets, n_sets, 0);
	normalize_fname(filename, MAX_PATH_LEN);
}

static void get_fname(TSDCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info, const TSDCHAR *ext)
{
	int i;
	tsdstr_replace_set_t sets[64];
	size_t n_sets = 0;

	TSDCHAR filename_base[MAX_PATH_LEN + 1];
	TSDCHAR suffix_str[6];

	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{Q}"), TSD_TEXT("\""));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{{"), TSD_TEXT("{"));
	replace_sets_proginfo_vars(sets, &n_sets, pi, ch_info);
	replace_sets_date_vars(sets, &n_sets);

	if (!PGINFO_READY(pi->status)) {
		gen_filename_base(filename_base, param_filename_format_noinfo, sets, n_sets);
		path_join(fname, param_base_dir, filename_base);
		tsd_strlcat(fname, MAX_PATH_LEN, ext);
		if (!path_isexist(fname)) {
			goto END;
		}
	} else {
		gen_filename_base(filename_base, param_filename_format, sets, n_sets);
		path_join(fname, param_base_dir, filename_base);
		tsd_strlcat(fname, MAX_PATH_LEN, ext);
		if (!path_isexist(fname)) {
			goto END;
		}

		/* ファイルが既に存在したら代替フォーマットを使用 */
		gen_filename_base(filename_base, param_filename_format_alt, sets, n_sets);
		path_join(fname, param_base_dir, filename_base);
		tsd_strlcat(fname, MAX_PATH_LEN, ext);
		if (!path_isexist(fname)) {
			goto END;
		}
	}

	/* まだファイルが存在したらsuffixをつける */
	for (i = 2; i < 9999; i++) {
		tsd_snprintf(suffix_str, 6, TSD_TEXT("_%d"), i);

		path_join(fname, param_base_dir, filename_base);
		tsd_strlcat(fname, MAX_PATH_LEN, suffix_str);
		tsd_strlcat(fname, MAX_PATH_LEN, ext);
		if (!path_isexist(fname)) {
			goto END;
		}
	}

	/* 最終的にsuffix:_9999のままファイル名を返すがmod_fileoutでは既存ファイルを作成するとき失敗になる */
	output_message(MSG_ERROR, TSD_TEXT("既にファイルが存在します: %s"), fname);

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

static const TSDCHAR* set_filename_format_all(const TSDCHAR* param)
{
	tsd_strlcpy(param_filename_format, param, MAX_PATH_LEN - 1);
	tsd_strlcpy(param_filename_format_alt, param, MAX_PATH_LEN - 1);
	tsd_strlcpy(param_filename_format_noinfo, param, MAX_PATH_LEN - 1);
	return NULL;
}

static const TSDCHAR* set_filename_format(const TSDCHAR* param)
{
	tsd_strlcpy(param_filename_format, param, MAX_PATH_LEN - 1);
	return NULL;
}

static const TSDCHAR* set_filename_format_alt(const TSDCHAR* param)
{
	tsd_strlcpy(param_filename_format_alt, param, MAX_PATH_LEN - 1);
	return NULL;
}

static const TSDCHAR* set_filename_format_noinfo(const TSDCHAR* param)
{
	tsd_strlcpy(param_filename_format_noinfo, param, MAX_PATH_LEN - 1);
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
	{ TSD_TEXT("--filename-format-all"), TSD_TEXT("ファイル名フォーマット(全)"), 1, set_filename_format_all },
	{ TSD_TEXT("--filename-format"), TSD_TEXT("ファイル名フォーマット(デフォルト)"), 1, set_filename_format },
	{ TSD_TEXT("--filename-format-alt"), TSD_TEXT("ファイル名フォーマット(同名ファイルが存在する場合)"), 1, set_filename_format_alt },
	{ TSD_TEXT("--filename-format-noinfo"), TSD_TEXT("ファイル名フォーマット(番組情報が取得できていない場合)"), 1, set_filename_format_noinfo },
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