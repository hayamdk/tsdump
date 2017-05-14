#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#pragma comment(lib, "shlwapi.lib")
#include <windows.h>
#include <shlwapi.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <inttypes.h>
#include <assert.h>

#include "utils/arib_proginfo.h"
#include "core/module_api.h"
#include "utils/tsdstr.h"
#include "utils/path.h"

static int flg_set_no_fileout = 0;

#define FILEOUT_BLOCK_SIZE	1024*1024

typedef struct {
#ifdef TSD_PLATFORM_MSVC
	unsigned int write_busy : 1;
	HANDLE fh;
	OVERLAPPED ol;
	uint8_t writebuf[FILEOUT_BLOCK_SIZE];
	int written_bytes;
	int write_bytes;
#else
	int fd;
#endif
	TSDCHAR fn[MAX_PATH_LEN];
	int is_pi;
	TSDCHAR fn_pi[MAX_PATH_LEN];
	proginfo_t initial_pi;
} file_output_stat_t;

static inline int64_t gettime()
{
	int64_t result;
#ifdef TSD_PLATFORM_MSVC
	struct _timeb tv;
	_ftime64_s(&tv);
#else
	struct timeb tv;
	ftime(&tv);
#endif
	result = (int64_t)tv.time * 1000;
	result += tv.millitm;

	return result;
}

int create_proginfo_file(TSDCHAR *fname, const TSDCHAR *fname_ts, const proginfo_t *pi)
{
	TSDCHAR extended_text[4096];
	const TSDCHAR *genre1, *genre2;
	FILE *fp = NULL;
#ifdef TSD_PLATFORM_MSVC
	errno_t err;
#else
#endif
	int i;

	if (!PGINFO_READY(pi->status)) {
		output_message(MSG_WARNING, TSD_TEXT("番組情報が取得できなかったので番組情報ファイルを生成しません"));
		return 0;
	}

	tsd_strlcpy(fname, fname_ts, MAX_PATH_LEN - 1);
	path_changeext(fname, TSD_TEXT(".txt"));

#ifdef TSD_PLATFORM_MSVC
	err = _wfopen_s(&fp, fname, L"wt, ccs=UTF-8");
	if (err) {
#else
	fp = fopen(fname, "wt");
	if (!fp) {
#endif
		output_message(MSG_ERROR, TSD_TEXT("番組情報ファイルを保存できません: %s"), fname);
		return 0;
	}

	tsd_fprintf(fp, TSD_TEXT("%d%02d%02d\n%02d%02d%02d\n"), pi->start.year, pi->start.mon, pi->start.day,
		pi->start.hour, pi->start.min, pi->start.sec);
	tsd_fprintf(fp, TSD_TEXT("%02d%02d%02d\n"), pi->dur.hour, pi->dur.min, pi->dur.sec);
	tsd_fprintf(fp, TSD_TEXT("%s\n"), pi->service_name.str);
	tsd_fprintf(fp, TSD_TEXT("%s\n----------------\n"), pi->event_name.str);

	if (pi->status & PGINFO_GET_GENRE) {
		for (i = 0; i < pi->genre_info.n_items; i++) {
			get_genre_str(&genre1, &genre2, pi->genre_info.items[i]);
			tsd_fprintf(fp, TSD_TEXT("%s (%s)\n"), genre1, genre2);
		}
	}

	tsd_fprintf(fp, TSD_TEXT("----------------\n%s\n----------------\n"), pi->event_text.str);

	if (pi->status & PGINFO_GET_EXTEND_TEXT) {
		get_extended_text(extended_text, sizeof(extended_text)/sizeof(TSDCHAR), pi);
		tsd_fprintf(fp, TSD_TEXT("%s\n"), extended_text);
	}
	fclose(fp);

	return 1;
}

static void create_new_proginfo_file(const TSDCHAR *fname_ts, const TSDCHAR *fname_pi_init, const proginfo_t *pi)
{
	TSDCHAR fname[MAX_PATH_LEN];

	if (!PGINFO_READY(pi->status)) {
		return;
	}

	if (fname_pi_init) {
		tsd_strlcpy(fname, fname_ts, MAX_PATH_LEN - 1);
		path_removeext(fname);
		tsd_strlcat(fname, MAX_PATH_LEN, TSD_TEXT("_init.txt"));
#ifdef TSD_PLATFORM_MSVC
		MoveFile(fname_pi_init, fname);
#else
		rename(fname_pi_init, fname);
#endif
	}
	create_proginfo_file(fname, fname_ts, pi);
}

#ifdef TSD_PLATFORM_MSVC
static void add_ovelapped_offset(OVERLAPPED *ol, int add_offset)
{
	LARGE_INTEGER new_offset;
	new_offset.HighPart = ol->OffsetHigh;
	new_offset.LowPart = ol->Offset;
	new_offset.QuadPart += add_offset;
	ol->OffsetHigh = new_offset.HighPart;
	ol->Offset = new_offset.LowPart;
}

static int nonblock_write(file_output_stat_t *fos)
{
	int remain;
	DWORD written, errnum;

	while(1) {
		remain = fos->write_bytes - fos->written_bytes;
		if (remain <= 0) {
			fos->write_busy = 0;
			break;
		}

		if ( !WriteFile(fos->fh, &fos->writebuf[fos->written_bytes], (DWORD)remain, &written, &(fos->ol)) ) {
			if ((errnum = GetLastError()) == ERROR_IO_PENDING) {
				/* do nothing */
			} else {
				output_message(MSG_SYSERROR, TSD_TEXT("WriteFile()に失敗しました"));
				fos->write_busy = 0;
				break;
			}
			break;
		} else {
			fos->written_bytes += written;
			add_ovelapped_offset(&(fos->ol), written);
		}
	}
	return fos->write_busy;
}

static int check_io_status(file_output_stat_t *fos, int wait_mode)
{
	static int64_t last = 0;
	static DWORD errnum_last = 0;
	DWORD written, errnum;
	int64_t now;
	int remain;

	if (!fos->write_busy) {
		return 1;
	}

	if (!GetOverlappedResult(fos->fh, &(fos->ol), &written, wait_mode)) {
		if ((errnum = GetLastError()) == ERROR_IO_INCOMPLETE) {
			/* do nothing */
			return 1;
		} else {
			/* IOエラー発生時 */
			now = gettime();
			if (now - last > 1000 || errnum_last != errnum) {
				output_message(MSG_SYSERROR, L"IOエラー(GetOverlappedResult)");
				errnum_last = errnum;
				last = now;
			}
			fos->write_busy = 0;
			return 0;
		}
	}

#ifdef TSD_PLATFORM_MSVC
	add_ovelapped_offset(&fos->ol, written);
#endif
	fos->written_bytes += written;
	remain = fos->write_bytes - fos->written_bytes;
	if (remain <= 0) {
		fos->write_busy = 0;
	} else {
		nonblock_write(fos);
	}

	return fos->write_busy;
}

#else

static int nonblock_write(file_output_stat_t *fos, const uint8_t *buf, int write_bytes)
{
	int w = write_bytes * (rand() % 100) / 100 + 1;
	int written = write(fos->fd, buf, w);
	if (written < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
			/* do nothing */
			return 0;
		} else {
			output_message(MSG_SYSERROR, "write(fd=%d)", fos->fd);
			return 0;
		}
	}
	return written;
}

