#include <stdio.h>
#include <windows.h>
#include <tchar.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <inttypes.h>

#include "ts_parser.h"
#include "modules_def.h"
#include "tsdump.h"
#include "ts_output.h"
#include "load_modules.h"
#include "strfuncs.h"

//#include "timecalc.h"

static inline int pi_endtime_unknown(const proginfo_t *pi)
{
	if(pi->status & PGINFO_UNKNOWN_STARTTIME || pi->status & PGINFO_UNKNOWN_DURATION) {
		return 1;
	}
	return 0;
}

void printpi(const proginfo_t *pi)
{
	int64_t endtime;
	int endhour, endmin, endsec;
	WCHAR et[4096];

	get_extended_text(et, sizeof(et) / sizeof(WCHAR), pi);

	if ( pi_endtime_unknown(pi) ) {
		output_message(MSG_DISP, L"<<< ------------ 番組情報 ------------\n"
			L"[%d/%02d/%02d %02d:%02d:%02d～ +未定]\n%s:%s\n「%s」\n"
			L"---------------------------------- >>>",
			pi->start_year, pi->start_month, pi->start_day,
			pi->start_hour, pi->start_min, pi->start_sec,
			pi->service_name.str,
			pi->event_name.str,
			et
		);
	} else {
		endtime = timenum_end14(pi);
		endhour = (endtime/10000) % 100;
		endmin = (endtime/100) % 100;
		endsec = endtime % 100;
		output_message(MSG_DISP, L"<<< ------------ 番組情報 ------------\n"
			L"[%d/%02d/%02d %02d:%02d:%02d～%02d:%02d:%02d]\n%s:%s\n「%s」\n"
			L"---------------------------------- >>>",
			pi->start_year, pi->start_month, pi->start_day,
			pi->start_hour, pi->start_min, pi->start_sec,
			endhour, endmin, endsec,
			pi->service_name.str,
			pi->event_name.str,
			et
		);
	}
}

int create_tos_per_service(ts_output_stat_t **ptos, ts_parse_stat_t *tps, ch_info_t *ch_info)
{
	int i, j, k;
	int n_tos;
	ts_output_stat_t *tos;

	if (ch_info->mode_all_services) {
		n_tos = tps->n_programs;
		tos = (ts_output_stat_t*)malloc(n_tos*sizeof(ts_output_stat_t));
		for (i = 0; i < n_tos; i++) {
			init_tos(&tos[i]);
			tos[i].tps_index = i;
			tos[i].service_id = tps->programs[i].service_id;
		}
	} else {
		n_tos = 0;
		for (i = 0; i < ch_info->n_services; i++) {
			for (j = 0; j < tps->n_programs; j++) {
				if (ch_info->services[i] == tps->programs[j].service_id) {
					n_tos++;
					break;
				}
			}
		}
		tos = (ts_output_stat_t*)malloc(n_tos*sizeof(ts_output_stat_t));
		k = 0;
		for (i = 0; i < ch_info->n_services; i++) {
			for (j = 0; j < tps->n_programs; j++) {
				if (ch_info->services[i] == tps->programs[j].service_id) {
					init_tos(&tos[k]);
					tos[k].tps_index = j;
					tos[k].service_id = tps->programs[j].service_id;
					k++;
				}
			}
		}
	}

	ch_info->services = (int*)malloc(n_tos*sizeof(int));
	for (i = 0; i < n_tos; i++) {
		ch_info->services[i] = tos[i].service_id;
	}
	ch_info->n_services = n_tos;

	*ptos = tos;
	return n_tos;
}

void init_tos(ts_output_stat_t *tos)
{
	int i;

	tos->n_pgos = 0;
	tos->pgos = (pgoutput_stat_t*)malloc(MAX_PGOVERLAP * sizeof(pgoutput_stat_t));
	for (i = 0; i < MAX_PGOVERLAP; i++) {
		tos->pgos[i].fn = (WCHAR*)malloc(MAX_PATH_LEN*sizeof(WCHAR));
	}

	tos->retry_count = 0;

	//tos->pi_last.recyear = tos->pi.recyear = 9999;
	//tos->last_pitime = gettime();

	tos->buf = (BYTE*)malloc(BUFSIZE);
	tos->pos_filled = 0;
	tos->pos_filled_old = 0;
	tos->pos_pi = 0;
	tos->pos_write = 0;
	tos->write_busy = 0;
	tos->service_id = -1;
	tos->dropped_bytes = 0;

	tos->last_proginfo.status = 0;

	tos->n_th = OVERLAP_SEC * 1000 / CHECK_INTERVAL + 1;
	if (tos->n_th < 2) {
		tos->n_th = 2; /* tos->th[1]へのアクセスあるので最低でも2 */
	}
	tos->th = (transfer_history_t*)malloc(tos->n_th * sizeof(transfer_history_t));
	memset(tos->th, 0, tos->n_th * sizeof(transfer_history_t));

	return;
}

