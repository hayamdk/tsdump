#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <Windows.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#endif

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <time.h>
#include <sys/timeb.h>

#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"
#include "utils/tsdstr.h"
#include "utils/path.h"
#include "utils/ts_parser.h"
#include "core/default_decoder.h"

static int flg_set_infile = 0;
static int flg_set_mbps = 0;
static int flg_set_unlim = 0;
static int flg_set_eof = 0;
static int reg_hooks;
static TSDCHAR infile_name[MAX_PATH_LEN];
static double mbps;

#define BLOCK_SIZE		(188*256)

typedef struct {
	int bytes;
	int eof;
	uint8_t buf[BLOCK_SIZE];
	uint8_t tmp_buf[BLOCK_SIZE];
#ifdef TSD_PLATFORM_MSVC
	int read_busy;
	HANDLE fh;
	OVERLAPPED ol;
#else
	int fd;
#endif
	int64_t timestamp_orig;
	int64_t timestamp;
	int64_t total_bytes;
	int PCR_set_orig;
	PSI_parse_t pid_PAT;
	PSI_parse_t pid_PMT;
	proginfo_t proginfo;
	ts_alignment_filter_t filter;
} filein_stat_t;

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

static int hook_postconfig()
{
	if (!flg_set_infile) {
		return 1;
	}
	if(!reg_hooks) {
		output_message(MSG_ERROR, TSD_TEXT("generatorフックの登録に失敗しました"));
		return 0;
	}
	return 1;
}

static void pat_handler(void *param, const int n, const int i, const PAT_item_t *PAT_item)
{
	UNREF_ARG(n);
	UNREF_ARG(i);
	filein_stat_t *stat = (filein_stat_t*)param;
	if (stat->proginfo.status & PGINFO_GET_PAT) {
		return;
	}
	if (PAT_item->program_number != 0) {
		stat->pid_PMT.pid = PAT_item->pid;
		stat->pid_PMT.stat = PAYLOAD_STAT_INIT;
		store_PAT(&stat->proginfo, PAT_item);
	}
}

static proginfo_t *pcr_handler(void *param, const unsigned int pcr_pid)
{
	filein_stat_t *stat = (filein_stat_t*)param;
	if (!(stat->proginfo.status & PGINFO_GET_PMT)) {
		return NULL;
	}
	if (stat->proginfo.PCR_pid != pcr_pid) {
		return NULL;
	}
	return &stat->proginfo;
}

void set_timpstamp_bytes(filein_stat_t *stat, int size)
{
	stat->total_bytes += size;
	int64_t timestamp_offset = (int64_t)( (double)stat->total_bytes / mbps / 1024 / 1024 * 8 * 1000 );
	stat->timestamp = stat->timestamp_orig + timestamp_offset;
}

static void set_timestamp_pcr(filein_stat_t *stat, const uint8_t *buf, const int size)
{
	uint8_t *buf_filtered;
	int size_filtered;
	ts_header_t tsh;
	int i, set=0;
	int64_t timestamp_offset = 0;

	ts_alignment_filter(&stat->filter, &buf_filtered, &size_filtered, buf, size);
	for (i = 0; i < size_filtered; i += 188) {
		if (!parse_ts_header(&buf_filtered[i], &tsh)) {
			/* 無効なパケットはスルー */
			continue;
		}
		if (tsh.transport_scrambling_control) {
			/* 暗号化されたパケットはスルー */
			continue;
		}
		if (!(stat->proginfo.status & PGINFO_GET_PAT)) {
			parse_PAT(&stat->pid_PAT, &buf_filtered[i], &tsh, stat, pat_handler);
		} else {
			parse_PMT(&buf_filtered[i], &tsh, &stat->pid_PMT, &stat->proginfo);
			parse_PCR(&buf_filtered[i], &tsh, stat, pcr_handler);
			if (stat->proginfo.status & PGINFO_VALID_PCR && stat->proginfo.status & PGINFO_PCR_UPDATED) {
				timestamp_offset = stat->proginfo.PCR_base * 1000 / PCR_BASE_HZ;
				if (!stat->PCR_set_orig) {
					stat->PCR_set_orig = 1;
					stat->timestamp_orig = gettime() - timestamp_offset;
				} else if (stat->proginfo.PCR_wraparounded) {
					stat->timestamp_orig += PCR_BASE_MAX * 1000 / PCR_BASE_MAX;
				}
				set = 1;
				stat->proginfo.status &= ~PGINFO_PCR_UPDATED;
			}
		}
	}
	if (!stat->PCR_set_orig) {
		stat->timestamp = gettime();
		return;
	}
	if (set) {
		stat->timestamp = stat->timestamp_orig + timestamp_offset;
	}
}

