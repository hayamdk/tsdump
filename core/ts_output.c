#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <windows.h>
#else
#include <unistd.h>
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
#include "core/module_hooks.h"
#include "utils/ts_parser.h"
#include "core/tsdump.h"
#include "utils/advanced_buffer.h"
#include "core/load_modules.h"
#include "core/ts_output.h"
#include "utils/tsdstr.h"

//#include "timecalc.h"

static void module_buffer_output(ab_buffer_t *gb, void *param, const uint8_t *buf, int size)
{
	UNREF_ARG(gb);
	output_status_t *status = (output_status_t*)param;
	if (status->parent->module->hooks.hook_pgoutput) {
		status->parent->module->hooks.hook_pgoutput(status->param, buf, size);
	}
}

static void module_buffer_notify_skip(ab_buffer_t *gb, void *param, int skip_bytes)
{
	UNREF_ARG(gb);
	UNREF_ARG(param);
	UNREF_ARG(skip_bytes);
}

static void module_buffer_close(ab_buffer_t *gb, void *param, const uint8_t *buf, int remain_size)
{
	UNREF_ARG(gb);
	UNREF_ARG(buf);
	UNREF_ARG(remain_size);
	output_status_t *status = (output_status_t*)param;
	output_status_t *to_free;

	assert(status->parent->refcount > 0);

	if (status->parent->module->hooks.hook_pgoutput_close) {
		status->parent->module->hooks.hook_pgoutput_close(status->param, &status->parent->parent->final_pi);
	}
	status->parent->refcount--;

	if (status->parent->refcount <= 0) {
		if (status->parent->module->hooks.hook_pgoutput_postclose) {
			status->parent->module->hooks.hook_pgoutput_postclose(status->parent->param);
		}
		status->parent->parent->refcount--;
		to_free = status->parent->client_array; /* save before expire */
		if (status->parent->parent->refcount <= 0) {
			status->parent->parent->parent->n_pgos--;
			free(status->parent->parent->client_array); /* expire */
		}
		free(to_free);
	}
}

static int module_buffer_pre_output(ab_buffer_t *gb, void *param, int *acceptable_bytes)
{
	UNREF_ARG(gb);
	UNREF_ARG(acceptable_bytes);
	int busy = 0;
	output_status_t *status = (output_status_t*)param;
	if (status->parent->module->hooks.hook_pgoutput_check) {
		busy = status->parent->module->hooks.hook_pgoutput_check(status->param);
	}
	return busy;
}

static const ab_downstream_handler_t module_buffer_handlers = {
	module_buffer_output,
	module_buffer_notify_skip,
	module_buffer_close,
	NULL,
	module_buffer_pre_output
};

static output_status_module_t *do_pgoutput_create(ab_buffer_t *buf, ab_history_t *history, output_status_prog_t *pgos, const proginfo_t *pi, ch_info_t *ch_info, const int actually_start)
{
	int i, j, n;
	output_status_module_t *module_status_array = (output_status_module_t*)malloc(sizeof(output_status_module_t)*n_modules);
	output_status_t *output_status_array;

	for ( i = 0; i < n_modules; i++ ) {
		n = 0;
		if (modules[i].hooks.hook_pgoutput_precreate) {
			module_status_array[i].param = modules[i].hooks.hook_pgoutput_precreate(pgos->fn, pi, ch_info, actually_start, &n);
		} else {
			module_status_array[i].param = NULL;
		}

		module_status_array[i].refcount = 0;
		if (n > 0) {
			output_status_array = (output_status_t*)malloc(sizeof(output_status_t) * n);
		} else {
			output_status_array = NULL;
		}
		for (j = 0; j < n; j++) {
			output_status_array[j].parent = &module_status_array[i];
			output_status_array[j].param = NULL;
			output_status_array[j].downstream_id = ab_connect_downstream_history_backward(
					buf, &module_buffer_handlers, 188, modules[i].hooks.output_block_size, 0, &output_status_array[j], history
				);
			if (output_status_array[j].downstream_id < 0) {
				output_message(MSG_ERROR, TSD_TEXT("バッファに対して下流ストリームを追加できませんでした: モジュール:%s"), modules[i].def->modname);
				continue;
			}

			if (modules[i].hooks.hook_pgoutput_create) {
				output_status_array[j].param = modules[i].hooks.hook_pgoutput_create(module_status_array[i].param);
			}
			module_status_array[i].refcount++;
		}
		module_status_array[i].n_clients = module_status_array[i].refcount;
		module_status_array[i].module = &modules[i];
		module_status_array[i].parent = pgos;
		module_status_array[i].client_array = output_status_array;
	}
	return module_status_array;
}

