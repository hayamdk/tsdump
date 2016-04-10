#pragma comment(lib, "shlwapi.lib")

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <inttypes.h>

#include <shlwapi.h>

#include "module_def.h"
#include "ts_proginfo.h"
#include "module_hooks.h"
#include "strfuncs.h"

typedef struct {
	WCHAR *fn;
	WCHAR *fn_pi;
	HANDLE fh;
	OVERLAPPED ol;
	const unsigned char *writebuf;
	int write_bytes;
	int written_bytes;
	int write_busy;
	proginfo_t initial_pi;
} file_output_stat_t;

static inline int64_t gettime()
{
	int64_t result;
	struct _timeb tv;

	_ftime64_s(&tv);
	result = (int64_t)tv.time * 1000;
	result += tv.millitm;

	return result;
}

static WCHAR* create_proginfo_file(const WCHAR *fname_ts, const proginfo_t *pi)
{
	WCHAR fname[MAX_PATH_LEN];
	WCHAR extended_text[4096];
	const WCHAR *genre1, *genre2;
	FILE *fp = NULL;
	errno_t err;
	int i;

	if (!PGINFO_READY(pi->status)) {
		output_message(MSG_WARNING, L"番組情報が取得できなかったので番組情報ファイルを生成しません");
		return NULL;
	}

	wcscpy_s(fname, MAX_PATH_LEN - 1, fname_ts);
	PathRemoveExtension(fname);
	PathAddExtension(fname, L".txt");
	err = _wfopen_s(&fp, fname, L"wt, ccs=UTF-8");
	if (err) {
		output_message(MSG_ERROR, L"番組情報ファイルを保存できません: %s", fname);
		return NULL;
	}

	fwprintf(fp, L"%d%02d%02d\n%02d%02d%02d\n", pi->start.year, pi->start.mon, pi->start.day,
		pi->start.hour, pi->start.min, pi->start.sec);
	fwprintf(fp, L"%02d%02d%02d\n", pi->dur.hour, pi->dur.min, pi->dur.sec);
	fwprintf(fp, L"%s\n", pi->service_name.str);
	fwprintf(fp, L"%s\n----------------\n", pi->event_name.str);

	if (pi->status & PGINFO_GET_GENRE) {
		for (i = 0; i < pi->genre_info.n_items; i++) {
			get_genre_str(&genre1, &genre2, pi->genre_info.items[i]);
			fwprintf(fp, L"%s (%s)\n", genre1, genre2);
		}
	}

	fwprintf(fp, L"----------------\n%s\n----------------\n", pi->event_text.str);

	if (pi->status & PGINFO_GET_EXTEND_TEXT) {
		get_extended_text(extended_text, sizeof(extended_text)/sizeof(WCHAR), pi);
		fwprintf(fp, L"%s\n", extended_text);
	}
	fclose(fp);

	return _wcsdup(fname);
}

static void create_new_proginfo_file(const WCHAR *fname_ts, const WCHAR *fname_pi_init, const proginfo_t *pi)
{
	WCHAR fname[MAX_PATH_LEN];

	if (!PGINFO_READY(pi->status)) {
		return;
	}

	if (fname_pi_init) {
		wcscpy_s(fname, MAX_PATH_LEN - 1, fname_ts);
		tsd_strncpy(fname, fname_ts, MAX_PATH_LEN);
		PathRemoveExtension(fname);
		tsd_strlcat(fname, MAX_PATH_LEN, TSD_TEXT("_init.txt"));
		MoveFile(fname_pi_init, fname);
	}
	create_proginfo_file(fname_ts, pi);
}

