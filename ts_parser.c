#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/timeb.h>

typedef wchar_t			WCHAR;
typedef long			BOOL;
typedef unsigned long	DWORD;

#include "module_def.h"
#include "ts_proginfo.h"
#include "module_hooks.h"
#include "ts_parser.h"
#include "aribstr.h"
#include "tsdump.h"

const WCHAR *genre_main[] = {
	L"ニュース／報道",			L"スポーツ",	L"情報／ワイドショー",	L"ドラマ",
	L"音楽",					L"バラエティ",	L"映画",				L"アニメ／特撮",
	L"ドキュメンタリー／教養",	L"劇場／公演",	L"趣味／教育",			L"福祉",
	L"予備",					L"予備",		L"拡張",				L"その他"
};

const WCHAR *genre_detail[] = {
	/* 0x0 */
	L"定時・総合", L"天気", L"特集・ドキュメント", L"政治・国会", L"経済・市況", L"海外・国際", L"解説", L"討論・会談",
	L"報道特番", L"ローカル・地域", L"交通", L"-", L"-", L"-", L"-", L"その他",

	/* 0x1 */
	L"スポーツニュース", L"野球", L"サッカー", L"ゴルフ", L"その他の球技", L"相撲・格闘技", L"オリンピック・国際大会", L"マラソン・陸上・水泳",
	L"モータースポーツ", L"マリン・ウィンタースポーツ", L"競馬・公営競技", L"-", L"-", L"-", L"-", L"その他",

	/* 0x2 */
	L"芸能・ワイドショー", L"ファッション", L"暮らし・住まい", L"健康・医療", L"ショッピング・通販", L"グルメ・料理", L"イベント", L"番組紹介・お知らせ",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

	/* 0x3 */
	L"国内ドラマ", L"海外ドラマ", L"時代劇", L"-", L"-", L"-", L"-", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

	/* 0x4 */
	L"国内ロック・ポップス", L"海外ロック・ポップス", L"クラシック・オペラ", L"ジャズ・フュージョン", L"歌謡曲・演歌", L"ライブ・コンサート", L"ランキング・リクエスト", L"カラオケ・のど自慢",
	L"民謡・邦楽", L"童謡・キッズ", L"民族音楽・ワールドミュージック", L"-", L"-", L"-", L"-", L"その他",

	/* 0x5 */
	L"クイズ", L"ゲーム", L"トークバラエティ", L"お笑い・コメディ", L"音楽バラエティ", L"旅バラエティ", L"料理バラエティ", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

	/* 0x6 */
	L"洋画", L"邦画", L"アニメ", L"-", L"-", L"-", L"-", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

	/* 0x7 */
	L"国内アニメ", L"海外アニメ", L"特撮", L"-", L"-", L"-", L"-", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

	/* 0x8 */
	L"社会・時事", L"歴史・紀行", L"自然・動物・環境", L"宇宙・科学・医学", L"カルチャー・伝統芸能", L"文学・文芸", L"スポーツ", L"ドキュメンタリー全般",
	L"インタビュー・討論", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

	/* 0x9 */
	L"現代劇・新劇", L"ミュージカル", L"ダンス・バレエ", L"落語・演芸", L"歌舞伎・古典", L"-", L"-", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

	/* 0xA */
	L"旅・釣り・アウトドア", L"園芸・ペット・手芸", L"音楽・美術・工芸", L"囲碁・将棋", L"麻雀・パチンコ", L"車・オートバイ", L"コンピュータ・ＴＶゲーム", L"会話・語学",
	L"幼児・小学生", L"中学生・高校生", L"大学生・受験", L"生涯教育・資格", L"教育問題", L"-", L"-", L"その他",

	/* 0xB */
	L"高齢者", L"障害者", L"社会福祉", L"ボランティア", L"手話", L"文字（字幕）", L"音声解説", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

	/* 0xC */
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",

	/* 0xD */
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",

	/* 0xE */
	L"BS/地上デジタル放送用番組付属情報", L"広帯域CSデジタル放送用拡張", L"衛星デジタル音声放送用拡張", L"サーバー型番組付属情報", L"IP放送用番組付属情報", L"-", L"-", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",

	/* 0xF */
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他"
};

