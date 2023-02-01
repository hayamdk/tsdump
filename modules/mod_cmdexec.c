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
#include <assert.h>

#include "utils/arib_proginfo.h"
#include "core/module_api.h"
#include "utils/tsdstr.h"
#include "utils/path.h"

#include "core/tsdump.h"

#ifdef TSD_PLATFORM_MSVC
typedef HANDLE my_process_handle_t;
typedef HANDLE my_file_handle_t;
typedef DWORD my_retcode_t;
#else
typedef pid_t my_process_handle_t;
typedef int my_file_handle_t;
typedef int my_retcode_t;
#endif

#define MAX_PIPECMDS		32
#define MAX_CMDS			32
#define PIPE_BLOCK_SIZE		512*1024
#define CMDEXEC_LIMIT_SEC	120
#define CMDEXEC_KILL_SEC	30
#define MAX_KILL_WAIT_MS	100

typedef struct {
	unsigned int use_stdout : 1;
	unsigned int use_stderr : 1;
	TSDCHAR stdout_path[MAX_PATH_LEN];
	TSDCHAR stderr_path[MAX_PATH_LEN];
	int64_t timenum;
} redirect_pathinfo_t;

typedef struct {
	unsigned int used : 1;
	unsigned int soft_closed : 1;
	unsigned int aborted : 1;
	unsigned int no_pipe : 1;
#ifdef TSD_PLATFORM_MSVC
	unsigned int write_busy : 1;
	OVERLAPPED ol;
	uint8_t buf[PIPE_BLOCK_SIZE];
	int write_bytes;
	int written_bytes;
#endif
	my_retcode_t cmd_retcode;
	int cmd_result;
	int n_connected_cmds;
	const TSDCHAR *cmd;
	redirect_pathinfo_t redirects;
	my_process_handle_t child_process;
	my_file_handle_t write_pipe;
} pipestat_t;

typedef struct {
	TSDCHAR filename[MAX_PATH_LEN];
	pipestat_t pipestats[MAX_PIPECMDS];
	int idx_of_pipestats;
} module_stat_t;

typedef struct {
	unsigned int set_opt : 1;
	unsigned int connecting : 1;
	unsigned int no_pipe : 1;
	TSDCHAR cmd[MAX_PATH_LEN];
	TSDCHAR opt[2048];
} cmd_opt_t;

typedef struct {
	my_process_handle_t child_process;
	const TSDCHAR *cmd;
	int64_t lasttime;
	int killmode;
	redirect_pathinfo_t redirects;
} running_process_info_t;

#define MAX_EXEC_PROCESSES	128

static int n_exec_ps = 0;
static running_process_info_t exec_ps[MAX_EXEC_PROCESSES];

static int n_pipecmds = 0;
static cmd_opt_t pipecmds[MAX_PIPECMDS];
static int pcwindow_min = 0;
static int open_connect = 0;

static int n_execcmds = 0;
static cmd_opt_t execcmds[MAX_CMDS];
static int cwindow_min = 0;

/* the last is: 0=none, 1=pipe-command, 2=post-execution-command */
static int set_last_cmd = 0;

static int output_redirect = 0;
static TSDCHAR output_redirect_dir[MAX_PATH_LEN];


