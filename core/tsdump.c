#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
	#pragma comment(lib, "shlwapi.lib")
	#pragma comment(lib, "Ws2_32.lib")

	#include <windows.h>
#else
	#include <errno.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <inttypes.h>
#include <stdarg.h>

#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"
#include "utils/ts_parser.h"
#include "core/tsdump.h"
#include "core/ts_output.h"
#include "core/load_modules.h"
#include "utils/tsdstr.h"
#include "utils/aribstr.h"
#include "utils/path.h"

//#define HAVE_TIMECALC_DECLARATION
//#include "timecalc.h"

int BUFSIZE = BUFSIZE_DEFAULT * 1024 * 1024;
int OVERLAP_SEC = OVERLAP_SEC_DEFAULT;
int CHECK_INTERVAL = CHECK_INTERVAL_DEFAULT;
int MAX_PGOVERLAP = MAX_PGOVERLAP_DEFAULT;

int termflag = 0;

int param_sp_num = -1;
int param_ch_num = -1;
int param_all_services;
unsigned int param_services[MAX_SERVICES];
int param_n_services = 0;
int param_nowait = 0;

int need_clear_line = 0;

void signal_handler(int sig)
{
	UNREF_ARG(sig);
	termflag = 1;
	output_message(MSG_NOTIFY, TSD_TEXT("\n終了シグナルをキャッチ"));
}

void _output_message(const char *fname, message_type_t msgtype, const TSDCHAR *fmt, ...)
{
	va_list list;
	va_start(list, fmt);
	tsd_syserr_t lasterr, *plasterr = NULL;
	TSDCHAR msg[2048];
	const TSDCHAR *modname;
	const char *cp;
	int len;

	/* 拡張子を除いたファイル名=モジュール名をコピー */
#ifdef TSD_PLATFORM_MSVC
	WCHAR modpath[MAX_PATH_LEN], *wcp;
		for (wcp = modpath, cp = fname;
			*cp != '\0' && *cp != '.' && wcp < &modpath[MAX_PATH_LEN];
			cp += len, wcp++) {
		len = mbtowc(wcp, cp, MB_CUR_MAX);
	}
	*wcp = L'\0';

	/* __FILE__にフルパスが入っている場合があるのでファイル名のみ取り出す */
	modname = path_getfile(modpath);

	if (msgtype == MSG_SYSERROR) {
		lasterr = GetLastError();
		plasterr = &lasterr;
	} else if (msgtype == MSG_WINSOCKERROR) {
		lasterr = WSAGetLastError();
		plasterr = &lasterr;
	}
#else
	/* __FILE__にフルパスが入っている場合があるのでファイル名のみ取り出す */
	modname = path_getfile(fname);

	if (msgtype == MSG_SYSERROR) {
		lasterr = errno;
		plasterr = &lasterr;
	}
#endif

	tsd_vsnprintf(msg, 2048-1, fmt, list);
	va_end(list);

	if ( tsd_strncmp(modname, TSD_TEXT("mod_"), 4) == 0 ) {
		do_message(modname, msgtype, plasterr, msg);
	} else {
		do_message(NULL, msgtype, plasterr, msg);
	}
}

int print_services(ts_service_list_t *services_list)
{
	int i, get_service_info = 0, sid;

	for (i = 0; i < services_list->n_services; i++) {
		if ( !(services_list->proginfos[i].status & PGINFO_GET_PAT) ) {
			return 0;
		}
		if (services_list->proginfos[i].status & PGINFO_GET_SERVICE_INFO) {
			get_service_info = 1;
		}
	}
	if (!get_service_info) {
		/* 最低1サービスでもサービス情報が取得できるのを待つ */
		return 0;
	}

	output_message(MSG_DISP, TSD_TEXT("サービス数: %d"), services_list->n_services);
	for (i = 0; i < services_list->n_services; i++) {
		sid = services_list->proginfos[i].service_id;
		if (services_list->proginfos[i].status & PGINFO_GET_SERVICE_INFO) {
			output_message(MSG_DISP, TSD_TEXT("サービス%d: ID=%04x(%d), %s"), i, sid, sid, services_list->proginfos[i].service_name.str);
		} else {
			output_message(MSG_DISP, TSD_TEXT("サービス%d: ID=%04x(%d), (名称不明)"), i, sid, sid);
		}
	}
	return 1;
}