static int check_io_status(file_output_stat_t *fos, BOOL wait_mode)
{
	static int64_t last = 0;
	static DWORD errnum_last = 0;
	DWORD errnum;
	int64_t now;

	if (!fos->write_busy) {
		return 0;
	}

	static DWORD wb;
	if (!GetOverlappedResult(fos->fh, &(fos->ol), &wb, wait_mode)) {
		if ((errnum = GetLastError()) == ERROR_IO_INCOMPLETE) {
			/* do nothing */
			return 0;
		} else {
			/* IOエラー発生時 */
			now = gettime();
			if (now - last > 1000 || errnum_last != errnum) {
				output_message(MSG_SYSERROR, L"IOエラー(GetOverlappedResult)");
				errnum_last = errnum;
				last = now;
			}
			fos->write_busy = 0;
			return 1;
		}
	}

	fos->written_bytes += wb;
	int remain = fos->write_bytes - fos->written_bytes;
	if (remain == 0) {
		LARGE_INTEGER offset;
		offset.HighPart = fos->ol.OffsetHigh;
		offset.LowPart = fos->ol.Offset;
		offset.QuadPart += fos->write_bytes;
		fos->ol.OffsetHigh = offset.HighPart;
		fos->ol.Offset = offset.LowPart;
		fos->write_busy = 0;

		/*if (fos->close_flag) {
			fos->close_remain -= fos->oli.write_bytes;
		}*/
	} else {
		WriteFile(fos->fh, &(fos->writebuf[fos->write_bytes]), (DWORD)remain, &wb, &(fos->ol));
	}

	return 0;
}

static void *hook_pgoutput_create(const WCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info)
{
	UNREF_ARG(ch_info);

	HANDLE fh = CreateFile(
		fname,
		GENERIC_WRITE,
		FILE_SHARE_READ,
		NULL,
		CREATE_NEW,
		FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL
	);

	if (fh == INVALID_HANDLE_VALUE) {
		output_message(MSG_SYSERROR, L"ファイルをオープンできません(CreateFile): %s", fname);
		return NULL;
	}

	file_output_stat_t *fos = (file_output_stat_t*)malloc(sizeof(file_output_stat_t));
	fos->write_busy = 0;
	fos->fh = fh;
	fos->fn = _wcsdup(fname);
	memset(&(fos->ol), 0, sizeof(OVERLAPPED));

	fos->initial_pi = *pi;
	fos->fn_pi = create_proginfo_file(fname, pi);

	output_message(MSG_NOTIFY, L"[録画開始]: %s", fos->fn);
	return fos;
}

static void hook_pgoutput(void *pstat, const unsigned char *buf, const size_t size)
{
	DWORD written, errnum;
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return;
	}

	if (fos->write_busy) {
		output_message(MSG_ERROR, L"以前のファイルIOが完了しないうちに次のファイルIOを発行しました");
		return;
	}

	if ( !WriteFile(fos->fh, buf, (DWORD)size, &written, &(fos->ol)) ) {
		if ( (errnum = GetLastError()) == ERROR_IO_PENDING ) {
			fos->writebuf = buf;
			fos->write_bytes = size;
			fos->written_bytes = 0;
			fos->write_busy = 1;
			return;
		} else {
			output_message(MSG_SYSERROR, L"WriteFile()に失敗しました");
			fos->write_busy = 0;
		}
	} else {
		if (written < size) {
			fos->writebuf = buf;
			fos->write_bytes = size;
			fos->written_bytes = written;
			fos->write_busy = 1;
		} else {
			fos->write_busy = 0;
		}
	}
}

static const int hook_pgoutput_check(void *pstat)
{
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return 0;
	}

	if (fos->write_busy) {
		check_io_status(fos, FALSE);
	}
	return fos->write_busy;
}

static const int hook_pgoutput_wait(void *pstat)
{
	int err = 0;
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return 0;
	}

	while ( fos->write_busy ) {
		err = check_io_status(fos, TRUE);
	}
	return err;
}

static void hook_pgoutput_close(void *pstat, const proginfo_t *final_pi)
{
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return;
	}

	if (fos->write_busy) {
		output_message(MSG_ERROR, L"IOが完了しないうちにファイルを閉じようとしました");
		*((char*)NULL) = 0; /* segfault */
	}
	CloseHandle(fos->fh);

	if (fos->fn_pi) {
		/* 番組情報が途中で変わっていたら新しく作る */
		if ( proginfo_cmp(&fos->initial_pi, final_pi) ) {
			create_new_proginfo_file(fos->fn, fos->fn_pi, final_pi);
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
	register_hook_pgoutput_check(hook_pgoutput_check);
	register_hook_pgoutput_wait(hook_pgoutput_wait);
	register_hook_pgoutput_close(hook_pgoutput_close);
}

MODULE_DEF module_def_t mod_fileoutput_win = {
	TSDUMP_MODULE_V3,
	L"mod_fileoutput_win",
	register_hooks,
	NULL
};
