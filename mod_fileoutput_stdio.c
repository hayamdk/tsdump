#pragma comment(lib, "shlwapi.lib")

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <sys/timeb.h>
#include <inttypes.h>

#include <shlwapi.h>

#include "modules_def.h"
#include "strfuncs.h"

typedef struct {
	WCHAR *fn;
	WCHAR *fn_pi;
	FILE *fp;
	ProgInfo initial_pi;
} file_output_stat_t;

static WCHAR* create_proginfo_file(const WCHAR *fname_ts, const ProgInfo *pi)
{
	WCHAR fname[MAX_PATH_LEN];
	WCHAR genre[1024];
	FILE *fp = NULL;
	errno_t err;

	if (!pi->isok) {
		output_message(MSG_WARNING, L"番組情報が取得できなかったので番組情報ファイルを生成しません");
		return NULL;
	}

	wcscpy_s(fname, MAX_PATH_LEN - 1, fname_ts);
	PathRemoveExtension(fname);
	PathAddExtension(fname, L".txt");
	err = _wfopen_s(&fp, fname, L"wt, ccs=UTF-8");
	if (err != 0) {
		output_message(MSG_ERROR, L"番組情報ファイルを保存できません: %s", fname);
		return NULL;
	}

	putGenreStr(genre, 1024 - 1, pi->genretype, pi->genre);

	fwprintf(fp, L"%d%02d%02d\n%02d%02d%02d\n", pi->recyear, pi->recmonth, pi->recday,
		pi->rechour, pi->recmin, pi->recsec);
	fwprintf(fp, L"%02d%02d%02d\n", pi->durhour, pi->durmin, pi->dursec);
	fwprintf(fp, L"%s\n", pi->chname);
	fwprintf(fp, L"%s\n", genre);
	fwprintf(fp, L"%s\n----------------\n", pi->pname);
	fwprintf(fp, L"%s\n--------\n", pi->pdetail);
	fwprintf(fp, L"%s\n", pi->pextend);
	fclose(fp);

	return _wcsdup(fname);
}

static void create_new_proginfo_file(const WCHAR *fname_ts, const WCHAR *fname_pi_init, const ProgInfo *pi)
{
	WCHAR fname[MAX_PATH_LEN];

	if (!pi->isok) {
		return;
	}

	if (fname_pi_init) {
		wcscpy_s(fname, MAX_PATH_LEN - 1, fname_ts);
		tsd_strncpy(fname, fname_ts, MAX_PATH_LEN);
		PathRemoveExtension(fname);
		//wcsncat_s(fname, L"_init.txt", MAX_PATH_LEN - 1);
		tsd_strlcat(fname, MAX_PATH_LEN, TSD_TEXT("_init.txt"));
		MoveFile(fname_pi_init, fname);
	}
	create_proginfo_file(fname_ts, pi);
}

static void *hook_pgoutput_create(const WCHAR *fname, const ProgInfo *pi, const ch_info_t *ch_info)
{
	FILE *fp = NULL;
	errno_t err;

	UNREF_ARG(ch_info);
	
	err = _wfopen_s(&fp, fname, L"wb");
	if (err) {
		output_message(MSG_ERROR, L"tsファイルをオープンできません: %s", fname);
		return NULL;
	}

	file_output_stat_t *fos = (file_output_stat_t*)malloc(sizeof(file_output_stat_t));
	fos->fn = _wcsdup(fname);
	fos->fp = fp;
	fos->initial_pi = *pi;
	fos->fn_pi = create_proginfo_file(fname, pi);

	output_message(MSG_NOTIFY, L"[録画開始] %s", fos->fn);
	return fos;
}

static void hook_pgoutput(void *pstat, const unsigned char *buf, const size_t size)
{
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (fos) {
		fwrite(buf, 1, size, fos->fp);
	}
}


static void hook_pgoutput_close(void *pstat, const ProgInfo *pi)
{
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return;
	}
	fclose(fos->fp);

	if (fos->fn_pi) {
		/* 番組情報が途中で変っていたら新しく作る */
		if (fos->initial_pi.durhour != pi->durhour ||
			fos->initial_pi.durmin != pi->durmin ||
			fos->initial_pi.dursec != pi->dursec ||
			wcscmp(fos->initial_pi.pdetail, pi->pdetail) != 0 ||
			wcscmp(fos->initial_pi.pextend, pi->pextend) != 0
			) {
			create_new_proginfo_file(fos->fn, fos->fn_pi, pi);
		}
		free(fos->fn_pi);
	}

	output_message(MSG_NOTIFY, L"[録画終了] %s", fos->fn);
	free(fos->fn);
	free(fos);
}

static void register_hooks()
{
	register_hook_pgoutput_create(hook_pgoutput_create);
	register_hook_pgoutput(hook_pgoutput);
	register_hook_pgoutput_close(hook_pgoutput_close);
}

MODULE_DEF module_def_t mod_fileoutput_stdio = {
	TSDUMP_MODULE_V2,
	L"mod_fileoutput_stdio",
	register_hooks,
	NULL
};
