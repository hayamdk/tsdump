#include <Windows.h>
#include <inttypes.h>
#include <stdio.h>
#include <process.h>
#include <time.h>
#include <sys/timeb.h>
#include <share.h>

#include "module_def.h"
#include "ts_proginfo.h"
#include "module_hooks.h"

static int output_log = 0;
static const WCHAR *log_fname = NULL;
static FILE *logfp = NULL;

static const WCHAR* set_log(const WCHAR *param)
{
	UNREF_ARG(param);
	output_log = 1;
	return NULL;
}

static const WCHAR *set_logfile(const WCHAR* param)
{
	output_log = 1;
	log_fname = _wcsdup(param);
	return NULL;
}

static int hook_postconfig()
{
	WCHAR fname[MAX_PATH_LEN];
	const WCHAR *pfname;
	int pid;

	if (!output_log) {
		return 1;
	}

	if (log_fname) {
		pfname = log_fname;
	} else {
		pid = _getpid();
		swprintf(fname, MAX_PATH_LEN-1, L"./tsdump_%d.log", pid);
		pfname = fname;
	}

	logfp = _wfsopen(pfname, L"a, ccs=UTF-8", _SH_DENYWR); /* 他のプロセスからログのreadはできるように */
	if (!logfp) {
		output_message(MSG_ERROR, L"ログファイル: %s を開けません", pfname);
		return 0;
	}
	setvbuf(logfp, NULL, _IONBF, 0); /* テキストエディタ等から直ちに最新のログを見られるように */

	return 1;
}

static void hook_message(const WCHAR* modname, message_type_t msgtype, DWORD* err, const WCHAR* msg)
{
	const WCHAR *msgtype_str = L"";
	WCHAR msgbuf[256], timestr[64], d_msg[1024];
	int i, len, errtype = 0;
	struct _timeb tv;
	struct tm stm;

	if (!logfp) {
		return;
	}

	/* 改行を取り除く */
	len = wcslen(msg);
	if (len >= 1024) {
		len = 1023;
	}
	for (i = 0; i < len; i++) {
		if (msg[i] == L'\n' || msg[i] == L'\r') {
			d_msg[i] = L' ';
		} else {
			d_msg[i] = msg[i];
		}
	}
	d_msg[len] = L'\0';

	_ftime_s(&tv);
	_localtime64_s(&stm, &tv.time);
	wsprintf(timestr, L"[%d/%02d/%02d %02d:%02d:%02d.%03d]", stm.tm_year + 1900, stm.tm_mon + 1, stm.tm_mday,
		stm.tm_hour, stm.tm_min, stm.tm_sec, tv.millitm);

	if ( msgtype == MSG_WARNING || msgtype == MSG_PACKETERROR ) {
		msgtype_str = L"[WARNING] ";
		errtype = 1;
	} else if ( msgtype == MSG_ERROR || msgtype == MSG_SYSERROR || msgtype == MSG_WINSOCKERROR ) {
		msgtype_str = L"[ERROR] ";
		errtype = 1;
	}

	if ( msgtype == MSG_SYSERROR || msgtype == MSG_WINSOCKERROR ) {
		FormatMessage(
			/*FORMAT_MESSAGE_ALLOCATE_BUFFER |*/
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS |
			FORMAT_MESSAGE_MAX_WIDTH_MASK,
			NULL,
			*err,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			msgbuf,
			256,
			NULL
		);

		if (msgbuf[wcslen(msgbuf)-1] == L' ') {
			msgbuf[wcslen(msgbuf)-1] = L'\0';
		}

		if (modname && errtype) {
			fwprintf(logfp, L"%s %s(%s): %s <0x%x:%s>\n", timestr, msgtype_str, modname, d_msg, *err, msgbuf);
		} else {
			fwprintf(logfp, L"%s %s%s <0x%x:%s>\n", timestr, msgtype_str, d_msg, *err, msgbuf);
		}
	} else {
		if (modname && errtype) {
			fwprintf(logfp, L"%s %s(%s): %s\n", timestr, msgtype_str, modname, d_msg);
		} else {
			fwprintf(logfp, L"%s %s%s\n", timestr, msgtype_str, d_msg);
		}
	}
}

static void register_hooks()
{
	register_hook_message(hook_message);
	register_hook_postconfig(hook_postconfig);
}

static cmd_def_t cmds[] = {
	{ L"--log", L"ログを出力する", 0, set_log },
	{ L"--logfile", L"ログのファイル名", 1, set_logfile },
	NULL,
};

MODULE_DEF module_def_t mod_log = {
	TSDUMP_MODULE_V3,
	L"mod_log",
	register_hooks,
	cmds
};