int do_pgoutput_changed(output_status_module_t *status, const proginfo_t *old_pi, const proginfo_t *new_pi)
{
	int i;
	int err = 0;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_pgoutput_changed) {
			modules[i].hooks.hook_pgoutput_changed(status[i].param, old_pi, new_pi);
		}
	}
	return err;
}

void do_pgoutput_end(output_status_module_t *status, const proginfo_t *pi)
{
	int i;
	for (i = 0; i < n_modules; i++) {
		if (modules[i].hooks.hook_pgoutput_end) {
			modules[i].hooks.hook_pgoutput_end(status[i].param, pi);
		}
	}
}

void do_pgoutput_close(output_status_prog_t *pgos)
{
	int i, j;
	for (i = 0; i < n_modules; i++) {
		if (pgos->client_array[i].n_clients > 0) {
			/* 出力を行っているモジュールはストリームのcloseフックを経由してモジュールのフックを呼び出す */
			for (j = 0; j < pgos->client_array[i].n_clients; j++) {
				if (pgos->client_array[i].client_array[j].downstream_id >= 0) {
					ab_disconnect_downstream(pgos->parent->ab, 
						pgos->client_array[i].client_array[j].downstream_id, 0);
				}
			}
		} else {
			/* 出力を行っていないモジュールは即フックを呼び出す */
			if (modules[i].hooks.hook_pgoutput_postclose) {
				modules[i].hooks.hook_pgoutput_postclose(pgos->client_array[i].param);
			}
			pgos->refcount--;
			if (pgos->refcount <= 0) {
				pgos->parent->n_pgos--;
				free(pgos->client_array);
			}
		}
	}
}

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
	TSDCHAR et[4096];

	if (!PGINFO_READY(pi->status)) {
		if (pi->status & PGINFO_GET_SERVICE_INFO) {
			output_message(MSG_DISP, TSD_TEXT("<<< ------------ 番組情報 ------------\n")
				TSD_TEXT("[番組情報なし]\n%s\n")
				TSD_TEXT("---------------------------------- >>>"), pi->service_name.str);
			return;
		} else {
			output_message(MSG_DISP, TSD_TEXT("<<< ------------ 番組情報 ------------\n")
				TSD_TEXT("[番組情報なし]\n")
				TSD_TEXT("---------------------------------- >>>"));
			return;
		}
	}

	get_extended_text(et, sizeof(et) / sizeof(TSDCHAR), pi);

	if ( pi_endtime_unknown(pi) ) {
		output_message(MSG_DISP, TSD_TEXT("<<< ------------ 番組情報 ------------\n")
			TSD_TEXT("[%d/%02d/%02d %02d:%02d:%02d〜 +未定]\n%s:%s\n「%s」\n")
			TSD_TEXT("---------------------------------- >>>"),
			pi->start.year, pi->start.mon, pi->start.day,
			pi->start.hour, pi->start.min, pi->start.sec,
			pi->service_name.str,
			pi->event_name.str,
			et
		);
	} else {
		time_add_offset(&endtime, &pi->start, &pi->dur);
		output_message(MSG_DISP, TSD_TEXT("<<< ------------ 番組情報 ------------\n")
			TSD_TEXT("[%d/%02d/%02d %02d:%02d:%02d〜%02d:%02d:%02d]\n%s:%s\n「%s」\n")
			TSD_TEXT("---------------------------------- >>>"),
			pi->start.year, pi->start.mon, pi->start.day,
			pi->start.hour, pi->start.min, pi->start.sec,
			endtime.hour, endtime.min, endtime.sec,
			pi->service_name.str,
			pi->event_name.str,
			et
		);
	}
}