#endif

static void *hook_pgoutput_precreate(const TSDCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info, const int actually_start, int *n_output)
{
	UNREF_ARG(fname);
	UNREF_ARG(pi);
	UNREF_ARG(ch_info);
	UNREF_ARG(actually_start);
	*n_output = 1;
	return NULL;
}

static void *hook_pgoutput_create(void *param, const TSDCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info, const int actually_start)
{
	UNREF_ARG(param);
	UNREF_ARG(ch_info);
	UNREF_ARG(actually_start);

#ifdef TSD_PLATFORM_MSVC
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
#else
	int fd = open(fname, O_WRONLY | O_NONBLOCK | O_CREAT, 0644);
	if (fd < 0) {
		output_message(MSG_SYSERROR, "ファイルをオープンできません(open): %s", fname);
		return NULL;
	}
#endif

	file_output_stat_t *fos = (file_output_stat_t*)malloc(sizeof(file_output_stat_t));
#ifdef TSD_PLATFORM_MSVC
	fos->write_busy = 0;
	fos->fh = fh;
	memset(&(fos->ol), 0, sizeof(OVERLAPPED));
#else
	fos->fd = fd;
#endif
	tsd_strlcpy(fos->fn, fname, MAX_PATH_LEN-1);

	fos->initial_pi = *pi;
	fos->is_pi = create_proginfo_file(fos->fn_pi, fname, pi);

	output_message(MSG_NOTIFY, TSD_TEXT("[録画開始]: %s"), fos->fn);
	return fos;
}

static int hook_pgoutput(void *pstat, const uint8_t *buf, const size_t size)
{
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return 0;
	}

#ifdef TSD_PLATFORM_MSVC
	if (fos->write_busy) {
		output_message(MSG_ERROR, TSD_TEXT("ファイルIOが完了しないうちに新たなファイルIOを発行しました"));
		assert(1);
		return 1;
	}
	memcpy(fos->writebuf, buf, size);
	fos->write_busy = 1;
	fos->write_bytes = size;
	fos->written_bytes = 0;
	nonblock_write(fos);
#else
	return nonblock_write(fos, buf, size);
#endif
	return 1;
}

#ifdef TSD_PLATFORM_MSVC
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

#endif

static void hook_pgoutput_close(void *pstat, const proginfo_t *final_pi)
{
	file_output_stat_t *fos = (file_output_stat_t*)pstat;
	if (!fos) {
		return;
	}

#ifdef TSD_PLATFORM_MSVC
	if (fos->write_busy) {
		output_message(MSG_WARNING, TSD_TEXT("IOが完了しないうちにファイルを閉じようとしました"));
		CancelIo(fos->fh);
		check_io_status(fos, TRUE);
	}
	CloseHandle(fos->fh);
#else
	close(fos->fd);
#endif

	if (fos->is_pi) {
		/* 番組情報が途中で変わっていたら新しく作る */
		if ( proginfo_cmp(&fos->initial_pi, final_pi) ) {
			create_new_proginfo_file(fos->fn, fos->fn_pi, final_pi);
		}
	}

	output_message(MSG_NOTIFY, TSD_TEXT("[録画終了] %s"), fos->fn);

	free(fos);
}

static void register_hooks()
{
	if (!flg_set_no_fileout) {
		register_hook_pgoutput_precreate(hook_pgoutput_precreate);
		register_hook_pgoutput_create(hook_pgoutput_create);
		register_hook_pgoutput(hook_pgoutput, FILEOUT_BLOCK_SIZE);
#ifdef TSD_PLATFORM_MSVC
		register_hook_pgoutput_check(hook_pgoutput_check);
#else
		set_use_retval_pgoutput();
#endif
		register_hook_pgoutput_close(hook_pgoutput_close);
	}
}

static const TSDCHAR *set_no_fileout(const TSDCHAR* param)
{
	UNREF_ARG(param);
	flg_set_no_fileout = 1;
	return NULL;
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--no-fileout"), TSD_TEXT("ファイルを出力しない"), 0, set_no_fileout },
	{ NULL },
};

TSD_MODULE_DEF(
	mod_fileout,
	register_hooks,
	cmds,
	NULL
);
