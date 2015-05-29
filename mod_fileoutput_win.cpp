#pragma comment(lib, "shlwapi.lib")

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include <sys/timeb.h>

#include <shlwapi.h>

#include "modules_def.h"

typedef struct {
	WCHAR *fn;
	WCHAR *fn_pi;
	HANDLE fh;
	OVERLAPPED ol;
	const unsigned char *writebuf;
	int write_bytes;
	int written_bytes;
	int write_busy;
	ProgInfo initial_pi;
} file_output_stat_t;

static inline __int64 gettime()
{
	__int64 result;
	_timeb tv;

	_ftime64_s(&tv);
	result = (__int64)tv.time * 1000;
	result += tv.millitm;

	return result;
}

static WCHAR* create_proginfo_file(const WCHAR *fname_ts, const ProgInfo *pi)
{
	WCHAR fname[MAX_PATH_LEN];
	WCHAR genre[1024];
	FILE *fp = NULL;
	errno_t err;

	if (!pi->isok) {
		fprintf(stderr, "[WARN] 番組情報が取得できなかったので番組情報ファイルを生成しません\n");
		return NULL;
	}

	wcscpy_s(fname, MAX_PATH_LEN - 1, fname_ts);
	PathRemoveExtension(fname);
	PathAddExtension(fname, L".txt");
	err = _wfopen_s(&fp, fname, L"wt, ccs=UTF-8");
	if (err) {
		fwprintf(stderr, L"[ERROR] 番組情報ファイルを保存できません: %s\n", fname);
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
		PathRemoveExtension(fname);
		wcsncat_s(fname, L"_init.txt", MAX_PATH_LEN - 1);
		MoveFile(fname_pi_init, fname);
	}
	create_proginfo_file(fname_ts, pi);
}

static int check_io_status(file_output_stat_t *fos, BOOL wait_mode)
{
	static __int64 last = 0;
	static DWORD errnum_last = 0;
	DWORD errnum;
	__int64 now;

	if (!fos->write_busy) {
		return 0;
	}

	static DWORD wb;
	if (!GetOverlappedResult(fos->fh, &(fos->ol), &wb, wait_mode)) {
		if ((errnum = GetLastError()) == ERROR_IO_INCOMPLETE) {
			/* do nothing */
			return 0;
		}
		else {
			/* IOエラー発生時 */
			now = gettime();
			if (now - last > 1000 || errnum_last != errnum) {
				print_err(L"[ERROR] GetOverlappedResult()", errnum);
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

static void *hook_pgoutput_create(const WCHAR *fname, const ProgInfo *pi, const ch_info_t*)
{
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
		print_err(L"[ERROR] CreateFile()", GetLastError());
		fwprintf(stderr, L"[ERROR] tsファイルをオープンできません: %s\n", fname);
		return NULL;
	}

	file_output_stat_t *fos = (file_output_stat_t*)malloc(sizeof(file_output_stat_t));
	fos->write_busy = 0;
	fos->fh = fh;
	fos->fn = _wcsdup(fname);
	memset(&(fos->ol), 0, sizeof(OVERLAPPED));

	fos->initial_pi = *pi;
	fos->fn_pi = create_proginfo_file(fname, pi);

	create_proginfo_file(fname, pi);

	wprintf(L"[START] %s\n", fos->fn);
	return fos;
}

static void hook_pgoutput(void *pstat, const unsigned char *buf, const size_t size)
{
	DWORD written;
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return;
	}

	if (fos->write_busy) {
		fprintf(stderr, "[ERROR] 以前のファイルIOが完了しないうちに次のファイルIOを発行しました\n");
		return;
	}

	WriteFile(fos->fh, buf, (DWORD)size, &written, &(fos->ol));
	fos->writebuf = buf;
	fos->write_bytes = size;
	fos->written_bytes = 0;
	fos->write_busy = 1;
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

static void hook_pgoutput_close(void *pstat, const ProgInfo *pi)
{
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return;
	}

	if (fos->write_busy) {
		fprintf(stderr, "[ERROR] IOが完了しないうちにファイルを閉じようとしました\n");
		*((char*)NULL) = 0; /* segfault */
	}
	CloseHandle(fos->fh);

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

	wprintf(L"[FINISHED] %s\n", fos->fn);

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
	TSDUMP_MODULE_V1,
	L"mod_fileoutput_win",
	register_hooks,
	NULL
};
