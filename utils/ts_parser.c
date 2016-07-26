#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/timeb.h>

typedef uint32_t DWORD;

#include "core/tsdump_def.h"
#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"
#include "utils/ts_parser.h"
#include "utils/tsdstr.h"
#include "utils/aribstr.h"
#include "core/tsdump.h"

#define TT TSD_TEXT

const TSDCHAR *genre_main[] = {
	TSD_TEXT("ニュース／報道"),			TSD_TEXT("スポーツ"),	TSD_TEXT("情報／ワイドショー"),	TSD_TEXT("ドラマ"),
	TSD_TEXT("音楽"),					TSD_TEXT("バラエティ"),	TSD_TEXT("映画"),				TSD_TEXT("アニメ／特撮"),
	TSD_TEXT("ドキュメンタリー／教養"),	TSD_TEXT("劇場／公演"),	TSD_TEXT("趣味／教育"),			TSD_TEXT("福祉"),
	TSD_TEXT("予備"),					TSD_TEXT("予備"),		TSD_TEXT("拡張"),				TSD_TEXT("その他")
};

const TSDCHAR *genre_detail[] = {
	/* 0x0 */
	TSD_TEXT("定時・総合"), TSD_TEXT("天気"), TSD_TEXT("特集・ドキュメント"), TSD_TEXT("政治・国会"), TSD_TEXT("経済・市況"), TSD_TEXT("海外・国際"), TSD_TEXT("解説"), TSD_TEXT("討論・会談"),
	TSD_TEXT("報道特番"), TSD_TEXT("ローカル・地域"), TSD_TEXT("交通"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x1 */
	TSD_TEXT("スポーツニュース"), TSD_TEXT("野球"), TSD_TEXT("サッカー"), TSD_TEXT("ゴルフ"), TSD_TEXT("その他の球技"), TSD_TEXT("相撲・格闘技"), TSD_TEXT("オリンピック・国際大会"), TSD_TEXT("マラソン・陸上・水泳"),
	TSD_TEXT("モータースポーツ"), TSD_TEXT("マリン・ウィンタースポーツ"), TSD_TEXT("競馬・公営競技"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x2 */
	TSD_TEXT("芸能・ワイドショー"), TSD_TEXT("ファッション"), TSD_TEXT("暮らし・住まい"), TSD_TEXT("健康・医療"), TSD_TEXT("ショッピング・通販"), TSD_TEXT("グルメ・料理"), TSD_TEXT("イベント"), TSD_TEXT("番組紹介・お知らせ"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x3 */
	TSD_TEXT("国内ドラマ"), TSD_TEXT("海外ドラマ"), TSD_TEXT("時代劇"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x4 */
	TSD_TEXT("国内ロック・ポップス"), TSD_TEXT("海外ロック・ポップス"), TSD_TEXT("クラシック・オペラ"), TSD_TEXT("ジャズ・フュージョン"), TSD_TEXT("歌謡曲・演歌"), TSD_TEXT("ライブ・コンサート"), TSD_TEXT("ランキング・リクエスト"), TSD_TEXT("カラオケ・のど自慢"),
	TSD_TEXT("民謡・邦楽"), TSD_TEXT("童謡・キッズ"), TSD_TEXT("民族音楽・ワールドミュージック"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x5 */
	TSD_TEXT("クイズ"), TSD_TEXT("ゲーム"), TSD_TEXT("トークバラエティ"), TSD_TEXT("お笑い・コメディ"), TSD_TEXT("音楽バラエティ"), TSD_TEXT("旅バラエティ"), TSD_TEXT("料理バラエティ"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x6 */
	TSD_TEXT("洋画"), TSD_TEXT("邦画"), TSD_TEXT("アニメ"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x7 */
	TSD_TEXT("国内アニメ"), TSD_TEXT("海外アニメ"), TSD_TEXT("特撮"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x8 */
	TSD_TEXT("社会・時事"), TSD_TEXT("歴史・紀行"), TSD_TEXT("自然・動物・環境"), TSD_TEXT("宇宙・科学・医学"), TSD_TEXT("カルチャー・伝統芸能"), TSD_TEXT("文学・文芸"), TSD_TEXT("スポーツ"), TSD_TEXT("ドキュメンタリー全般"),
	TSD_TEXT("インタビュー・討論"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0x9 */
	TSD_TEXT("現代劇・新劇"), TSD_TEXT("ミュージカル"), TSD_TEXT("ダンス・バレエ"), TSD_TEXT("落語・演芸"), TSD_TEXT("歌舞伎・古典"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0xA */
	TSD_TEXT("旅・釣り・アウトドア"), TSD_TEXT("園芸・ペット・手芸"), TSD_TEXT("音楽・美術・工芸"), TSD_TEXT("囲碁・将棋"), TSD_TEXT("麻雀・パチンコ"), TSD_TEXT("車・オートバイ"), TSD_TEXT("コンピュータ・ＴＶゲーム"), TSD_TEXT("会話・語学"),
	TSD_TEXT("幼児・小学生"), TSD_TEXT("中学生・高校生"), TSD_TEXT("大学生・受験"), TSD_TEXT("生涯教育・資格"), TSD_TEXT("教育問題"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0xB */
	TSD_TEXT("高齢者"), TSD_TEXT("障害者"), TSD_TEXT("社会福祉"), TSD_TEXT("ボランティア"), TSD_TEXT("手話"), TSD_TEXT("文字（字幕）"), TSD_TEXT("音声解説"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他"),

	/* 0xC */
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),

	/* 0xD */
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),

	/* 0xE */
	TSD_TEXT("BS/地上デジタル放送用番組付属情報"), TSD_TEXT("広帯域CSデジタル放送用拡張"), TSD_TEXT("衛星デジタル音声放送用拡張"), TSD_TEXT("サーバー型番組付属情報"), TSD_TEXT("IP放送用番組付属情報"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),

	/* 0xF */
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("その他")
};

const TSDCHAR *genre_user[] = {
	TSD_TEXT("中止の可能性あり"),
	TSD_TEXT("延長の可能性あり"),
	TSD_TEXT("中断の可能性あり"),
	TSD_TEXT("同一シリーズの別話数放送の可能性あり"),
	TSD_TEXT("編成未定枠"),
	TSD_TEXT("繰り上げの可能性あり"),
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"),

	TSD_TEXT("中断ニュースあり"), TSD_TEXT("当該イベントに関連する臨時サービスあり")
	TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-"), TSD_TEXT("-")
};

void get_genre_str(const TSDCHAR **genre1, const TSDCHAR **genre2, Cd_t_item item)
{
	if (item.content_nibble_level_1 != 0xe) {
		*genre1 = genre_main[item.content_nibble_level_1];
		*genre2 = genre_detail[item.content_nibble_level_1 * 0x10 + item.content_nibble_level_2];
	} else {
		*genre1 = genre_detail[item.content_nibble_level_1 * 0x10 + item.content_nibble_level_2];
		if (item.user_nibble_1 <= 0x01) {
			*genre2 = genre_user[item.user_nibble_1 * 0x10 + item.user_nibble_2];
		} else {
			*genre2 = TSD_TEXT("-");
		}
	}
}

int get_extended_text(TSDCHAR *dst, size_t n, const proginfo_t *pi)
{
	int i;
	TSDCHAR *p = dst, *end = &dst[n - 1];

	*p = L'\0';
	if (!(pi->status & PGINFO_GET_EXTEND_TEXT)) {
		return 0;
	}

	for (i = 0; i < pi->n_items && p < end; i++) {
		tsd_strncpy(p, pi->items[i].desc.str, end - p);
		while (*p != L'\0') { p++; }
		tsd_strncpy(p, TSD_TEXT("\n"), end - p);
		while (*p != L'\0') { p++; }
		tsd_strncpy(p, pi->items[i].item.str, end - p);
		while (*p != L'\0') { p++; }
		tsd_strncpy(p, TSD_TEXT("\n"), end - p);
		while (*p != L'\0') { p++; }
	}
	return 1;
}

int cmp_offset(const time_offset_t *offset1, const time_offset_t *offset2)
{
	if (offset1->sign != offset2->sign) {
		return 1;
	} else if (offset1->sign != 0) {
		if ( offset1->day != offset2->day ||
				offset1->hour != offset2->hour ||
				offset1->min != offset2->min ||
				offset1->sec != offset2->sec ||
				offset1->usec != offset2->usec ) {
			return 1;
		}
	}
	return 0;
}

int cmp_time(const time_mjd_t *offset1, const time_mjd_t *offset2)
{
	if( offset1->mjd != offset2->mjd ||
			offset1->hour != offset2->hour ||
			offset1->min != offset2->min ||
			offset1->sec != offset2->sec ||
			offset1->usec != offset2->usec ) {
		return 1;
	}
	return 0;
}

/* 論理OR演算子は副作用完了点なので不正なメモリ参照は無い */
#define cmp_aribstr(x, y) ( ((x)->aribstr_len != (y)->aribstr_len) || \
	(((x)->aribstr_len > 0) && memcmp((x)->aribstr, (y)->aribstr, (x)->aribstr_len)) )

int cmp_genre(const Cd_t *genre1, const Cd_t *genre2)
{
	int i;
	if (genre1->n_items != genre2->n_items) {
		return 1;
	}
	for (i = 0; i < genre1->n_items; i++) {
		if( genre1->items[i].content_nibble_level_1 != genre2->items[i].content_nibble_level_1 ||
				genre1->items[i].content_nibble_level_2 != genre1->items[i].content_nibble_level_2 ||
				genre1->items[i].user_nibble_1 != genre1->items[i].user_nibble_1 ||
				genre1->items[i].user_nibble_2 != genre1->items[i].user_nibble_2 ) {
			return 1;
		}
	}
	return 0;
}

int cmp_extended_text(const proginfo_t *pi1, const proginfo_t *pi2)
{
	int i;
	if (pi1->n_items != pi2->n_items) {
		return 1;
	}
	for (i = 0; i < pi1->n_items; i++) {
		if ( cmp_aribstr(&pi1->items[i].desc, &pi2->items[i].desc) ||
				cmp_aribstr(&pi1->items[i].item, &pi2->items[i].item) ) {
			return 1;
		}
	}
	return 0;
}

int proginfo_cmp(const proginfo_t *pi1, const proginfo_t *pi2)
{
	if ( (pi1->status & (PGINFO_GET_ALL|PGINFO_UNKNOWN_STARTTIME|PGINFO_UNKNOWN_DURATION)) !=
			(pi2->status & (PGINFO_GET_ALL|PGINFO_UNKNOWN_STARTTIME|PGINFO_UNKNOWN_DURATION)) ) {
		return 1;
	}

	if ( pi1->status & PGINFO_GET_PAT && pi1->service_id != pi2->service_id ) {
		return 1;
	}

	if ( pi1->status == PGINFO_GET_SERVICE_INFO ) {
		if ( pi1->network_id != pi2->network_id ||
				pi1->ts_id != pi2->ts_id ) {
			return 1;
		}
	}

	if (pi1->status&PGINFO_GET_EVENT_INFO) {
		if (pi1->event_id != pi2->event_id) {
			return 1;
		}
		if ( !(pi1->status&PGINFO_UNKNOWN_STARTTIME) && cmp_time(&pi1->start, &pi2->start) ) {
			return 1;
		}
		if ( !(pi1->status&PGINFO_UNKNOWN_DURATION) && cmp_offset(&pi1->dur, &pi2->dur) ) {
			return 1;
		}
	}

	if ( pi1->status & PGINFO_GET_SHORT_TEXT ) {
		if ( cmp_aribstr(&pi1->event_text, &pi2->event_text) != 0 || 
				cmp_aribstr(&pi1->event_name, &pi2->event_name) ) {
			return 1;
		}
	}

	if ( pi1->status & PGINFO_GET_GENRE ) {
		if (cmp_genre(&pi1->genre_info, &pi2->genre_info)) {
			return 1;
		}
	}

	if ( pi1->status & PGINFO_GET_EXTEND_TEXT ) {
		if (cmp_extended_text(pi1, pi2)) {
			return 1;
		}
	}

	return 0;
}

/* MJD(修正ユリウス日) -> YMD */
void mjd_to_ymd(const unsigned int mjd16, int *year, int *mon, int *day)
{
	double mjd;
	int y, m, d, k;

	/*　2100年2月28日までの間有効な公式（ARIB STD-B10 第２部より）　*/
	mjd = (double)mjd16;
	y = (int)((mjd - 15078.2) / 365.25);
	m = (int)((mjd - 14956.1 - (int)((double)y*365.25)) / 30.6001);
	d = mjd16 - 14956 - (int)((double)y*365.25) - (int)((double)m*30.6001);
	if (m == 14 || m == 15) {
		k = 1;
	}
	else {
		k = 0;
	}
	*year = 1900 + y + k;
	*mon = m - 1 - k * 12;
	*day = d;
}

int get_stream_timestamp(const proginfo_t *pi, time_mjd_t *jst_time)
{
	int64_t diff_pcr, usec;
	int sec, min, hour, day_diff;

	if ( (pi->status&PGINFO_TIMEINFO) != PGINFO_TIMEINFO ) {
		return 0;
	}

	diff_pcr = pi->PCR_base - pi->TOT_PCR;
	if (pi->PCR_wraparounded) {
		diff_pcr += PCR_BASE_MAX;
	}
	if (diff_pcr < 0) {
		return 0;
	}

	usec = diff_pcr*1000*1000 / PCR_BASE_HZ + pi->TOT_time.usec;
	sec = (int)(usec/(1000*1000)) + pi->TOT_time.sec;
	min = sec/60 + pi->TOT_time.min;
	hour = min / 60 + pi->TOT_time.hour;
	day_diff = hour / 24;

	usec = usec % (1000 * 1000);
	sec = sec % 60;
	min = min % 60;
	hour = hour % 24;

	jst_time->mjd = pi->TOT_time.mjd + day_diff;
	if (day_diff > 0) {
		mjd_to_ymd(jst_time->mjd, &jst_time->year, &jst_time->mon, &jst_time->day);
	} else {
		jst_time->year = pi->TOT_time.year;
		jst_time->mon = pi->TOT_time.mon;
		jst_time->day = pi->TOT_time.day;
	}
	jst_time->hour = hour;
	jst_time->min = min;
	jst_time->sec = sec;
	jst_time->usec = (int)usec;

	return 1;
}

/* 最低でもTOTがあればタイムスタンプを返す */
int get_stream_timestamp_rough(const proginfo_t *pi, time_mjd_t *time_mjd)
{
	if (PGINFO_READY_TIMESTAMP(pi->status)) {
		get_stream_timestamp(pi, time_mjd);
		return 1;
	}
	if (pi->status & PGINFO_GET_TOT) {
		*time_mjd = pi->TOT_time;
		return 2;
	}
	return 0;
}

int get_time_offset(time_offset_t *offset, const time_mjd_t *time_target, const time_mjd_t *time_orig)
{
	int64_t offset_usec;
	int offset_day, sign = 0;

	offset_usec = (time_target->hour - time_orig->hour);
	offset_usec = offset_usec * 60 + (time_target->min - time_orig->min);
	offset_usec = offset_usec * 60 + time_target->sec - time_orig->sec;
	offset_usec = offset_usec * 1000 * 1000 + (time_target->usec - time_orig->usec);
	offset_day = time_target->mjd - time_orig->mjd;

	if (offset_day > 0) {
		sign = 1;
		if (offset_usec < 0) {
			offset_usec += (int64_t)1000*1000*60*60*24;
			offset_day--;
		}
	} else if(offset_day == 0) {
		if (offset_usec < 0) {
			sign = -1;
			offset_usec = -offset_usec;
		} else if(offset_usec > 0) {
			sign = 1;
		}
	} else {
		sign = -1;
		if (offset_usec > 0) {
			offset_usec = -offset_usec + (int64_t)1000*1000*60*60*24;
			offset_day = -offset_day - 1;
		}
	}

	if (offset) {
		offset->sign = sign;
		offset->day = offset_day;
		offset->usec = offset_usec % (1000*1000);
		offset_usec /= 1000*1000;
		offset->sec = offset_usec % 60;
		offset_usec /= 60;
		offset->min = offset_usec % 60;
		offset_usec /= 60;
		offset->hour = (int)offset_usec;
	}

	return sign;
}

void time_add_offset(time_mjd_t *dst, const time_mjd_t *orig, const time_offset_t *offset)
{
	int sign;
	int mjd, hour, min, sec, usec;

	if (offset->sign < 0) { sign = -1; }
	else if (offset->sign == 0) { sign = 0; }
	else { sign = 1; }

	mjd = orig->mjd + offset->day * sign;
	usec = orig->usec + offset->usec * sign;
	sec = orig->sec + offset->sec * sign;
	min = orig->min + offset->min * sign;
	hour = orig->hour + offset->hour * sign;

	if (usec < 0) {
		usec += 1000 * 1000;
		sec -= 1;
	} else if (usec >= 1000 * 1000) {
		usec -= 1000 * 1000;
		sec += 1;
	}

	if (sec < 0) {
		sec += 60;
		min -= 1;
	} else if(sec >= 60) {
		sec -= 60;
		min += 1;
	}

	if (min < 0) {
		min += 60;
		hour -= 1;
	} else if (min >= 60) {
		min -= 60;
		hour += 1;
	}

	if (hour < 0) {
		hour += 24;
		mjd -= 1;
	} else if(hour >= 24) {
		hour -= 24;
		mjd += 1;
	}

	dst->mjd = (unsigned int)mjd;
	mjd_to_ymd(dst->mjd, &dst->year, &dst->mon, &dst->day);
	dst->hour = hour;
	dst->min = min;
	dst->sec = sec;
	dst->usec = usec;
}

int parse_ts_header(const uint8_t *packet, ts_header_t *tsh)
{
	int pos;
	uint8_t sync_byte;

	sync_byte = packet[0];

	if (sync_byte != 0x47) {
		tsh->valid_sync_byte = 0;
		return 0;
	}

	tsh->valid_sync_byte				= 1;
	tsh->sync_byte						= sync_byte;
	tsh->transport_error_indicator		= get_bits(packet, 8, 1);
	tsh->payload_unit_start_indicator	= get_bits(packet, 9, 1);
	tsh->transport_priority				= get_bits(packet, 10, 1);
	tsh->pid							= get_bits(packet, 11, 13);
	tsh->transport_scrambling_control	= get_bits(packet, 24, 2);
	tsh->adaptation_field_control		= get_bits(packet, 26, 2);
	tsh->continuity_counter				= get_bits(packet, 28, 4);

	if(tsh->transport_scrambling_control) {
		return 1;
	}

	pos = 4;
	tsh->adaptation_field_len = 0;
	if (tsh->adaptation_field_control & 0x02) {
		/* have adaptation_field */
		tsh->adaptation_field_len = packet[pos];
		tsh->adaptation_field_pos = (uint8_t)pos + 1;
		pos += 1 + tsh->adaptation_field_len;
	}

	tsh->payload_pos = 0;
	tsh->payload_data_pos = 0;
	tsh->pointer_field = 0;
	if (tsh->adaptation_field_control & 0x01) {
		/* have payload */
		if (pos >= 188) {
			return 0;
		}
		tsh->payload_pos = (uint8_t)pos;
		if (tsh->payload_unit_start_indicator) {
			tsh->pointer_field = packet[tsh->payload_pos];
			pos += 1 + tsh->pointer_field;
		}
		if (pos >= 188) {
			return 0;
		}
		tsh->payload_data_pos = (uint8_t)pos;
	}

	return 1;
}

static inline void parse_PSI(const uint8_t *packet, const ts_header_t *tsh, PSI_parse_t *ps)
{
	int pos, remain, pointer_field;

	/* FINISHED状態を初期状態に戻す */
	if (ps->stat == PAYLOAD_STAT_FINISHED) {
		if (ps->next_recv_payload > 0) {
			/* 前回受信した残りのペイロード */
			ps->stat = PAYLOAD_STAT_PROC;
			memcpy(ps->payload, ps->next_payload, ps->next_recv_payload);
			ps->n_payload = ps->n_next_payload;
			ps->recv_payload = ps->next_recv_payload;
			ps->n_next_payload = 0;
			ps->next_recv_payload = 0;
		} else {
			/* 前回の残りが無いので初期状態にする */
			ps->stat = PAYLOAD_STAT_INIT;
		}
	}

	/* 対象PIDかどうかチェック */
	if (ps->pid != tsh->pid) {
		return;
	}

	/* パケットの処理 */
	if (ps->stat == PAYLOAD_STAT_INIT) {
		if (!tsh->payload_unit_start_indicator) {
			//printf("pass!\n");
			return;
		}
		ps->stat = PAYLOAD_STAT_PROC;
		ps->n_payload = ts_get_section_length(packet, tsh) + 3;
		ps->recv_payload = ps->n_next_payload = ps->next_recv_payload = 0;
		ps->continuity_counter = tsh->continuity_counter;

		pos = tsh->payload_data_pos;

		remain = 188 - pos;
		if (remain > ps->n_payload) {
			remain = ps->n_payload;
			ps->stat = PAYLOAD_STAT_FINISHED;
		}
		memcpy(ps->payload, &packet[pos], remain);
		ps->recv_payload += remain;
	} else if (ps->stat == PAYLOAD_STAT_PROC) {
		/* continuity_counter の連続性を確認 */
		if ((ps->continuity_counter + 1) % 16 != tsh->continuity_counter) {
			/* drop! */
			output_message(MSG_PACKETERROR, TSD_TEXT("packet continuity_counter is discontinuous! (pid=0x%02x)"), ps->pid);
			ps->n_payload = ps->recv_payload = 0;
			ps->stat = PAYLOAD_STAT_INIT;
			return;
		}
		ps->continuity_counter = tsh->continuity_counter;

		if (tsh->payload_unit_start_indicator) {
			pos = tsh->payload_pos;
			pointer_field = tsh->pointer_field;
			pos++;

			/* 不正なパケットかどうかのチェック */
			if (pos + pointer_field >= 188) {
				ps->stat = PAYLOAD_STAT_INIT;
				output_message(MSG_PACKETERROR, TSD_TEXT("Invalid payload data_byte offset! (pid=0x%02x)"), ps->pid);
				return;
			}

			ps->n_next_payload = ts_get_section_length(packet, tsh) + 3;
			ps->next_recv_payload = 188 - pos - pointer_field;
			if (ps->next_recv_payload > ps->n_next_payload) {
				ps->next_recv_payload = ps->n_next_payload;
			}
			memcpy(ps->next_payload, &packet[pos+pointer_field], ps->next_recv_payload);
			ps->stat = PAYLOAD_STAT_FINISHED;

			remain = pointer_field;
		} else {
			pos = tsh->payload_data_pos;
			/* 不正なパケットかどうかのチェック */
			if (pos > 188) {
				ps->stat = PAYLOAD_STAT_INIT;
				output_message(MSG_PACKETERROR, TSD_TEXT("Invalid payload data_byte offset! (pid=0x%02x)"), ps->pid);
				return;
			}

			remain = 188 - pos;
		}

		if (remain >= ps->n_payload - ps->recv_payload) {
			remain = ps->n_payload - ps->recv_payload;
			ps->stat = PAYLOAD_STAT_FINISHED;
		}
		memcpy(&(ps->payload[ps->recv_payload]), &packet[pos], remain);
		ps->recv_payload += remain;
	}

	if (ps->stat == PAYLOAD_STAT_FINISHED) {
		ps->crc32 = get_payload_crc32(ps);
		uint32_t crc = crc32(ps->payload, ps->n_payload - 4);
		if (ps->crc32 != crc) {
			ps->stat = PAYLOAD_STAT_INIT;
			output_message(MSG_PACKETERROR, TSD_TEXT("Payload CRC32 mismatch! (pid=0x%02x)"), ps->pid);
		}
	}
}

void clear_proginfo_all(proginfo_t *proginfo)
{
	/* 最低限のものを除いたオールクリア */
	/* PAT、PMT、SDTの取得状況はイベントの切り替わりと無関係なのでクリアしない */
	/* TOTとPCRも同様 */
	proginfo->status &= (PGINFO_GET_PAT | PGINFO_GET_PMT | PGINFO_GET_SERVICE_INFO | PGINFO_TIMEINFO);
	proginfo->last_desc = -1;
}

void init_proginfo(proginfo_t *proginfo)
{
	proginfo->status = 0;
	proginfo->last_desc = -1;
	proginfo->PCR_base = 0;
	proginfo->PCR_wraparounded = 0;
}

int parse_EIT_Sed(const uint8_t *desc, Sed_t *sed)
{
	const uint8_t *desc_end;

	//sed->descriptor_tag					= desc[0];
	sed->descriptor_length				= desc[1];

	desc_end							= &desc[2 + sed->descriptor_length];

	memcpy(sed->ISO_639_language_code,	  &desc[2], 3);
	sed->ISO_639_language_code[3]		= '\0';
	
	sed->event_name_length				= desc[5];
	sed->event_name_char				= &desc[6];
	if ( &sed->event_name_char[sed->event_name_length] >= desc_end ) {
		return 0;
	}

	sed->text_length					= sed->event_name_char[sed->event_name_length];
	sed->text_char						= &sed->event_name_char[sed->event_name_length + 1];
	if ( &sed->text_char[sed->text_length] > desc_end ) {
		return 0;
	}
	return 1;
}

int parse_EIT_Eed(const uint8_t *desc, Eed_t *eed)
{
	const uint8_t *desc_end;

	//eed->descriptor_tag					= desc[0];
	eed->descriptor_length				= desc[1];
	desc_end							= &desc[2+eed->descriptor_length];

	eed->descriptor_number				= get_bits(desc, 16, 4);
	eed->last_descriptor_number			= get_bits(desc, 20, 4);
	memcpy(eed->ISO_639_language_code,    &desc[3], 3);
	eed->ISO_639_language_code[3]		= '\0';
	eed->length_of_items				= desc[6];

	if ( &desc[7+eed->length_of_items] >= desc_end ) {
		return 0;
	}

	eed->text_length					= desc[7 + eed->length_of_items];
	eed->text_char						= &desc[8 + eed->length_of_items];
	if ( &eed->text_char[eed->text_length] > desc_end ) {
		return 0;
	}

	return 1;
}

int parse_EIT_Eed_item(const uint8_t *item, const uint8_t *item_end, Eed_item_t *eed_item)
{
	eed_item->item_description_length	= item[0];
	eed_item->item_description_char		= &item[1];
	if ( &eed_item->item_description_char[ eed_item->item_description_length ] >= item_end) {
		return 0;
	}

	eed_item->item_length				= eed_item->item_description_char[eed_item->item_description_length];
	eed_item->item_char					= &eed_item->item_description_char[eed_item->item_description_length+1];
	if ( &eed_item->item_char[eed_item->item_length] > item_end) {
		return 0;
	}
	return 1;
}

void parse_EIT_header(const uint8_t *payload, EIT_header_t *eit)
{
	eit->table_id						= payload[0];
	eit->section_syntax_indicator		= get_bits(payload, 8, 1);
	eit->section_length					= get_bits(payload, 12, 12);
	eit->service_id						= get_bits(payload, 24, 16);
	eit->version_number					= get_bits(payload, 42, 5);
	eit->current_next_indicator			= get_bits(payload, 47, 1);
	eit->section_number					= get_bits(payload, 48, 8);
	eit->last_section_number			= get_bits(payload, 56, 8);
	eit->transport_stream_id			= get_bits(payload, 64, 16);
	eit->original_network_id			= get_bits(payload, 80, 16);
	eit->segment_last_section_number	= get_bits(payload, 96, 8);
	eit->last_table_id					= get_bits(payload, 104, 8);
}

void parse_EIT_body(const uint8_t *body, EIT_body_t *eit_b)
{
	eit_b->event_id						= get_bits(body, 0, 16);
	eit_b->start_time_mjd				= get_bits(body, 16, 16);
	eit_b->start_time_jtc				= get_bits(body, 32, 24);
	eit_b->duration						= get_bits(body, 56, 24);
	eit_b->running_status				= get_bits(body, 80, 3);
	eit_b->free_CA_mode					= get_bits(body, 83, 1);
	eit_b->descriptors_loop_length		= get_bits(body, 84, 12);
}

void store_EIT_Sed(const Sed_t *sed, proginfo_t *proginfo)
{
	proginfo->event_name.aribstr_len = sed->event_name_length;
	memcpy(proginfo->event_name.aribstr, sed->event_name_char, sed->event_name_length);
	proginfo->event_text.aribstr_len = sed->text_length;
	memcpy(proginfo->event_text.aribstr, sed->text_char, sed->text_length);

	proginfo->event_name.str_len = 
		AribToString(proginfo->event_name.str, sizeof(proginfo->event_name.str),
					proginfo->event_name.aribstr, proginfo->event_name.aribstr_len);

	proginfo->event_text.str_len =
		AribToString(proginfo->event_text.str, sizeof(proginfo->event_text.str),
			proginfo->event_text.aribstr, proginfo->event_text.aribstr_len);
	proginfo->status |= PGINFO_GET_SHORT_TEXT;
}

void store_EIT_body(const EIT_body_t *eit_b, proginfo_t *proginfo)
{
	if (proginfo->status & PGINFO_GET_EVENT_INFO && proginfo->event_id != eit_b->event_id) {
		/* 前回の取得から番組が切り替わった */
		clear_proginfo_all(proginfo);
	}
	proginfo->event_id = eit_b->event_id;

	/* EITでは使われない項目 */
	proginfo->start.usec = 0;
	proginfo->dur.sign = 1;
	proginfo->dur.day = 0;
	proginfo->dur.usec = 0;

	if (eit_b->start_time_mjd == 0xffff && eit_b->start_time_jtc == 0xffffff) {
		proginfo->start.mjd = 0;
		proginfo->start.year = 0;
		proginfo->start.mon = 0;
		proginfo->start.day = 0;
		proginfo->start.hour = 0;
		proginfo->start.min = 0;
		proginfo->start.sec = 0;
		proginfo->status |= PGINFO_UNKNOWN_STARTTIME;
	} else {
		proginfo->start.mjd = eit_b->start_time_mjd;
		mjd_to_ymd(proginfo->start.mjd, &proginfo->start.year, &proginfo->start.mon, &proginfo->start.day);
		proginfo->start.hour = (eit_b->start_time_jtc >> 20 & 0x0f) * 10 +
			((eit_b->start_time_jtc >> 16) & 0x0f);
		proginfo->start.min = (eit_b->start_time_jtc >> 12 & 0x0f) * 10 +
			((eit_b->start_time_jtc >> 8) & 0x0f);
		proginfo->start.sec = (eit_b->start_time_jtc >> 4 & 0x0f) * 10 +
			(eit_b->start_time_jtc & 0x0f);
		if (proginfo->start.hour >= 24) { proginfo->start.hour = 23; }
		if (proginfo->start.min >= 60) { proginfo->start.min = 59; }
		if (proginfo->start.sec >= 60) { proginfo->start.sec = 59; }

		proginfo->status &= ~PGINFO_UNKNOWN_STARTTIME;
	}

	if (eit_b->duration == 0xffffff) {
		proginfo->dur.hour = 0;
		proginfo->dur.min = 0;
		proginfo->dur.sec = 0;
		proginfo->status |= PGINFO_UNKNOWN_DURATION;
	} else {
		proginfo->dur.hour = (eit_b->duration >> 20 & 0x0f) * 10 +
			((eit_b->duration >> 16) & 0x0f);
		proginfo->dur.min = (eit_b->duration >> 12 & 0x0f) * 10 +
			((eit_b->duration >> 8) & 0x0f);
		proginfo->dur.sec = (eit_b->duration >> 4 & 0x0f) * 10 +
			(eit_b->duration & 0x0f);
		if (proginfo->dur.hour >= 24) { proginfo->dur.hour = 23; }
		if (proginfo->dur.min >= 60) { proginfo->dur.min = 59; }
		if (proginfo->dur.sec >= 60) { proginfo->dur.sec = 59; }
		proginfo->status &= ~PGINFO_UNKNOWN_DURATION;
	}

	proginfo->status |= PGINFO_GET_EVENT_INFO;
}

void store_EIT_Eed_item(const Eed_t *eed, const Eed_item_t *eed_item, proginfo_t *proginfo)
{
	int i;
	int item_len;
	Eed_itemset_t *curr_item;

	if (proginfo->last_desc != -1) {
		/* 連続性チェック */
		if (proginfo->curr_desc == (int)eed->descriptor_number) {
			if (eed_item->item_description_length > 0) {
				/* 前回と同じdescriptor_numberで項目名があるので不連続 */
				proginfo->last_desc = -1;
			} else {
				/* 前回の続き */
				proginfo->curr_desc = eed->descriptor_number;
			}
		} else if( proginfo->curr_desc + 1 == (int)eed->descriptor_number ) {
			/* 前回の続き */
			proginfo->curr_desc = eed->descriptor_number;
		} else {
			/* 不連続 */
			proginfo->last_desc = -1;
		}
	}

	/* 初期状態から */
	if (proginfo->last_desc == -1) {
		if (eed->descriptor_number == 0 && eed_item->item_description_length > 0) {
			proginfo->curr_desc = 0;
			proginfo->last_desc = eed->last_descriptor_number;
			proginfo->n_items = 0;
		} else {
			return;
		}
	}

	if (eed_item->item_description_length > 0) {
		/* 新規項目 */
		curr_item = &proginfo->items[proginfo->n_items];
		if (proginfo->n_items < sizeof(proginfo->items) / sizeof(proginfo->items[0])) {
			proginfo->n_items++;
			if (eed_item->item_description_length <= sizeof(curr_item->desc.aribstr)) {
				curr_item->desc.aribstr_len = eed_item->item_description_length;
			} else {
				/* サイズオーバーなので切り詰める */
				curr_item->desc.aribstr_len = sizeof(curr_item->desc.aribstr);
			}
			memcpy(curr_item->desc.aribstr, eed_item->item_description_char, eed_item->item_description_length);
			curr_item->item.aribstr_len = 0;
		} else {
			/* これ以上itemを追加できない */
			return;
		}
	} else {
		/* 前回の項目の続き */
		if (proginfo->n_items == 0) {
			//curr_item = &proginfo->items[0];
			return;
		} else {
			curr_item = &proginfo->items[proginfo->n_items-1];
		}
	}

	item_len = curr_item->item.aribstr_len + eed_item->item_length;
	if ( item_len > sizeof(curr_item->item.aribstr) ) {
		/* サイズオーバーなので切り詰める */
		item_len = sizeof(curr_item->item.aribstr);
	}
	memcpy(&curr_item->item.aribstr[curr_item->item.aribstr_len], eed_item->item_char, eed_item->item_length);
	curr_item->item.aribstr_len = item_len;

	if (proginfo->curr_desc == proginfo->last_desc) {
		for (i = 0; i < proginfo->n_items; i++) {
			proginfo->items[i].desc.str_len = AribToString(
					proginfo->items[i].desc.str,
					sizeof(proginfo->items[i].desc.str),
					proginfo->items[i].desc.aribstr,
					proginfo->items[i].desc.aribstr_len
				);
			proginfo->items[i].item.str_len = AribToString(
					proginfo->items[i].item.str,
					sizeof(proginfo->items[i].item.str),
					proginfo->items[i].item.aribstr,
					proginfo->items[i].item.aribstr_len
				);
		}
		proginfo->status |= PGINFO_GET_EXTEND_TEXT;
	}
}

void parse_EIT_Cd(const uint8_t *desc, Cd_t *cd)
{
	int i;
	cd->n_items							= desc[1] / 2;
	if (cd->n_items > 8) {
		cd->n_items = 8;
	}

	for (i = 0; i < cd->n_items; i++) {
		cd->items[i].content_nibble_level_1 = get_bits(&desc[2+i*2], 0, 4);
		cd->items[i].content_nibble_level_2 = get_bits(&desc[2+i*2], 4, 4);
		cd->items[i].user_nibble_1 = get_bits(&desc[2+i*2], 8, 4);
		cd->items[i].user_nibble_2 = get_bits(&desc[2+i*2], 12, 4);
	}
}

void parse_PCR(const uint8_t *packet, const ts_header_t *tsh, void *param, service_callback_handler_t handler)
{
	int get = 0, wraparounded;
	const uint8_t *p;
	uint64_t PCR_base = 0;
	int64_t offset;
	unsigned int PCR_ext = 0;
	proginfo_t *current_proginfo;

	if ( !(tsh->adaptation_field_control & 0x02) ) {
		return;
	}

	p = &packet[tsh->adaptation_field_pos];
	if ( !get_bits(p, 3, 1) ) {
		/* no PCR */
		return;
	}

	current_proginfo = handler(param, tsh->pid);
	if (!current_proginfo) {
		return;
	}

	if (tsh->pid == current_proginfo->PCR_pid) {
		if (!get) {
			PCR_base = get_bits64(p, 8, 33);
			PCR_ext = get_bits(p, 47, 9);
			get = 1;
		}
		offset = (int64_t)PCR_base - (int64_t)current_proginfo->PCR_base;
		wraparounded = 0;
		if (offset < 0) {
			/* wrap-around対策 */
			offset += PCR_BASE_MAX;
			wraparounded = 1;
		}

		if( offset < 1*PCR_BASE_HZ ) {
			current_proginfo->status |= PGINFO_VALID_PCR;
			current_proginfo->status |= PGINFO_PCR_UPDATED;
			//output_message(MSG_DISP, TSD_TEXT("PCR %x: %"PRId64" %I64x %d %d"),
			//	sl->proginfos[i].service_id, PCR_base, PCR_base, PCR_ext, wraparounded);
		} else {
			/* 前のPCRから1秒以上差があれば有効とは見なさない */
			current_proginfo->status &= ~PGINFO_VALID_PCR;
		}
		current_proginfo->PCR_base = PCR_base;
		current_proginfo->PCR_ext = PCR_ext;
		current_proginfo->PCR_wraparounded |= wraparounded;
	}
}

void store_TOT(proginfo_t *proginfo, const time_mjd_t *TOT_time)
{
	proginfo->TOT_time = *TOT_time;
	if (proginfo->status & PGINFO_VALID_PCR) {
		proginfo->TOT_PCR = proginfo->PCR_base;
		proginfo->status |= PGINFO_VALID_TOT_PCR;
	} else {
		proginfo->status &= ~PGINFO_VALID_TOT_PCR;
	}
	proginfo->PCR_wraparounded = 0;
	proginfo->status |= PGINFO_GET_TOT;
}

void parse_TOT_TDT(const uint8_t *packet, const ts_header_t *tsh, PSI_parse_t *TOT_payload, void *param, tot_callback_handler_t handler)
{
	unsigned int slen, mjd;
	uint8_t tid;
	uint32_t bcd_jst;
	time_mjd_t TOT_time;

	parse_PSI(packet, tsh, TOT_payload);
	if (TOT_payload->stat != PAYLOAD_STAT_FINISHED || TOT_payload->n_payload < 8) {
		return;
	}

	tid = TOT_payload->payload[0];
	slen = get_bits(TOT_payload->payload, 12, 12);
	if (tid == 0x70) {
		/* TDT */
		if (slen != 5) {  return; }
	} else if (tid == 0x73) {
		/* TOT */
		if (slen < 5) { return; }
	} else { return; }

	mjd = get_bits(TOT_payload->payload, 24, 16);
	bcd_jst= get_bits(TOT_payload->payload, 40, 24);

	mjd_to_ymd(mjd, &TOT_time.year, &TOT_time.mon, &TOT_time.day);
	TOT_time.hour = (bcd_jst >> 20 & 0x0f) * 10 + ((bcd_jst >> 16) & 0x0f);
	TOT_time.min = (bcd_jst >> 12 & 0x0f) * 10 + ((bcd_jst >> 8) & 0x0f);
	TOT_time.sec = (bcd_jst >> 4 & 0x0f) * 10 + (bcd_jst & 0x0f);
	TOT_time.usec = 0;

	//output_message( MSG_DISP, TSD_TEXT("TOT %04d/%02d/%02d %02d:%02d:%02d"), year, mon, day, hour, min, sec );

	handler(param, &TOT_time);
}

void parse_EIT(PSI_parse_t *payload_stat, const uint8_t *packet, const ts_header_t *tsh, void *param, eit_callback_handler_t handler)
{
	int len;
	EIT_header_t eit_h;
	EIT_body_t eit_b;
	uint8_t *p_eit_b, *p_eit_end;
	uint8_t *p_desc, *p_desc_end;
	uint8_t dtag, dlen;
	proginfo_t *curr_proginfo;

	parse_PSI(packet, tsh, payload_stat);

	if (payload_stat->stat != PAYLOAD_STAT_FINISHED || payload_stat->payload[0] != 0x4e) {
		return;
	}

	parse_EIT_header(payload_stat->payload, &eit_h);

	/* コールバック関数を呼び、取得対象の番組情報かどうかチェックする */
	curr_proginfo = handler(param, &eit_h);
	if(!curr_proginfo) {
		return;
	}

	len = payload_stat->n_payload - 14 - 4/*=sizeof(crc32)*/;
	p_eit_b = &payload_stat->payload[14];
	p_eit_end = &p_eit_b[len];
	while(&p_eit_b[12] < p_eit_end) {
		parse_EIT_body(p_eit_b, &eit_b); /* read 12bytes */
		store_EIT_body(&eit_b, curr_proginfo);

		p_desc = &p_eit_b[12];
		p_desc_end = &p_desc[eit_b.descriptors_loop_length];
		if (p_desc_end > p_eit_end) {
			break;
		}

		while( p_desc < p_desc_end ) {
			dtag = p_desc[0];
			dlen = p_desc[1];
			if ( &p_desc[2+dlen] > p_desc_end ) {
				break;
			}

			if (dtag == 0x4d) {
				Sed_t sed;
				if (parse_EIT_Sed(p_desc, &sed)) {
					store_EIT_Sed(&sed, curr_proginfo);
					if (PGINFO_READY(curr_proginfo->status)) {
						curr_proginfo->status |= PGINFO_READY_UPDATED;
					}
				}
			} else if (dtag == 0x4e) {
				Eed_t eed;
				Eed_item_t eed_item;
				uint8_t *p_eed_item, *p_eed_item_end;
				if (parse_EIT_Eed(p_desc, &eed)) {
					p_eed_item = &p_desc[7];
					p_eed_item_end = &p_eed_item[eed.length_of_items];
					while (p_eed_item < p_eed_item_end) {
						if (parse_EIT_Eed_item(p_eed_item, p_eed_item_end, &eed_item)) {

							/* ARIB TR-B14 第四編 地上デジタルテレビジョン放送 PSI/SI 運用規定 12.2 セクションへの記述子の配置 は、
							Eedが複数のEITにまたがって送信されることは無いことを規定していると解釈できる。
							よってparse_EIT()を抜けたときはcurr_proginfo->itemsに全てのアイテムが収納されていることが保障される。
							仮にこの仮定が満たされない場合でも、単に中途半端な番組情報が見えてしまうタイミングが存在するだけである */
							store_EIT_Eed_item(&eed, &eed_item, curr_proginfo);
						}
						p_eed_item += ( 2 + eed_item.item_description_length + eed_item.item_length );
					}
				}
			} else if (dtag == 0x54) {
				parse_EIT_Cd(p_desc, &curr_proginfo->genre_info);
				curr_proginfo->status |= PGINFO_GET_GENRE;
			}

			p_desc += (2+dlen);
		}
		p_eit_b = p_desc_end;
	}
}

int parse_SDT_Sd(const uint8_t *desc, Sd_t *sd)
{
	//sd->descriptor_tag					= desc[0];
	sd->descriptor_length				= desc[1];
	sd->service_type					= desc[2];

	sd->service_provider_name_length	= desc[3];
	sd->service_provider_name_char		= &desc[4];
	if ( &sd->service_provider_name_char[ sd->service_provider_name_length ] >
			&desc[ 2 + sd->descriptor_length ] ) {
		return 0;
	}

	sd->service_name_length				= sd->service_provider_name_char[ sd->service_provider_name_length ];
	sd->service_name_char				= &sd->service_provider_name_char[ sd->service_provider_name_length + 1 ];
	if ( &sd->service_name_char[ sd->service_name_length ] >
			&desc[ 2 + sd->descriptor_length ] ) {
		return 0;
	}
	return 1;
}

void parse_SDT_header(const uint8_t *payload, SDT_header_t *sdt_h)
{
	sdt_h->table_id						= payload[0];
	sdt_h->section_syntax_indicator 	= get_bits(payload,  8,  1);
	sdt_h->section_length 				= get_bits(payload, 12, 12);
	sdt_h->transport_stream_id 			= get_bits(payload, 24, 16);
	sdt_h->version_number 				= get_bits(payload, 42,  5);
	sdt_h->current_next_indicator 		= get_bits(payload, 47,  1);
	sdt_h->section_number 				= get_bits(payload, 48,  8);
	sdt_h->last_section_number 			= get_bits(payload, 56,  8);
	sdt_h->original_network_id 			= get_bits(payload, 64, 16);
}

void parse_SDT_body(const uint8_t *body, SDT_body_t *sdt_b)
{
	sdt_b->service_id					= get_bits(body,  0, 16);
	sdt_b->EIT_user_defined_flags		= get_bits(body, 19,  3);
	sdt_b->EIT_schedule_flag			= get_bits(body, 22,  1);
	sdt_b->EIT_present_following_flag	= get_bits(body, 23,  1);
	sdt_b->running_status				= get_bits(body, 24,  3);
	sdt_b->free_CA_mode					= get_bits(body, 27,  1);
	sdt_b->descriptors_loop_length 		= get_bits(body, 28, 12);
}

void store_SDT(const SDT_header_t *sdt_h, const Sd_t *sd, proginfo_t *proginfo)
{
	proginfo->network_id = sdt_h->original_network_id;
	proginfo->ts_id = sdt_h->transport_stream_id;

	proginfo->service_name.aribstr_len = sd->service_name_length;
	memcpy(proginfo->service_name.aribstr, sd->service_name_char, sd->service_name_length);
	proginfo->service_provider_name.aribstr_len = sd->service_provider_name_length;
	memcpy(proginfo->service_provider_name.aribstr, sd->service_provider_name_char, sd->service_provider_name_length);

	proginfo->service_name.str_len =
		AribToString(proginfo->service_name.str, sizeof(proginfo->service_name.str),
			proginfo->service_name.aribstr, proginfo->service_name.aribstr_len);

	proginfo->service_provider_name.str_len =
		AribToString(proginfo->service_provider_name.str, sizeof(proginfo->service_provider_name.str),
			proginfo->service_provider_name.aribstr, proginfo->service_provider_name.aribstr_len);

	proginfo->status |= PGINFO_GET_SERVICE_INFO;
}

void parse_SDT(PSI_parse_t *payload_stat, const uint8_t *packet, const ts_header_t *tsh, void *param, service_callback_handler_t handler)
{
	int len;
	SDT_header_t sdt_h;
	SDT_body_t sdt_b;
	Sd_t sd;
	uint8_t *p_sdt_b, *p_sdt_end;
	uint8_t *p_desc, *p_desc_end;
	uint8_t dtag, dlen;
	proginfo_t *curr_proginfo;

	parse_PSI(packet, tsh, payload_stat);

	if (payload_stat->stat != PAYLOAD_STAT_FINISHED || payload_stat->payload[0] != 0x42) {
		return;
	}

	parse_SDT_header(payload_stat->payload, &sdt_h);

	len = payload_stat->n_payload - 11 - 4/*=sizeof(crc32)*/;
	p_sdt_b = &payload_stat->payload[11];
	p_sdt_end = &p_sdt_b[len];
	while(&p_sdt_b[5] < p_sdt_end) {
		parse_SDT_body(p_sdt_b, &sdt_b); /* read 5bytes */

		p_desc = &p_sdt_b[5];
		p_desc_end = &p_desc[sdt_b.descriptors_loop_length];
		if (p_desc_end > p_sdt_end) {
			break;
		}

		/* 対象のサービスIDかどうか */
		curr_proginfo = handler(param, sdt_b.service_id);
		if (curr_proginfo) {
			while( p_desc < p_desc_end ) {
				dtag = p_desc[0];
				dlen = p_desc[1];
				if (dtag == 0x48) {
					if (parse_SDT_Sd(p_desc, &sd)) {
						store_SDT(&sdt_h, &sd, curr_proginfo);
					}
				}
				p_desc += (2+dlen);
			}
		}
		p_sdt_b = p_desc_end;
	}
}

/* PMT: ISO 13818-1 2.4.4.8 Program Map Table */
void parse_PMT(const uint8_t *packet, const ts_header_t *tsh, PSI_parse_t *PMT_payload, proginfo_t *proginfo)
{
	int pos, n_pids, len;
	uint16_t pid;
	uint8_t stream_type;
	uint8_t *payload;

	parse_PSI(packet, tsh, PMT_payload);
	if (PMT_payload->stat != PAYLOAD_STAT_FINISHED) {
		return;
	}

	len = PMT_payload->n_payload - 4/*crc32*/;
	payload = PMT_payload->payload;
	proginfo->PCR_pid = get_bits(payload, 67, 13);
	pos = 12 + get_bits(payload, 84, 12);
	n_pids = 0;
	while ( pos < len && n_pids <= MAX_PIDS_PER_SERVICE ) {
		stream_type = payload[pos];
		pid = (uint16_t)get_bits(payload, pos*8+11, 13);
		pos += get_bits(payload, pos*8+28, 12) + 5;
		proginfo->service_pids[n_pids].stream_type = stream_type;
		proginfo->service_pids[n_pids].pid = pid;
		n_pids++;
	}
	proginfo->n_service_pids = n_pids;
	PMT_payload->stat = PAYLOAD_STAT_INIT;
	proginfo->status |= PGINFO_GET_PMT;
}

void parse_PAT(PSI_parse_t *PAT_payload, const uint8_t *packet, const ts_header_t *tsh, void *param, pat_callback_handler_t handler)
{
	int i, n;
	PAT_item_t pat_item;
	uint8_t *payload;

	parse_PSI(packet, tsh, PAT_payload);
	if (PAT_payload->stat == PAYLOAD_STAT_FINISHED) {
		n = (PAT_payload->n_payload - 4/*crc32*/ - 8/*fixed length*/) / 4;
		payload = &(PAT_payload->payload[8]);
		for (i = 0; i < n; i++) {
			pat_item.program_number = get_bits(payload, i * 32, 16);
			pat_item.pid = get_bits(payload, i * 32 + 19, 13);
			handler(param, n, i, &pat_item);
		}
	}
}

void store_PAT(proginfo_t *proginfo, const PAT_item_t *PAT_item)
{
	proginfo->service_id = PAT_item->program_number;
	proginfo->status |= PGINFO_GET_PAT;
}
