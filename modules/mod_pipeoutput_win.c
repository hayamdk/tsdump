#pragma comment(lib, "shlwapi.lib")

#include <windows.h>
#include <stdio.h>
#include <inttypes.h>
#include <process.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>

#include "core/tsdump_def.h"
#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"
#include "utils/tsdstr.h"
#include "core/tsdump.h"

typedef struct{
	int used;
	int write_busy;
	const uint8_t *buf;
	int write_bytes;
	int written_bytes;
	OVERLAPPED ol;
	HANDLE write_pipe;
	HANDLE child_process;
} pipestat_t;

#define MAX_PIPECMDS 32

static int n_pipecmds = 0;
static WCHAR *pipecmds[MAX_PIPECMDS];
static int pcwindow_min = 0;
static int waitmode = 0;

static void create_pipe(pipestat_t *ps, const WCHAR *cmdname, const WCHAR *fname)
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
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	if (pcwindow_min) {
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_MINIMIZE;
	}

	swprintf(cmdarg, 2048 - 1, L"\"%s\" \"%s\"", cmdname, fname);

	output_message(MSG_NOTIFY, L"パイプコマンド実行: %s", cmdarg);

	if (!CreateProcess(cmdname, cmdarg, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
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

static void *hook_pgoutput_create(const WCHAR *fname, const proginfo_t *pi, const ch_info_t *ch_info_t, const int actually_start)
{
	int i;
	UNREF_ARG(pi);
	UNREF_ARG(ch_info_t);
	UNREF_ARG(actually_start);
	pipestat_t *ps = (pipestat_t*)malloc( sizeof(pipestat_t) * n_pipecmds );
	for (i = 0; i < n_pipecmds; i++) {
		create_pipe(&ps[i], pipecmds[i], fname);
	}
	return ps;
}

static void close_pipe(pipestat_t *ps)
{
	ps->write_busy = 0;
	if (ps->used) {
		CloseHandle(ps->write_pipe);
	}
}

static void wait_child_process(pipestat_t *ps)
{
	DWORD ret;
	if (ps->used) {
		ret = WaitForSingleObject(ps->child_process, 500);
		if (ret != WAIT_OBJECT_0) {
			output_message(MSG_SYSERROR, L"プロセスが応答しないので強制終了します(WaitForSingleObject)");
			TerminateProcess(ps->child_process, 1);
		}
		CloseHandle(ps->child_process);
		ps->used = 0;
	}
}

static void ps_write(pipestat_t *ps)
{
	int remain;
	DWORD written, errcode;

	if (ps->write_busy) {
		return;
	}

	while (1) {
		remain = ps->write_bytes - ps->written_bytes;
		if (remain <= 0) {
			ps->write_bytes = 0;
			break;
		}
		if (!WriteFile(ps->write_pipe, &ps->buf[ps->written_bytes], remain, &written, &ps->ol)) {
			if ((errcode=GetLastError()) == ERROR_IO_PENDING) {
				ps->write_busy = 1;
			} else {
				output_message(MSG_SYSERROR, L"書き込みエラーのためパイプを閉じます(WriteFile):");
				close_pipe(ps);
				wait_child_process(ps);
				return;
			}
			break;
		}
		ps->written_bytes += written;
	}
}

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
				wait_child_process(ps);
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
			ps_write(&pss[i]);
		} else {
			/* never come */
			*(char*)(NULL) = 'A';
		}
	}
}

static int hook_pgoutput_check(void *stat)
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

static int hook_pgoutput_wait(void *stat)
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
		if (pss[i].write_busy) {
			CancelIo(pss[i].write_pipe);
			ps_check(&pss[i], 1);
		}
	}
	return ret;
}

static void hook_pgoutput_close(void *pstat, const proginfo_t *pi)
{
	UNREF_ARG(pi);

	pipestat_t *ps = (pipestat_t*)pstat;
	int i;
	for (i = 0; i < n_pipecmds; i++) {
		close_pipe(&ps[i]);
	}
	for (i = 0; i < n_pipecmds; i++) {
		wait_child_process(&ps[i]);
	}
	free(ps);
}

static const WCHAR* set_pipe(const WCHAR *param)
{
	if (n_pipecmds >= MAX_PIPECMDS) {
		return L"指定するパイプ実行コマンドの数が多すぎます";
	}
	pipecmds[n_pipecmds] = _wcsdup(param);
	n_pipecmds++;
	return NULL;
}

static const WCHAR *set_min(const WCHAR* param)
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
	{ L"--pipe", L"パイプ実行コマンド (複数指定可)", 1, set_pipe },
	{ L"--pwmin", L"パイプ実行コマンドのウィンドウを最小化する", 0, set_min },
	NULL,
};

MODULE_DEF module_def_t mod_pipeoutput_win = {
	TSDUMP_MODULE_V4,
	L"mod_pipeoutput_win",
	register_hooks,
	cmds
};