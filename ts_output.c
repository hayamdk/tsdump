#include <stdio.h>
#include <windows.h>
#include <tchar.h>
#include <time.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <inttypes.h>

#include "module_def.h"
#include "ts_proginfo.h"
#include "module_hooks.h"
#include "ts_parser.h"
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
	time_mjd_t endtime;
	WCHAR et[4096];

	if (!PGINFO_READY(pi->status)) {
		if (pi->status & PGINFO_GET_SERVICE_INFO) {
			output_message(MSG_DISP, L"<<< ------------ 番組情報 ------------\n"
				L"[番組情報なし]\n%s\n"
				L"---------------------------------- >>>", pi->service_name.str);
			return;
		} else {
			output_message(MSG_DISP, L"<<< ------------ 番組情報 ------------\n"
				L"[番組情報なし]\n"
				L"---------------------------------- >>>");
			return;
		}
	}

	get_extended_text(et, sizeof(et) / sizeof(WCHAR), pi);

	if ( pi_endtime_unknown(pi) ) {
		output_message(MSG_DISP, L"<<< ------------ 番組情報 ------------\n"
			L"[%d/%02d/%02d %02d:%02d:%02d～ +未定]\n%s:%s\n「%s」\n"
			L"---------------------------------- >>>",
			pi->start.year, pi->start.mon, pi->start.day,
			pi->start.hour, pi->start.min, pi->start.sec,
			pi->service_name.str,
			pi->event_name.str,
			et
		);
	} else {
		time_add_offset(&endtime, &pi->start, &pi->dur);
		output_message(MSG_DISP, L"<<< ------------ 番組情報 ------------\n"
			L"[%d/%02d/%02d %02d:%02d:%02d～%02d:%02d:%02d]\n%s:%s\n「%s」\n"
			L"---------------------------------- >>>",
			pi->start.year, pi->start.mon, pi->start.day,
			pi->start.hour, pi->start.min, pi->start.sec,
			endtime.hour, endtime.min, endtime.sec,
			pi->service_name.str,
			pi->event_name.str,
			et
		);
	}
}

