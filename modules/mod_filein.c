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

static int flg_set_infile = 0;
static int flg_set_mbps = 0;
static int reg_hooks;
static TSDCHAR infile_name[MAX_PATH_LEN];
static double mbps;

#define BLOCK_SIZE		(188*256)

typedef struct {
	int bytes;
	int eof;
	int64_t last_timestamp;
	int64_t timestamp;
	int timestamp_ns;
	uint8_t buf[BLOCK_SIZE];
	uint8_t tmp_buf[BLOCK_SIZE];
#ifdef TSD_PLATFORM_MSVC
	int read_busy;
	HANDLE fh;
	OVERLAPPED ol;
#else
	int fd;
#endif
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

void forward_timpstamp(filein_stat_t *stat, const uint8_t *buf, int size)
{
	UNREF_ARG(buf);
	double tick_ns = (double)size / mbps / 1024 / 1024 * 8 * 1000 * 1000 * 1000;
	int64_t tick_ms = (int64_t)(tick_ns / 1000 / 1000);
	int tick_ns_i = (int)(tick_ns - tick_ms);
	if (tick_ns_i < 0) {
		tick_ns_i = 0;
	}
	stat->timestamp_ns += tick_ns_i;
	if (stat->timestamp_ns >= 1000 * 1000) {
		stat->timestamp_ns -= 1000 * 1000;
		stat->timestamp++;
	}
	stat->timestamp += tick_ms;
}

int wait_timestamp(filein_stat_t *stat, int timeout_ms)
{
	int64_t tn = gettime();
	int64_t offset;
	int remain = 0;

	if (tn < stat->last_timestamp || tn > stat->last_timestamp + 5*1000) {
		/* 時間が巻き戻った、あるいは前回より5秒以上たっていたら不正なタイムスタンプとしてリセットする */
		stat->timestamp = tn;
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

	stat->last_timestamp = tn;
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

#ifdef TSD_PLATFORM_MSVC
	if (!stat->read_busy) {
#else
	{
#endif
		if (stat->bytes < 188) {
			goto RET_ZERO;
		}

		if (flg_set_mbps) {
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

		forward_timpstamp(stat, *buf, *size);

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
		if (flg_set_mbps) {
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
	stat->timestamp = gettime();
	stat->timestamp_ns = 0;
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
	tsd_strncpy(infile_name, param, MAX_PATH_LEN);
	return NULL;
}

static const TSDCHAR *set_mbps(const TSDCHAR* param)
{
	flg_set_mbps = 1;
	mbps = tsd_atof(param);
	if (mbps <= 0) {
		return TSD_TEXT("不正なビットレートです");
	}
	return NULL;
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--filein"), TSD_TEXT("入力ファイル"), 1, set_infile },
	{ TSD_TEXT("--filembps"), TSD_TEXT("ファイル読み込みのビットレート(Mbps)を指定"), 1, set_mbps },
	NULL,
};

MODULE_DEF module_def_t mod_filein = {
	TSDUMP_MODULE_V4,
	TSD_TEXT("mod_filein"),
	register_hooks,
	cmds
};