#ifdef TSD_PLATFORM_MSVC

int save_line(COORD *new_pos)
{
	CONSOLE_SCREEN_BUFFER_INFO ci;
	HANDLE hc;
	int console_width = 0;

	hc = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hc != INVALID_HANDLE_VALUE) {
		if (GetConsoleScreenBufferInfo(hc, &ci) != 0) {
			if (ci.dwCursorPosition.X != 0 || ci.dwCursorPosition.Y != 0) { /* WINEだとこれを取得できない(0がセットされる) */
				console_width = ci.dwSize.X;
				new_pos->X = 0;
				new_pos->Y = ci.dwCursorPosition.Y;
				if ( ci.dwCursorPosition.Y > ci.dwSize.Y - 3 ) { /* 3行分の余白が無ければ最後に行を戻す */
					new_pos->Y = ci.dwSize.Y - 3;
				}
			}
		}
	} else {
		console_width = -1;
	}

	if (console_width >= 256) {
		console_width = 255;
	}

	return console_width;
}

void restore_line(const COORD new_pos)
{
	HANDLE hc = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hc != INVALID_HANDLE_VALUE) {
		SetConsoleCursorPosition(hc, new_pos);
	}
}

void clear_line()
{
	int console_width;
	COORD new_pos;
	WCHAR line[256];
	int i;

	if (!need_clear_line) {
		return;
	}

	console_width = save_line(&new_pos);
	if (console_width > 0) {
		for (i = 0; i < console_width - 1; i++) {
			line[i] = TSD_CHAR(' ');
		}
		line[console_width - 1] = TSD_NULLCHAR;

		tsd_printf(TSD_TEXT("%s\n"), line);
		tsd_printf(TSD_TEXT("%s\n"), line);
		tsd_printf(TSD_TEXT("%s\n"), line);
		restore_line(new_pos);
	}

	need_clear_line = 0;
}

#endif

void print_stat(ts_output_stat_t *tos, int n_tos, const TSDCHAR *stat)
{
#ifdef TSD_PLATFORM_MSVC
	int n, i, j, backward_size, console_width, width;
	char line[256], hor[256];
	char *p = line;
	static int cnt = 0;
	COORD new_pos;
	time_mjd_t time_jst;
#endif
	double rate;

	if(!tos) {
		return;
	}

#ifdef TSD_PLATFORM_MSVC
	console_width = save_line(&new_pos);
	if (console_width < 0) {
		fprintf(stderr, "console error\r");
	} else if (console_width == 0) {
#endif
		rate = 100.0 * tos->pos_filled / BUFSIZE;
		tsd_printf(TSD_TEXT("%s buf:%.1f%% \r"), stat, rate);
#ifdef TSD_PLATFORM_MSVC
	} else {
		width = ( console_width - 6 - (n_tos-1) ) / n_tos;

		for (i = 0; i < n_tos; i++) {
			for (backward_size = j = 0; j < tos[i].n_th; j++) {
				backward_size += tos[i].th[j].bytes;
			}

			for (n = 0; n < width; n++) {
				int pos = (int)( (double)BUFSIZE / width * (n+0.5) );
				if (pos < tos[i].pos_write) {
					*p = '-';
				} else if (pos < tos[i].pos_filled) {
					*p = '!';
				} else {
					*p = '_';
				}
				if (pos > tos[i].pos_filled - backward_size) {
					if (*p == '-') {
						*p = '+';
					} else if(*p == '/') {
						*p = '|';
					}
				}
				p++;
			}
			if (i != n_tos - 1) {
				*(p++) = ' ';
			}
		}
		*p = '\0';

		if (get_stream_timestamp(tos->proginfo, &time_jst)) {
			snprintf(hor, sizeof(hor), "---- [%04d/%02d/%02d %02d:%02d:%02d.%03d] ",
				time_jst.year,
				time_jst.mon,
				time_jst.day,
				time_jst.hour,
				time_jst.min,
				time_jst.sec,
				time_jst.usec/1000
			);
		} else {
			snprintf(hor, sizeof(hor), "---- [UNKNOWN TIMESTAMP] ");
		}

		width = strlen(hor);
		if (console_width > width+1) {
			memset(&hor[width], '-', console_width - width - 1);
			hor[console_width - 1] = '\0';
		}

		tsd_printf(TSD_TEXT("%S\n%s\nbuf: %S"),hor, stat, line);
		restore_line(new_pos);

		need_clear_line = 1;
	}
#endif
}

