#pragma comment(lib, "shlwapi.lib")

#include <windows.h>
#include <stdio.h>
#include <inttypes.h>
#include <process.h>

#include <shlwapi.h>

#include "modules_def.h"

typedef struct{
	HANDLE master_event;
	HANDLE slave_event;
	HANDLE h_thread;
	HANDLE fh;
	int busy;
	int endflag;
	int write_size;
	const uint8_t *write_buf;
	int err;
	DWORD errcode;
	const WCHAR *errmsg;
} my_nonblockio_t;

typedef struct{
	int used;
	HANDLE write_pipe;
	HANDLE child_process;
	my_nonblockio_t nbio;
} pipestat_t;

#define MAX_PIPECMDS 32

static int n_pipecmds = 0;
static WCHAR *pipecmds[MAX_PIPECMDS];
static int pcwindow_min = 0;
static int waitmode = 0;

static unsigned int __stdcall my_nonblockio_worker(void *param)
{
	my_nonblockio_t *nbio = (my_nonblockio_t*)param;
	DWORD ret, written_total, written;

	for (;;) {
		ret = WaitForSingleObject(nbio->master_event, INFINITE);
		if (ret == WAIT_OBJECT_0) {
			if (nbio->endflag) {
				nbio->err = 0;
				SetEvent(nbio->slave_event);
				return 0;
			}
		} else {
			nbio->err = 1;
			nbio->errcode = GetLastError();
			nbio->errmsg = L"スレッドのWaitForSingleObject()が失敗しました";
			SetEvent(nbio->slave_event);
			return 1;
		}

		written_total = 0;
		while (written_total < (DWORD)nbio->write_size) {
			ret = WriteFile(nbio->fh,
					&nbio->write_buf[written_total],
					nbio->write_size - written_total,
					&written,
					NULL);
			if (!ret) {
				if (nbio->endflag) {
					/* masterからの指示でエラーになった */
					break;
				}
				nbio->err = 1;
				nbio->errcode = GetLastError();
				nbio->errmsg = L"WriteFile()が失敗しました";
				SetEvent(nbio->slave_event);
				return 1;
			}
			written_total += written;
		}
		SetEvent(nbio->slave_event);
	}
}

static int my_nonblockio_create(my_nonblockio_t *nbio, HANDLE fh)
{
	nbio->master_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (nbio->master_event == NULL) {
		nbio->err = 1;
		nbio->errcode = GetLastError();
		nbio->errmsg = L"イベントの生成が失敗しました(CreateEvent)";
		return 0;
	}
	nbio->slave_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (nbio->slave_event == NULL) {
		nbio->err = 1;
		nbio->errcode = GetLastError();
		nbio->errmsg = L"イベントの生成が失敗しました(CreateEvent)";
		CloseHandle(nbio->master_event);
		return 0;
	}
	nbio->h_thread = (HANDLE)_beginthreadex(NULL, 0, my_nonblockio_worker, nbio, 0, NULL);
	if (nbio->h_thread == NULL) {
		nbio->err = 1;
		nbio->errcode = ERROR_SUCCESS;
		nbio->errmsg = L"スレッドの生成が失敗しました";
		CloseHandle(nbio->master_event);
		CloseHandle(nbio->slave_event);
		return 0;
	}
	nbio->fh = fh;
	nbio->endflag = 0;
	nbio->err = 0;
	nbio->busy = 0;
	return 1;
}

static int my_nonblockio_check(my_nonblockio_t *nbio, DWORD wait_ms)
{
	DWORD ret;
	if (nbio->busy) {
		ret = WaitForSingleObject(nbio->slave_event, wait_ms);
		if (ret == WAIT_OBJECT_0) {
			nbio->busy = 0;
		} else if (ret == WAIT_TIMEOUT) {
			nbio->busy = 1;
		} else {
			nbio->busy = 0;
			nbio->err = 1;
			nbio->errcode = GetLastError();
			nbio->errmsg = L"WaitForSingleObject()が失敗しました";
		}
	}

	if (nbio->err) {
		return -1;
	}
	return nbio->busy;
}

static int my_nonblockio_write(my_nonblockio_t *nbio, const uint8_t *buf, int size)
{
	while (nbio->busy) {
		//printf("ここには来ない！\n");
		my_nonblockio_check(nbio, INFINITE);
	}
	if (nbio->err) {
		return 0;
	}

	if (nbio->busy == 0) {
		nbio->write_size = size;
		nbio->write_buf = buf;
		nbio->busy = 1;
		SetEvent(nbio->master_event);
	}
	return 1;
}

static void my_nonblockio_setendflag(my_nonblockio_t *nbio)
{
	nbio->endflag= 1;
}

static void my_nonblockio_close(my_nonblockio_t *nbio)
{
	DWORD ret;

	my_nonblockio_setendflag(nbio);
	SetEvent(nbio->master_event);
	ret = WaitForSingleObject(nbio->h_thread, 500);
	if (ret != WAIT_OBJECT_0) {
		output_message(MSG_SYSERROR, L"スレッドが応答しないので強制終了します(WaitForSingleObject)");
		TerminateThread(nbio->h_thread, 1);
	}
	CloseHandle(nbio->h_thread);
}

static const WCHAR* my_nonblockio_errinfo(my_nonblockio_t *nbio, DWORD *errcode)
{
	if (!nbio->err) {
		return NULL;
	}
	*errcode = nbio->errcode;
	return nbio->errmsg;
}

