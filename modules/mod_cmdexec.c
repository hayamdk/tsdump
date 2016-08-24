#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <windows.h>
#include <process.h>
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
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

#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"
#include "utils/tsdstr.h"
#include "core/tsdump.h"
#include "utils/path.h"

#define MAX_PIPECMDS		32
#define MAX_CMDS			32

typedef struct {
	int use_stdout;
	TSDCHAR stdout_path[MAX_PATH_LEN];
	int use_stderr;
	TSDCHAR stderr_path[MAX_PATH_LEN];
	int64_t timenum;
} redirect_pathinfo_t;

typedef struct {
	int used;
	int write_busy;
	const uint8_t *buf;
	int write_bytes;
	int written_bytes;
	const TSDCHAR *cmd;
	int connected;
	redirect_pathinfo_t redirects;
#ifdef TSD_PLATFORM_MSVC
	OVERLAPPED ol;
	HANDLE write_pipe;
	HANDLE child_process;
#else
	int fd_pipe;
	pid_t child_process;
#endif
} pipestat_t;

typedef struct {
	TSDCHAR filename[MAX_PATH_LEN];
	pipestat_t pipestats[MAX_PIPECMDS];
	proginfo_t last_proginfo;
} module_stat_t;

typedef struct {
	TSDCHAR cmd[MAX_PATH_LEN];
	TSDCHAR opt[2048];
	int set_opt;
	int connecting;
} cmd_opt_t;

typedef struct {
#ifdef TSD_PLATFORM_MSVC
	HANDLE child_process;
#else
	pid_t child_process;
#endif
	const TSDCHAR *cmd;
	int64_t lasttime;
	redirect_pathinfo_t redirects;
} orphan_process_info_t;

#define MAX_ORPHANS	128

static int n_orphans = 0;
static orphan_process_info_t orphans[MAX_ORPHANS];

static int n_pipecmds = 0;
static cmd_opt_t pipecmds[MAX_PIPECMDS];
static int pcwindow_min = 0;
static int open_connect = 0;

static int n_execcmds = 0;
static cmd_opt_t execcmds[MAX_CMDS];
static int cwindow_min = 0;

static int output_redirect = 0;
static TSDCHAR output_redirect_dir[MAX_PATH_LEN];

static int get_primary_video_pid(const proginfo_t *proginfo)
{
	int i;
	if (PGINFO_READY(proginfo->status)) {
		for (i = 0; i < proginfo->n_service_pids; i++) {
			if (proginfo->service_pids[i].stream_type == 0x02) {
				return proginfo->service_pids[i].pid;
			}
		}
	}
	return -1;
}

static int get_primary_audio_pid(const proginfo_t *proginfo)
{
	int i;
	if (PGINFO_READY(proginfo->status)) {
		for (i = 0; i < proginfo->n_service_pids; i++) {
			if (proginfo->service_pids[i].stream_type == 0x0f) {
				return proginfo->service_pids[i].pid;
			}
		}
	}
	return -1;
}

static void generate_arg(TSDCHAR *arg, size_t maxlen_arg, const cmd_opt_t *cmd, const TSDCHAR *fname, const proginfo_t *proginfo)
{
	const TSDCHAR *chname = TSD_TEXT("unknown"), *progname = TSD_TEXT("unkonwn");
	int year, mon, day, hour, min, sec;
	TSDCHAR fname_base[MAX_PATH_LEN];
	const TSDCHAR *fname_base_r, *fname_r;
	TSDCHAR tn_str[20], year_str[5], mon_str[3], day_str[3], hour_str[3], min_str[3], sec_str[3];
	TSDCHAR mon_str0[3], day_str0[3], hour_str0[3], min_str0[3], sec_str0[3];
	TSDCHAR v_pid_str[8], a_pid_str[8], eid_str[8], tsid_str[8], nid_str[8], sid_str[8];
	time_t t;
	struct tm lt;
	int64_t timenum;
	tsdstr_replace_set_t sets[32];
	int n_sets = 0;
	int v_pid, a_pid;

	if (PGINFO_READY(proginfo->status)) {
		chname = proginfo->event_name.str;
		progname = proginfo->service_name.str;

		timenum = timenum_timemjd(proginfo->start);
		year = proginfo->start.year;
		mon = proginfo->start.mon;
		day = proginfo->start.day;
		hour = proginfo->start.hour;
		min = proginfo->start.min;
		sec = proginfo->start.sec;

		tsd_snprintf(eid_str, tsd_sizeof(eid_str), TSD_TEXT("%d"), proginfo->event_id);
		tsd_snprintf(tsid_str, tsd_sizeof(tsid_str), TSD_TEXT("%d"), proginfo->ts_id);
		tsd_snprintf(nid_str, tsd_sizeof(nid_str), TSD_TEXT("%d"), proginfo->network_id);
		tsd_snprintf(sid_str, tsd_sizeof(sid_str), TSD_TEXT("%d"), proginfo->service_id);
	} else {
		timenum = timenumnow();

		t = time(NULL);
#ifdef TSD_PLATFORM_MSVC
		localtime_s(&lt, &t);
#else
		lt = *(localtime(&t));
#endif
		year = lt.tm_year + 1900;
		mon = lt.tm_mon + 1;
		day = lt.tm_mday;
		hour = lt.tm_hour;
		min = lt.tm_min;
		sec = lt.tm_sec;

		eid_str[0] = TSD_NULLCHAR;
		tsid_str[0] = TSD_NULLCHAR;
		nid_str[0] = TSD_NULLCHAR;
		sid_str[0] = TSD_NULLCHAR;
	}

	tsd_strlcpy(fname_base, fname, MAX_PATH_LEN - 1);
	path_removeext(fname_base);
	fname_r = path_getfile(fname);
	fname_base_r = path_getfile(fname_base);

	v_pid = get_primary_video_pid(proginfo);
	a_pid = get_primary_audio_pid(proginfo);

	tsd_snprintf(tn_str, 20, TSD_TEXT("%"PRId64), timenum );
	tsd_snprintf(year_str, 5, TSD_TEXT("%d"), year);
	tsd_snprintf(mon_str, 3, TSD_TEXT("%d"), mon);
	tsd_snprintf(day_str, 3, TSD_TEXT("%d"), day);
	tsd_snprintf(hour_str, 3, TSD_TEXT("%d"), hour);
	tsd_snprintf(min_str, 3, TSD_TEXT("%d"), min);
	tsd_snprintf(sec_str, 3, TSD_TEXT("%d"), sec);
	tsd_snprintf(mon_str0, 3, TSD_TEXT("%02d"), mon);
	tsd_snprintf(day_str0, 3, TSD_TEXT("%02d"), day);
	tsd_snprintf(hour_str0, 3, TSD_TEXT("%02d"), hour);
	tsd_snprintf(min_str0, 3, TSD_TEXT("%02d"), min);
	tsd_snprintf(sec_str0, 3, TSD_TEXT("%02d"), sec);

	if (v_pid >= 0) {
		tsd_snprintf(v_pid_str, 8, TSD_TEXT("%d"), v_pid);
	} else {
		v_pid_str[0] = TSD_NULLCHAR;
	}

	if (a_pid >= 0) {
		tsd_snprintf(a_pid_str, 8, TSD_TEXT("%d"), a_pid);
	} else {
		a_pid_str[0] = TSD_NULLCHAR;
	}

	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%Q%"), TSD_TEXT("\""));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%BS%"), TSD_TEXT("\\"));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%%"), TSD_TEXT("%"));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%FILE%"), fname);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%FILENE%"), fname_base);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%FNAME%"), fname_r);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%FNAMENE%"), fname_base_r);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%CH%"), chname);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%PROG%"), progname);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%TN%"), tn_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%Y%"), year_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%M%"), mon_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%D%"), day_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%h%"), hour_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%m%"), min_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%s%"), sec_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%MM%"), mon_str0);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%DD%"), day_str0);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%hh%"), hour_str0);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%mm%"), min_str0);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%ss%"), sec_str0);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%PID_V%"), v_pid_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%PID_A%"), a_pid_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%EVID%"), eid_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%TSID%"), tsid_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%NID%"), nid_str);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%SID%"), sid_str);

	tsd_strlcpy(arg, cmd->opt, maxlen_arg - 1);
	tsd_replace_sets(arg, maxlen_arg - 1, sets, n_sets, 0);
}