const WCHAR *genre_user[] = {
	L"中止の可能性あり",
	L"延長の可能性あり",
	L"中断の可能性あり",
	L"同一シリーズの別話数放送の可能性あり",
	L"編成未定枠",
	L"繰り上げの可能性あり",
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",

	L"中断ニュースあり", L"当該イベントに関連する臨時サービスあり"
	L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-"
};

void get_genre_str(const WCHAR **genre1, const WCHAR **genre2, Cd_t_item item)
{
	if (item.content_nibble_level_1 != 0xe) {
		*genre1 = genre_main[item.content_nibble_level_1];
		*genre2 = genre_detail[item.content_nibble_level_1 * 0x10 + item.content_nibble_level_2];
	} else {
		*genre1 = genre_detail[item.content_nibble_level_1 * 0x10 + item.content_nibble_level_2];
		if (item.user_nibble_1 <= 0x01) {
			*genre2 = genre_user[item.user_nibble_1 * 0x10 + item.user_nibble_2];
		} else {
			*genre2 = L"-";
		}
	}
}

int get_extended_text(WCHAR *dst, size_t n, const proginfo_t *pi)
{
	int i;
	WCHAR *p = dst, *end = &dst[n - 1];

	*p = L'\0';
	if (!(pi->status & PGINFO_GET_EXTEND_TEXT)) {
		return 0;
	}

	for (i = 0; i < pi->n_items && p < end; i++) {
		wcscpy_s(p, end - p, pi->items[i].desc);
		while (*p != L'\0') { p++; }
		wcscpy_s(p, end - p, L"\n");
		while (*p != L'\0') { p++; }
		wcscpy_s(p, end - p, pi->items[i].item);
		while (*p != L'\0') { p++; }
		wcscpy_s(p, end - p, L"\n");
		while (*p != L'\0') { p++; }
	}
	return 1;
}

int proginfo_cmp(const proginfo_t *pi1, const proginfo_t *pi2)
{
	WCHAR et1[4096], et2[4096];
	int i;

	if ((pi1->status&PGINFO_GET_ALL) != (pi2->status&PGINFO_GET_ALL)) {
		return 1;
	}

	if (pi1->dur_hour != pi2->dur_hour ||
		pi1->dur_min != pi2->dur_min ||
		pi1->dur_sec != pi2->dur_sec ||
		pi1->start_hour != pi2->start_hour ||
		pi1->start_min != pi2->start_min ||
		pi1->start_sec != pi2->start_sec ||
		pi1->start_year != pi2->start_year ||
		pi1->start_month != pi2->start_month ||
		pi1->start_day != pi2->start_day) {
		return 1;
	}

	if ( pi1->status & PGINFO_GET_SHORT_TEXT ) {
		if (wcscmp(pi1->event_text.str, pi2->event_text.str) != 0) {
			return 1;
		}
	}

	if ( pi1->status & PGINFO_GET_GENRE ) {
		if( pi1->genre_info.n_items != pi2->genre_info.n_items ) {
			return 1;
		}
		for (i = 0; i < pi1->genre_info.n_items; i++) {
			if ( memcmp(&pi1->genre_info.items[i], 
						&pi2->genre_info.items[i],
						sizeof(pi1->genre_info.items[i]) ) != 0 ) {
				return 1;
			}
		}
	}

	if ( get_extended_text(et1, sizeof(et1) / sizeof(WCHAR), pi1) !=
		get_extended_text(et2, sizeof(et2) / sizeof(WCHAR), pi2) ) {
		return 1;
	}
	if (wcscmp(et1, et2) != 0) {
		return 1;
	}

	return 0;
}

static const char *get_stream_type_str(int stream_type) {
	static const char *table[] = { "reserved", "video", "video", "audio", "audio",
		"private_sections", "private data", "MHEG", "DSM-CC", "H.222.1",
		"typeA", "typeB", "typeC", "typeD", "auxiliary", "audio" };
	if (stream_type <= 0xf) {
		return table[stream_type];
	}
	else {
		return "unkonwn";
	}
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

int get_stream_timestamp(const proginfo_t *pi, JST_time_t *jst_time, unsigned int *p_usec)
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

	usec = diff_pcr * 1000 * 1000 / PCR_BASE_HZ;
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

	if (p_usec) {
		*p_usec = (unsigned int)usec;
	}

	return 1;
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
			output_message(MSG_PACKETERROR, L"packet continuity_counter is discontinuous! (pid=0x%02x)", ps->pid);
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
				output_message(MSG_PACKETERROR, L"Invalid payload data_byte offset! (pid=0x%02x)", ps->pid);
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
				output_message(MSG_PACKETERROR, L"Invalid payload data_byte offset! (pid=0x%02x)", ps->pid);
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
		unsigned __int32 crc = crc32(ps->payload, ps->n_payload - 4);
		if (ps->crc32 != crc) {
			ps->stat = PAYLOAD_STAT_INIT;
			output_message(MSG_PACKETERROR, L"Payload CRC32 mismatch! (pid=0x%02x)", ps->pid);
		}
	}
}