void close_tos(ts_output_stat_t *tos)
{
	int i;

	/* close all output */
	for (i = 0; i < tos->n_pgos; i++) {
		do_pgoutput_close(tos->pgos[i].modulestats, &tos->pgos[i].final_pi);
	}

	for (i = 0; i < MAX_PGOVERLAP; i++) {
		free((WCHAR*)tos->pgos[i].fn);
	}
	free(tos->pgos);

	free(tos->th);
	free(tos->buf);
	//free(tos);
}

void ts_copy_backward(ts_output_stat_t *tos, int64_t nowtime)
{
	int backward_size, start_pos;
	int i;

	backward_size = 0;
	for (i = 0; i < tos->n_th; i++) {
		if (tos->th[i].time < nowtime - OVERLAP_SEC * 1000) {
			break;
		}
		backward_size += tos->th[i].bytes;
	}
	//backward_size = ((backward_size - 1) / 188 + 1) * 188; /* 188byte units */

	start_pos = tos->pos_filled - backward_size;
	if (start_pos < 0) {
		start_pos = 0;
	}
	if ( tos->n_pgos > 0 && start_pos > tos->pos_write ) {
		tos->pgos[tos->n_pgos].delay_remain = start_pos - tos->pos_write;
		//printf("[INFOOOOOOOOOOOOO] write start delay: %d bytes\n", tos->pgos[tos->n_pgos].delay_remain);
	} else if ( tos->pos_write - start_pos ) {
		do_pgoutput(tos->pgos[tos->n_pgos].modulestats, &(tos->buf[start_pos]), tos->pos_write - start_pos);
		tos->write_busy = 1;
	}
	//printf("\n[INFOOOOOOOOOOOOO] backward_size = %d bytes\n", backward_size);
}

int ts_wait_pgoutput(ts_output_stat_t *tos)
{
	int i;
	int err = 0;
	if (tos->write_busy) {
		for (i = 0; i < tos->n_pgos; i++) {
			err |= do_pgoutput_wait(tos->pgos[i].modulestats);
		}
		tos->write_busy = 0;
	}
	return err;
}

void ts_check_pgoutput(ts_output_stat_t *tos)
{
	int i;
	if (tos->write_busy) {
		tos->write_busy = 0;
		for (i = 0; i < tos->n_pgos; i++) {
			tos->write_busy |= do_pgoutput_check(tos->pgos[i].modulestats);
		}
	}
}

void ts_close_oldest_pg(ts_output_stat_t *tos)
{
	int i;
	pgoutput_stat_t pgos;

	/* 一番古いpgosを閉じる */
	do_pgoutput_close(tos->pgos[0].modulestats, &tos->pgos[0].final_pi);

	/* pgos[0]の中身を残しておかないといけないのは、pgos[0].fn_baseのポインタを保存するため */
	pgos = tos->pgos[0];
	for (i = 0; i < tos->n_pgos - 1; i++) {
		tos->pgos[i] = tos->pgos[i + 1];
	}
	tos->pgos[tos->n_pgos - 1] = pgos;
	tos->n_pgos--;
}

void ts_output(ts_output_stat_t *tos, int64_t nowtime, int force_write)
{
	int i, write_size, diff;

	if (tos->write_busy) {
		/* 最新の書き込み完了状態をチェック */
		//tc_start("check");
		ts_check_pgoutput(tos);
		//tc_end();
	} else { /* 最新の状態が書き込み完了でも即座に完了処理を行わない(この後main_loop一周分の処理を終えてから) */
		/* ファイル分割 */
		if (tos->n_pgos >= 1 && 0 < tos->pgos[0].closetime && tos->pgos[0].closetime < nowtime) {
			if ( ! tos->pgos[0].close_flag ) {
				tos->pgos[0].close_flag = 1;
				tos->pgos[0].close_remain = tos->pos_filled - tos->pos_write;
				tos->pgos[0].delay_remain = 0;
			} else if ( tos->pgos[0].close_remain <= 0 ) {
				ts_close_oldest_pg(tos);
			}
		}

		/* ファイル書き出し */
		write_size = tos->pos_filled - tos->pos_write;
		if ( write_size < 1024*1024 && !force_write ) { /* 書き出しが一定程度溜まっていない場合はパス */
			//ts_update_transfer_history(tos, nowtime, 0);
			return;
		} else if (write_size > MAX_WRITE_SIZE) {
			write_size = MAX_WRITE_SIZE;
		}

		/* 新たな書き込みを開始 */
		//tc_start("write");
		for (i = 0; i < tos->n_pgos; i++) {
			/* 書き出しの開始が遅延させられている場合 */
			if (tos->pgos[i].delay_remain > 0) {
				if (tos->pgos[i].delay_remain < write_size) {
					diff = write_size - tos->pgos[i].delay_remain;
					do_pgoutput(tos->pgos[i].modulestats, &(tos->buf[tos->pos_write+write_size-diff]), diff);
					tos->pgos[i].delay_remain = 0; /* ここで0にしないのはバグだった？ */
				} else {
					tos->pgos[i].delay_remain -= write_size;
				}
			/* 端数の書き出しが残っている場合 */
			} else if ( tos->pgos[i].close_flag && tos->pgos[i].close_remain > 0 ) {
				if (tos->pgos[i].close_remain > write_size) {
					do_pgoutput(tos->pgos[i].modulestats, &(tos->buf[tos->pos_write]), write_size);
					tos->pgos[i].close_remain -= write_size;
				} else {
					do_pgoutput(tos->pgos[i].modulestats, &(tos->buf[tos->pos_write]), tos->pgos[i].close_remain);
					tos->pgos[i].close_remain = 0;
				}
			/* 通常の書き出し */
			} else {
				do_pgoutput(tos->pgos[i].modulestats, &(tos->buf[tos->pos_write]), write_size);
			}
		}
		//tc_end();
		tos->write_busy = 1;
		tos->pos_write += write_size;
	}
}