static void generate_arg(TSDCHAR *arg, size_t maxlen_arg, const cmd_opt_t *cmd, const TSDCHAR *fname, const proginfo_t *proginfo, pipestat_t *ps)
{
	TSDCHAR fname_base[MAX_PATH_LEN];
	const TSDCHAR *fname_base_r, *fname_r;
	TSDCHAR retcode_name[MAX_PIPECMDS][8], retcode[MAX_PIPECMDS][16];
	TSDCHAR cmdresult_name[MAX_PIPECMDS][8], cmdresult[MAX_PIPECMDS][16];
	tsdstr_replace_set_t sets[64+MAX_PIPECMDS];
	size_t n_sets = 0;
	int i;

	tsd_strlcpy(fname_base, fname, MAX_PATH_LEN - 1);
	path_removeext(fname_base);
	fname_r = path_getfile(fname);
	fname_base_r = path_getfile(fname_base);

	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{Q}"), TSD_TEXT("\""));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{BS}"), TSD_TEXT("\\"));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{{"), TSD_TEXT("{"));
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{FILE}"), fname);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{FILENE}"), fname_base);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{FNAME}"), fname_r);
	TSD_REPLACE_ADD_SET(sets, n_sets, TSD_TEXT("{FNAMENE}"), fname_base_r);
	replace_sets_proginfo_vars(sets, &n_sets, proginfo, NULL);
	replace_sets_date_vars(sets, &n_sets);

	if (ps) {
		for (i = 0; i < n_pipecmds; i++) {
			tsd_snprintf(cmdresult_name[i], 8, TSD_TEXT("{RES%d}"), i);
			tsd_snprintf(retcode_name[i], 8, TSD_TEXT("{RET%d}"), i);
			tsd_snprintf(cmdresult[i], 16, TSD_TEXT("%d"), ps[i].cmd_result);
			tsd_snprintf(retcode[i], 16, TSD_TEXT("%d"), (int)ps[i].cmd_retcode);
			TSD_REPLACE_ADD_SET(sets, n_sets, cmdresult_name[i], cmdresult[i]);
			TSD_REPLACE_ADD_SET(sets, n_sets, retcode_name[i], retcode[i]);
		}
	}

	tsd_strlcpy(arg, cmd->opt, maxlen_arg - 1);
	tsd_replace_sets(arg, maxlen_arg - 1, sets, n_sets, 0);
}

static my_file_handle_t create_redirect_file(TSDCHAR *path)
{
#ifdef TSD_PLATFORM_MSVC
	HANDLE fh;
	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

#else
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

#ifdef TSD_PLATFORM_MSVC
	tsd_snprintf(fname, MAX_PATH_LEN, TSD_TEXT("tmp_%"PRId64"_%d_%d"), gettime(), _getpid(), counter++);
#else
	tsd_snprintf(fname, MAX_PATH_LEN, TSD_TEXT("tmp_%"PRId64"_%d_%d"), gettime(), getpid(), counter++);
#endif
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
		tsd_strlcpy(dst->stderr_path, src->stderr_path, MAX_PATH_LEN-1);
	}
	dst->timenum = src->timenum;
}