void clear_proginfo_all(proginfo_t *proginfo)
{
	/* 最低限のものを除いたオールクリア */
	/* PAT、PMTの取得状況はイベントの切り替わりと無関係なのでクリアしない */
	/* TOTとPCRも同様 */
	proginfo->status &= (PGINFO_GET_PAT | PGINFO_GET_PMT | PGINFO_TIMEINFO);
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

	if (eit_b->start_time_mjd == 0xffff && eit_b->start_time_jtc == 0xffffff) {
		proginfo->start_year = 0;
		proginfo->start_month = 0;
		proginfo->start_day = 0;
		proginfo->start_hour = 0;
		proginfo->start_min = 0;
		proginfo->start_sec = 0;
		proginfo->status |= PGINFO_UNKNOWN_STARTTIME;
	} else {
		mjd_to_ymd(eit_b->start_time_mjd, &proginfo->start_year, &proginfo->start_month, &proginfo->start_day);
		proginfo->start_hour = (eit_b->start_time_jtc >> 20 & 0x0f) * 10 +
			((eit_b->start_time_jtc >> 16) & 0x0f);
		proginfo->start_min = (eit_b->start_time_jtc >> 12 & 0x0f) * 10 +
			((eit_b->start_time_jtc >> 8) & 0x0f);
		proginfo->start_sec = (eit_b->start_time_jtc >> 4 & 0x0f) * 10 +
			(eit_b->start_time_jtc & 0x0f);
		if (proginfo->start_hour >= 24) { proginfo->start_hour = 23; }
		if (proginfo->start_min >= 60) { proginfo->start_min = 59; }
		if (proginfo->start_sec >= 60) { proginfo->start_sec = 59; }

		proginfo->status &= ~PGINFO_UNKNOWN_STARTTIME;
	}

	if (eit_b->duration == 0xffffff) {
		proginfo->dur_hour = 0;
		proginfo->dur_min = 0;
		proginfo->dur_sec = 0;
		proginfo->status |= PGINFO_UNKNOWN_DURATION;
	} else {
		proginfo->dur_hour = (eit_b->duration >> 20 & 0x0f) * 10 +
			((eit_b->duration >> 16) & 0x0f);
		proginfo->dur_min = (eit_b->duration >> 12 & 0x0f) * 10 +
			((eit_b->duration >> 8) & 0x0f);
		proginfo->dur_sec = (eit_b->duration >> 4 & 0x0f) * 10 +
			(eit_b->duration & 0x0f);
		if (proginfo->dur_hour >= 24) { proginfo->dur_hour = 23; }
		if (proginfo->dur_min >= 60) { proginfo->dur_min = 59; }
		if (proginfo->dur_sec >= 60) { proginfo->dur_sec = 59; }
		proginfo->status &= ~PGINFO_UNKNOWN_DURATION;
	}

	proginfo->status |= PGINFO_GET_EVENT_INFO;
}