void init_service_list(ts_service_list_t *service_list)
{
	int i;

	service_list->pid0x00.pid = 0;
	service_list->pid0x00.stat = PAYLOAD_STAT_INIT;
	service_list->pid0x11.pid = 0x11;
	service_list->pid0x11.stat = PAYLOAD_STAT_INIT;
	service_list->pid0x12.pid = 0x12;
	service_list->pid0x12.stat = PAYLOAD_STAT_INIT;
	service_list->pid0x14.pid = 0x14;
	service_list->pid0x14.stat = PAYLOAD_STAT_INIT;
	service_list->pid0x26.pid = 0x26;
	service_list->pid0x26.stat = PAYLOAD_STAT_INIT;
	service_list->pid0x27.pid = 0x27;
	service_list->pid0x27.stat = PAYLOAD_STAT_INIT;

	for (i = 0; i < MAX_SERVICES_PER_CH; i++) {
		init_proginfo(&service_list->proginfos[i]);
	}
}

void main_loop(void *generator_stat, void *decoder_stat, int encrypted, ch_info_t *ch_info)
{
	uint8_t *recvbuf, *decbuf;

	int n_recv, n_dec;

	int64_t total = 0;
	int64_t subtotal = 0;

	int64_t nowtime, lasttime;

	ts_output_stat_t *tos = NULL;
	int n_tos = 0;

	TSDCHAR title[256];
	decoder_stats_t stats;

	lasttime = nowtime = gettime();

	double tdiff, Mbps=0.0;

	int i;
	int single_mode = 0;

	int pos;
	int printservice = 0;

	ts_service_list_t service_list;

	const uint8_t *packet;
	ts_header_t tsh;

	init_service_list(&service_list);

	if ( !param_all_services && param_n_services == 0 ) {
		single_mode = 1;
	}

	do_open_stream();

	while ( !termflag ) {
		nowtime = gettime();

		do_stream_generator(generator_stat, &recvbuf, &n_recv);
		do_encrypted_stream(recvbuf, n_recv);

		do_stream_decoder(decoder_stat, &decbuf, &n_dec, recvbuf, n_recv);
		do_stream(decbuf, n_dec, encrypted);

		for (i = 0; i < n_dec; i+=188) {
			packet = &decbuf[i];
			if (!parse_ts_header(packet, &tsh)) {
				if (tsh.valid_sync_byte) {
					/* PESパケットでここに来る場合があるので警告はひとまずOFF  e.g. NHK BS1
					   PESパケットの規格を要調査 */
					//output_message(MSG_PACKETERROR, L"Invalid ts header! pid=0x%x(%d)", tsh.pid, tsh.pid);
				} else {
					output_message(MSG_PACKETERROR, TSD_TEXT("Invalid ts packet!"));
				}
				continue; /* pass */
			}

			if (service_list.pid0x00.stat != PAYLOAD_STAT_FINISHED) {
				/* PATの取得は初回のみ */
				parse_PAT(&service_list.pid0x00, packet, &tsh, &service_list);
			} else {
				parse_PMT(packet, &tsh, &service_list);
				if (!printservice) {
					printservice = print_services(&service_list);
				}
			}
			parse_PCR(packet, &tsh, &service_list);
			parse_TOT_TDT(packet, &tsh, &service_list);
			parse_SDT(&service_list.pid0x11, packet, &tsh, &service_list);
			parse_EIT(&service_list.pid0x12, packet, &tsh, &service_list);
			parse_EIT(&service_list.pid0x26, packet, &tsh, &service_list);
			parse_EIT(&service_list.pid0x27, packet, &tsh, &service_list);
		}

		//tc_start("bufcopy");
		if ( single_mode ) { /* 単一書き出しモード */
			/* tosを生成 */
			if ( ! tos ) {
				n_tos = param_n_services = 1;
				tos = (ts_output_stat_t*)malloc(1 * sizeof(ts_output_stat_t));
				init_tos(tos);
				tos->proginfo = &service_list.proginfos[0];
			}

			/* パケットをバッファにコピー */
			ts_copybuf(tos, decbuf, n_dec);
			ts_update_transfer_history(tos, nowtime, n_dec);

		} else {  /* サービスごと書き出しモード */
			/* pos_filledをコピー */
			for (i = 0; i < n_tos; i++) {
				tos[i].pos_filled_old = tos[i].pos_filled;
			}

			/* パケットを処理 */
			for (pos = 0; pos < (int)n_dec; pos += 188) {
				packet = &decbuf[pos];

				/* PAT, PMTを取得 */
				//parse_ts_packet(&tps, packet);

				/* tosを生成 */
				if ( ! tos && service_list.pid0x00.stat == PAYLOAD_STAT_FINISHED ) {
					n_tos = create_tos_per_service(&tos, &service_list, ch_info);
				}

				/* サービスごとにパケットをバッファにコピー */
				for (i = 0; i < n_tos; i++) {
					copy_current_service_packet(&tos[i], &service_list, packet);
				}
			}

			/* コピーしたバイト数の履歴を保存 */
			for (i = 0; i < n_tos; i++) {
				ts_update_transfer_history( &tos[i], nowtime, tos[i].pos_filled - tos[i].pos_filled_old );
			}
		}
		//tc_end();

		//tc_start("output");
		for (i = 0; i < n_tos; i++) {
			ts_output(&tos[i], nowtime, 0);
		}
		//tc_end();

		//tc_start("proginfo");
		/* 定期的に番組情報をチェック */
		if ( nowtime / CHECK_INTERVAL != lasttime / CHECK_INTERVAL ) {
			tdiff = (double)(nowtime - lasttime) / 1000;
			Mbps = (double)subtotal * 8 / 1024 / 1024 / tdiff;



			do_stream_decoder_stats(decoder_stat, &stats);

			double siglevel = do_stream_generator_siglevel(generator_stat);
			
#ifdef TSD_PLATFORM_MSVC
			tsd_snprintf(title, 256, TSD_TEXT("%s:%s:%s|%.1fdb %.1fMbps D:%I64d S:%I64d %.1fGB"),
				ch_info->tuner_name, ch_info->sp_str, ch_info->ch_str, siglevel, Mbps,
				stats.n_dropped, stats.n_scrambled,
				(double)total / 1024 / 1024 / 1024);
			SetConsoleTitle(title);
#endif
			
			lasttime = nowtime;
			subtotal = 0;

			/* 番組情報のチェック */
			for (i = 0; i < n_tos; i++) {
				if (tos[i].th[1].bytes > 0) { /* 前のintervalで何も受信できてない時は番組情報のチェックをパスする */
					ts_check_pi(&tos[i], nowtime, ch_info);
				}
			}

			//tc_start("printbuf");
			print_stat(tos, n_tos, title);
			//tc_end();

			/* 溢れたバイト数を表示 */
			for (i = 0; i < n_tos; i++) {
				if (tos[i].dropped_bytes > 0) {
					if (n_tos > 1) {
						output_message(MSG_ERROR, TSD_TEXT("バッファフルのためデータが溢れました(サービス%d, %dバイト)"), tos[i].proginfo->service_id, tos[i].dropped_bytes);
					} else {
						output_message(MSG_ERROR, TSD_TEXT("バッファフルのためデータが溢れました(%dバイト)"), tos[i].dropped_bytes);
					}
					tos[i].dropped_bytes = 0;
				}
			}
		}

		subtotal += n_dec;
		total += n_dec;
	}

	int err;
	/* 終了処理 */
	output_message(MSG_NOTIFY, TSD_TEXT("まだ書き出していないバッファを書き出ています"));
	for (i = 0; i < n_tos; i++) {
		/* まだ書き出していないバッファを書き出し */
		err = ts_wait_pgoutput(&tos[i]);
		while (tos[i].pos_filled - tos[i].pos_write > 0 && !err) {
			ts_output(&tos[i], gettime(), 1);
			err = ts_wait_pgoutput(&tos[i]);
			print_stat(&tos[i], n_tos-i, TSD_TEXT(""));
		}
		close_tos(&tos[i]);
	}
	free(tos);

	do_close_stream();

	//tc_report_id(123);
}