int wait_timestamp(filein_stat_t *stat, int timeout_ms)
{
	int64_t tn = gettime();
	int64_t offset;
	int remain = 0;

	/* 現在のタイムスタンプと5秒以上離れていたら不正なタイムスタンプとしてリセット */
	if (tn < stat->timestamp - 5 * 1000 || tn > stat->timestamp + 5 * 1000) {
		offset = tn - stat->timestamp;
		stat->timestamp = tn;
		stat->timestamp_orig += offset;
	}

	offset = stat->timestamp - tn;
	if (offset > timeout_ms) {
		remain = (int)(offset - timeout_ms);
		offset = timeout_ms;
	}

	if (offset > 0) {
#ifdef TSD_PLATFORM_MSVC
		Sleep((int)offset);
#else
		usleep((int)offset * 1000);
#endif
	}

	return remain;
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

static int check_read(filein_stat_t *stat, int timeout_ms)
{
	static int64_t last = 0;
	static DWORD errnum_last = 0;
	DWORD rb, errnum;
	int64_t now;

	if (!stat->read_busy) {
		return 1;
	}
	if (stat->eof) {
		return 1;
	}

	if (!GetOverlappedResult(stat->fh, &stat->ol, &rb, 0)) {
		if ((errnum = GetLastError()) == ERROR_IO_INCOMPLETE) {
			if (timeout_ms > 0) {
				WaitForSingleObject(stat->fh, timeout_ms);
				/* リトライチェック */
				return check_read(stat, 0);
			}
			return 0;
		} else if (errnum == ERROR_HANDLE_EOF) {
			stat->eof = 1;
			stat->read_busy = 0;
			return 1;
		} else {
			/* IOエラー発生時 */
			now = gettime();
			if (now - last > 1000 || errnum_last != errnum) {
				output_message(MSG_SYSERROR, L"IOエラー(GetOverlappedResult)");
				errnum_last = errnum;
				last = now;
			}
			stat->read_busy = 0;
			return 1;
		}
	}
	if (rb == 0) {
		stat->eof = 1;
	}
	add_ovelapped_offset(&stat->ol, rb);
	stat->bytes += rb;
	stat->read_busy = 0;
	return 1;
}

static void read_from_file(filein_stat_t *stat)
{
	DWORD rb, errnum;

	if (stat->read_busy) {
		return;
	}
	if (stat->bytes >= BLOCK_SIZE) {
		return;
	}
	if (stat->eof) {
		return;
	}

	if (ReadFile(stat->fh, &stat->buf[stat->bytes], BLOCK_SIZE - stat->bytes, &rb, &stat->ol)) {
		if (rb == 0) {
			stat->eof = 1;
			return;
		}
		stat->bytes += rb;
		add_ovelapped_offset(&stat->ol, rb);
	} else {
		if ((errnum = GetLastError()) == ERROR_IO_PENDING) {
			stat->read_busy = 1;
			return;
		} else if (errnum == ERROR_HANDLE_EOF) {
			stat->eof = 1;
			return;
		} else {
			output_message(MSG_SYSERROR, L"ReadFile()に失敗しました");
			stat->read_busy = 0;
		}
	}
}

#else

static void read_from_file(filein_stat_t *stat)
{
	int ret;
	if (stat->bytes >= BLOCK_SIZE) {
		return;
	}
	if (stat->eof) {
		return;
	}

	ret = read(stat->fd, &stat->buf[stat->bytes], BLOCK_SIZE - stat->bytes);
	if( ret < 0 ) {
		if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ) {
			/* do nothing */
		} else {
			output_message(MSG_SYSERROR, "read(fd=%d)", stat->fd);
		}
	} else {
		if (ret == 0) {
			stat->eof = 1;
		}
		stat->bytes += ret;
	}
}

static int check_read(filein_stat_t *stat, int timeout_ms)
{
	int ret;
	fd_set set;
	struct timeval tv;

	if(stat->bytes >= 188) {
		return 1;
	}

	FD_ZERO(&set);
	FD_SET(stat->fd, &set);
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = timeout_ms % 1000 * 1000;

	ret = select(stat->fd+1, &set, NULL, NULL, &tv);
	if( ret == 0 ) {
		return 0;
	} else if (ret == 1) {
		return 1;
	} else {
		if( errno != EINTR ) {
			output_message(MSG_SYSERROR, "select(fd=%d)", stat->fd);
		}
	}
	return 0;
}

#endif

