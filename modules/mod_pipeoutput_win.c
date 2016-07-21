#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <windows.h>
#include <process.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
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

typedef struct {
	int used;
	int write_busy;
	const uint8_t *buf;
	int write_bytes;
	int written_bytes;
#ifdef TSD_PLATFORM_MSVC
	OVERLAPPED ol;
	HANDLE write_pipe;
	HANDLE child_process;
#else
	int fd_pipe;
	int pid_child;
#endif
} pipestat_t;

typedef struct {
	TSDCHAR cmd[MAX_PATH_LEN];
	TSDCHAR opt[2048];
	int set_opt;
} pipe_cmd_t;

#define MAX_PIPECMDS 32

static int n_pipecmds = 0;
static pipe_cmd_t pipecmds[MAX_PIPECMDS];
static int pcwindow_min = 0;
//static int waitmode = 0;

static void generate_arg(TSDCHAR *arg, size_t maxlen_arg, const pipe_cmd_t *pipe_cmd, const TSDCHAR *fname, const proginfo_t *proginfo)
{
	const TSDCHAR *chname = TSD_TEXT("unknown"), *progname = TSD_TEXT("unkonwn");
	int year, mon, day, hour, min, sec;
	TSDCHAR fname_base[MAX_PATH_LEN];
	TSDCHAR tn_str[21], year_str[5], mon_str[3], day_str[3], hour_str[3], min_str[3], sec_str[3];
	TSDCHAR mon_str0[3], day_str0[3], hour_str0[3], min_str0[3], sec_str0[3];
	time_t t;
	struct tm lt;
	int64_t timenum;
	tsdstr_replace_set_t sets[32];
	int n_sets = 0;

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
	}

	tsd_strncpy(fname_base, fname, MAX_PATH_LEN);
	path_removeext(fname_base);

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

	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%Q%"), TSD_TEXT("\""));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%BS%"), TSD_TEXT("\\"));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%%"), TSD_TEXT("%"));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%FILE%"), fname);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("%FILENE%"), fname_base);
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

	tsd_strncpy(arg, pipe_cmd->opt, maxlen_arg - 1);
	tsd_replace_sets(arg, maxlen_arg - 1, sets, n_sets, 0);
}

#ifdef TSD_PLATFORM_MSVC