void load_ini()
{
#ifdef TSD_PLATFORM_MSVC
	int bufsize = GetPrivateProfileInt(L"TSDUMP", L"BUFSIZE", BUFSIZE_DEFAULT, L".\\tsdump.ini");
	int overlap_sec = GetPrivateProfileInt(L"TSDUMP", L"OVERLAP_SEC", OVERLAP_SEC_DEFAULT, L".\\tsdump.ini");
	int check_interval = GetPrivateProfileInt(L"TSDUMP", L"CHECK_INTERVAL", CHECK_INTERVAL_DEFAULT, L".\\tsdump.ini");
	int max_pgoverlap = GetPrivateProfileInt(L"TSDUMP", L"MAX_PGOVERLAP", MAX_PGOVERLAP_DEFAULT, L".\\tsdump.ini");

	BUFSIZE = bufsize * 1024 * 1024;
	OVERLAP_SEC = overlap_sec;
	CHECK_INTERVAL = check_interval;
	MAX_PGOVERLAP = max_pgoverlap;
#endif

	output_message(MSG_NONE, TSD_TEXT("BUFSIZE: %dMiB\nOVERLAP_SEC: %ds\nCHECK_INTERVAL: %dms\nMAX_PGOVERLAP: %d\n"),
		BUFSIZE/1024/1024, OVERLAP_SEC, CHECK_INTERVAL, MAX_PGOVERLAP);
}