static void hook_stream_generator(void *param, uint8_t **buf, int *size)
{
	int bytes188, remain;
	filein_stat_t *stat = (filein_stat_t*)param;

	read_from_file(stat);
	check_read(stat, 0);

	if (stat->eof && flg_set_eof) {
		request_shutdown(0);
	}

#ifdef TSD_PLATFORM_MSVC
	if (!stat->read_busy) {
#else
	{
#endif
		if (stat->bytes < 188) {
			goto RET_ZERO;
		}

		if (flg_set_mbps || !flg_set_unlim) {
			if (wait_timestamp(stat, 0) > 0) {
				goto RET_ZERO;
			}
		}

		bytes188 = stat->bytes / 188 * 188;
		remain = stat->bytes - bytes188;

		if (remain > 0) {
			memcpy(stat->tmp_buf, stat->buf, bytes188);
			memmove(&stat->buf[0], &stat->buf[bytes188], remain);
			*buf = stat->tmp_buf;
			*size = bytes188;
			stat->bytes = remain;
		} else {
			*buf = stat->buf;
			*size = bytes188;
			stat->bytes = 0;
		}

		if (flg_set_mbps) {
			set_timpstamp_bytes(stat, *size);
		} else if(!flg_set_unlim) {
			set_timestamp_pcr(stat, *buf, *size);
		} 
		
		return;
	}

RET_ZERO:
	*size = 0;
	*buf = NULL;
}

static int hook_stream_generator_wait(void *param, int timeout_ms)
{
	filein_stat_t *stat = (filein_stat_t*)param;
	if (stat->eof && stat->bytes < 188) {
#ifdef TSD_PLATFORM_MSVC
		Sleep(timeout_ms);
#else
		usleep(timeout_ms*1000);
#endif
		return 0;
	} else {
		if (flg_set_mbps || !flg_set_unlim) {
			timeout_ms = wait_timestamp(stat, timeout_ms);
			if(timeout_ms <= 0) {
				return 1;
			}
		}
	}
	return check_read(stat, timeout_ms);
}

static int hook_stream_generator_open(void **param, ch_info_t *chinfo)
{
	filein_stat_t *stat;
#ifdef TSD_PLATFORM_MSVC
	HANDLE fh = CreateFile(
		infile_name,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
		NULL
	);
	if (fh == INVALID_HANDLE_VALUE) {
		output_message(MSG_SYSERROR, L"ファイルをオープンできません(CreateFile): %s", infile_name);
		return 0;
	}
#else
	int fd = open(infile_name, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		output_message(MSG_SYSERROR, "ファイルをオープンできません(open): %s", infile_name);
		return 0;
	}
#endif
	stat = (filein_stat_t*)malloc(sizeof(filein_stat_t));
	stat->bytes = 0;
	stat->eof = 0;
	stat->total_bytes = 0;
	stat->timestamp = stat->timestamp_orig = gettime();
#ifdef TSD_PLATFORM_MSVC
	stat->read_busy = 0;
	memset(&stat->ol, 0, sizeof(OVERLAPPED));
#endif
	chinfo->tuner_name = TSD_TEXT("mod_filein");
	chinfo->sp_str = TSD_TEXT("ファイル入力");
	chinfo->ch_str = TSD_TEXT("ファイル入力");
#ifdef TSD_PLATFORM_MSVC
	stat->fh = fh;
#else
	stat->fd = fd;
#endif
	*param = stat;

	if (!flg_set_mbps && !flg_set_unlim) {
		init_proginfo(&stat->proginfo);
		create_ts_alignment_filter(&stat->filter);
		stat->pid_PAT.pid= 0;
		stat->pid_PAT.stat = PAYLOAD_STAT_INIT;
		stat->PCR_set_orig = 0;
	}
	return 1;
}

static void hook_stream_generator_close(void *param)
{
	filein_stat_t *stat = (filein_stat_t*)param;
#ifdef TSD_PLATFORM_MSVC
	CloseHandle(stat->fh);
#else
	close(stat->fd);
#endif
	free(stat);
}

static hooks_stream_generator_t hooks_stream_generator = {
	hook_stream_generator_open,
	hook_stream_generator,
	hook_stream_generator_wait,
	NULL,
	NULL,
	hook_stream_generator_close
};

static void register_hooks()
{
	if (flg_set_infile) {
		reg_hooks = register_hooks_stream_generator(&hooks_stream_generator);
	}
	register_hook_postconfig(hook_postconfig);
}

static const TSDCHAR *set_infile(const TSDCHAR* param)
{
	flg_set_infile = 1;
	tsd_strlcpy(infile_name, param, MAX_PATH_LEN - 1);
	return NULL;
}

static const TSDCHAR *set_mbps(const TSDCHAR* param)
{
	if (flg_set_unlim) {
		return TSD_TEXT("ファイルのビットレートが既に無制限に指定されています");
	}
	flg_set_mbps = 1;
	mbps = tsd_atof(param);
	if (mbps <= 0) {
		return TSD_TEXT("不正なビットレートです");
	}
	return NULL;
}

static const TSDCHAR *set_unlim(const TSDCHAR* param)
{
	UNREF_ARG(param);
	if (flg_set_mbps) {
		return TSD_TEXT("ファイルのビットレートが既に指定されています");
	}
	flg_set_unlim = 1;
	return NULL;
}

static const TSDCHAR *set_eof(const TSDCHAR* param)
{
	UNREF_ARG(param);
	flg_set_eof = 1;
	return NULL;
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--filein"), TSD_TEXT("入力ファイル"), 1, set_infile },
	{ TSD_TEXT("--filembps"), TSD_TEXT("ファイル読み込みのビットレート(Mbps)を指定"), 1, set_mbps },
	{ TSD_TEXT("--unlimited-filebps"), TSD_TEXT("無制限のビットレートでファイルを読み込む"), 0, set_unlim },
	{ TSD_TEXT("--eof"), TSD_TEXT("ファイルのEOFで終了する"), 0, set_eof },
	{ NULL },
};

TSD_MODULE_DEF(
	mod_filein,
	register_hooks,
	cmds,
	NULL
);