void ts_minimize_buf(ts_output_stat_t *tos)
{
	int i;
	int backward_size, clear_size, move_size;

	if (tos->write_busy) {
		//printf("[DEBUG] 書き込みが完了していないのでts_minimize_buf()をパス\n");
		return;
	}

	/* バッファが溜まってる場合番組情報ポインタを進める */
	if ( tos->pos_filled >= (int)((double)0.95 * BUFSIZE) ) {
		//printf("[DEBUG] バッファが溜まっているのでpi用バッファをクリア\n");
		ts_giveup_pibuf(tos);
	}

	for (backward_size = i = 0; i < tos->n_th; i++) {
		backward_size += tos->th[i].bytes;
	}
	clear_size = tos->pos_filled - backward_size;
	if (clear_size > tos->pos_pi) {
		clear_size = tos->pos_pi;
	}
	if (clear_size > tos->pos_write) {
		clear_size = tos->pos_write;
	}

	int min_clear_size = (int)((double)MIN_CLEAR_RATIO * BUFSIZE);
	if ( clear_size < min_clear_size ) {
		return;
	}

	move_size = tos->pos_filled - clear_size;
	memmove(tos->buf, &(tos->buf[clear_size]), move_size);
	tos->pos_filled -= clear_size;
	tos->pos_pi -= clear_size;
	tos->pos_write -= clear_size;
	//printf("[DEBUG] ts_minimize_buf()を実行: clear_size=%d\n", clear_size);
}

void ts_require_buf(ts_output_stat_t *tos, int require_size)
{
	output_message(MSG_WARNING, L"バッファが足りません。バッファの空き容量が増えるのを待機します。");
	ts_wait_pgoutput(tos);
	while (BUFSIZE - tos->pos_filled + tos->pos_write < require_size) {
		ts_output(tos, gettime(), 1);
		ts_wait_pgoutput(tos);
	}
	//printf("完了\n");
	output_message(MSG_WARNING, L"待機完了");

	int move_size = tos->pos_write;

	memmove(tos->buf, &(tos->buf[tos->pos_filled - move_size]), move_size);
	tos->pos_filled -= move_size;
	tos->pos_pi -= move_size;
	if (tos->pos_pi < 0) {
		tos->pos_pi = 0;
	}
	tos->pos_write = 0;
}

void ts_copybuf(ts_output_stat_t *tos, BYTE *buf, int n_buf)
{
	//fprintf(logfp, " filled=%d n_buf=%d\n", tos->filled, n_buf);

	if (tos->pos_filled + n_buf > BUFSIZE) {
		if ( ! param_nowait ) {
			ts_wait_pgoutput(tos);
		}
		ts_minimize_buf(tos);
	}
	if (tos->pos_filled + n_buf > BUFSIZE) {
		if (param_nowait) {
			tos->dropped_bytes += n_buf;
			return; /* データを捨てる */
		} else {
			ts_require_buf(tos, n_buf);
		}
	}
	memcpy(&(tos->buf[tos->pos_filled]), buf, n_buf);
	tos->pos_filled += n_buf;
}