static void create_pipe(pipestat_t *ps, const pipe_cmd_t *pipe_cmd, const WCHAR *fname, const proginfo_t *proginfo)
{
	HANDLE h_pipe, h_read_temp, h_read;
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};
	BOOL res;
	DWORD err;

	WCHAR cmdarg[2048], pipe_path[1024];

	ps->used = 0;

	tsd_snprintf(pipe_path, 1024 - 1, TSD_TEXT("\\\\.\\pipe\\tsdump_pipe_%d_%"PRId64""), _getpid(), gettime());

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
		return;
	}

	h_read_temp = CreateFile(pipe_path,
			GENERIC_READ,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
		NULL);
	if (h_read_temp == INVALID_HANDLE_VALUE) {
		output_message(MSG_SYSERROR, L"パイプのオープンに失敗(CreateFile)");
		return;
	}

	if (!ConnectNamedPipe(h_pipe, NULL)) {
		err = GetLastError();
		if (err != 535) {
			output_message(MSG_SYSERROR, L"パイプの接続に失敗(ConnectNamedPipe)");
			CloseHandle(h_read_temp);
			CloseHandle(h_pipe);
			return;
		}
	}

	res = DuplicateHandle(
			GetCurrentProcess(),
			h_read_temp,
			GetCurrentProcess(),
			&h_read,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS
		);
	if (!res) {
		output_message(MSG_SYSERROR, L"DuplicateHandle()に失敗");
		CloseHandle(h_read_temp);
		CloseHandle(h_pipe);
		return;
	}

	CloseHandle(h_read_temp);

	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = h_read;

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

	output_message(MSG_NOTIFY, L"パイプコマンド実行: %s", cmdarg);

	if (!CreateProcess(pipe_cmd->cmd, cmdarg, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
		output_message(MSG_SYSERROR, L"子プロセスの生成に失敗(CreateProcess)");
		CloseHandle(h_read);
		CloseHandle(h_pipe);
		return;
	}

	ps->child_process = pi.hProcess;
	ps->write_pipe = h_pipe;
	ps->used = 1;
	ps->write_busy = 0;
	memset(&ps->ol, 0, sizeof(OVERLAPPED));

	CloseHandle(pi.hThread);
	CloseHandle(h_read);

	return;
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

static void create_pipe(pipestat_t *ps, const pipe_cmd_t *pipe_cmd, const char *fname, const proginfo_t *proginfo)
{
	int ret, status;
	int pipefds[2];
	pid_t pid, pid_ret;
	char argline[2048];
	int n_args;
	char *args[32];

	if (pipe(pipefds) < 0) {
		output_message(MSG_SYSERROR, "パイプの生成に失敗(pipe2)");
		goto ERROR1;
	}

	if (fcntl(pipefds[1], F_SETFL, O_NONBLOCK) < 0) {
		output_message(MSG_SYSERROR, "パイプをノンブロッキングに設定できませんでした(fcntl)");
		goto ERROR2;
	}

	if (pipe_cmd->set_opt) {
		generate_arg(argline, 2048 - 1, pipe_cmd, fname, proginfo);
		args[0] = (char*)pipe_cmd->cmd;
		split_args(argline, &args[1], &n_args, 32 - 2);
		args[1 + n_args] = NULL;
		output_message(MSG_NOTIFY, "パイプコマンド実行: %s %s", pipe_cmd->cmd, argline);
	} else {
		args[0] = (char*)pipe_cmd->cmd;
		args[1] = (char*)fname;
		args[2] = NULL;
		output_message(MSG_NOTIFY, "パイプコマンド実行: %s \"%s\"", pipe_cmd->cmd, fname);
	}

	if ( (pid=fork()) < 0 ) {
		output_message(MSG_SYSERROR, "子プロセスを作成できませんでした(fork)");
		goto ERROR2;
	} else if ( pid == 0 ) {
		/* child */
		dup2(pipefds[0], 0);
		close(pipefds[0]);
		close(pipefds[1]);
		
		ret = execvp(pipe_cmd->cmd, args);
		if (ret < 0) {
			exit(1);
		}
	}

	pid_ret = waitpid(pid, &status, WNOHANG);
	if (pid_ret < 0) {
		output_message(MSG_SYSERROR, "子プロセスの状態を取得できませんでした(waitpid)");
		kill(pid, SIGKILL);
		goto ERROR2;
	} else if (pid_ret > 0) {
		if (WIFEXITED(status)) {
			output_message(MSG_ERROR, "子プロセスがすぐに終了しました:exitcode=%02x(waitpid)", WEXITSTATUS(status));
		} else {
			output_message(MSG_ERROR, "子プロセスがすぐに終了しました(waitpid)");
		}
		goto ERROR2;
	}

	close(pipefds[0]);

	ps->fd_pipe= pipefds[1];
	ps->used = 1;
	ps->write_busy = 0;
	ps->pid_child = pid;

	return;

ERROR2:
	close(pipefds[0]);
	close(pipefds[1]);
ERROR1:
	return;
}

#endif

static void *hook_pgoutput_create(const TSDCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info_t, const int actually_start)
{
	int i;
	UNREF_ARG(ch_info_t);
	UNREF_ARG(actually_start);
	pipestat_t *ps = (pipestat_t*)malloc( sizeof(pipestat_t) * n_pipecmds );
	for (i = 0; i < n_pipecmds; i++) {
		create_pipe(&ps[i], &pipecmds[i], fname, pi);
	}
	return ps;
}

static void close_pipe(pipestat_t *ps)
{
	ps->write_busy = 0;
	if (ps->used) {
#ifdef TSD_PLATFORM_MSVC
		CloseHandle(ps->write_pipe);
#else
		close(ps->fd_pipe);
#endif
	}
}

#ifndef TSD_PLATFORM_MSVC
static void kill_child_process(pipestat_t *ps)
{
	int i, status;
	kill(ps->pid_child, SIGKILL);
	for(i=0; i<10; i++) {
		if (waitpid(ps->pid_child, &status, WNOHANG) > 0) {
			break;
		}
		usleep(1000);
	}
}

int my_sigtimedwait(sigset_t *set, siginfo_t *info, const struct timespec *timeout)
{
#ifdef __CYGWIN__
	/* cygwinにはsigtimedwaitが存在しないので、同等のものをpselectでエミュレートする */
	int ret;
	ret = pselect(0, NULL, NULL, NULL, timeout, set);
	if (ret < 0) {
		if (errno == EINTR) {
			/* シグナルを受信した */
			return 1;
		}
	}
	return ret;
#else
	return sigtimedwait(set, info, timeout);
#endif
}
#endif

static void wait_child_process(pipestat_t *ps, int timeout)
{
	if (ps->used) {
#ifdef TSD_PLATFORM_MSVC
		DWORD ret = WaitForSingleObject(ps->child_process, timeout);
		if (ret != WAIT_OBJECT_0) {
			output_message(MSG_SYSERROR, L"プロセスが応答しないので強制終了します(WaitForSingleObject)");
			TerminateProcess(ps->child_process, 1);
		}
		CloseHandle(ps->child_process);
#else
		int64_t time1;
		int ret, status;
		sigset_t set, oldset;
		struct timespec ts;

		sigemptyset(&set);
		if (sigaddset(&set, SIGCHLD) != 0) {
			output_message(MSG_ERROR, "sigaddsetに失敗しました");
			kill_child_process(ps);
			return;
		}
		if (sigprocmask(SIG_BLOCK, &set, &oldset) != 0) {
			output_message(MSG_ERROR, "sigprocmaskに失敗しました");
			kill_child_process(ps);
			return;
		}

		while(1) {
			ret = waitpid(ps->pid_child, &status, WNOHANG);
			if (ret < 0) {
				output_message(MSG_SYSERROR, "waitpidに失敗しました");
				sigprocmask(SIG_SETMASK, &oldset, NULL);
				kill_child_process(ps);
				return;
			} else if (ret == 0) {
				if (timeout == 0) {
					output_message(MSG_ERROR, "プロセスが応答しないので強制終了します(waitpid)");
					sigprocmask(SIG_SETMASK, &oldset, NULL);
					kill_child_process(ps);
					return;
				}
				time1 = gettime();
				ts.tv_sec = timeout / 1000;
				ts.tv_nsec = (timeout % 1000) * 1000 * 1000;
				ret = my_sigtimedwait(&set, NULL, &ts);
				if (ret < 0) {
					if (errno == EAGAIN) {
						output_message(MSG_ERROR, "プロセスが応答しないので強制終了します(sigtimedwait)");
					} else {
						output_message(MSG_SYSERROR, "sigtimedwaitに失敗しました");
					}
					sigprocmask(SIG_SETMASK, &oldset, NULL);
					kill_child_process(ps);
					return;
				}
				timeout -= (int)(gettime() - time1);
				if (timeout < 0) {
					timeout = 0;
				}
			} else {
				/* プロセスの終了を無事にキャッチした */
				break;
			}
		}
		sigprocmask(SIG_SETMASK, &oldset, NULL);
#endif
		ps->used = 0;
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
				output_message(MSG_SYSERROR, L"書き込みエラーのためパイプを閉じます(WriteFile):");
				goto ERROR_END;
			}
			break;
		}
#else
		/* SIGPIPEを無視するように設定する */
		if (sigaction(SIGPIPE, &sa, &sa_old) < 0) {
			output_message(MSG_SYSERROR, "sigactionがエラーを返しましたためパイプを閉じます:");
			goto ERROR_END;
		}

		written = write(ps->fd_pipe, &ps->buf[ps->written_bytes], remain);
		errno_t = errno;

		/* SIGPIPEのハンドラの設定を戻す */
		if (sigaction(SIGPIPE, &sa_old, NULL) < 0) {
			output_message(MSG_SYSERROR, "sigactionがエラーを返しましたためパイプを閉じます:");
			goto ERROR_END;
		}

		if (written < 0) {
			if (errno_t == EAGAIN || errno_t == EWOULDBLOCK || errno_t == EINTR) {
				ps->write_busy = 1;
			} else {
				output_message(MSG_SYSERROR, "書き込みエラーのためパイプを閉じます(write):");
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
	wait_child_process(ps, 500);
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
				output_message(MSG_WARNING, L"パイプへの書き込みが滞っているためIOをキャンセルしました");
			} else {
				output_message(MSG_SYSERROR, L"書き込みエラーのためパイプを閉じます(GetOverlappedResult)");
				close_pipe(ps);
				wait_child_process(ps, 0);
				return;
			}
			ps->write_busy = 0;
		}
	} else {
		ps->written_bytes += written;
		remain = ps->write_bytes - ps->written_bytes;
		if (remain > 0) {
			if (canceled) {
				output_message(MSG_WARNING, L"パイプへの書き込みが滞っているためIOをキャンセルしました");
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
	pipestat_t *pss = (pipestat_t*)stat;
	int i;

	for (i = 0; i < n_pipecmds; i++) {
		if (!pss[i].used) {
			continue;
		}

		if (!pss[i].write_busy) {
			pss[i].buf = buf;
			pss[i].written_bytes = 0;
			pss[i].write_bytes = size;
			pss[i].write_busy = 1;
			ps_write(&pss[i]);
		} else {
			/* never come */
			*(char*)(NULL) = 'A';
		}
	}
}

static const int hook_pgoutput_check(void *stat)
{
	pipestat_t *pss = (pipestat_t*)stat;
	int i, ret = 0;

	for (i = 0; i < n_pipecmds; i++) {
		if (!pss[i].used) {
			continue;
		}
		if (pss[i].write_busy) {
			ps_check(&pss[i], 0);
		}
		ret |= pss[i].write_busy;
	}
	return ret;
}

static const int hook_pgoutput_wait(void *stat)
{
	pipestat_t *pss = (pipestat_t*)stat;
	int i, ret = 0;

	for (i = 0; i < n_pipecmds; i++) {
		if (!pss[i].used) {
			continue;
		}
		if (pss[i].write_busy) {
#ifdef TSD_PLATFORM_MSVC
			ps_check(&pss[i], 0);
		}
		if (pss[i].write_busy) {
			CancelIo(pss[i].write_pipe);
#endif
			ps_check(&pss[i], 1);
		}
	}
	return ret;
}

static void hook_pgoutput_close(void *pstat, const proginfo_t *pi)
{
	int64_t time1, timeout;
	UNREF_ARG(pi);

	pipestat_t *ps = (pipestat_t*)pstat;
	int i;
	for (i = 0; i < n_pipecmds; i++) {
		close_pipe(&ps[i]);
	}

	time1 = gettime();
	timeout = 5 * 1000;
	for (i = 0; i < n_pipecmds; i++) {
		wait_child_process(&ps[i], (int)timeout);
		timeout = time1 + 5*1000 - gettime();
		if (timeout < 0) {
			timeout = 0;
		}
	}
	free(ps);
}

static const TSDCHAR* set_pipe_cmd(const TSDCHAR *param)
{
	if (n_pipecmds >= MAX_PIPECMDS) {
		return TSD_TEXT("指定するパイプ実行コマンドの数が多すぎます");
	}
	tsd_strncpy(pipecmds[n_pipecmds].cmd, param, MAX_PATH_LEN - 1);
	pipecmds[n_pipecmds].set_opt = 0;
	n_pipecmds++;
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
	tsd_strncpy(pipecmds[n_pipecmds-1].opt, param, 1024 - 1);
	pipecmds[n_pipecmds - 1].set_opt = 1;
	return NULL;
}

static const TSDCHAR *set_min(const TSDCHAR* param)
{
	UNREF_ARG(param);
	pcwindow_min = 1;
	return NULL;
}

static void register_hooks()
{
	register_hook_pgoutput_create(hook_pgoutput_create);
	register_hook_pgoutput(hook_pgoutput);
	register_hook_pgoutput_check(hook_pgoutput_check);
	register_hook_pgoutput_wait(hook_pgoutput_wait);
	register_hook_pgoutput_close(hook_pgoutput_close);
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--pipecmd"), TSD_TEXT("パイプ実行コマンド (複数指定可)"), 1, set_pipe_cmd },
	{ TSD_TEXT("--pipeopt"), TSD_TEXT("パイプ実行コマンドのオプション (複数指定可)"), 1, set_pipe_opt },
	{ TSD_TEXT("--pwmin"), TSD_TEXT("パイプ実行コマンドのウィンドウを最小化する"), 0, set_min },
	{ NULL },
};

MODULE_DEF module_def_t mod_pipeoutput_win = {
	TSDUMP_MODULE_V4,
	TSD_TEXT("mod_pipeoutput_win"),
	register_hooks,
	cmds,
};