static void rename_redirect_file(int pid, const redirect_pathinfo_t *redirects)
{
	TSDCHAR fname[MAX_PATH_LEN], path[MAX_PATH_LEN];

	if (!output_redirect) {
		return;
	}

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

static int pid_of(my_process_handle_t pid)
{
#ifdef TSD_PLATFORM_MSVC
	return (int)GetProcessId(pid);
#else
	return (int)pid;
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
	HANDLE h_pipe = INVALID_HANDLE_VALUE, h_read = INVALID_HANDLE_VALUE, h_write, h_error;
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};

	WCHAR cmdarg[2048];

	if (prev) {
		h_read = *prev;
	} else if (!ps->no_pipe) {
		if (!create_pipe1(&h_read, &h_pipe)) {
			return 0;
		}
	}

	if (next) {
		if (!create_pipe2(next, &h_write)) {
			if (!ps->no_pipe) {
				CloseHandle(h_read);
			}
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
	if (!ps->no_pipe) {
		si.hStdInput = h_read;
	}
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
		generate_arg(&cmdarg[len], 2048 - 1 - len, pipe_cmd, fname, proginfo, NULL);
	} else {
		swprintf(cmdarg, 2048 - 1, L"\"%s\" \"%s\"", pipe_cmd->cmd, fname);
	}

	if (!CreateProcess(NULL, cmdarg, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
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

	output_message(MSG_NOTIFY, L"パイプコマンド実行(pid=%d): %s ", pid_of(pi.hProcess), cmdarg);

	ps->child_process = pi.hProcess;
	ps->write_pipe = h_pipe;
	ps->used = 1;
	ps->write_busy = 0;
	ps->soft_closed = 0;
	ps->aborted = 0;
	ps->cmd = pipe_cmd->cmd;

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

static HANDLE exec_cmd(const cmd_opt_t *cmd, const WCHAR *fname, const proginfo_t *proginfo, redirect_pathinfo_t *redirects, pipestat_t *ps)
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
		generate_arg(&cmdarg[len], 2048 - 1 - len, cmd, fname, proginfo, ps);
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

	if (!CreateProcess(NULL, cmdarg, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
		output_message(MSG_SYSERROR, L"子プロセスの生成に失敗(CreateProcess): %s", cmdarg);
		return INVALID_HANDLE_VALUE;
	}

	output_message(MSG_NOTIFY, L"コマンド実行(pid=%d): %s", pid_of(pi.hProcess), cmdarg);

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

	ps->cmd_result = -1;
	ps->cmd_retcode = 1;
	ps->used = 0;

	if (prev) {
		readfd = *prev;
	} else if(!ps->no_pipe) {
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
		generate_arg(argline, 2048 - 1, pipe_cmd, fname, proginfo, NULL);
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
		if (!ps->no_pipe) {
			if (!prev) {
				close(writefd);
			}
			dup2(readfd, STDIN_FILENO);
			close(readfd);
		}

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

	if (prev || ps->no_pipe) {
		ps->write_pipe = -1;
	} else {
		ps->write_pipe = writefd;
	}

	if (next) {
		*next = conn_readfd;
	}

	ps->used = 1;
	ps->cmd = pipe_cmd->cmd;
	ps->child_process = pid;
	ps->soft_closed = 0;
	ps->aborted = 0;

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
	if (!ps->no_pipe) {
		if (!prev && !ret) {
			close(writefd);
		}
		close(readfd);
	}
ERROR1:
	return ret;
}

static pid_t exec_cmd(const cmd_opt_t *cmd, const char *fname, const proginfo_t *proginfo, redirect_pathinfo_t *redirects, pipestat_t *ps)
{
	int ret;
	pid_t pid;
	char argline[2048], argline2[2048];
	int n_args, fd_stdout, fd_stderr;
	char *args[32];

	if (cmd->set_opt) {
		generate_arg(argline, 2048 - 1, cmd, fname, proginfo, ps);
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

static void *hook_pgoutput_precreate(const TSDCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info_t, const int actually_start, int *n_output)
{
	UNREF_ARG(pi);
	UNREF_ARG(ch_info_t);
	UNREF_ARG(actually_start);
	module_stat_t *stat = (module_stat_t*)malloc(sizeof(module_stat_t));
	int i, n, connected = 0;
	int last_pipe_cmd = 0;

	tsd_strlcpy(stat->filename, fname, MAX_PATH_LEN - 1);

	for (i = n = 0; i < n_pipecmds; i++) {
		stat->pipestats[i].used = 0;
		stat->pipestats[i].no_pipe = 0;
		stat->pipestats[i].cmd_result = -1;
		stat->pipestats[i].cmd_retcode = 0;

		if (!connected) {
			if (pipecmds[i].no_pipe) {
				stat->pipestats[i].no_pipe = 1;
			}
			n++;
			last_pipe_cmd = i;
			stat->pipestats[last_pipe_cmd].n_connected_cmds = 0;
		} else {
			assert(!pipecmds[i].no_pipe);
			stat->pipestats[last_pipe_cmd].n_connected_cmds++;
		}
		if (pipecmds[i].connecting) {
			connected = 1;
		} else {
			connected = 0;
		}
	}
	stat->idx_of_pipestats = 0;
	*n_output = n;

	return stat;
}

static void *hook_pgoutput_create(void *param, const TSDCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info_t, const int actually_start)
{
#ifdef TSD_PLATFORM_MSVC
	HANDLE
#else
	int
#endif
		next = 0, *p_next;
	int i;
	UNREF_ARG(ch_info_t);
	UNREF_ARG(actually_start);
	module_stat_t *ms = (module_stat_t*)param;
	pipestat_t *ps, *ps_conn;

	ps = &ms->pipestats[ms->idx_of_pipestats];
	if (ps->n_connected_cmds > 0) {
		p_next = &next;
	} else {
		p_next = NULL;
	}

	if (exec_child(ps, &pipecmds[ms->idx_of_pipestats], fname, pi, NULL, p_next)) {
		for (i = 1; i <= ps->n_connected_cmds; i++) {
			ps_conn = &ms->pipestats[ms->idx_of_pipestats + i];
			if (pipecmds[ms->idx_of_pipestats + i].connecting) {
				p_next = &next;
			} else {
				p_next = NULL;
			}
			if (!exec_child(ps_conn, &pipecmds[ms->idx_of_pipestats + i], fname, pi, &next, p_next)) {
				break;
			}
		}
	}
	ms->idx_of_pipestats += ps->n_connected_cmds + 1;
	return ps;
}

static void my_close(my_file_handle_t file)
{
#ifdef TSD_PLATFORM_MSVC
	CloseHandle(file);
#else
	close(file);
#endif
}

static void close_pipe(pipestat_t *ps)
{
	my_close(ps->write_pipe);
#ifdef TSD_PLATFORM_MSVC
	ps->write_busy = 0;
#endif
}

#ifdef TSD_PLATFORM_MSVC

static BOOL CALLBACK term_enum_windows(HWND hwnd, LPARAM param)
{
	DWORD pid;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid == (DWORD)param) {
		PostMessage(hwnd, WM_CLOSE, 0, 0);
		return FALSE;
	}
	return TRUE;
}

static void soft_kill(HANDLE h_process)
{
	/* Windowsには任意のプロセス間のシグナルのようなメカニズムは無いのでWM_CLOSEを送ってみる */
	EnumWindows((WNDENUMPROC)term_enum_windows, (LPARAM)GetProcessId(h_process));
}

static my_retcode_t hard_kill(HANDLE h_process, const WCHAR *cmd)
{
	my_retcode_t ret;
	int pid = pid_of(h_process);
	TerminateProcess(h_process, 1);
	if (WaitForSingleObject(h_process, MAX_KILL_WAIT_MS) == WAIT_OBJECT_0) {
		GetExitCodeProcess(h_process, &ret);
		CloseHandle(h_process);
		output_message(MSG_NOTIFY, L"子プロセスの強制終了(pid=%d, exitcode=%d): %s", pid, ret, cmd);
		return ret;
	}
	output_message(MSG_NOTIFY, L"子プロセスを強制終了しましたが終了状態を確認できませんでした(pid=%d): %s", pid, cmd);
	CloseHandle(h_process);
	return 99;
}

#else

static void soft_kill(pid_t pid)
{
	kill(pid, SIGINT);
}

static my_retcode_t hard_kill(pid_t pid, const char *cmd)
{
	int i, ret, status;
	kill(pid, SIGKILL);
	for(i=0; i<10; i++) {
		ret = waitpid(pid, &status, WNOHANG);
		if (ret > 0) {
			output_message(MSG_NOTIFY, "子プロセス終了(pid=%d, exitcode=%d): %s", (int)pid, WEXITSTATUS(status), cmd);
			return WEXITSTATUS(status);
		} else if (ret < 0) {
			output_message(MSG_SYSERROR, "子プロセスを強制終了しましたがwaitpidがエラーを返しました(pid=%d): %s", (int)pid, cmd);
			return 98;
		}
		usleep(1000 * MAX_KILL_WAIT_MS / 10);
	}
	output_message(MSG_ERROR, "子プロセスを強制終了しましたがwaitpidで終了状態を確認できませんでした(pid=%d): %s", (int)pid, cmd);
	return 99;
}

#endif

void insert_runngin_ps(my_process_handle_t child_process, const TSDCHAR *cmd, const redirect_pathinfo_t *redirects)
{
	int i, pid;
	if (n_exec_ps >= MAX_EXEC_PROCESSES) {
		pid = pid_of(exec_ps[0].child_process);
		output_message(MSG_ERROR, TSD_TEXT("実行中の子プロセスが多すぎるため最も古いものを強制終了します(pid=%d): %s"), pid, exec_ps[0].cmd);
		hard_kill(exec_ps[0].child_process, exec_ps[0].cmd);
		rename_redirect_file(pid, &exec_ps[0].redirects);
		for (i = 1; i < n_exec_ps; i++) {
			exec_ps[i-1] = exec_ps[i];
		}
		n_exec_ps--;
	}
	copy_redirect_pathinfo(&exec_ps[n_exec_ps].redirects, redirects);
	exec_ps[n_exec_ps].cmd = cmd;
	exec_ps[n_exec_ps].child_process = child_process;
	exec_ps[n_exec_ps].lasttime = gettime();
	exec_ps[n_exec_ps].killmode = 0;
	n_exec_ps++;
}

/* return: 0 if child process exited, 1 if child process is still alive. */
static int check_child_process(my_process_handle_t child_process, my_retcode_t *p_retcode, const redirect_pathinfo_t *redirects, const TSDCHAR *cmd)
{
	/* TODO: 1つずつチェックするのではなく、
		WaitMultipleObjectsあるいはwaitを用いてシステムコール呼び出しを減らす */
	my_retcode_t retcode;
	int pid;
#ifdef TSD_PLATFORM_MSVC
	if (WaitForSingleObject(child_process, 0) == WAIT_OBJECT_0) {
		GetExitCodeProcess(child_process, &retcode);
#else
	int status;
	if (waitpid(child_process, &status, WNOHANG) > 0) {
		retcode = WEXITSTATUS(status);
#endif
	} else {
		return 1;
	}

	output_message(MSG_NOTIFY, TSD_TEXT("子プロセス終了(pid=%d, exitcode=%d): %s"), pid_of(child_process), (int)retcode, cmd);
	if (p_retcode) {
		*p_retcode = retcode;
	}
	pid = pid_of(child_process);
#ifdef TSD_PLATFORM_MSVC
	CloseHandle(child_process);
#endif
	rename_redirect_file(pid, redirects);
	return 0;
}

static void check_exec_child_processes(const int64_t time_ms, const int timeout_soft, const int timeout_hard)
{
	my_retcode_t retcode;
	int i, j, del, pid;
	for (i = 0; i < n_exec_ps; i++) {
		pid = pid_of(exec_ps[i].child_process);
		del = !check_child_process(exec_ps[i].child_process, &retcode, &exec_ps[i].redirects, exec_ps[i].cmd);
		if (!del && !exec_ps[i].killmode && exec_ps[i].lasttime + timeout_soft < time_ms) {
			output_message(MSG_WARNING, TSD_TEXT("子プロセスが%d秒経っても終了しないため終了を試みます(pid=%d): %s"),
				timeout_soft/1000, pid, exec_ps[i].cmd);
			soft_kill(exec_ps[i].child_process);
			exec_ps[i].killmode = 1;
			exec_ps[i].lasttime = time_ms;
		} else if (!del && exec_ps[i].killmode && exec_ps[i].lasttime + timeout_hard < time_ms) {
			
			output_message(MSG_WARNING, TSD_TEXT("子プロセスが%d秒経っても終了しないため強制終了します(pid=%d): %s"),
				timeout_hard/1000, pid, exec_ps[i].cmd);
			hard_kill(exec_ps[i].child_process, exec_ps[i].cmd);
			rename_redirect_file(pid, &exec_ps[i].redirects);
			del = 1;
		}

		if (del) {
			for (j = i + 1; j < n_exec_ps; j++) {
				exec_ps[j-1] = exec_ps[j];
			}
			n_exec_ps--;
		}
	}
}

#ifdef TSD_PLATFORM_MSVC

static void ps_write(pipestat_t *ps)
{
	int remain;
	DWORD written, errcode;

	while (1) {
		remain = ps->write_bytes - ps->written_bytes;
		if (remain <= 0) {
			ps->write_busy = 0;
			break;
		}
		/* 再利用の際に初期化が必要かは不明だが、念のため使用前に必ず初期化しておく */
		memset(&ps->ol, 0, sizeof(OVERLAPPED));
		if (!WriteFile(ps->write_pipe, &ps->buf[ps->written_bytes], remain, &written, &ps->ol)) {
			if ((errcode=GetLastError()) == ERROR_IO_PENDING) {
				/* do nothing */
			} else {
				output_message(MSG_SYSERROR, L"書き込みエラーのためパイプを閉じます(WriteFile): pid=%d", pid_of(ps->child_process));
				goto ERROR_END;
			}
			break;
		}
		ps->written_bytes += written;
	}
	return;
ERROR_END:
	close_pipe(ps);
	ps->aborted = 1;
}

static void ps_check(pipestat_t *ps, int cancel)
{
	int remain;
	DWORD written, errcode;
	BOOL wait = FALSE;

	if (!ps->write_busy) {
		return;
	}

	if (cancel) {
		CancelIo(ps->write_pipe);
		wait = TRUE;
	}

	if (!GetOverlappedResult(ps->write_pipe, &ps->ol, &written, wait)) {
		if ((errcode=GetLastError()) == ERROR_IO_INCOMPLETE) {
			/* do nothing */
		} else {
			if (errcode == ERROR_OPERATION_ABORTED && cancel) {
				output_message(MSG_WARNING, L"IOをキャンセルしました: pid=%d", pid_of(ps->child_process));
			} else {
				output_message(MSG_SYSERROR, L"書き込みエラーのためパイプを閉じます(GetOverlappedResult): pid=%d", pid_of(ps->child_process));
				close_pipe(ps);
				ps->aborted = 1;
				return;
			}
			ps->write_busy = 0;
		}
	} else {
		ps->written_bytes += written;
		remain = ps->write_bytes - ps->written_bytes;
		if (remain > 0) {
			if (cancel) {
				output_message(MSG_WARNING, L"IOをキャンセルしました: pid=%d", pid_of(ps->child_process));
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

static int ps_write(pipestat_t *ps, const uint8_t *buf, int write_size)
{
	struct sigaction sa, sa_old;
	int errno_t, written;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;

	/* SIGPIPEを無視するように設定する */
	if (sigaction(SIGPIPE, &sa, &sa_old) < 0) {
		output_message(MSG_SYSERROR, "sigactionがエラーを返しましたためパイプを閉じます: pid=%d", (int)(ps->child_process));
		goto ERROR_END;
	}

	written = write(ps->write_pipe, buf, write_size);
	errno_t = errno;

	/* SIGPIPEのハンドラの設定を戻す */
	if (sigaction(SIGPIPE, &sa_old, NULL) < 0) {
		output_message(MSG_SYSERROR, "sigactionがエラーを返しましたためパイプを閉じます: pid=%d", (int)(ps->child_process));
		goto ERROR_END;
	}

	if (written < 0) {
		if (errno_t == EAGAIN || errno_t == EWOULDBLOCK || errno_t == EINTR) {
			/* do nothing */
			return 0;
		} else {
			output_message(MSG_SYSERROR, "書き込みエラーのためパイプを閉じます(write): pid=%d", (int)(ps->child_process));
			goto ERROR_END;
		}
	}
	return written;
ERROR_END:
	close_pipe(ps);
	ps->aborted = 1;
	return 0;
}

#endif

static int hook_pgoutput(void *param, const uint8_t *buf, const size_t size)
{
	my_retcode_t retcode;
	pipestat_t *pstat = (pipestat_t*)param;

	if (!pstat->used) {
		return 0;
	}

	if (pstat->aborted || pstat->no_pipe) {
		if (check_child_process(pstat->child_process, &retcode, &pstat->redirects, pstat->cmd) == 0) {
			pstat->cmd_result = pstat->no_pipe ? 0 : -2;
			pstat->cmd_retcode = retcode;
			pstat->used = 0;
		}
#ifdef TSD_PLATFORM_MSVC
		return 0;
#else
		return size;
#endif
	}

#ifdef TSD_PLATFORM_MSVC
	assert(!pstat->write_busy);
	memcpy(pstat->buf, buf, size);
	pstat->written_bytes = 0;
	pstat->write_bytes = (int)size;
	pstat->write_busy = 1;
	ps_write(pstat);
	return 1;
#else
	return ps_write(pstat, buf, size);
#endif
}

#ifdef TSD_PLATFORM_MSVC

static const int hook_pgoutput_check(void *param)
{
	pipestat_t *ps = (pipestat_t*)param;

	if (!ps->used || ps->aborted || ps->no_pipe) {
		return 0;
	}
	if (ps->write_busy) {
		ps_check(ps, 0);
	}
	return ps->write_busy;
}

#endif

static void hook_pgoutput_close(void *param, const proginfo_t *pi)
{
	UNREF_ARG(pi);
	pipestat_t *pstat = (pipestat_t*)param;

#ifdef TSD_PLATFORM_MSVC
	if (pstat->write_busy) {
		ps_check(pstat, 1);
	}
#endif
	if (pstat->aborted || pstat->no_pipe) {
		return;
	}
	close_pipe(pstat);
}

static int check_pipe_child_process(pipestat_t *pstat, int force_close, int remain_ms)
{
	my_retcode_t retcode;
	int pid;
	if (!pstat->used) {
		return 0;
	}
	if (check_child_process(pstat->child_process, &retcode, &pstat->redirects, pstat->cmd) == 0) {
		pstat->cmd_result = pstat->soft_closed;
		pstat->cmd_retcode = retcode;
		pstat->used = 0;
		return 0;
	}
	if (pstat->used) {
		if (force_close) {
			pid = pid_of(pstat->child_process);
			retcode = hard_kill(pstat->child_process, pstat->cmd);
			rename_redirect_file(pid, &pstat->redirects);
			pstat->cmd_result = 2;
			pstat->cmd_retcode = retcode;
			pstat->used = 0;
			return 0;
		} else if (remain_ms <= 0) {
			pstat->soft_closed = 1;
			output_message(MSG_WARNING, TSD_TEXT("子プロセスの終了を試みます(pid=%d): %s"),
				pid_of(pstat->child_process), pstat->cmd);
			soft_kill(pstat->child_process);
		}
	}
	return 1;
}

static int hook_pgoutput_forceclose(void *param, int force_close, int remain_ms)
{
	pipestat_t *pstat = (pipestat_t*)param;
	int i, busy = 0;

	busy |= check_pipe_child_process(pstat, force_close, remain_ms);
	for (i = 1; i <= pstat->n_connected_cmds; i++) {
		busy |= check_pipe_child_process(&pstat[i], force_close, remain_ms);
	}
	return busy;
}

static void hook_pgoutput_postclose(void *stat, const proginfo_t *pi)
{
	int i;
	my_file_handle_t child;
	redirect_pathinfo_t redirect;

	module_stat_t *pstat = (module_stat_t*)stat;

	for (i = 0; i < n_execcmds; i++) {
		child = exec_cmd(&execcmds[i], pstat->filename, pi, &redirect, pstat->pipestats);
#ifdef TSD_PLATFORM_MSVC
		if (child != INVALID_HANDLE_VALUE) {
#else
		if (child > 0) {
#endif
			insert_runngin_ps(child, execcmds[i].cmd, &redirect);
		}
	}
	free(pstat);
}

static void hook_tick(int64_t time_ms)
{
	static int64_t last_time = 0;
	if (last_time / 1000 != time_ms / 1000) {
		/* 実行中プロセスのチェックは1秒に1回だけ呼び出す */
		check_exec_child_processes(time_ms, CMDEXEC_LIMIT_SEC*1000, CMDEXEC_KILL_SEC*1000);
		last_time = time_ms;
	}
}

static void hook_preclose_module()
{
	/* prepare */
	check_exec_child_processes(gettime(), 10*1000, CMDEXEC_KILL_SEC*1000);
}

static void hook_close_module()
{
	/* collect all processes */
	while (n_exec_ps > 0) {
		check_exec_child_processes(gettime(), 5*1000, 5*1000);
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
	pipecmds[n_pipecmds].connecting = 0;
	pipecmds[n_pipecmds].no_pipe = 0;
	n_pipecmds++;
	open_connect = 0;
	set_last_cmd = 1;
	return NULL;
}

static const TSDCHAR* _set_pipe_opt(const TSDCHAR *param)
{
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

static const TSDCHAR *set_nopipe(const TSDCHAR* param)
{
	UNREF_ARG(param);
	if (n_pipecmds < 1) {
		return TSD_TEXT("コマンドが指定されていません");
	} else if (open_connect) {
		return TSD_TEXT("入力があるコマンドに--nopipeは使用できません");
	}
	pipecmds[n_pipecmds - 1].no_pipe = 1;
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
	set_last_cmd = 2;
	return NULL;
}

static const TSDCHAR* _set_cmd_opt(const TSDCHAR *param)
{
	if (execcmds[n_execcmds - 1].set_opt) {
		return TSD_TEXT("番組終了時実行コマンドのオプションは既に指定されています");
	}
	tsd_strlcpy(execcmds[n_execcmds - 1].opt, param, 2048 - 1);
	execcmds[n_execcmds - 1].set_opt = 1;
	return NULL;
}

static const TSDCHAR* set_opt(const TSDCHAR *param)
{
	if (set_last_cmd == 0) {
		return TSD_TEXT("オプション指定先のコマンドが設定されていません");
	} if (set_last_cmd == 1) {
		return _set_pipe_opt(param);
	} /*else*/
	return _set_cmd_opt(param);
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
	register_hook_pgoutput_precreate(hook_pgoutput_precreate);
	register_hook_pgoutput_postclose(hook_pgoutput_postclose);
	register_hook_pgoutput_create(hook_pgoutput_create);
	register_hook_pgoutput(hook_pgoutput, PIPE_BLOCK_SIZE);
#ifdef TSD_PLATFORM_MSVC
	register_hook_pgoutput_check(hook_pgoutput_check);
#else
	set_use_retval_pgoutput();
#endif
	register_hook_pgoutput_close(hook_pgoutput_close);
	register_hook_pgoutput_forceclose(hook_pgoutput_forceclose);
	register_hook_tick(hook_tick);
	register_hook_preclose_module(hook_preclose_module);
	register_hook_close_module(hook_close_module);
	register_hook_postconfig(hook_postconfig);
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--pipecmd"), TSD_TEXT("パイプ実行コマンド (複数指定可)"), 1, set_pipe_cmd },
	{ TSD_TEXT("--pwmin"), TSD_TEXT("パイプ実行コマンドのウィンドウを最小化する"), 0, set_min },
	{ TSD_TEXT("--pipeconn"), TSD_TEXT("パイプ実行コマンドの出力を次のコマンドの入力に接続する"), 0, set_connect },
	{ TSD_TEXT("--nopipe"), TSD_TEXT("実行のみでパイプ出力を行わない"), 0, set_nopipe },
	{ TSD_TEXT("--cmd"), TSD_TEXT("番組終了時実行コマンド (複数指定可)"), 1, set_cmd },
	{ TSD_TEXT("--setopt"), TSD_TEXT("コマンドのオプションを指定する"), 1, set_opt },
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