void store_EIT_Eed_item(const Eed_t *eed, const Eed_item_t *eed_item, proginfo_t *proginfo)
{
	int i;
	int item_len;
	Eed_item_string_t *curr_item;

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
			if (eed_item->item_description_length <= sizeof(curr_item->aribdesc)) {
				curr_item->aribdesc_len = eed_item->item_description_length;
			} else {
				/* サイズオーバーなので切り詰める */
				curr_item->aribdesc_len = sizeof(curr_item->aribdesc);
			}
			memcpy(curr_item->aribdesc, eed_item->item_description_char, eed_item->item_description_length);
			curr_item->aribitem_len = 0;
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

	item_len = curr_item->aribitem_len + eed_item->item_length;
	if ( item_len > sizeof(curr_item->aribitem) ) {
		/* サイズオーバーなので切り詰める */
		item_len = sizeof(curr_item->aribitem);
	}
	memcpy(&curr_item->aribitem[curr_item->aribitem_len], eed_item->item_char, eed_item->item_length);
	curr_item->aribitem_len = item_len;

	if (proginfo->curr_desc == proginfo->last_desc) {
		for (i = 0; i < proginfo->n_items; i++) {
			proginfo->items[i].desc_len = AribToString(
					proginfo->items[i].desc,
					sizeof(proginfo->items[i].desc),
					proginfo->items[i].aribdesc,
					proginfo->items[i].aribdesc_len
				);
			proginfo->items[i].item_len = AribToString(
					proginfo->items[i].item,
					sizeof(proginfo->items[i].item),
					proginfo->items[i].aribitem,
					proginfo->items[i].aribitem_len
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

proginfo_t *find_curr_service(ts_service_list_t *sl, unsigned int service_id)
{
	int i;
	for (i = 0; i < sl->n_services; i++) {
		if (service_id == sl->proginfos[i].service_id) {
			return &sl->proginfos[i];
		}
	}
	return NULL;
}

void parse_PCR(const uint8_t *packet, const ts_header_t *tsh, ts_service_list_t *sl)
{
	int i, get = 0, wraparounded;
	const uint8_t *p;
	uint64_t PCR_base = 0;
	int64_t offset;
	unsigned int PCR_ext = 0;

	if ( !(tsh->adaptation_field_control & 0x02) ) {
		return;
	}

	p = &packet[tsh->adaptation_field_pos];
	if ( !get_bits(p, 3, 1) ) {
		/* no PCR */
		return;
	}

	for (i = 0; i < sl->n_services; i++) {
		if (tsh->pid == sl->proginfos[i].PCR_pid) {
			if (!get) {
				PCR_base = get_bits64(p, 8, 33);
				PCR_ext = get_bits(p, 47, 9);
				get = 1;
			}
			offset = (int64_t)PCR_base - (int64_t)sl->proginfos[i].PCR_base;
			wraparounded = 0;
			if (offset < 0) {
				/* wrap-around対策 */
				offset += PCR_BASE_MAX;
				wraparounded = 1;
			}

			if( offset < 1*PCR_BASE_HZ ) {
				sl->proginfos[i].status |= PGINFO_VALID_PCR;
				sl->proginfos[i].status |= PGINFO_PCR_UPDATED;
				//output_message(MSG_DISP, L"PCR %x: %I64d %I64x %d %d",
				//	sl->proginfos[i].service_id, PCR_base, PCR_base, PCR_ext, wraparounded);
			} else {
				/* 前のPCRから1秒以上差があれば有効とは見なさない */
				sl->proginfos[i].status &= ~PGINFO_VALID_PCR;
			}
			sl->proginfos[i].PCR_base = PCR_base;
			sl->proginfos[i].PCR_ext = PCR_ext;
			sl->proginfos[i].PCR_wraparounded |= wraparounded;
		}
	}
}

void parse_TOT_TDT(const uint8_t *packet, const ts_header_t *tsh, ts_service_list_t *sl)
{
	unsigned int slen, mjd;
	uint8_t tid;
	uint32_t bcd_jst;
	int i, year, mon, day, hour, min, sec;

	parse_PSI(packet, tsh, &sl->pid0x14);
	if (sl->pid0x14.stat != PAYLOAD_STAT_FINISHED || sl->pid0x14.n_payload < 8) {
		return;
	}

	tid = sl->pid0x14.payload[0];
	slen = get_bits(sl->pid0x14.payload, 12, 12);
	if (tid == 0x70) {
		/* TDT */
		if (slen != 5) {  return; }
	} else if (tid == 0x73) {
		/* TOT */
		if (slen < 5) { return; }
	} else { return; }

	mjd = get_bits(sl->pid0x14.payload, 24, 16);
	bcd_jst= get_bits(sl->pid0x14.payload, 40, 24);

	mjd_to_ymd(mjd, &year, &mon, &day);
	hour = (bcd_jst >> 20 & 0x0f) * 10 + ((bcd_jst >> 16) & 0x0f);
	min = (bcd_jst >> 12 & 0x0f) * 10 + ((bcd_jst >> 8) & 0x0f);
	sec = (bcd_jst >> 4 & 0x0f) * 10 + (bcd_jst & 0x0f);

	//output_message( MSG_DISP, L"TOT %04d/%02d/%02d %02d:%02d:%02d", year, mon, day, hour, min, sec );

	for (i = 0; i < sl->n_services; i++) {
		sl->proginfos[i].TOT_time.year = year;
		sl->proginfos[i].TOT_time.mon = mon;
		sl->proginfos[i].TOT_time.day = day;
		sl->proginfos[i].TOT_time.hour = hour;
		sl->proginfos[i].TOT_time.min = min;
		sl->proginfos[i].TOT_time.sec = sec;

		if (sl->proginfos[i].status & PGINFO_VALID_PCR) {
			sl->proginfos[i].TOT_PCR = sl->proginfos[i].PCR_base;
			sl->proginfos[i].status |= PGINFO_VALID_TOT_PCR;
		} else {
			sl->proginfos[i].status &= ~PGINFO_VALID_TOT_PCR;
		}
		sl->proginfos[i].PCR_wraparounded = 0;
		sl->proginfos[i].status |= PGINFO_GET_TOT;
	}
}

void parse_EIT(PSI_parse_t *payload_stat, const uint8_t *packet, const ts_header_t *tsh, ts_service_list_t *sl)
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

	if (eit_h.section_number != 0) {
		/* 今の番組ではない */
		return;
	}

	/* 対象のサービスIDかどうか */
	curr_proginfo = find_curr_service(sl, eit_h.service_id);
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

void parse_SDT(PSI_parse_t *payload_stat, const uint8_t *packet, const ts_header_t *tsh, ts_service_list_t *sl)
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
		curr_proginfo = find_curr_service(sl, sdt_b.service_id);
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
void parse_PMT(const uint8_t *packet, const ts_header_t *tsh, ts_service_list_t *sl)
{
	int i;
	int pos, n_pids, len;
	uint16_t pid;
	uint8_t stream_type;
	uint8_t *payload;

	for (i = 0; i < sl->n_services; i++) {
		parse_PSI(packet, tsh, &sl->proginfos[i].PMT_payload);
		if (sl->proginfos[i].PMT_payload.stat != PAYLOAD_STAT_FINISHED) {
			continue;
		}

		len = sl->proginfos[i].PMT_payload.n_payload - 4/*crc32*/;
		payload = sl->proginfos[i].PMT_payload.payload;
		sl->proginfos[i].PCR_pid = get_bits(payload, 67, 13);
		pos = 12 + get_bits(payload, 84, 12);
		n_pids = 0;
		while ( pos < len && n_pids <= MAX_PIDS_PER_SERVICE ) {
			stream_type = payload[pos];
			pid = (uint16_t)get_bits(payload, pos*8+11, 13);
			pos += get_bits(payload, pos*8+28, 12) + 5;
			sl->proginfos[i].service_pids[n_pids].stream_type = stream_type;
			sl->proginfos[i].service_pids[n_pids].pid = pid;
			n_pids++;
		}
		sl->proginfos[i].n_service_pids = n_pids;
		sl->proginfos[i].PMT_payload.stat = PAYLOAD_STAT_INIT;
		sl->proginfos[i].status |= PGINFO_GET_PMT;
		sl->proginfos[i].PMT_last_CRC = sl->proginfos[i].PMT_payload.crc32;
	}
}

void parse_PAT(PSI_parse_t *PAT_payload, const uint8_t *packet, const ts_header_t *tsh, ts_service_list_t *sl)
{
	int i, n, pn, pid;
	uint8_t *payload;

	parse_PSI(packet, tsh, PAT_payload);
	if (PAT_payload->stat == PAYLOAD_STAT_FINISHED) {
		n = (PAT_payload->n_payload - 4/*crc32*/ - 8/*fixed length*/) / 4;
		if (n > MAX_SERVICES_PER_CH) {
			n = MAX_SERVICES_PER_CH;
		}

		payload = &(PAT_payload->payload[8]);
		sl->n_services = 0;
		for (i = 0; i < n; i++) {
			pn = get_bits(payload, i*32, 16);
			pid = get_bits(payload, i*32+19, 13);
			if (pn == 0) {
				/* do nothing */
			} else {
				sl->proginfos[sl->n_services].service_id = pn;
				sl->proginfos[sl->n_services].PMT_payload.stat = PAYLOAD_STAT_INIT;
				sl->proginfos[sl->n_services].PMT_payload.pid = pid;
				sl->proginfos[sl->n_services].status |= PGINFO_GET_PAT;
				(sl->n_services)++;
			}
		}
	}
}
