#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
	#pragma comment(lib, "Ws2_32.lib")
	#include <windows.h>
#else
	#include <errno.h>
	#include <unistd.h>
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
#include "utils/advanced_buffer.h"
#include "core/load_modules.h"
#include "core/ts_output.h"
#include "utils/tsdstr.h"
#include "utils/aribstr.h"
#include "utils/path.h"
#include "core/default_decoder.h"

//#define HAVE_TIMECALC_DECLARATION
//#include "timecalc.h"

int BUFSIZE = BUFSIZE_DEFAULT * 1024 * 1024;
int OVERLAP_SEC = OVERLAP_SEC_DEFAULT;
int CHECK_INTERVAL = CHECK_INTERVAL_DEFAULT;
int MAX_PGOVERLAP = MAX_PGOVERLAP_DEFAULT;

static volatile int termflag = 0;
static volatile int termedflag = 0;

int param_sp_num = -1;
int param_ch_num = -1;
int param_all_services;
unsigned int param_services[MAX_SERVICES];
int param_n_services = 0;
int param_nowait = 0;

int need_clear_line = 0;

#ifdef TSD_PLATFORM_MSVC

BOOL WINAPI console_ctrl_handler(DWORD ctrl)
{
	int64_t t = gettime();
	UNREF_ARG(ctrl);
	termflag = 1;
	output_message(MSG_NOTIFY, TSD_TEXT("\n終了シグナルをキャッチ"));
	while (!termedflag) {
		if (gettime() - t > 1000*15) {
			/* 15秒以上経過したら諦めて出る */
			break;
		}
		Sleep(10);
	}
	return FALSE;
}

#else

void signal_handler(int sig)
{
	UNREF_ARG(sig);
	termflag = 1;
	output_message(MSG_NOTIFY, TSD_TEXT("\n終了シグナルをキャッチ"));
}

#endif