#ifdef TSD_PLATFORM_MSVC
int wmain(int argc, const WCHAR* argv[])
#else
int main(int argc, const char* argv[])
#endif
{
	int ret=0;
	ch_info_t ch_info;
	void *generator_stat = NULL;
	void *decoder_stat = NULL;
	int encrypted;

#ifdef TSD_PLATFORM_MSVC
	_wsetlocale(LC_ALL, L"Japanese_Japan.932");
#endif

	output_message(MSG_NONE, TSD_TEXT("tsdump ver%s (%s)\n"), VERSION_STR, DATE_STR);

	/* iniファイルをロード */
	load_ini();

	/* モジュールをロード */
	if (load_modules() < 0) {
		output_message(MSG_ERROR, TSD_TEXT("モジュールのロード時にエラーが発生しました!"));
		ret = 1;
		goto END;
	}

	/* モジュールを初期化 */
	if ( !init_modules(argc, argv) ) {
		output_message(MSG_ERROR, TSD_TEXT("モジュールの初期化時にエラーが発生しました!"));
		print_cmd_usage();
		ret = 1;
		goto END;
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	if ( ! do_stream_generator_open(&generator_stat, &ch_info) ) {
		output_message(MSG_ERROR, TSD_TEXT("ストリームジェネレータを開けませんでした"));
		ret = 1;
		goto END;
	}

	ch_info.mode_all_services = param_all_services;
	ch_info.n_services = param_n_services;
	ch_info.services = param_services;

	if ( ! do_stream_decoder_open(&decoder_stat, &encrypted) ) {
		output_message(MSG_ERROR, TSD_TEXT("ストリームデコーダを開けませんでした"));
		ret = 1;
		goto END1;
	}

	/* 処理の本体 */
	main_loop(generator_stat, decoder_stat, encrypted, &ch_info);

	//printf("正常終了\n");
	output_message(MSG_NONE, TSD_TEXT("正常終了"));

	do_stream_decoder_close(decoder_stat);

END1:
	do_stream_generator_close(generator_stat);
	//fclose(fp);

END:
	do_close_module();

	free_modules();

	if( ret ) {
		output_message(MSG_NOTIFY, TSD_TEXT("\n何かキーを押すと終了します"));
		getchar();
	}
	return ret;
}

static const TSDCHAR *set_nowait(const TSDCHAR *param)
{
	UNREF_ARG(param);
	param_nowait = 1;
	return NULL;
}

static const TSDCHAR* set_sv(const TSDCHAR *param)
{
	int sv;
	if (param_all_services) {
		return NULL;
	}
	if ( tsd_strcmp(param, TSD_TEXT("all")) == 0 ) {
		param_all_services = 1;
	} else {
		if (param_n_services < MAX_SERVICES) {
			sv = tsd_atoi(param);
			if (sv <= 0 || sv > 65535) {
				return TSD_TEXT("サービス番号が不正です");
			}
			param_services[param_n_services] = sv;
			param_n_services++;
		} else {
			return TSD_TEXT("指定するサービスの数が多すぎます\n");
		}
	}
	return NULL;
}

void ghook_message(const TSDCHAR *modname, message_type_t msgtype, tsd_syserr_t *err, const TSDCHAR *msg)
{
	const TSDCHAR *msgtype_str = TSD_TEXT("");
	FILE *fp = stdout;
	TSDCHAR msgbuf[256];
	int errtype = 0;

#ifdef TSD_PLATFORM_MSVC
	clear_line();
#endif

	if ( msgtype == MSG_WARNING || msgtype == MSG_PACKETERROR ) {
		msgtype_str = TSD_TEXT("[WARNING] ");
		fp = stderr;
		errtype = 1;
	} else if ( msgtype == MSG_ERROR || msgtype == MSG_SYSERROR || msgtype == MSG_WINSOCKERROR ) {
		msgtype_str = TSD_TEXT("[ERROR] ");
		fp = stderr;
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
			tsd_fprintf(fp, TSD_TEXT("%s(%s): %s <0x%x:%s>\n"), msgtype_str, modname, msg, *err, msgbuf);
		} else {
			tsd_fprintf(fp, TSD_TEXT("%s%s <0x%x:%s>\n"), msgtype_str, msg, *err, msgbuf);
		}
	} else {
		if (modname && errtype) {
			tsd_fprintf(fp, TSD_TEXT("%s(%s): %s\n"), msgtype_str, modname, msg);
		} else {
			tsd_fprintf(fp, TSD_TEXT("%s%s\n"), msgtype_str, msg);
		}
	}
}

static void register_hooks()
{
	//register_hook_message(hook_message);
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--sv"), TSD_TEXT("サービス番号(複数指定可能)"), 1, set_sv },
	{ TSD_TEXT("--nowait"), TSD_TEXT("バッファフル時にあふれたデータは捨てる"), 0, set_nowait },
	NULL,
};

MODULE_DEF module_def_t mod_core = {
	TSDUMP_MODULE_V4,
	TSD_TEXT("mod_core"),
	register_hooks,
	cmds
};