#ifdef TSD_PLATFORM_MSVC
static HANDLE create_redirect_file(TSDCHAR *path)
{
	HANDLE fh;
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

#else
static int create_redirect_file(TSDCHAR *path)
{
	int fd;
#endif
	static int counter = 0;
	TSDCHAR fname[MAX_PATH_LEN];

	if (!output_redirect) {
#ifdef TSD_PLATFORM_MSVC
		return INVALID_HANDLE_VALUE;
#else
		return -1;
#endif
	}

	tsd_snprintf(fname, MAX_PATH_LEN, TSD_TEXT("tmp_%"PRId64"_%d_%d"), gettime(), _getpid(), counter++);
	path_join(path, output_redirect_dir, fname);

#ifdef TSD_PLATFORM_MSVC
	fh = CreateFile(path,
		GENERIC_WRITE,
		FILE_SHARE_READ,
		&sa,
		CREATE_NEW,
		FILE_ATTRIBUTE_NORMAL,
		NULL
	);
	if (fh == INVALID_HANDLE_VALUE) {
		output_message(MSG_SYSERROR, L"リダイレクトファイルをオープンできません(CreateFile): %s", path);
	}
	return fh;
#else
	fd = open(path, O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		output_message(MSG_SYSERROR, "リダイレクトファイルをオープンできません(open): %s", fname);
	}
	return fd;
#endif
}

static void copy_redirect_pathinfo(redirect_pathinfo_t *dst, const redirect_pathinfo_t *src)
{
	if (!output_redirect) {
		return;
	}
	dst->use_stdout = src->use_stdout;
	if (dst->use_stdout) {
		tsd_strlcpy(dst->stdout_path, src->stdout_path, MAX_PATH_LEN-1);
	}
	dst->use_stderr = src->use_stderr;
	if (dst->use_stderr) {
		tsd_strlcpy(dst->stderr_path, src->stderr_path, MAX_PATH_LEN - 1);
	}
	dst->timenum = src->timenum;
}

static void rename_redirect_file(int pid, orphan_process_info_t *orphan)
{
	redirect_pathinfo_t *redirects;
	TSDCHAR fname[MAX_PATH_LEN], path[MAX_PATH_LEN];

	if (!output_redirect) {
		return;
	}
	redirects = &orphan->redirects;

	if (redirects->use_stdout) {
		tsd_snprintf(fname, MAX_PATH_LEN - 1, TSD_TEXT("%"PRId64"_%d_stdout.out"), redirects->timenum, pid);
		path_join(path, output_redirect_dir, fname);
#ifdef TSD_PLATFORM_MSVC
		if (!MoveFile(redirects->stdout_path, path)) {
#else
		if (rename(redirects->stdout_path, path)) {
#endif
			output_message(MSG_SYSERROR, TSD_TEXT("リダイレクトファイルのリネームに失敗しました: %s -> %s"),
				redirects->stdout_path, path);
		}
	}
	if (redirects->use_stderr) {
		tsd_snprintf(fname, MAX_PATH_LEN - 1, TSD_TEXT("%"PRId64"_%d_stderr.out"), redirects->timenum, pid);
		path_join(path, output_redirect_dir, fname);
#ifdef TSD_PLATFORM_MSVC
		if (!MoveFile(redirects->stderr_path, path)) {
#else
		if (rename(redirects->stderr_path, path)) {
#endif
			output_message(MSG_SYSERROR, TSD_TEXT("リダイレクトファイルのリネームに失敗しました: %s -> %s"),
				redirects->stderr_path, path);
		}
	}
}

static int pid_of(orphan_process_info_t *orphan)
{
#ifdef TSD_PLATFORM_MSVC
	return (int)GetProcessId(orphan->child_process);
#else
	return (int)orphan->child_process;
#endif
}

#ifdef TSD_PLATFORM_MSVC

/* 出力用パイプの作成 */
static int create_pipe1(HANDLE *h_read, HANDLE *h_write)
{
	static int counter = 0;
	HANDLE h_pipe, h_read_temp;
	WCHAR pipe_path[1024];
	DWORD ret;

	tsd_snprintf(pipe_path, 1024 - 1, TSD_TEXT("\\\\.\\pipe\\tsdump_pipe_%d_%d_%"PRId64""), _getpid(), counter++, gettime());

	h_pipe = CreateNamedPipe(pipe_path,
			PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE,
			2,
			1024*1024,
			0,
			100,
			NULL
		);
	if (h_pipe == INVALID_HANDLE_VALUE) {
		output_message(MSG_SYSERROR, L"パイプの作成に失敗(CreateNamedPipe)");
		return 0;
	}

	h_read_temp = CreateFile(pipe_path,
			GENERIC_READ,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
			NULL
		);
	if (h_read_temp == INVALID_HANDLE_VALUE) {
		output_message(MSG_SYSERROR, L"パイプのオープンに失敗(CreateFile)");
		CloseHandle(h_pipe);
		return 0;
	}

	if (!ConnectNamedPipe(h_pipe, NULL)) {
		if (GetLastError() != 535) {
			output_message(MSG_SYSERROR, L"パイプの接続に失敗(ConnectNamedPipe)");
			CloseHandle(h_read_temp);
			CloseHandle(h_pipe);
			return 0;
		}
	}

	ret = DuplicateHandle(
			GetCurrentProcess(),
			h_read_temp,
			GetCurrentProcess(),
			h_read,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS
		);
	CloseHandle(h_read_temp);

	if (!ret) {
		output_message(MSG_SYSERROR, L"DuplicateHandle()に失敗");
		CloseHandle(*h_read);
		CloseHandle(h_pipe);
		return 0;
	}

	*h_write = h_pipe;
	return 1;
}

/* 子プロセス→子プロセスの接続用パイプの作成 */
static int create_pipe2(HANDLE *h_read, HANDLE *h_write)
{
	HANDLE h_read_temp, h_write_temp;
	DWORD ret;

	if (!CreatePipe(&h_read_temp, &h_write_temp, NULL, 0)) {
		output_message(MSG_SYSERROR, L"パイプの作成に失敗(CreatePipe)");
		return 0;
	}

	ret = DuplicateHandle(
			GetCurrentProcess(),
			h_read_temp,
			GetCurrentProcess(),
			h_read,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS
		);
	CloseHandle(h_read_temp);

	if (!ret) {
		output_message(MSG_SYSERROR, L"DuplicateHandle()に失敗");
		CloseHandle(h_write_temp);
		return 0;
	}

	ret = DuplicateHandle(
			GetCurrentProcess(),
			h_write_temp,
			GetCurrentProcess(),
			h_write,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS
		);
	CloseHandle(h_write_temp);

	if (!ret) {
		output_message(MSG_SYSERROR, L"DuplicateHandle()に失敗");
		CloseHandle(*h_read);
		return 0;
	}

	return 1;
}

static int exec_child(pipestat_t *ps, const cmd_opt_t *pipe_cmd, const WCHAR *fname, const proginfo_t *proginfo, const HANDLE *prev, HANDLE *next)
{
	HANDLE h_pipe = INVALID_HANDLE_VALUE, h_read, h_write, h_error;
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};

	WCHAR cmdarg[2048];

	ps->used = 0;

	if (prev) {
		h_read = *prev;
	} else {
		if (!create_pipe1(&h_read, &h_pipe)) {
			return 0;
		}
	}

	if (next) {
		if (!create_pipe2(next, &h_write)) {
			CloseHandle(h_read);
			if (!prev) {
				CloseHandle(h_pipe);
			}
			return 0;
		}
		ps->redirects.use_stdout = 0;
	} else {
		h_write = create_redirect_file(ps->redirects.stdout_path);
		if (h_write == INVALID_HANDLE_VALUE) {
			ps->redirects.use_stdout = 0;
		} else {
			ps->redirects.use_stdout = 1;
		}
	}

	h_error = create_redirect_file(ps->redirects.stderr_path);
	if (h_error == INVALID_HANDLE_VALUE) {
		ps->redirects.use_stderr = 0;
	} else {
		ps->redirects.use_stderr = 1;
	}

	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = h_read;
	/* 以下の2つに0を指定することで子プロセスのコンソールにカーソルを合わせても親プロセスのコンソールがブロックしない */
	/* 定義済みの挙動なのかは未調査 */
	if (next || ps->redirects.use_stdout) {
		si.hStdOutput = h_write;
	} else {
		si.hStdOutput = 0;
	}
	if (ps->redirects.use_stderr) {
		si.hStdError = h_error;
	} else {
		si.hStdError = 0;
	}

	if (output_redirect) {
		ps->redirects.timenum = timenumnow();
	}

	if (pcwindow_min) {
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_MINIMIZE;
	}

	if (pipe_cmd->set_opt) {
		swprintf(cmdarg, 2048 - 1, L"\"%s\" ", pipe_cmd->cmd);
		size_t len = tsd_strlen(cmdarg);
		generate_arg(&cmdarg[len], 2048 - 1 - len, pipe_cmd, fname, proginfo);
	} else {
		swprintf(cmdarg, 2048 - 1, L"\"%s\" \"%s\"", pipe_cmd->cmd, fname);
	}

	if (!CreateProcess(pipe_cmd->cmd, cmdarg, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
		output_message(MSG_SYSERROR, L"子プロセスの生成に失敗(CreateProcess): %s", pipe_cmd->cmd);
		CloseHandle(h_read);
		if (!prev) {
			CloseHandle(h_pipe);
		}
		if (next || ps->redirects.use_stdout) {
			CloseHandle(h_write);
		}
		if (ps->redirects.use_stderr) {
			CloseHandle(h_error);
		}
		return 0;
	}

	output_message(MSG_NOTIFY, L"パイプコマンド実行(pid=%d): %s ", (int)GetProcessId(pi.hProcess), cmdarg);

	ps->child_process = pi.hProcess;
	ps->write_pipe = h_pipe;
	if (!prev) {
		ps->connected = 0;		
	} else {
		ps->connected = 1;
	}
	ps->used = 1;
	ps->write_busy = 0;
	ps->cmd = pipe_cmd->cmd;
	memset(&ps->ol, 0, sizeof(OVERLAPPED));

	CloseHandle(pi.hThread);
	CloseHandle(h_read);
	if (next || ps->redirects.use_stdout) {
		CloseHandle(h_write);
	}
	if (ps->redirects.use_stderr) {
		CloseHandle(h_error);
	}

	return 1;
}

static HANDLE exec_cmd(const cmd_opt_t *cmd, const WCHAR *fname, const proginfo_t *proginfo, redirect_pathinfo_t *redirects)
{
	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	WCHAR cmdarg[2048];
	HANDLE h_stdout, h_stderr;

	si.cb = sizeof(STARTUPINFO);

	if (cwindow_min) {
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_MINIMIZE;
	}

	if (cmd->set_opt) {
		swprintf(cmdarg, 2048 - 1, L"\"%s\" ", cmd->cmd);
		size_t len = tsd_strlen(cmdarg);
		generate_arg(&cmdarg[len], 2048 - 1 - len, cmd, fname, proginfo);
	} else {
		swprintf(cmdarg, 2048 - 1, L"\"%s\" \"%s\"", cmd->cmd, fname);
	}

	if (output_redirect) {
		si.cb = sizeof(STARTUPINFO);
		si.dwFlags = STARTF_USESTDHANDLES;

		h_stdout = create_redirect_file(redirects->stdout_path);
		if (h_stdout != INVALID_HANDLE_VALUE) {
			redirects->use_stdout = 1;
			si.hStdOutput = h_stdout;
		} else {
			redirects->use_stdout = 0;
			si.hStdOutput = 0;
		}
		h_stderr = create_redirect_file(redirects->stderr_path);
		if (h_stderr != INVALID_HANDLE_VALUE) {
			redirects->use_stderr = 1;
			si.hStdError = h_stderr;
		} else {
			redirects->use_stderr = 0;
			si.hStdError = 0;
		}

		redirects->timenum = timenumnow();
	}

	if (!CreateProcess(cmd->cmd, cmdarg, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
		output_message(MSG_SYSERROR, L"子プロセスの生成に失敗(CreateProcess): %s", cmdarg);
		return INVALID_HANDLE_VALUE;
	}

	output_message(MSG_NOTIFY, L"コマンド実行(pid=%d): %s", (int)GetProcessId(pi.hProcess), cmdarg);

	CloseHandle(pi.hThread);
	if (output_redirect) {
		if (redirects->use_stdout) {
			CloseHandle(h_stdout);
		}
		if (redirects->use_stderr) {
			CloseHandle(h_stderr);
		}
	}
	return pi.hProcess;
}

#else

static void split_args(char *argline, char *args[], int *n_args, int max_args)
{
	const char *p = argline;
	char *q = argline;
	char c;
	int n = 0;
	int proc = 0, in_quote = 0, bs = 0;
	for (n = 0; *p != '\0' && n < max_args; p++) {
		c = *p;
		switch (c) {
			case ' ':
				if (proc) {
					if (!in_quote && !bs) {
						c = '\0';
						proc = 0;
					}
				} else {
					continue;
				}
				break;
			case '\\':
				if (bs) {
					bs = 0;
				} else {
					if (!proc) {
						q++;
						args[n] = q;
						n++;
						proc = 1;
					}
					bs = 1;
					continue;
				}
				break;
			case '"':
				if (bs) {
					bs = 0;
					break;
				}
				if (in_quote) {
					in_quote = 0;
				} else {
					if (!proc) {
						q++;
						args[n] = q;
						n++;
						proc = 1;
					}
					in_quote = 1;
				}
				continue;
			default:
				bs = 0;
				if (!proc) {
					args[n] = q;
					n++;
					proc = 1;
				}
		}
		*q = c;
		q++;
	}
	*q = '\0';
	*n_args = n;
}

static int create_pipe1(int *readfd, int *writefd)
{
	int pipefds[2];
	if (pipe(pipefds) < 0) {
		output_message(MSG_SYSERROR, "パイプの生成に失敗(pipe)");
		return 0;
	}

	if (fcntl(pipefds[1], F_SETFL, O_NONBLOCK) < 0) {
		output_message(MSG_SYSERROR, "パイプをノンブロッキングに設定できませんでした(fcntl)");
		close(pipefds[0]);
		close(pipefds[1]);
		return 0;
	}

	if (fcntl(pipefds[1], F_SETFD, FD_CLOEXEC) < 0) {
		/* FD_CLOEXECを指定しないと複数の子プロセスを作ったときにパイプが閉じられない */
		output_message(MSG_SYSERROR, "パイプにFD_CLOEXECを設定できませんでした(fcntl)");
		close(pipefds[0]);
		close(pipefds[1]);
		return 0;
	}
	*readfd = pipefds[0];
	*writefd = pipefds[1];
	return 1;
}

static int create_pipe2(int *readfd, int *writefd)
{
	int pipefds[2];
	if (pipe(pipefds) < 0) {
		output_message(MSG_SYSERROR, "パイプの生成に失敗(pipe)");
		return 0;
	}
	*readfd = pipefds[0];
	*writefd = pipefds[1];
	return 1;
}

static int exec_child(pipestat_t *ps, const cmd_opt_t *pipe_cmd, const char *fname, const proginfo_t *proginfo, const int *prev, int *next)
{
	int ret = 0, status;
	int readfd, writefd, conn_readfd, conn_writefd, stderrfd;
	pid_t pid, pid_ret;
	char argline[2048], argline2[2048];
	int n_args;
	char *args[32];

	if (prev) {
		readfd = *prev;
	} else {
		if (!create_pipe1(&readfd, &writefd)) {
			goto ERROR1;
		}
	}

	if (next) {
		if (!create_pipe2(&conn_readfd, &conn_writefd)) {
			goto ERROR2;
		}
	} else {
		conn_writefd = create_redirect_file(ps->redirects.stdout_path);
		if (conn_writefd >= 0) {
			ps->redirects.use_stdout = 1;
		} else {
			ps->redirects.use_stdout = 0;
		}
	}

	stderrfd = create_redirect_file(ps->redirects.stderr_path);
	if (stderrfd >= 0) {
		ps->redirects.use_stderr = 1;
	} else {
		ps->redirects.use_stderr = 0;
	}
	if (output_redirect) {
		ps->redirects.timenum = timenumnow();
	}

	if (pipe_cmd->set_opt) {
		generate_arg(argline, 2048 - 1, pipe_cmd, fname, proginfo);
		tsd_strlcpy(argline2, argline, 2048);
		args[0] = (char*)pipe_cmd->cmd;
		split_args(argline2, &args[1], &n_args, 32 - 2);
		args[1 + n_args] = NULL;
	} else {
		args[0] = (char*)pipe_cmd->cmd;
		args[1] = (char*)fname;
		args[2] = NULL;
		snprintf(argline, 2048, "\"%s\"", fname);
	}

	if ( (pid=fork()) < 0 ) {
		output_message(MSG_SYSERROR, "子プロセスを作成できませんでした(fork): %s %s", pipe_cmd->cmd, argline);
		goto ERROR3;
	} else if ( pid == 0 ) {
		/* child */
		if (!prev) {
			close(writefd);
		}
		dup2(readfd, STDIN_FILENO);
		close(readfd);

		if (next) {
			dup2(conn_writefd, STDOUT_FILENO);
			close(conn_writefd);
			close(conn_readfd);
		} else if (ps->redirects.use_stdout) {
			dup2(conn_writefd, STDOUT_FILENO);
			close(conn_writefd);
		}

		if (ps->redirects.use_stderr) {
			dup2(stderrfd, STDERR_FILENO);
			close(stderrfd);
		}
		
		ret = execvp(pipe_cmd->cmd, args);
		if (ret < 0) {
			exit(1);
		}
	} else {
		output_message(MSG_NOTIFY, "パイプコマンド実行(pid=%d): %s %s", (int)pid, pipe_cmd->cmd, argline);
	}

	pid_ret = waitpid(pid, &status, WNOHANG);
	if (pid_ret < 0) {
		output_message(MSG_SYSERROR, "子プロセスの状態を取得できませんでした(waitpid): pid=%d", (int)pid);
		kill(pid, SIGKILL);
		goto ERROR3;
	} else if (pid_ret > 0) {
		if (WIFEXITED(status)) {
			output_message(MSG_ERROR, "子プロセスがすぐに終了しました(waitpid): pid=%d, exitcode=%02x", (int)pid, WEXITSTATUS(status));
		} else {
			output_message(MSG_ERROR, "子プロセスがすぐに終了しました(waitpid): pid=%d", (int)pid);
		}
		goto ERROR3;
	}

	if (prev) {
		ps->fd_pipe = -1;
		ps->connected = 1;
	} else {
		ps->fd_pipe = writefd;
		ps->connected = 0;
	}

	if (next) {
		*next = conn_readfd;
	}

	ps->used = 1;
	ps->cmd = pipe_cmd->cmd;
	ps->write_busy = 0;
	ps->child_process = pid;

	ret = 1;

ERROR3:
	if (next) {
		if (!ret) {
			close(conn_readfd);
		}
		close(conn_writefd);
	} else if(ps->redirects.use_stdout) {
		close(conn_writefd);
	}
	if (ps->redirects.use_stderr) {
		close(stderrfd);
	}
ERROR2:
	if (!prev && !ret) {
		close(writefd);
	}
	close(readfd);
ERROR1:
	return ret;
}

static pid_t exec_cmd(const cmd_opt_t *cmd, const char *fname, const proginfo_t *proginfo, redirect_pathinfo_t *redirects)
{
	int ret;
	pid_t pid;
	char argline[2048], argline2[2048];
	int n_args, fd_stdout, fd_stderr;
	char *args[32];

	if (cmd->set_opt) {
		generate_arg(argline, 2048 - 1, cmd, fname, proginfo);
		tsd_strlcpy(argline2, argline, 2048);
		args[0] = (char*)cmd->cmd;
		split_args(argline2, &args[1], &n_args, 32 - 2);
		args[1 + n_args] = NULL;
	} else {
		args[0] = (char*)cmd->cmd;
		args[1] = (char*)fname;
		args[2] = NULL;
		snprintf(argline, 2048, "\"%s\"", fname);
	}

	if (output_redirect) {
		fd_stdout = create_redirect_file(redirects->stdout_path);
		if (fd_stdout >= 0) {
			redirects->use_stdout = 1;
		} else {
			redirects->use_stdout = 0;
		}
		fd_stderr = create_redirect_file(redirects->stderr_path);
		if (fd_stderr >= 0) {
			redirects->use_stderr = 1;
		} else {
			redirects->use_stderr = 0;
		}
		redirects->timenum = timenumnow();
	}

	if ( (pid=fork()) < 0 ) {
		output_message(MSG_SYSERROR, "子プロセスを作成できませんでした(fork): %s %s", cmd->cmd, argline);
	} else if ( pid == 0 ) {
		/* child */	
		if (output_redirect) {
			if (redirects->use_stdout) {
				dup2(fd_stdout, STDOUT_FILENO);
				close(fd_stdout);
			}
			if (redirects->use_stderr) {
				dup2(fd_stderr, STDERR_FILENO);
				close(fd_stderr);
			}
		}
		ret = execvp(cmd->cmd, args);
		if (ret < 0) {
			exit(1);
		}
	} else {
		output_message(MSG_NOTIFY, "コマンド実行(pid=%d): %s %s", (int)pid, cmd->cmd, argline);
	}

	if (output_redirect) {
		if (redirects->use_stdout) {
			close(fd_stdout);
		}
		if (redirects->use_stderr) {
			close(fd_stderr);
		}
	}

	return pid;
}

#endif

static void *hook_pgoutput_create(const TSDCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info_t, const int actually_start)
{
#ifdef TSD_PLATFORM_MSVC
	HANDLE
#else
	int
#endif
		prev, next, *p_prev = NULL;
	int i, oc = 0;
	UNREF_ARG(ch_info_t);
	UNREF_ARG(actually_start);
	module_stat_t *stat = (module_stat_t*)malloc(sizeof(module_stat_t));
	for (i = 0; i < n_pipecmds; i++) {
		if (oc && !p_prev) {
			/* 前のコマンドから接続があるのに前のコマンドの実行が失敗している場合実行をパス */
			if (!pipecmds[i].connecting) {
				oc = 0;
			}
			stat->pipestats[i].used = 0;
			continue;
		}
		if (pipecmds[i].connecting) {
			if (!exec_child(&stat->pipestats[i], &pipecmds[i], fname, pi, p_prev, &next)) {
				p_prev = NULL;
			} else {
				prev = next;
				p_prev = &prev;
			}
			oc = 1;
		} else {
			exec_child(&stat->pipestats[i], &pipecmds[i], fname, pi, p_prev, NULL);
			oc = 0;
		}
	}
	tsd_strlcpy(stat->filename, fname, MAX_PATH_LEN - 1);
	return stat;
}

static void close_pipe(pipestat_t *ps)
{
	ps->write_busy = 0;
#ifdef TSD_PLATFORM_MSVC
	CloseHandle(ps->write_pipe);
#else
	close(ps->fd_pipe);
#endif
}


#ifndef TSD_PLATFORM_MSVC

static void kill_child_process(pid_t pid)
{
	int i, ret, status;
	kill(pid, SIGKILL);
	for(i=0; i<10; i++) {
		ret = waitpid(pid, &status, WNOHANG);
		if (ret > 0) {
			/*強制終了完了*/
			return;
		} else if (ret < 0) {
			output_message(MSG_SYSERROR, "子プロセスを強制終了しましたがwaitpidがエラーを返しました");
			return;
		}
		usleep(1000);
	}
	output_message(MSG_ERROR, "子プロセスを強制終了しましたがwaitpidで終了を確認できませんでした");
}

#endif

#ifdef TSD_PLATFORM_MSVC
void insert_orphan(HANDLE child_process, const WCHAR *cmd, const redirect_pathinfo_t *redirects)
#else
void insert_orphan(pid_t child_process, const char *cmd, const redirect_pathinfo_t *redirects)
#endif
{
	int i, pid;
	if (n_orphans >= MAX_ORPHANS) {
		pid = pid_of(&orphans[0]);
		output_message(MSG_ERROR, TSD_TEXT("終了待ちの子プロセスが多すぎるため最も古いものを強制終了します(pid=%d): %s"), pid, orphans[0].cmd);
#ifdef TSD_PLATFORM_MSVC
		TerminateProcess(orphans[0].child_process, 1);
		CloseHandle(orphans[0].child_process);
#else
		kill_child_process(orphans[0].child_process);
#endif
		rename_redirect_file(pid, &orphans[0]);
		for (i = 1; i < n_orphans; i++) {
			orphans[i - 1] = orphans[i];
		}
		n_orphans--;
	}
	copy_redirect_pathinfo(&orphans[n_orphans].redirects, redirects);
	orphans[n_orphans].cmd = cmd;
	orphans[n_orphans].child_process = child_process;
	orphans[n_orphans].lasttime = gettime();
	n_orphans++;
}

void collect_zombies(const int64_t time_ms, const int timeout)
{
	/* TODO: WaitMultipleObjectsあるいはwaitを用いてシステムコール呼び出しを減らす */
	int i, j, pid, del;
	for (i = 0; i < n_orphans; i++) {
		del = 0;
		pid = pid_of(&orphans[i]);
#ifdef TSD_PLATFORM_MSVC
		if (WaitForSingleObject(orphans[i].child_process, 0) == WAIT_OBJECT_0) {
			DWORD ret;
			GetExitCodeProcess(orphans[i].child_process, &ret);
			CloseHandle(orphans[i].child_process);
			output_message(MSG_NOTIFY, L"子プロセス終了(pid=%d, exitcode=%d): %s", pid, ret, orphans[i].cmd);
			del = 1;
		}
#else
		int ret, status;
		ret = waitpid(orphans[i].child_process, &status, WNOHANG);
		if (ret > 0) {
			output_message(MSG_NOTIFY, "子プロセス終了(pid=%d, exitcode=%d): %s", pid, WEXITSTATUS(status), orphans[i].cmd);
			del = 1;
		}
#endif
		if (!del && orphans[i].lasttime + timeout < time_ms) {
			output_message(MSG_ERROR, TSD_TEXT("終了待ちの子プロセスが%d秒応答しないため強制終了します(pid=%d): %s"), timeout/1000, pid_of(&orphans[i]), orphans[i].cmd);
#ifdef TSD_PLATFORM_MSVC
			TerminateProcess(orphans[i].child_process, 1);
			CloseHandle(orphans[i].child_process);
#else
			kill_child_process(orphans[i].child_process);
#endif
			del = 1;
		}

		if (del) {
			rename_redirect_file(pid, &orphans[i]);
			for (j = i + 1; j < n_orphans; j++) {
				orphans[j - 1] = orphans[j];
			}
			n_orphans--;
		}
	}
}

static void ps_write(pipestat_t *ps)
{
	int remain;
#ifdef TSD_PLATFORM_MSVC
	DWORD written, errcode;
#else
	struct sigaction sa, sa_old;
	int errno_t, written;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
#endif

	while (1) {
		remain = ps->write_bytes - ps->written_bytes;
		if (remain <= 0) {
			ps->write_bytes = 0;
			break;
		}
#ifdef TSD_PLATFORM_MSVC
		if (!WriteFile(ps->write_pipe, &ps->buf[ps->written_bytes], remain, &written, &ps->ol)) {
			if ((errcode=GetLastError()) == ERROR_IO_PENDING) {
				/* do nothing */
			} else {
				output_message(MSG_SYSERROR, L"書き込みエラーのためパイプを閉じます(WriteFile): pid=%d", (int)GetProcessId(ps->child_process));
				goto ERROR_END;
			}
			break;
		}
#else
		/* SIGPIPEを無視するように設定する */
		if (sigaction(SIGPIPE, &sa, &sa_old) < 0) {
			output_message(MSG_SYSERROR, "sigactionがエラーを返しましたためパイプを閉じます: pid=%d", (int)(ps->child_process));
			goto ERROR_END;
		}

		written = write(ps->fd_pipe, &ps->buf[ps->written_bytes], remain);
		errno_t = errno;

		/* SIGPIPEのハンドラの設定を戻す */
		if (sigaction(SIGPIPE, &sa_old, NULL) < 0) {
			output_message(MSG_SYSERROR, "sigactionがエラーを返しましたためパイプを閉じます: pid=%d", (int)(ps->child_process));
			goto ERROR_END;
		}

		if (written < 0) {
			if (errno_t == EAGAIN || errno_t == EWOULDBLOCK || errno_t == EINTR) {
				ps->write_busy = 1;
			} else {
				output_message(MSG_SYSERROR, "書き込みエラーのためパイプを閉じます(write): pid=%d", (int)(ps->child_process));
				goto ERROR_END;
			}
			break;
		}
#endif
		ps->written_bytes += written;
	}
	return;
ERROR_END:
	close_pipe(ps);
	insert_orphan(ps->child_process, ps->cmd, &ps->redirects);
	ps->used = 0;
}

#ifdef TSD_PLATFORM_MSVC

static void ps_check(pipestat_t *ps, int canceled)
{
	int remain;
	DWORD written, errcode;
	BOOL wait = FALSE;

	if (!ps->write_busy) {
		return;
	}

	if (canceled) {
		wait = TRUE;
	}

	if (!GetOverlappedResult(ps->write_pipe, &ps->ol, &written, wait)) {
		if ((errcode=GetLastError()) == ERROR_IO_INCOMPLETE) {
			/* do nothing */
		} else {
			if (errcode == ERROR_OPERATION_ABORTED && canceled) {
				output_message(MSG_WARNING, L"パイプへの書き込みが滞っているためIOをキャンセルしました: pid=%d", (int)GetProcessId(ps->child_process));
			} else {
				output_message(MSG_SYSERROR, L"書き込みエラーのためパイプを閉じます(GetOverlappedResult): pid=%d", (int)GetProcessId(ps->child_process));
				close_pipe(ps);
				insert_orphan(ps->child_process, ps->cmd, &ps->redirects);
				ps->used = 0;
				return;
			}
			ps->write_busy = 0;
		}
	} else {
		ps->written_bytes += written;
		remain = ps->write_bytes - ps->written_bytes;
		if (remain > 0) {
			if (canceled) {
				output_message(MSG_WARNING, L"パイプへの書き込みが滞っているためIOをキャンセルしました: pid=%d", (int)GetProcessId(ps->child_process));
				ps->write_busy = 0;
			} else {
				ps_write(ps);
			}
		} else {
			ps->write_busy = 0;
		}
	}
}

#else

static void ps_check(pipestat_t *ps, int canceled)
{
	int remain;

	if (!ps->write_busy) {
		return;
	}
	remain = ps->write_bytes - ps->written_bytes;
	if (remain > 0) {
		if (canceled) {
			output_message(MSG_WARNING, "パイプへの書き込みが滞っているためIOをキャンセルしました");
			ps->write_busy = 0;
		} else {
			ps_write(ps);
		}
	} else {
		ps->write_busy = 0;
	}
}

#endif

static void hook_pgoutput(void *stat, const uint8_t *buf, const size_t size)
{
	module_stat_t *pstat = (module_stat_t*)stat;
	int i;

	for (i = 0; i < n_pipecmds; i++) {
		if (!pstat->pipestats[i].used || pstat->pipestats[i].connected) {
			continue;
		}

		if (!pstat->pipestats[i].write_busy) {
			pstat->pipestats[i].buf = buf;
			pstat->pipestats[i].written_bytes = 0;
			pstat->pipestats[i].write_bytes = size;
			pstat->pipestats[i].write_busy = 1;
			ps_write(&pstat->pipestats[i]);
		} else {
			/* never come */
			*(char*)(NULL) = 'A';
		}
	}
}

static const int hook_pgoutput_check(void *stat)
{
	module_stat_t *pstat = (module_stat_t*)stat;
	int i, ret = 0;

	for (i = 0; i < n_pipecmds; i++) {
		if (!pstat->pipestats[i].used || pstat->pipestats[i].connected) {
			continue;
		}
		if (pstat->pipestats[i].write_busy) {
			ps_check(&pstat->pipestats[i], 0);
		}
		ret |= pstat->pipestats[i].write_busy;
	}
	return ret;
}

static const int hook_pgoutput_wait(void *stat)
{
	module_stat_t *pstat = (module_stat_t*)stat;
	int i, ret = 0;

	for (i = 0; i < n_pipecmds; i++) {
		if (!pstat->pipestats[i].used) {
			continue;
		}
		if (pstat->pipestats[i].write_busy) {
#ifdef TSD_PLATFORM_MSVC
			ps_check(&pstat->pipestats[i], 0);
		}
		if (pstat->pipestats[i].write_busy) {
			CancelIo(pstat->pipestats[i].write_pipe);
#endif
			ps_check(&pstat->pipestats[i], 1);
		}
	}
	return ret;
}

static void hook_pgoutput_close(void *stat, const proginfo_t *pi)
{
	UNREF_ARG(pi);

	module_stat_t *pstat = (module_stat_t*)stat;
	int i;
	for (i = 0; i < n_pipecmds; i++) {
		if (pstat->pipestats[i].used && !pstat->pipestats[i].connected) {
			close_pipe(&pstat->pipestats[i]);
		}
	}

	/* postcloseで使うために退避しておく */
	pstat->last_proginfo = *pi;
}

static void hook_pgoutput_postclose(void *stat)
{
	int i, n_children=0;
	int64_t time1, timeout;
	const int total_timeout = 100;

#ifdef TSD_PLATFORM_MSVC
	HANDLE c, children[MAX_CMDS];
#else
	pid_t c, children[MAX_CMDS];
#endif
	TSDCHAR *cmds[MAX_CMDS];
	redirect_pathinfo_t redirects[MAX_CMDS];

	module_stat_t *pstat = (module_stat_t*)stat;

	for (i = 0; i < n_execcmds; i++) {
		c = exec_cmd(&execcmds[i], pstat->filename, &pstat->last_proginfo, &redirects[n_children]);
#ifdef TSD_PLATFORM_MSVC
		if (c != INVALID_HANDLE_VALUE) {
#else
		if (c > 0) {
#endif
			cmds[n_children] = execcmds[i].cmd;
			children[n_children] = c;
			n_children++;
		}
	}

	time1 = gettime();
	timeout = total_timeout;
	for (i = 0; i < n_pipecmds; i++) {
		if (pstat->pipestats[i].used) {
			insert_orphan(pstat->pipestats[i].child_process, pstat->pipestats[i].cmd, &pstat->pipestats[i].redirects);
			timeout = time1 + total_timeout - gettime();
			if (timeout < 0) {
				timeout = 0;
			}
		}
	}
	for (i = 0; i < n_children; i++) {
		insert_orphan(children[i], cmds[i], &redirects[i]);
		timeout = time1 + total_timeout - gettime();
		if (timeout < 0) {
			timeout = 0;
		}
	}
	free(pstat);
}

static void hook_tick(int64_t time_ms)
{
	static int64_t last_time = 0;
	if (last_time / 1000 != time_ms / 1000) {
		/* ゾンビコレクタは1秒に1回だけ呼び出す */
		collect_zombies(time_ms, 60 * 1000);
		last_time = time_ms;
	}
}

static void hook_close_module()
{
	/* ゾンビプロセスをすべて回収する */
	while (n_orphans > 0) {
		collect_zombies(gettime(), 10 * 1000);
#ifdef TSD_PLATFORM_MSVC
		Sleep(10);
#else
		usleep(1000*10);
#endif
	}
}

static int hook_postconfig()
{
	if (open_connect) {
		output_message(MSG_ERROR, TSD_TEXT("接続先のコマンドが指定されていません"));
		return 0;
	}
	return 1;
}

static const TSDCHAR* set_pipe_cmd(const TSDCHAR *param)
{
	if (n_pipecmds >= MAX_PIPECMDS) {
		return TSD_TEXT("指定するパイプ実行コマンドの数が多すぎます");
	}
	tsd_strlcpy(pipecmds[n_pipecmds].cmd, param, MAX_PATH_LEN - 1);
	pipecmds[n_pipecmds].set_opt = 0;
	n_pipecmds++;
	open_connect = 0;
	return NULL;
}

static const TSDCHAR* set_pipe_opt(const TSDCHAR *param)
{
	if (n_pipecmds == 0) {
		return TSD_TEXT("パイプ実行コマンドが指定されていません");
	}
	if (pipecmds[n_pipecmds - 1].set_opt) {
		return TSD_TEXT("パイプ実行コマンドのオプションは既に指定されています");
	}
	tsd_strlcpy(pipecmds[n_pipecmds - 1].opt, param, 2048 - 1);
	pipecmds[n_pipecmds - 1].set_opt = 1;
	return NULL;
}

static const TSDCHAR *set_min(const TSDCHAR* param)
{
	UNREF_ARG(param);
	pcwindow_min = 1;
	return NULL;
}

static const TSDCHAR *set_connect(const TSDCHAR* param)
{
	UNREF_ARG(param);
	if (n_pipecmds < 1) {
		return TSD_TEXT("接続元のコマンドが指定されていません");
	}
	pipecmds[n_pipecmds - 1].connecting = 1;
	open_connect = 1;
	return NULL;
}

static const TSDCHAR* set_cmd(const TSDCHAR *param)
{
	if (n_execcmds >= MAX_CMDS) {
		return TSD_TEXT("指定する番組終了時実行コマンドの数が多すぎます");
	}
	tsd_strlcpy(execcmds[n_execcmds].cmd, param, MAX_PATH_LEN - 1);
	execcmds[n_execcmds].set_opt = 0;
	n_execcmds++;
	return NULL;
}

static const TSDCHAR* set_cmd_opt(const TSDCHAR *param)
{
	if (n_execcmds == 0) {
		return TSD_TEXT("番組終了時実行コマンドが指定されていません");
	}
	if (execcmds[n_execcmds - 1].set_opt) {
		return TSD_TEXT("番組終了時実行コマンドのオプションは既に指定されています");
	}
	tsd_strlcpy(execcmds[n_execcmds - 1].opt, param, 2048 - 1);
	execcmds[n_execcmds - 1].set_opt = 1;
	return NULL;
}

static const TSDCHAR *set_cmin(const TSDCHAR* param)
{
	cwindow_min = 1;
	UNREF_ARG(param);
	return NULL;
}

static const TSDCHAR *set_output_redirect(const TSDCHAR* param)
{
	output_redirect = 1;
	tsd_strlcpy(output_redirect_dir, param, MAX_PATH_LEN-1);
	return NULL;
}

static void register_hooks()
{
	register_hook_pgoutput_create(hook_pgoutput_create);
	register_hook_pgoutput(hook_pgoutput);
	register_hook_pgoutput_check(hook_pgoutput_check);
	register_hook_pgoutput_wait(hook_pgoutput_wait);
	register_hook_pgoutput_close(hook_pgoutput_close);
	register_hook_pgoutput_postclose(hook_pgoutput_postclose);
	register_hook_tick(hook_tick);
	register_hook_close_module(hook_close_module);
	register_hook_postconfig(hook_postconfig);
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--pipecmd"), TSD_TEXT("パイプ実行コマンド (複数指定可)"), 1, set_pipe_cmd },
	{ TSD_TEXT("--pipeopt"), TSD_TEXT("パイプ実行コマンドのオプション (複数指定可)"), 1, set_pipe_opt },
	{ TSD_TEXT("--pwmin"), TSD_TEXT("パイプ実行コマンドのウィンドウを最小化する"), 0, set_min },
	{ TSD_TEXT("--pipeconn"), TSD_TEXT("パイプ実行コマンドの出力を次のコマンドの入力に接続する"), 0, set_connect },
	{ TSD_TEXT("--cmd"), TSD_TEXT("番組終了時実行コマンド (複数指定可)"), 1, set_cmd },
	{ TSD_TEXT("--cmdopt"), TSD_TEXT("番組終了時実行コマンドのオプション (複数指定可)"), 1, set_cmd_opt },
	{ TSD_TEXT("--cwmin"), TSD_TEXT("番組終了時実行コマンドのウィンドウを最小化する"), 0, set_cmin },
	{ TSD_TEXT("--cmd-output-redirect"), TSD_TEXT("各コマンドの標準出力／エラー出力を指定したディレクトリに書き出す"), 1, set_output_redirect },
	{ NULL },
};

TSD_MODULE_DEF(
	mod_cmdexec,
	register_hooks,
	cmds,
	NULL
);