void _output_message(const char *fname, message_type_t msgtype, const TSDCHAR *fmt, ...)
{
	va_list list;
	va_start(list, fmt);
	tsd_syserr_t lasterr, *plasterr = NULL;
	TSDCHAR msg[2048];
	const TSDCHAR *modname;

	/* 拡張子を除いたファイル名=モジュール名をコピー */
#ifdef TSD_PLATFORM_MSVC
	const char *cp;
	int len;

	if (msgtype == MSG_SYSERROR) {
		lasterr = GetLastError();
		plasterr = &lasterr;
	} else if (msgtype == MSG_WINSOCKERROR) {
		lasterr = WSAGetLastError();
		plasterr = &lasterr;
	}

	WCHAR modpath[MAX_PATH_LEN], *wcp;
		for (wcp = modpath, cp = fname;
			*cp != '\0' && *cp != '.' && wcp < &modpath[MAX_PATH_LEN];
			cp += len, wcp++) {
		len = mbtowc(wcp, cp, MB_CUR_MAX);
	}
	*wcp = L'\0';

	/* __FILE__にフルパスが入っている場合があるのでファイル名のみ取り出す */
	modname = path_getfile(modpath);
#else
	if (msgtype == MSG_SYSERROR) {
		lasterr = errno;
		plasterr = &lasterr;
	}

	/* __FILE__にフルパスが入っている場合があるのでファイル名のみ取り出す */
	modname = path_getfile(fname);
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

void print_buf(ts_output_stat_t *tos, int n_tos, const TSDCHAR *stat)
{
#ifdef TSD_PLATFORM_MSVC
	int n, i, backward_size, console_width, width, pos_write, pos;
	char line[256], hor[256];
	char *p = line;
	static int cnt = 0;
	COORD new_pos;
	time_mjd_t time_jst;
#endif
	int buf_used;
	double rate;

	if(!tos) {
		return;
	}

	ab_get_status(tos->ab, &buf_used);
#ifdef TSD_PLATFORM_MSVC
	console_width = save_line(&new_pos);
	if (console_width < 0) {
		fprintf(stderr, "console error\r");
	} else if (console_width == 0) {
#endif
		rate = 100.0 * buf_used / BUFSIZE;
		tsd_printf(TSD_TEXT("%s buf:%.1f%% \r"), stat, rate);
		fflush(stdout);
#ifdef TSD_PLATFORM_MSVC
	} else {
		width = ( console_width - 6 - (n_tos-1) ) / n_tos;

		for (i = 0; i < n_tos; i++) {
			backward_size = ab_get_history_backward_bytes(tos->ab_history);

			pos_write = buf_used;
			for (n = ab_first_downstream(tos->ab); n >= 0; n = ab_next_downstream(tos->ab, n)) {
				ab_get_downstream_status(tos->ab, n, &pos, NULL);
				if (pos_write < pos) {
					pos_write = pos;
				}
			}

			for (n = 0; n < width; n++) {
				pos = (int)( (double)BUFSIZE / width * (n+0.5) );
				if (pos < pos_write) {
					*p = '-';
				} else if (pos < buf_used) {
					*p = '!';
				} else {
					*p = '_';
				}
				if (pos > buf_used - backward_size) {
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

static void print_stat(ts_output_stat_t *tos, int n_tos, const ch_info_t *ch_info)
{
	const stream_stats_t *stats;
	TSDCHAR title[256];
	TSDCHAR siglevel_str[16], cnr_str[16];
	TSDCHAR sig_separator[2] = { TSD_NULLCHAR };

	get_stream_stats(&stats);

	if (stats->s_signal.level_scale == TSDUMP_SCALE_DECIBEL) {
		tsd_snprintf(siglevel_str, sizeof(siglevel_str) - 1, TSD_TEXT("%.1fdBm"), stats->s_signal.level);
	} else if (stats->s_signal.level_scale == TSDUMP_SCALE_RELATIVE) {
		tsd_snprintf(siglevel_str, sizeof(siglevel_str) - 1, TSD_TEXT("%.1f%%"), stats->s_signal.level*100);
	} else if (stats->s_signal.level_scale == TSDUMP_SCALE_COUNTER) {
		tsd_snprintf(siglevel_str, sizeof(siglevel_str) - 1, TSD_TEXT("%d"), (int)stats->s_signal.level);
	} else {
		siglevel_str[0] = TSD_NULLCHAR;
	}

	if (stats->s_signal.cnr_scale == TSDUMP_SCALE_DECIBEL) {
		tsd_snprintf(cnr_str, sizeof(cnr_str) - 1, TSD_TEXT("%.1fdB"), stats->s_signal.cnr);
	} else if (stats->s_signal.cnr_scale == TSDUMP_SCALE_RELATIVE) {
		tsd_snprintf(cnr_str, sizeof(cnr_str) - 1, TSD_TEXT("%.1f%%"), stats->s_signal.cnr*100);
	} else if (stats->s_signal.cnr_scale == TSDUMP_SCALE_COUNTER) {
		tsd_snprintf(cnr_str, sizeof(cnr_str) - 1, TSD_TEXT("%d"), (int)stats->s_signal.cnr);
	} else {
		cnr_str[0] = TSD_NULLCHAR;
	}

	if (siglevel_str[0] != TSD_NULLCHAR && cnr_str[0] != TSD_NULLCHAR) {
		tsd_strcpy(sig_separator, TSD_TEXT(","));
	}

	tsd_snprintf(title, 256, TSD_TEXT("%s:%s:%s|%s%s%s %.1fMbps D:%"PRId64" S:%"PRId64" %.1fGB"),
		ch_info->tuner_name, ch_info->sp_str, ch_info->ch_str, siglevel_str, sig_separator, cnr_str, stats->mbps,
		stats->s_decoder.n_dropped, stats->s_decoder.n_scrambled,
		(double)stats->total_bytes / 1024 / 1024 / 1024);
#ifdef TSD_PLATFORM_MSVC
	SetConsoleTitle(title);
#endif
	print_buf(tos, n_tos, title);
}

void init_service_list(ts_service_list_t *service_list)
{
	int i;

	service_list->n_services = 0;
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

static proginfo_t *find_curr_service(void *param, const unsigned int service_id)
{
	int i;
	ts_service_list_t *sl = (ts_service_list_t*)param;
	for (i = 0; i < sl->n_services; i++) {
		if (service_id == sl->proginfos[i].service_id) {
			return &sl->proginfos[i];
		}
	}
	return NULL;
}

static proginfo_t *find_curr_service_pcr_pid(void *param, const unsigned int pcr_pid)
{
	int i;
	ts_service_list_t *sl = (ts_service_list_t*)param;
	for (i = 0; i < sl->n_services; i++) {
		if (pcr_pid == sl->proginfos[i].PCR_pid) {
			return &sl->proginfos[i];
		}
	}
	return NULL;
}

static proginfo_t *find_curr_service_eit(void *param, const EIT_header_t *eit_h)
{
	if (eit_h->section_number != 0) {
		/* 現在進行中の番組ではない */
		return NULL;
	}
	return find_curr_service(param, eit_h->service_id);
}

static void pat_handler(void *param, const int n, const int i, const PAT_item_t *PAT_item)
{
	ts_service_list_t *sl = (ts_service_list_t*)param;
	UNREF_ARG(n);
	UNREF_ARG(i);

	if (sl->n_services >= MAX_SERVICES_PER_CH) {
		return;
	}

	if (PAT_item->program_number != 0) {
		sl->PMT_payloads[sl->n_services].stat = PAYLOAD_STAT_INIT;
		sl->PMT_payloads[sl->n_services].pid = PAT_item->pid;
		store_PAT(&sl->proginfos[sl->n_services], PAT_item);
		(sl->n_services)++;
	}
}

static void tot_handler(void *param, const time_mjd_t *TOT_time)
{
	int i;
	ts_service_list_t *sl = (ts_service_list_t*)param;

	for (i = 0; i < sl->n_services; i++) {
		store_TOT(&sl->proginfos[i], TOT_time);
	}
}

void request_shutdown(int mode)
{
	UNREF_ARG(mode);
	output_message(MSG_NOTIFY, TSD_TEXT("\nモジュールからの終了リクエスト"));
	termflag = 1;
}

void main_loop(void *generator_stat, void *decoder_stat, int encrypted, ch_info_t *ch_info)
{
	uint8_t *recvbuf, *decbuf;

	int n_recv=0, n_dec;

	int64_t total = 0;
	int64_t subtotal = 0;

	int64_t nowtime, lasttime;

	ts_output_stat_t *tos = NULL;
	int n_tos = 0;

	lasttime = nowtime = gettime();

	double tdiff, Mbps=0.0;

	int i, j;
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

		if (n_recv == 0) {
			/* 前回の取得サイズが0だった場合はストリームの到着を待つ */
			if (do_stream_generator_wait(generator_stat, 100) < 0) {
				/* 待ち機能に非対応なストリームジェネレータの場合10ms待つ */
#ifdef TSD_PLATFORM_MSVC
				Sleep(10);
#else
				usleep(10*1000);
#endif
			}
		}
		do_stream_generator(generator_stat, &recvbuf, &n_recv);
		do_encrypted_stream(recvbuf, n_recv);

		do_stream_decoder(decoder_stat, &decbuf, &n_dec, recvbuf, n_recv);
		do_stream(decbuf, n_dec, encrypted);

		add_stream_stats_total_bytes(n_recv);

		do_tick(nowtime);

		int valid_ts_header;
		int need_default_counter = !(is_implemented_stream_decoder_stats());

		for (i = 0; i < n_dec; i+=188) {
			packet = &decbuf[i];
			valid_ts_header = parse_ts_header(packet, &tsh);

			if (need_default_counter) {
				if (!valid_ts_header) {
					ts_packet_counter(NULL);
				} else {
					ts_packet_counter(&tsh);
				}
			}

			if (tsh.transport_scrambling_control) {
				continue;
			}

			if (!valid_ts_header) {
				if (tsh.valid_sync_byte) {
					/* PESパケットでここに来る場合があるので警告はひとまずOFF  e.g. NHK BS1
					   PESパケットの規格を要調査 */
					//output_message(MSG_PACKETERROR, L"Invalid ts header! pid=0x%x(%d)", tsh.pid, tsh.pid);
				} else {
					output_message(MSG_PACKETERROR, TSD_TEXT("Invalid ts packet!"));
				}
				continue; /* pass */
			}

			if (service_list.n_services == 0) {
				/* PATの取得は初回のみ */
				parse_PAT(&service_list.pid0x00, packet, &tsh, &service_list, pat_handler);
			} else {
				for (j = 0; j < service_list.n_services; j++) {
					parse_PMT(packet, &tsh, &service_list.PMT_payloads[j], &service_list.proginfos[j]);
				}
				if (!printservice) {
					printservice = print_services(&service_list);
					if(printservice) {
						ts_n_drops = 0;
						ts_n_total = 0;
						ts_n_scrambled = 0;
					}
				}
			}
			parse_PCR(packet, &tsh, &service_list, find_curr_service_pcr_pid);
			parse_TOT_TDT(packet, &tsh, &service_list.pid0x14, &service_list, tot_handler);
			parse_SDT(&service_list.pid0x11, packet, &tsh, &service_list, find_curr_service);
			parse_EIT(&service_list.pid0x12, packet, &tsh, &service_list, find_curr_service_eit);
			parse_EIT(&service_list.pid0x26, packet, &tsh, &service_list, find_curr_service_eit);
			parse_EIT(&service_list.pid0x27, packet, &tsh, &service_list, find_curr_service_eit);
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
			//ts_copybuf(tos, decbuf, n_dec);
			ab_input_buf(tos->ab, decbuf, n_dec);
			//ts_update_transfer_history(tos, nowtime, n_dec);

		} else {  /* サービスごと書き出しモード */
			/* pos_filledをコピー */
			//for (i = 0; i < n_tos; i++) {
			//	tos[i].pos_filled_old = tos[i].pos_filled;
			//}

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
			//for (i = 0; i < n_tos; i++) {
			//	ts_update_transfer_history( &tos[i], nowtime, tos[i].pos_filled - tos[i].pos_filled_old );
			//}
		}
		//tc_end();

		//tc_start("output");
		for (i = 0; i < n_tos; i++) {
			ts_output(&tos[i], nowtime);
		}
		//tc_end();

		//tc_start("proginfo");
		/* 定期的に番組情報をチェック */
		if ( nowtime / CHECK_INTERVAL != lasttime / CHECK_INTERVAL ) {

			tdiff = (double)(nowtime - lasttime) / 1000;
			Mbps = (double)subtotal * 8 / 1024 / 1024 / tdiff;

			do_stream_decoder_stats(decoder_stat);
			do_stream_generator_siglevel(generator_stat);
			do_stream_generator_cnr(generator_stat);
			set_stream_stats_mbps(Mbps);

			lasttime = nowtime;
			subtotal = 0;

			/* 番組情報のチェック */
			for (i = 0; i < n_tos; i++) {
				if (ab_get_history_bytes(tos[i].ab_history, 0) > 0) { /* 前のintervalで何も受信できてない時は番組情報のチェックをパスする */
					ts_check_pi(&tos[i], nowtime, ch_info);
				}
			}

			//tc_start("printbuf");
			print_stat(tos, n_tos, ch_info);
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

	/* 終了処理 */
	output_message(MSG_NOTIFY, TSD_TEXT("まだ書き出していないバッファを書き出ています"));
	for (i = 0; i < n_tos; i++) {
		/* まだ書き出していないバッファを書き出し */
		close_tos(&tos[i]);
	}
	free(tos);

	do_close_stream();

	//tc_report_id(123);
}

void load_ini()
{
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

	output_message(MSG_NONE, TSD_TEXT("BUFSIZE: %dMiB\nOVERLAP_SEC: %ds\nCHECK_INTERVAL: %dms\nMAX_PGOVERLAP: %d\n"),
		BUFSIZE / 1024 / 1024, OVERLAP_SEC, CHECK_INTERVAL, MAX_PGOVERLAP);

#ifdef TSD_PLATFORM_MSVC
	SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
#endif

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
		output_message(MSG_NOTIFY, TSD_TEXT("\nエンターキーを押すと終了します"));
		getchar();
	}

	termedflag = 1;
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

static const TSDCHAR* set_bufsize(const TSDCHAR *param)
{
	int bs = tsd_atoi(param);
	if (bs <= 0) {
		return TSD_TEXT("不正なバッファサイズが指定されました");
	}
	BUFSIZE = bs * 1024 * 1024;
	return NULL;
}

static const TSDCHAR* set_pginterval(const TSDCHAR *param)
{
	int i = tsd_atoi(param);
	if (i <= 0 || i > 1000) {
		return TSD_TEXT("不正なインターバル時間が指定されました");
	}
	CHECK_INTERVAL = i;
	return NULL;
}

static const TSDCHAR* set_pgmargin(const TSDCHAR *param)
{
	int i = tsd_atoi(param);
	if (i <= 0 || i > 60) {
		return TSD_TEXT("不正なマージン時間が指定されました");
	}
	OVERLAP_SEC = i;
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
	{ TSD_TEXT("--bufsize"), TSD_TEXT("バッファサイズを指定(MiB)"), 1, set_bufsize },
	{ TSD_TEXT("--pginterval"), TSD_TEXT("番組情報チェックのインターバル(ms)"), 1, set_pginterval },
	{ TSD_TEXT("--pgmargin"), TSD_TEXT("番組録画の前後マージン(sec)"), 1, set_pgmargin },
	{ NULL },
};

TSD_MODULE_DEF(
	mod_core,
	register_hooks,
	cmds,
	NULL
);