int create_tos_per_service(output_status_stream_t **ptos, ts_service_list_t *service_list, ch_info_t *ch_info)
{
	int i, j, k;
	int n_tos;
	output_status_stream_t *tos;

	if (ch_info->mode_all_services) {
		n_tos = service_list->n_services;
		tos = (output_status_stream_t*)malloc(n_tos*sizeof(output_status_stream_t));
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
		tos = (output_status_stream_t*)malloc(n_tos*sizeof(output_status_stream_t));
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

void init_tos(output_status_stream_t *tos)
{
	int i;

	tos->n_pgos = 0;
	tos->pgos = (output_status_prog_t*)malloc(MAX_PGOVERLAP * sizeof(output_status_prog_t));
	for (i = 0; i < MAX_PGOVERLAP; i++) {
		tos->pgos[i].fn = (TSDCHAR*)malloc(MAX_PATH_LEN*sizeof(TSDCHAR));
		tos->pgos[i].refcount = 0;
	}

	tos->ab = ab_create(BUFSIZE);
	ab_set_history(tos->ab, &tos->ab_history, CHECK_INTERVAL, OVERLAP_SEC * 1000);
	tos->dropped_bytes = 0;

	init_proginfo(&tos->last_proginfo);
	tos->last_checkpi_time = gettime();
	tos->proginfo_retry_count = 0;
	tos->pcr_retry_count = 0;
	tos->last_bufminimize_time = gettime();
	tos->curr_pgos = NULL;

	return;
}

void close_tos(output_status_stream_t *tos)
{
	int64_t end;
	int i, j, busy, remain_size;

	/* close all output */
	for (i = j = 0; i < tos->n_pgos; j++) {
		assert(j < MAX_PGOVERLAP);
		if (tos->pgos[j].refcount <= 0) {
			continue;
		}
		i++;

		if (0 < tos->pgos[j].closetime) {
			/* マージン録画フェーズに移っていたら保存されているfinal_piを使う */
			do_pgoutput_close(&tos->pgos[j]);
		} else {
			/* そうでなければ最新のproginfoを */
			do_pgoutput_close(&tos->pgos[j]);
		}
	}
	
	/* 書き出し完了を待機(5秒まで) */
	for (	i = ab_first_downstream(tos->ab);
			i >= 0;
			i = ab_next_downstream(tos->ab, i) ) {
		ab_disconnect_downstream(tos->ab, i, 0);
	}
	for (end = gettime() + 5*1000; gettime() < end;) {
		busy = 0;
		for (	i = ab_first_downstream(tos->ab);
				i >= 0 && !busy;
				i = ab_next_downstream(tos->ab, i) ) {
			busy |= ab_get_downstream_status(tos->ab, i, NULL, &remain_size);
			busy |= (remain_size > 0);
		}
		if (!busy) {
			break;
		}

		ab_output_buf(tos->ab);
#ifdef TSD_PLATFORM_MSVC
		Sleep(100);
#else
		usleep(100 * 1000);
#endif
	}

	ab_close_buf(tos->ab);

	for (i = 0; i < MAX_PGOVERLAP; i++) {
		free((TSDCHAR*)tos->pgos[i].fn);
	}
	free(tos->pgos);

	//free(tos->th);
	//free(tos->buf);
	//free(tos);
}

void ts_output(output_status_stream_t *tos, int64_t nowtime)
{
	int i, j;

	/* ファイル出力終了タイミングをチェック */
	for (i = j = 0; i < tos->n_pgos; j++) {
		assert(j < MAX_PGOVERLAP);
		if (tos->pgos[j].refcount <= 0) {
			continue;
		}
		i++;

		/* 出力終了をチェック */
		if (0 < tos->pgos[j].closetime && tos->pgos[j].closetime < nowtime) {
			if (!tos->pgos[j].close_flag) {
				tos->pgos[j].close_flag = 1;
				/* closeフラグを設定 */
				do_pgoutput_close(&tos->pgos[j]);
			}
		}
	}

	/* バッファ切り詰め */
	if ( nowtime - tos->last_bufminimize_time >= OVERLAP_SEC*1000/4 ) {
		/* (OVERLAP_SEC/4)秒以上経っていたらバッファ切り詰めを試行する */
		//ts_minimize_buf(tos);
		ab_clear_buf(tos->ab, 0);
		tos->last_bufminimize_time = nowtime;
	} else if ( nowtime < tos->last_bufminimize_time ) {
		/* 時刻の巻き戻りに対応 */
		tos->last_bufminimize_time = nowtime;
	}

	/* ファイル書き出し */
	ab_output_buf(tos->ab);
}

void ts_check_extended_text(output_status_stream_t *tos)
{
	//output_status_prog_t *current_pgos;
	TSDCHAR et[4096];

	if (!tos->curr_pgos) {
		return;
	}

	//current_pgos = &tos->pgos[tos->n_pgos - 1];
	if (!(tos->curr_pgos->initial_pi_status & PGINFO_GET_EXTEND_TEXT) &&
			(tos->proginfo->status & PGINFO_GET_EXTEND_TEXT)) {
		/* 拡張形式イベント情報が途中から来た場合 */
		get_extended_text(et, sizeof(et) / sizeof(TSDCHAR), tos->proginfo);
		output_message(MSG_DISP, TSD_TEXT("<<< ------------ 追加番組情報 ------------\n")
			TSD_TEXT("「%s」\n")
			TSD_TEXT("---------------------------------- >>>"),
			et
		);
	}
}

void check_stream_timeinfo(output_status_stream_t *tos)
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

void ts_prog_changed(output_status_stream_t *tos, int64_t nowtime, ch_info_t *ch_info)
{
	int i, j, actually_start = 0;
	time_mjd_t curr_time;
	time_offset_t offset;
	output_status_prog_t *pgos;
	proginfo_t *final_pi, tmp_pi;

	/* print */
	printpi(tos->proginfo);

	/* split! */
	if (tos->n_pgos >= MAX_PGOVERLAP) {
		output_message(MSG_WARNING, TSD_TEXT("番組の切り替わりを短時間に連続して検出したためスキップします"));
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
			output_message(MSG_NOTIFY, TSD_TEXT("終了時刻が未定のまま番組が終了したので現在のタイムスタンプを番組終了時刻にします"));
		} else {
			final_pi = &tos->last_proginfo;
		}

		/* endフックを呼び出す */
		for (i = j = 0; i < tos->n_pgos; j++) {
			assert(j < MAX_PGOVERLAP);
			if (tos->pgos[j].refcount > 0) {
				do_pgoutput_end(tos->pgos[j].client_array, final_pi);
				i++;
			}
		}

		/* 追加すべきpgosのスロットを探索 */
		pgos = NULL;
		for (i = 0; i <= tos->n_pgos; i++) {
			if (tos->pgos[i].refcount == 0) {
				pgos = &tos->pgos[i];
				break;
			}
		}
		assert(pgos);

		/* すでに録画中の番組があるかどうか */
		if (tos->curr_pgos) {
			actually_start = 1;
			tos->curr_pgos->closetime = nowtime + OVERLAP_SEC * 1000;
			tos->curr_pgos->final_pi = *final_pi;
		}

		pgos->initial_pi_status = tos->proginfo->status;
		pgos->fn = do_path_resolver(tos->proginfo, ch_info); /* ここでch_infoにアクセス */
		pgos->client_array = do_pgoutput_create(tos->ab, tos->ab_history, pgos, tos->proginfo, ch_info, actually_start); /* ここでch_infoにアクセス */
		pgos->closetime = -1;
		pgos->close_flag = 0;
		pgos->close_remain = 0;
		pgos->refcount = n_modules;
		pgos->parent = tos;

		tos->curr_pgos = pgos;
		tos->n_pgos++;
	}
}

void ts_check_pi(output_status_stream_t *tos, int64_t nowtime, ch_info_t *ch_info)
{
	int changed = 0, time_changed = 0;
	time_mjd_t endtime, last_endtime, time1, time2;
	TSDCHAR msg1[64], msg2[64];

	check_stream_timeinfo(tos);

	if ( !(tos->proginfo->status & PGINFO_READY_UPDATED) && 
			( (PGINFO_READY(tos->last_proginfo.status) && tos->curr_pgos) || !tos->curr_pgos) ) {
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
		} else if (!tos->curr_pgos) {
			/* まだ出力が始まっていなかったら強制開始 */
			changed = 1;
		}
	}

	if (changed) {
		/* 番組が切り替わった */
		ts_prog_changed(tos, nowtime, ch_info);
	} else if( PGINFO_READY(tos->proginfo->status) && PGINFO_READY(tos->last_proginfo.status) ) {
		
		if( proginfo_cmp(tos->proginfo, &tos->last_proginfo) ) {
			if (tos->curr_pgos) {
				/* 呼び出すのは最新の番組に対してのみ */
				do_pgoutput_changed(tos->curr_pgos->client_array, &tos->last_proginfo, tos->proginfo);
			}

			/* 番組の時間が途中で変更された場合 */
			time_add_offset(&endtime, &tos->proginfo->start, &tos->proginfo->dur);
			time_add_offset(&last_endtime, &tos->last_proginfo.start, &tos->last_proginfo.dur);

			if ( get_time_offset(NULL, &tos->proginfo->start, &tos->last_proginfo.start) != 0 ) {
				tsd_snprintf(msg1, 64, TSD_TEXT("番組開始時間の変更(サービス%d)"), tos->proginfo->service_id);

				time_changed = 1;
				output_message( MSG_NOTIFY, TSD_TEXT("%s: %02d:%02d:%02d → %02d:%02d:%02d"), msg1,
					tos->proginfo->start.hour, tos->proginfo->start.min, tos->proginfo->start.sec,
					tos->last_proginfo.start.hour, tos->last_proginfo.start.min, tos->last_proginfo.start.sec );
			}
			if ( get_time_offset(NULL, &endtime, &last_endtime) != 0 ) {
				tsd_snprintf(msg1, 64, TSD_TEXT("番組終了時間の変更(サービス%d)"), tos->proginfo->service_id);

				if ( pi_endtime_unknown(&tos->last_proginfo) ) {
					tsd_strcpy(msg2, TSD_TEXT("未定 →"));
				} else {
					tsd_snprintf( msg2, 64, TSD_TEXT("%02d:%02d:%02d →"), last_endtime.hour, last_endtime.min, last_endtime.sec );
				}

				time_changed = 1;
				if ( pi_endtime_unknown(tos->proginfo) ) {
					output_message(MSG_NOTIFY, TSD_TEXT("%s: %s 未定"), msg1, msg2);
				} else {
					output_message( MSG_NOTIFY, TSD_TEXT("%s: %s %02d:%02d:%02d"), msg1, msg2,
						endtime.hour, endtime.min, endtime.sec );
				}
			}

			if (!time_changed) {
				output_message(MSG_NOTIFY, TSD_TEXT("番組情報の変更"));
				printpi(tos->proginfo);
			}
		}

	}

	tos->last_proginfo = *tos->proginfo;
	tos->last_checkpi_time = nowtime;
}