static void create_pipe(pipestat_t *ps, const WCHAR *cmdname, const WCHAR *fname)
{
	HANDLE hRead, hWrite, hReadTemp;
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};

	WCHAR cmdarg[2048];

	ps->used = 0;

	if (!CreatePipe(&hReadTemp, &hWrite, NULL, 0)) {
		output_message(MSG_SYSERROR, L"パイプの作成に失敗(CreatePipe)");
		return;
	}
	if ( !DuplicateHandle(
			GetCurrentProcess(),
			hReadTemp,
			GetCurrentProcess(),
			&hRead,
			0,
			TRUE,
			DUPLICATE_SAME_ACCESS) ) {
		output_message(MSG_SYSERROR, L"DuplicateHandle()に失敗");
		CloseHandle(hWrite);
		CloseHandle(hReadTemp);
		return;
	}

	CloseHandle(hReadTemp);

	si.cb = sizeof(STARTUPINFO);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = hRead;
	si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	if (pcwindow_min) {
		si.dwFlags |= STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_MINIMIZE;
	}

	if (!my_nonblockio_create(&ps->nbio, hWrite)) {
		if (ps->nbio.errcode == ERROR_SUCCESS) {
			output_message(MSG_ERROR, ps->nbio.errmsg);
		} else {
			SetLastError(ps->nbio.errcode);
			output_message(MSG_SYSERROR, ps->nbio.errmsg);
		}
		return;
	}

	swprintf(cmdarg, 2048 - 1, L"\"%s\" \"%s\"", cmdname, fname);

	output_message(MSG_NOTIFY, L"パイプコマンド実行: %s", cmdarg);

	if (!CreateProcess(cmdname, cmdarg, NULL, NULL, TRUE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
		output_message(MSG_SYSERROR, L"子プロセスの生成に失敗(CreateProcess)");
		CloseHandle(hWrite);
		CloseHandle(hRead);
		return;
	}

	ps->child_process = pi.hProcess;
	ps->write_pipe = hWrite;
	ps->used = 1;

	CloseHandle(pi.hThread);
	CloseHandle(hRead);

	return;
}

static void *hook_pgoutput_create(const WCHAR *fname, const ProgInfo *pi, const ch_info_t *ch_info_t)
{
	int i;
	UNREF_ARG(pi);
	UNREF_ARG(ch_info_t);
	pipestat_t *ps = (pipestat_t*)malloc( sizeof(pipestat_t) * n_pipecmds );
	for (i = 0; i < n_pipecmds; i++) {
		create_pipe(&ps[i], pipecmds[i], fname);
	}
	return ps;
}

static void close_pipe(pipestat_t *ps)
{
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

static void hook_pgoutput(void *stat, const unsigned char *buf, const size_t size)
{
	pipestat_t *ps = (pipestat_t*)stat;
	int i, ret, error;
	const WCHAR *errmsg;
	DWORD errcode;

	for (i = 0; i < n_pipecmds; i++) {
		if (!ps[i].used) {
			continue;
		}

		error = 0;
		if ( !my_nonblockio_write(&ps[i].nbio, buf, size) ) {
			/* 書き込み指令失敗 */
			error = 1;
		} else {
			/* IO状態をチェック */
			ret = my_nonblockio_check(&ps[i].nbio, 1000);
			if (ret < 0) {
				/* エラーが発生 */
				error = 1;
			} else if (ret) {
				/* タイムアウト(スレッドがブロック) */
				my_nonblockio_setendflag(&ps[i].nbio);
				TerminateProcess(ps[i].child_process, 1);
				output_message(MSG_ERROR, L"パイプがブロックしているのでプロセスを強制終了しました");
				my_nonblockio_close(&ps[i].nbio);
				close_pipe(&ps[i]);
				wait_child_process(&ps[i]);
			}
		}

		if(error) {
			errmsg = my_nonblockio_errinfo(&ps[i].nbio, &errcode);
			if (errcode != ERROR_SUCCESS) {
				SetLastError(errcode);
				output_message(MSG_SYSERROR, L"エラーが発生したのでパイプをクローズします: %s", errmsg, errcode);
			} else {
				output_message(MSG_ERROR, L"エラーが発生したのでパイプをクローズします: %s", errmsg, errcode);
			}
			my_nonblockio_close(&ps[i].nbio);
			close_pipe(&ps[i]);
			wait_child_process(&ps[i]);
		}
	}
}

static void hook_pgoutput_close(void *pstat, const ProgInfo *pi)
{
	pipestat_t *ps = (pipestat_t*)pstat;
	UNREF_ARG(pi);
	int i;
	for (i = 0; i < n_pipecmds; i++) {
		my_nonblockio_close(&ps[i].nbio);
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
	register_hook_pgoutput_close(hook_pgoutput_close);
}

static cmd_def_t cmds[] = {
	{ L"--pipe", L"パイプ実行コマンド (複数指定可)", 1, set_pipe },
	{ L"--pwmin", L"パイプ実行コマンドのウィンドウを最小化する", 0, set_min },
	NULL,
};

MODULE_DEF module_def_t mod_pipeoutput_win = {
	TSDUMP_MODULE_V2,
	L"mod_pipeoutput_win",
	register_hooks,
	cmds
};