void ts_check_extended_text(ts_output_stat_t *tos)
{
	pgoutput_stat_t *current_pgos;
	WCHAR et[4096];

	if (tos->n_pgos <= 0) {
		return;
	}

	current_pgos = &tos->pgos[tos->n_pgos - 1];
	if (!(current_pgos->initial_pi_status & PGINFO_GET_EXTEND_TEXT) &&
			(tos->proginfo->status & PGINFO_GET_EXTEND_TEXT)) {
		/* 拡張形式イベント情報が途中から来た場合 */
		get_extended_text(et, sizeof(et) / sizeof(WCHAR), tos->proginfo);
		output_message(MSG_DISP, L"<<< ------------ 追加番組情報 ------------\n"
			L"「%s」\n"
			L"---------------------------------- >>>",
			et
		);
	}
}

void ts_check_pi(ts_output_stat_t *tos, int64_t nowtime, ch_info_t *ch_info)
{
	int changed = 0;
	pgoutput_stat_t *pgos;
	int64_t starttime, endtime, last_starttime, last_endtime;
	WCHAR msg1[64], msg2[64];

	if ( PGINFO_READY(tos->proginfo->status) ) {
		printf("A!");
		if ( PGINFO_READY(tos->last_proginfo.status) ) {
			if (tos->last_proginfo.event_id != (int)tos->proginfo->event_id) {
				changed = 1;
			}
		} else {
			changed = 1;
		}
	} else if (nowtime - tos->proginfo->last_ready_time > 15 * 1000) {
		printf("B!");
		if (PGINFO_READY(tos->last_proginfo.status)) {
			tos->last_proginfo = *tos->proginfo;
			changed = 1;
		} else if( timenum64(nowtime) / 100 % 100 != timenum64(tos->last_nopi_time) / 100 % 100) {
			changed = 1;
		}
		tos->last_nopi_time = nowtime;
	}else {
		return;
	}

	if (changed) {
		/* print */
		printpi(tos->proginfo);

		/* split! */
		if (tos->n_pgos >= MAX_PGOVERLAP) {
			output_message(MSG_WARNING, L"番組の切り替わりを短時間に連続して検出したためスキップします");
		} else {
			pgos = &(tos->pgos[tos->n_pgos]);

			pgos->initial_pi_status = tos->proginfo->status;

			ch_info->service_id = tos->service_id; /* ここでch_infoの中身を書き換えている */

			pgos->fn = do_path_resolver(tos->proginfo, ch_info); /* ここでch_infoにアクセス */
			pgos->modulestats = do_pgoutput_create(pgos->fn, tos->proginfo, ch_info); /* ここでch_infoにアクセス */
			pgos->closetime = -1;
			pgos->close_flag = 0;
			pgos->close_remain = 0;
			pgos->delay_remain = 0;

			ts_copy_backward(tos, nowtime);
			if (tos->n_pgos >= 1) {
				pgos[-1].closetime = nowtime + OVERLAP_SEC * 1000;
				pgos[-1].final_pi = tos->last_proginfo;
			}
			tos->n_pgos++;
			//tos->pi_last = tos->pi;
		}
	} else if( PGINFO_READY(tos->proginfo->status) ) {
		if ( PGINFO_READY(tos->last_proginfo.status) ) {
			/* 番組の時間が途中で変更された場合 */

			starttime = timenum_start(tos->proginfo);
			endtime = timenum_end(tos->proginfo);
			last_starttime = timenum_start(&tos->last_proginfo);
			last_endtime = timenum_end(&tos->last_proginfo);

			if ( starttime != last_starttime ) {
				if (tos->service_id == -1) {
					tsd_strcpy(msg1, TSD_TEXT("番組開始時間の変更"));
				} else {
					swprintf(msg1, 128, L"番組開始時間の変更(サービス%d)", tos->service_id);
				}

				output_message( MSG_NOTIFY, L"%s: %02d:%02d → %02d:%02d", msg1,
					(int)(last_starttime/100%100), (int)(last_starttime%100),
					(int)(starttime/100%100), (int)(starttime%100) );
			} else if ( endtime != last_endtime ) {
				if (tos->service_id == -1) {
					tsd_strcpy(msg1, TSD_TEXT("番組終了時間の変更"));
				} else {
					swprintf(msg1, 128, L"番組終了時間の変更(サービス%d)", tos->service_id);
				}

				if (last_endtime == 0 ) {
					tsd_strcpy(msg2, TSD_TEXT("未定 → "));
				} else {
					wsprintf( msg2, L"%02d:%02d → ", (int)(last_endtime / 100 % 100), (int)(last_endtime % 100) );
				}

				if ( endtime == 0 ) {
					output_message(MSG_NOTIFY, L"%s: %s未定", msg1, msg2);
				} else {
					output_message( MSG_NOTIFY, L"%s: %s%02d:%02d",
						msg1, msg2, (int)(endtime / 100 % 100), (int)(endtime % 100) );
				}
			}
		}
	}

	tos->last_proginfo = *tos->proginfo;
}