int create_tos_per_service(ts_output_stat_t **ptos, ts_service_list_t *service_list, ch_info_t *ch_info)
{
	int i, j, k;
	int n_tos;
	ts_output_stat_t *tos;

	if (ch_info->mode_all_services) {
		n_tos = service_list->n_services;
		tos = (ts_output_stat_t*)malloc(n_tos*sizeof(ts_output_stat_t));
		for (i = 0; i < n_tos; i++) {
			init_tos(&tos[i]);
			tos[i].tps_index = i;
			tos[i].proginfo = &service_list->proginfos[i];
		}
	} else {
		n_tos = 0;
		for (i = 0; i < ch_info->n_services; i++) {
			for (j = 0; j < service_list->n_services; j++) {
				if (ch_info->services[i] == service_list->proginfos[j].service_id) {
					n_tos++;
					break;
				}
			}
		}
		tos = (ts_output_stat_t*)malloc(n_tos*sizeof(ts_output_stat_t));
		k = 0;
		for (i = 0; i < ch_info->n_services; i++) {
			for (j = 0; j < service_list->n_services; j++) {
				if (ch_info->services[i] == service_list->proginfos[j].service_id) {
					init_tos(&tos[k]);
					tos[k].tps_index = j;
					tos[k].proginfo = &service_list->proginfos[j];
					k++;
				}
			}
		}
	}

	ch_info->services = (unsigned int*)malloc(n_tos*sizeof(unsigned int));
	for (i = 0; i < n_tos; i++) {
		ch_info->services[i] = tos[i].proginfo->service_id;
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

	tos->buf = (BYTE*)malloc(BUFSIZE);
	tos->pos_filled = 0;
	tos->pos_filled_old = 0;
	tos->pos_write = 0;
	tos->write_busy = 0;
	tos->dropped_bytes = 0;

	init_proginfo(&tos->last_proginfo);
	tos->last_checkpi_time = gettime();
	tos->proginfo_retry_count = 0;
	tos->pcr_retry_count = 0;
	tos->last_bufminimize_time = gettime();

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
		if (0 < tos->pgos[i].closetime) {
			/* マージン録画フェーズに移っていたら保存されているfinal_piを使う */
			do_pgoutput_close(tos->pgos[i].modulestats, &tos->pgos[i].final_pi);
		} else {
			/* そうでなければ最新のproginfoを */
			do_pgoutput_close(tos->pgos[i].modulestats, tos->proginfo);
		}
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
	} else if ( tos->pos_write > start_pos ) {
		do_pgoutput(tos->pgos[tos->n_pgos].modulestats, &(tos->buf[start_pos]), tos->pos_write - start_pos);
		tos->write_busy = 1;
	}
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
	}

	if (!tos->write_busy) {
		/* バッファ切り詰め */
		if ( nowtime - tos->last_bufminimize_time >= OVERLAP_SEC*1000/4 ) {
			/* (OVERLAP_SEC/4)秒以上経っていたらバッファ切り詰めを実行 */
			ts_minimize_buf(tos);
			tos->last_bufminimize_time = nowtime;
		} else if ( nowtime < tos->last_bufminimize_time ) {
			/* 時刻の巻き戻りに対応 */
			tos->last_bufminimize_time = nowtime;
		}

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

	for (backward_size = i = 0; i < tos->n_th; i++) {
		backward_size += tos->th[i].bytes;
	}
	clear_size = tos->pos_filled - backward_size;
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

void check_stream_timeinfo(ts_output_stat_t *tos)
{
	uint64_t diff_prc;

	/* check_piのインターバルごとにPCR,TOT関連のチェック、クリアを行う */
	if ((tos->proginfo->status&PGINFO_TIMEINFO) == PGINFO_TIMEINFO) {

		/* 1秒程度以上PCRの更新がなければ無効としてクリアする */
		if (tos->proginfo->status & PGINFO_PCR_UPDATED) {
			tos->pcr_retry_count = 0;
		} else {
			if (tos->pcr_retry_count > (1000 / CHECK_INTERVAL)) {
				tos->proginfo->status &= ~PGINFO_VALID_PCR;
			}
			tos->pcr_retry_count++;
			return;
		}

		/* 120秒以上古いTOTは無効としてクリアする(通常は30秒以下の間隔で送出) */
		diff_prc = tos->proginfo->PCR_base - tos->proginfo->TOT_PCR;
		if (tos->proginfo->PCR_wraparounded) {
			diff_prc += PCR_BASE_MAX;
		}

		if (diff_prc > 120 * PCR_BASE_HZ) {
			tos->proginfo->status &= ~PGINFO_GET_TOT;
			tos->proginfo->status &= ~PGINFO_VALID_TOT_PCR;
		}
	} else {
		tos->pcr_retry_count = 0;
	}

	tos->proginfo->status &= ~PGINFO_PCR_UPDATED;
}

void ts_prog_changed(ts_output_stat_t *tos, int64_t nowtime, ch_info_t *ch_info)
{
	int i, actually_start = 0;
	time_mjd_t curr_time;
	time_offset_t offset;
	pgoutput_stat_t *pgos;
	proginfo_t *final_pi, tmp_pi;

	/* print */
	printpi(tos->proginfo);

	/* split! */
	if (tos->n_pgos >= MAX_PGOVERLAP) {
		output_message(MSG_WARNING, L"番組の切り替わりを短時間に連続して検出したためスキップします");
	} else {
		/* 番組終了時間をタイムスタンプから得るかどうか */
		if ( !(tos->last_proginfo.status & PGINFO_UNKNOWN_STARTTIME) &&		/* 開始時間が既知かつ */
				(tos->last_proginfo.status & PGINFO_UNKNOWN_DURATION) &&	/* 持続時刻が未知かつ */
				get_stream_timestamp(&tos->last_proginfo, &curr_time) ) {	/* タイムスタンプは正常 */
			get_time_offset(&offset, &curr_time, &tos->last_proginfo.start);
			tmp_pi = tos->last_proginfo;
			tmp_pi.dur = offset;
			tmp_pi.status &= ~PGINFO_UNKNOWN_DURATION;
			final_pi = &tmp_pi;
			output_message(MSG_NOTIFY, L"終了時刻が未定のまま番組が終了したので現在のタイムスタンプを番組終了時刻にします");
		} else {
			final_pi = &tos->last_proginfo;
		}

		/* endフックを呼び出す */
		for (i = 0; i < tos->n_pgos; i++) {
			do_pgoutput_end(tos->pgos[i].modulestats, final_pi);
		}

		/* pgosの追加 */
		pgos = &(tos->pgos[tos->n_pgos]);
		if (tos->n_pgos >= 1) {
			actually_start = 1; /* 前の番組があるということは本当のスタート */
			pgos[-1].closetime = nowtime + OVERLAP_SEC * 1000;
			pgos[-1].final_pi = *final_pi;
		}

		pgos->initial_pi_status = tos->proginfo->status;
		pgos->fn = do_path_resolver(tos->proginfo, ch_info); /* ここでch_infoにアクセス */
		pgos->modulestats = do_pgoutput_create(pgos->fn, tos->proginfo, ch_info, actually_start); /* ここでch_infoにアクセス */
		pgos->closetime = -1;
		pgos->close_flag = 0;
		pgos->close_remain = 0;
		pgos->delay_remain = 0;
		ts_copy_backward(tos, nowtime);

		tos->n_pgos++;
	}
}

void ts_check_pi(ts_output_stat_t *tos, int64_t nowtime, ch_info_t *ch_info)
{
	int changed = 0, time_changed = 0;
	time_mjd_t endtime, last_endtime, time1, time2;
	WCHAR msg1[64], msg2[64];

	check_stream_timeinfo(tos);

	if ( !(tos->proginfo->status & PGINFO_READY_UPDATED) && 
			( (PGINFO_READY(tos->last_proginfo.status) && tos->n_pgos > 0) || tos->n_pgos == 0 ) ) {
		/* 最新の番組情報が取得できていなくても15秒は判定を保留する */
		if (tos->proginfo_retry_count < 15 * 1000 / CHECK_INTERVAL) {
			tos->proginfo_retry_count++;
			return;
		}
		clear_proginfo_all(tos->proginfo);
	}

	tos->proginfo->status &= ~PGINFO_READY_UPDATED;
	tos->proginfo_retry_count = 0;

	if ( PGINFO_READY(tos->proginfo->status) ) {
		if ( PGINFO_READY(tos->last_proginfo.status) ) {
			/* 番組情報あり→ありの場合イベントIDを比較 */
			if (tos->last_proginfo.event_id != (int)tos->proginfo->event_id) {
				changed = 1;
			}
		} else {
			/* 番組情報なし→ありの変化 */
			changed = 1;
		}
	} else {
		if (PGINFO_READY(tos->last_proginfo.status)) {
			/* 番組情報あり→なしの変化 */
			changed = 1;
		/* 番組情報がなくても1時間おきに番組を切り替える */
		} else if( get_stream_timestamp_rough(tos->proginfo, &time1) &&
				get_stream_timestamp_rough(&tos->last_proginfo, &time2) ) {
			/* ストリームのタイムスタンプが正常に取得できていればそれを比較 */
			if (time1.hour > time2.hour) {
				changed = 1;
			}
		} else if( timenum64(nowtime) / 100 > timenum64(tos->last_checkpi_time) / 100 ) {
			/* そうでなければPCの現在時刻を比較 */
			changed = 1;
		} else if (tos->n_pgos == 0) {
			/* まだ出力が始まっていなかったら強制開始 */
			changed = 1;
		}
	}

	if (changed) {
		/* 番組が切り替わった */
		ts_prog_changed(tos, nowtime, ch_info);
	} else if( PGINFO_READY(tos->proginfo->status) && PGINFO_READY(tos->last_proginfo.status) ) {
		
		if( proginfo_cmp(tos->proginfo, &tos->last_proginfo) ) {
			if (tos->n_pgos >= 1) {
				/* 呼び出すのは最新の番組に対してのみ */
				do_pgoutput_changed(tos->pgos[tos->n_pgos-1].modulestats, &tos->last_proginfo, tos->proginfo);
			}

			/* 番組の時間が途中で変更された場合 */
			time_add_offset(&endtime, &tos->proginfo->start, &tos->proginfo->dur);
			time_add_offset(&last_endtime, &tos->last_proginfo.start, &tos->last_proginfo.dur);

			if ( get_time_offset(NULL, &tos->proginfo->start, &tos->last_proginfo.start) != 0 ) {
				swprintf(msg1, 128, L"番組開始時間の変更(サービス%d)", tos->proginfo->service_id);

				time_changed = 1;
				output_message( MSG_NOTIFY, L"%s: %02d:%02d:%02d → %02d:%02d:%02d", msg1,
					tos->proginfo->start.hour, tos->proginfo->start.min, tos->proginfo->start.sec,
					tos->last_proginfo.start.hour, tos->last_proginfo.start.min, tos->last_proginfo.start.sec );
			}
			if ( get_time_offset(NULL, &endtime, &last_endtime) != 0 ) {
				swprintf(msg1, 128, L"番組終了時間の変更(サービス%d)", tos->proginfo->service_id);

				if ( pi_endtime_unknown(&tos->last_proginfo) ) {
					tsd_strcpy(msg2, TSD_TEXT("未定 →"));
				} else {
					wsprintf( msg2, L"%02d:%02d:%02d →", last_endtime.hour, last_endtime.min, last_endtime.sec );
				}

				time_changed = 1;
				if ( pi_endtime_unknown(tos->proginfo) ) {
					output_message(MSG_NOTIFY, L"%s: %s 未定", msg1, msg2);
				} else {
					output_message( MSG_NOTIFY, L"%s: %s %02d:%02d:%02d", msg1, msg2,
						endtime.hour, endtime.min, endtime.sec );
				}
			}

			if (!time_changed) {
				output_message(MSG_NOTIFY, L"番組情報の変更");
				printpi(tos->proginfo);
			}
		}

	}

	tos->last_proginfo = *tos->proginfo;
	tos->last_checkpi_time = nowtime;
}
