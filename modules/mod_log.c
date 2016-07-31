#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <Windows.h>
#include <share.h>
#include <sys/timeb.h>
#include <process.h>
#else
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "utils/arib_proginfo.h"
#include "utils/tsdstr.h"
#include "core/module_hooks.h"

static int output_log = 0;
static int set_log_fname = 0;
static TSDCHAR log_fname[MAX_PATH_LEN] = {TSD_CHAR('\0')};
static FILE *logfp = NULL;

static const TSDCHAR* set_log(const TSDCHAR *param)
{
	UNREF_ARG(param);
	output_log = 1;
	return NULL;
}

static const TSDCHAR *set_logfile(const TSDCHAR* param)
{
	output_log = 1;
	set_log_fname = 1;
	tsd_strlcpy(log_fname, param, MAX_PATH_LEN-1);
	return NULL;
}

static int hook_postconfig()
{
	TSDCHAR fname[MAX_PATH_LEN];
	const TSDCHAR *pfname;
	int pid;

	if (!output_log) {
		return 1;
	}

	if (set_log_fname) {
		pfname = log_fname;
	} else {
#ifdef TSD_PLATFORM_MSVC
		pid = _getpid();
#else
		pid = getpid();
#endif
		tsd_snprintf(fname, MAX_PATH_LEN-1, TSD_TEXT("./tsdump_%d.log"), pid);
		pfname = fname;
	}

#ifdef TSD_PLATFORM_MSVC
	logfp = _wfsopen(pfname, L"a, ccs=UTF-8", _SH_DENYWR); /* 他のプロセスからログのreadはできるように */
#else
	logfp = fopen(pfname, "a");
#endif
	if (!logfp) {
		output_message(MSG_ERROR, TSD_TEXT("ログファイル: %s を開けません"), pfname);
		return 0;
	}
	setvbuf(logfp, NULL, _IONBF, 0); /* テキストエディタ等から直ちに最新のログを見られるように */

	return 1;
}

static void hook_message(const TSDCHAR* modname, message_type_t msgtype, tsd_syserr_t *err, const TSDCHAR* msg)
{
	const TSDCHAR *msgtype_str = TSD_TEXT("");
	TSDCHAR msgbuf[256], timestr[64], d_msg[1024];
	int i, len, msec, errtype = 0;

	struct tm stm;
#ifdef TSD_PLATFORM_MSVC
	struct _timeb tv;
	_ftime_s(&tv);
	_localtime64_s(&stm, &tv.time);
	msec = tv.millitm;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	stm = *localtime(&tv.tv_sec);
	msec = tv.tv_usec / 1000;
#endif

	if (!logfp) {
		return;
	}

	/* 改行を取り除く */
	len = tsd_strlen(msg);
	if (len >= 1024) {
		len = 1023;
	}
	for (i = 0; i < len; i++) {
		if (msg[i] == TSD_CHAR('\n') || msg[i] == TSD_CHAR('\r')) {
			d_msg[i] = TSD_CHAR(' ');
		} else {
			d_msg[i] = msg[i];
		}
	}
	d_msg[len] = TSD_NULLCHAR;

	tsd_snprintf(timestr, 64, TSD_TEXT("[%d/%02d/%02d %02d:%02d:%02d.%03d]"), stm.tm_year + 1900, stm.tm_mon + 1, stm.tm_mday,
		stm.tm_hour, stm.tm_min, stm.tm_sec, msec);

	if ( msgtype == MSG_WARNING || msgtype == MSG_PACKETERROR ) {
		msgtype_str = TSD_TEXT("[WARNING] ");
		errtype = 1;
	} else if ( msgtype == MSG_ERROR || msgtype == MSG_SYSERROR || msgtype == MSG_WINSOCKERROR ) {
		msgtype_str = TSD_TEXT("[ERROR] ");
		errtype = 1;
	}

	if ( msgtype == MSG_SYSERROR || msgtype == MSG_WINSOCKERROR ) {
#ifdef TSD_PLATFORM_MSVC
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
#else
		strerror_r(*err, msgbuf, 256);
#endif

		if (modname && errtype) {
			tsd_fprintf(logfp, TSD_TEXT("%s %s(%s): %s <0x%x:%s>\n"), timestr, msgtype_str, modname, d_msg, *err, msgbuf);
		} else {
			tsd_fprintf(logfp, TSD_TEXT("%s %s%s <0x%x:%s>\n"), timestr, msgtype_str, d_msg, *err, msgbuf);
		}
	} else {
		if (modname && errtype) {
			tsd_fprintf(logfp, TSD_TEXT("%s %s(%s): %s\n"), timestr, msgtype_str, modname, d_msg);
		} else {
			tsd_fprintf(logfp, TSD_TEXT("%s %s%s\n"), timestr, msgtype_str, d_msg);
		}
	}
}

static void register_hooks()
{
	register_hook_message(hook_message);
	register_hook_postconfig(hook_postconfig);
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--log"), TSD_TEXT("ログを出力する"), 0, set_log },
	{ TSD_TEXT("--logfile"), TSD_TEXT("ログのファイル名"), 1, set_logfile },
	{ NULL },
};

TSD_MODULE_DEF(
	mod_log,
	register_hooks,
	cmds,
	NULL
);
