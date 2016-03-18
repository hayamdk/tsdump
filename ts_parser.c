#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/timeb.h>

typedef wchar_t			WCHAR;
typedef long			BOOL;
typedef unsigned long	DWORD;

#include "ts_parser.h"
#include "modules_def.h"
#include "aribstr.h"
#include "tsdump.h"

static inline void parse_PSI(const uint8_t *packet, PSI_parse_t *ps)
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
	if (ps->pid != ts_get_pid(packet)) {
		return;
	}

	/* パケットの処理 */
	if (ps->stat == PAYLOAD_STAT_INIT) {
		if (!ts_get_payload_unit_start_indicator(packet)) {
			//printf("pass!\n");
			return;
		}
		ps->stat = PAYLOAD_STAT_PROC;
		ps->n_payload = ts_get_section_length(packet) + 3;
		ps->recv_payload = ps->n_next_payload = ps->next_recv_payload = 0;
		ps->continuity_counter = ts_get_continuity_counter(packet);

		pos = ts_get_payload_data_pos(packet);
		/* 不正なパケットかどうかのチェック */
		if (pos > 188) {
			ps->stat = PAYLOAD_STAT_INIT;
			output_message(MSG_PACKETERROR, L"Invalid payload data_byte offset! (pid=0x%02x)", ps->pid);
			return;
		}

		remain = 188 - pos;
		if (remain > ps->n_payload) {
			remain = ps->n_payload;
			ps->stat = PAYLOAD_STAT_FINISHED;
		}
		memcpy(ps->payload, &packet[pos], remain);
		ps->recv_payload += remain;
	} else if (ps->stat == PAYLOAD_STAT_PROC) {
		/* continuity_counter の連続性を確認 */
		if ((ps->continuity_counter + 1) % 16 != ts_get_continuity_counter(packet)) {
			/* drop! */
			output_message(MSG_PACKETERROR, L"packet continuity_counter is discontinuous! (pid=0x%02x)", ps->pid);
			ps->n_payload = ps->recv_payload = 0;
			ps->stat = PAYLOAD_STAT_INIT;
			return;
		}
		ps->continuity_counter = ts_get_continuity_counter(packet);

		if (ts_get_payload_unit_start_indicator(packet)) {
			pos = ts_get_payload_pos(packet);
			pointer_field = packet[pos];
			pos++;

			/* 不正なパケットかどうかのチェック */
			if (pos + pointer_field >= 188) {
				ps->stat = PAYLOAD_STAT_INIT;
				output_message(MSG_PACKETERROR, L"Invalid payload data_byte offset! (pid=0x%02x)", ps->pid);
				return;
			}

			ps->n_next_payload = ts_get_section_length(packet) + 3;
			ps->next_recv_payload = 188 - pos - pointer_field;
			if (ps->next_recv_payload > ps->n_next_payload) {
				ps->next_recv_payload = ps->n_next_payload;
			}
			memcpy(ps->next_payload, &packet[pos+pointer_field], ps->next_recv_payload);
			ps->stat = PAYLOAD_STAT_FINISHED;

			remain = pointer_field;
		} else {
			pos = ts_get_payload_data_pos(packet);
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

void clear_proginfo(proginfo_t *proginfo)
{
	proginfo->status &= PGINFO_GET_PAT;
	proginfo->last_desc = -1;
}

void init_proginfo(proginfo_t *proginfo)
{
	proginfo->status = 0;
	proginfo->last_desc = -1;
}

int parse_EIT_Sed(const uint8_t *desc, Sed_t *sed)
{
	const uint8_t *desc_end;

	sed->descriptor_tag					= desc[0];
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

	eed->descriptor_tag					= desc[0];
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
	int y, m, d, k;
	double mjd;

	if (proginfo->status & PGINFO_GET_EVENT_INFO && proginfo->event_id != eit_b->event_id) {
		/* 前回の取得から番組が切り替わった */
		clear_proginfo(proginfo);
		//proginfo->status |= PGINFO_FLAG_CHANGED;
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
		/* MJD -> YMD */
		/*　2100年2月28日までの間有効な公式（ARIB STD-B10 第２部より）　*/
		mjd = (double)eit_b->start_time_mjd;
		y = (int)((mjd - 15078.2) / 365.25);
		m = (int)((mjd - 14956.1 - (int)((double)y*365.25)) / 30.6001);
		d = eit_b->start_time_mjd - 14956 - (int)((double)y*365.25) - (int)((double)m*30.6001);
		if (m == 14 || m == 15) {
			k = 1;
		}
		else {
			k = 0;
		}
		proginfo->start_year = 1900 + y + k;
		proginfo->start_month = m - 1 - k * 12;
		proginfo->start_day = d;

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
		proginfo->status = PGINFO_UNKNOWN_DURATION;
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
		if ( proginfo->curr_desc == (int)eed->descriptor_number || 
				proginfo->curr_desc + 1 == (int)eed->descriptor_number ) {
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

proginfo_t *find_curr_service(proginfo_t *proginfos, int n_services, unsigned int service_id)
{
	int i;
	for (i = 0; i < n_services; i++) {
		if (service_id == proginfos[i].service_id) {
			return &proginfos[i];
		}
	}
	return NULL;
}

void parse_EIT(PSI_parse_t *payload_stat, const uint8_t *packet, proginfo_t *proginfo_all, int n_services)
{
	int len;
	//const char *rs[] = {"undef", "not running", "coming", "stopped", "running", "reserved", "reserved", "reserved" };
	//WCHAR s1[256], s2[256];
	EIT_header_t eit_h;
	EIT_body_t eit_b;
	uint8_t *p_eit_b, *p_eit_end;
	uint8_t *p_desc, *p_desc_end;
	uint8_t dtag, dlen;
	proginfo_t *curr_proginfo;

	parse_PSI(packet, payload_stat);

	if (payload_stat->stat != PAYLOAD_STAT_FINISHED || payload_stat->payload[0] != 0x4e) {
		return;
	}

	parse_EIT_header(payload_stat->payload, &eit_h);

	if (eit_h.section_number != 0) {
		/* 今の番組ではない */
		return;
	}

	/* 対象のサービスIDかどうか */
	curr_proginfo = find_curr_service(proginfo_all, n_services, eit_h.service_id);
	if(!curr_proginfo) {
		return;
	}

	/*output_message(MSG_DEBUG, L"table_id = 0x%02x, pid=0x%02x, service_id=0x%02x, len=%d, section_number=%d, ver=%d",
		eit_h.table_id, payload_stat->pid, eit_h.service_id, payload_stat->n_payload, eit_h.section_number, eit_h.version_number);*/
		
	len = payload_stat->n_payload - 14 - 4/*=sizeof(crc32)*/;
	p_eit_b = &payload_stat->payload[14];
	p_eit_end = &p_eit_b[len];
	while(&p_eit_b[12] < p_eit_end) {
		parse_EIT_body(p_eit_b, &eit_b); /* read 12bytes */
		store_EIT_body(&eit_b, curr_proginfo);

		/*output_message(MSG_DEBUG, L" eid=0x%04x start=%d|%8x dur=%8x dlen=%d running_status=%S(%d)",
			eit_b.event_id, eit_b.start_time_mjd, eit_b.start_time_jtc, eit_b.duration, eit_b.descriptors_loop_length,
			rs[eit_b.running_status], eit_b.running_status);*/
		/*output_message( MSG_DEBUG, L"eid=0x%04x, %d/%02d/%02d %02d:%02d:%02d +%02d:%02d:%02d",
			proginfo->event_id, proginfo->start_year, proginfo->start_month, proginfo->start_day,
			proginfo->start_hour, proginfo->start_min, proginfo->start_sec,
			proginfo->dur_hour, proginfo->dur_min, proginfo->dur_sec );*/

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
					//AribToString(s1, 256, sed.event_name_char, sed.event_name_length);
					//AribToString(s2, 256, sed.text_char, sed.text_length);

					//output_message(MSG_DEBUG, L"  tag=0x%02x dlen2=%d code=%S",
					//	sed.descriptor_tag, sed.descriptor_length, sed.ISO_639_language_code);
					//output_message(MSG_DEBUG, L"  [%s]\n[%s]", s1, s2);
					store_EIT_Sed(&sed, curr_proginfo);
					if (PGINFO_READY(curr_proginfo->status)) {
						curr_proginfo->last_ready_time = gettime();
					}
					//output_message(MSG_DEBUG, L"  [%s]\n[%s]", proginfo->event_name.str, proginfo->event_text.str);
				}
			} else if (dtag == 0x4e) {
				Eed_t eed;
				Eed_item_t eed_item;
				uint8_t *p_eed_item, *p_eed_item_end;
				if (parse_EIT_Eed(p_desc, &eed)) {
					//output_message(MSG_DEBUG, L"  tag=0x%02x dlen2=%d code=%S dnum: %d of %d",
					//	eed.descriptor_tag, eed.descriptor_length, eed.ISO_639_language_code, eed.descriptor_number, eed.last_descriptor_number);
					p_eed_item = &p_desc[7];
					p_eed_item_end = &p_eed_item[eed.length_of_items];
					while (p_eed_item < p_eed_item_end) {
						if (parse_EIT_Eed_item(p_eed_item, p_eed_item_end, &eed_item)) {

							/* ARIB TR-B14 第四編 地上デジタルテレビジョン放送 PSI/SI 運用規定 12.2 セクションへの記述子の配置 は、
							Eedが複数のEITにまたがって送信されることは無いことを規定していると解釈できる。
							よってparse_EIT()を抜けたときはcurr_proginfo->itemsに全てのアイテムが収納されていることが保障される。
							仮にこの仮定が満たされない場合でも、単に中途半端な番組情報が見えてしまうタイミングが存在するだけである */
							store_EIT_Eed_item(&eed, &eed_item, curr_proginfo);

							//AribToString(s1, 256, eed_item.item_description_char, eed_item.item_description_length);
							//AribToString(s2, 256, eed_item.item_char, eed_item.item_length);
							//output_message(MSG_DEBUG, L"   <%s>\n<%s>", s1, s2);
						}
						p_eed_item += ( 2 + eed_item.item_description_length + eed_item.item_length );
					}
					//AribToString(s1, 256, eed.text_char, eed.text_length);
					//output_message(MSG_DEBUG, L"  <<%s>>", s1);
				}
			}

			p_desc += (2+dlen);
		}
		p_eit_b = p_desc_end;
	}
}

int parse_SDT_Sd(const uint8_t *desc, Sd_t *sd)
{
	sd->descriptor_tag					= desc[0];
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

void parse_SDT(PSI_parse_t *payload_stat, const uint8_t *packet, proginfo_t *proginfo_all, int n_services)
{
	int len;
	//WCHAR sp[1024], s[1024];
	SDT_header_t sdt_h;
	SDT_body_t sdt_b;
	Sd_t sd;
	uint8_t *p_sdt_b, *p_sdt_end;
	uint8_t *p_desc, *p_desc_end;
	uint8_t dtag, dlen;
	proginfo_t *curr_proginfo;

	parse_PSI(packet, payload_stat);

	if (payload_stat->stat != PAYLOAD_STAT_FINISHED || payload_stat->payload[0] != 0x42) {
		return;
	}

	parse_SDT_header(payload_stat->payload, &sdt_h);

	//output_message(MSG_DEBUG, L"table_id = 0x%02x, pid=0x%02x, tsid=0x%02x, len=%d",
	//	sdt_h.table_id , payload_stat->pid, sdt_h.transport_stream_id, payload_stat->n_payload);

	len = payload_stat->n_payload - 11 - 4/*=sizeof(crc32)*/;
	p_sdt_b = &payload_stat->payload[11];
	p_sdt_end = &p_sdt_b[len];
	while(&p_sdt_b[5] < p_sdt_end) {
		parse_SDT_body(p_sdt_b, &sdt_b); /* read 5bytes */
		//output_message(MSG_DEBUG, L" service_id=0x%04x dlen=%d",
		//	sdt_b.service_id, sdt_b.descriptors_loop_length);

		/* 対象のサービスIDかどうか */
		curr_proginfo = find_curr_service(proginfo_all, n_services, sdt_b.service_id);
		if (!curr_proginfo) {
			continue;
		}

		p_desc = &p_sdt_b[5];
		p_desc_end = &p_desc[sdt_b.descriptors_loop_length];
		if (p_desc_end > p_sdt_end) {
			break;
		}
		while( p_desc < p_desc_end ) {
			dtag = p_desc[0];
			dlen = p_desc[1];
			//output_message(MSG_DEBUG, L"  tag=0x%02x dlen2=%d", dtag, dlen);
			if (dtag == 0x48) {
				if (parse_SDT_Sd(p_desc, &sd)) {
					store_SDT(&sdt_h, &sd, curr_proginfo);
					//AribToString(sp, 1024, sd.service_provider_name_char, sd.service_provider_name_length);
					//AribToString(s, 1024, sd.service_name_char, sd.service_name_length);
					//output_message(MSG_DEBUG, L"  |%s|%s|", sp, s);
				}
			}
			p_desc += (2+dlen);
		}
		p_sdt_b = p_desc_end;
	}
}

/* PMT: ISO 13818-1 2.4.4.8 Program Map Table */
void parse_PMT(uint8_t *packet, proginfo_t *proginfos, int n_services)
{
	int i;
	int pos, n_pids, len;
	uint16_t pid;
	uint8_t stream_type;
	uint8_t *payload;

	for (i = 0; i < n_services; i++) {
		parse_PSI(packet, &proginfos[i].PMT_payload);
		/*if (proginfos[i].status & PGINFO_GET_PMT) {
			continue;
		}*/
		if (proginfos[i].PMT_payload.stat != PAYLOAD_STAT_FINISHED) {
			continue;
		}

		if ( proginfos[i].PMT_last_CRC != proginfos[i].PMT_payload.crc32 ) { /* CRCが前回と違ったときのみ表示 */
			output_message(MSG_DISP, L"<<< ------------- PMT ---------------\n"
				L"program_number: %d(0x%X), payload crc32: 0x%08X",
				proginfos[i].service_id, proginfos[i].service_id, proginfos[i].PMT_payload.crc32 );
		}

		len = proginfos[i].PMT_payload.n_payload - 4/*crc32*/;
		payload = proginfos[i].PMT_payload.payload;
		pos = 12 + get_bits(payload, 84, 12);
		n_pids = 0;
		while ( pos < len && n_pids <= MAX_PIDS_PER_SERVICE ) {
			stream_type = payload[pos];
			pid = (uint16_t)get_bits(payload, pos*8+11, 13);
			//pid = (payload[pos + 1] & 0x1f) * 256 + payload[pos + 2];
			pos += get_bits(payload, pos*8+28, 12) + 5;
			//pos += (payload[pos + 3] & 0x0f) * 256 + payload[pos + 4] + 5;
			if ( proginfos[i].PMT_last_CRC != proginfos[i].PMT_payload.crc32 ) { /* CRCが前回と違ったときのみ表示 */
				output_message(MSG_DISP, L"stream_type:0x%x(%S), elementary_PID:%d(0x%X)",
					stream_type, get_stream_type_str(stream_type), pid, pid);
			}
			proginfos[i].service_pids[n_pids].stream_type = stream_type;
			proginfos[i].service_pids[n_pids].pid = pid;
			n_pids++;
		}
		proginfos[i].n_service_pids = n_pids;
		proginfos[i].PMT_payload.stat = PAYLOAD_STAT_INIT;
		proginfos[i].status |= PGINFO_GET_PMT;
		proginfos[i].PMT_last_CRC = proginfos[i].PMT_payload.crc32;
		if ( proginfos[i].PMT_last_CRC != proginfos[i].PMT_payload.crc32 ) {
			output_message(MSG_DISP, L"---------------------------------- >>>");
		}
	}
}

void parse_PAT(PSI_parse_t *PAT_payload, const uint8_t *packet, proginfo_t *proginfos, const int n_services_max, int *n_services)
{
	int i, n, pn, pid;
	uint8_t *payload;

	parse_PSI(packet, PAT_payload);
	if (PAT_payload->stat == PAYLOAD_STAT_FINISHED) {
		n = (PAT_payload->n_payload - 4/*crc32*/ - 8/*fixed length*/) / 4;
		if (n > n_services_max) {
			n = n_services_max;
		}

		payload = &(PAT_payload->payload[8]);
		output_message(MSG_DISP, L"<<< ------------- PAT ---------------");
		*n_services = 0;
		for (i = 0; i < n; i++) {
			pn = get_bits(payload, i*32, 16);
			pid = get_bits(payload, i*32+19, 13);
			if (pn == 0) {
				output_message(MSG_DISP, L"network_PID:%d(0x%X)", pid, pid);
			} else {
				proginfos[*n_services].service_id = pn;
				proginfos[*n_services].PMT_payload.stat = PAYLOAD_STAT_INIT;
				proginfos[*n_services].PMT_payload.pid = pid;
				proginfos[*n_services].status |= PGINFO_GET_PAT;
				(*n_services)++;
				output_message(MSG_DISP, L"program_number:%d(0x%X), program_map_PID:%d(0x%X)", pn, pn, pid, pid);
			}
		}
		output_message(MSG_DISP, L"---------------------------------- >>>");
	}
}

void parse_ts_packet(ts_parse_stat_t *tps, unsigned char *packet)
{
	int i;

	if (packet[0] != 0x47) {
		return;
	}

	parse_PSI(packet, &(tps->payload_PAT));
	for (i = 0; i < tps->n_programs/* should be initialized to 0 */; i++) {
		parse_PSI(packet, &(tps->payload_PMTs[i]));
		if (tps->payload_PMTs[i].stat == PAYLOAD_STAT_FINISHED) {
			/* parse PMT */
			int pid, stype, n_pids;
			unsigned char *payload = tps->payload_PMTs[i].payload;
			int pos = 12 + payload[11];
			n_pids = 0;
			if (tps->programs[i].payload_crc32 != tps->payload_PMTs[i].crc32) { /* CRCが前回と違ったときのみ表示 */
				output_message(MSG_DISP, L"<<< ------------- PMT ---------------\n"
					L"program_number: %d(0x%X), payload crc32: 0x%08X",
					tps->programs[i].service_id, tps->programs[i].service_id, tps->payload_PMTs[i].crc32);
			}
			while (pos < tps->payload_PMTs[i].n_payload - 4/*crc32*/) {
				stype = payload[pos];
				pid = (payload[pos + 1] & 0x1f) * 256 + payload[pos + 2];
				pos += (payload[pos + 3] & 0x0f) * 256 + payload[pos + 4] + 5;
				n_pids++;
				if (tps->programs[i].payload_crc32 != tps->payload_PMTs[i].crc32) { /* CRCが前回と違ったときのみ表示 */
					output_message(MSG_DISP, L"stream_type:0x%x(%S), elementary_PID:%d(0x%X)",
						stype, get_stream_type_str(stype), pid, pid);
				}
			}
			if (tps->programs[i].payload_crc32 != tps->payload_PMTs[i].crc32) {
				output_message(MSG_DISP, L"---------------------------------- >>>");
			}
			tps->programs[i].payload_crc32 = tps->payload_PMTs[i].crc32;
			tps->payload_PMTs[i].stat = PAYLOAD_STAT_INIT;

			if (tps->programs[i].n_pids != n_pids) {
				if (tps->programs[i].content_pids != NULL) {
					free(tps->programs[i].content_pids);
				}
				tps->programs[i].content_pids = (unsigned int*)malloc(n_pids*sizeof(unsigned int));
			}
			tps->programs[i].n_pids = n_pids;

			n_pids = 0;
			pos = 12 + payload[11];
			while (pos < tps->payload_PMTs[i].n_payload - 4/*crc32*/) {
				pid = (payload[pos + 1] & 0x1f) * 256 + payload[pos + 2];
				pos += (payload[pos + 3] & 0x0f) * 256 + payload[pos + 4] + 5;
				tps->programs[i].content_pids[n_pids] = pid;
				n_pids++;
			}
		}
	}

	if (tps->payload_PAT.stat == PAYLOAD_STAT_FINISHED && tps->payload_PMTs == NULL) {
		/* parse PAT */
		int n = (tps->payload_PAT.n_payload - 4/*crc32*/ - 8/*fixed length*/) / 4;
		int pn, pid, n_progs = 0;
		unsigned char *payload = &(tps->payload_PAT.payload[8]);
		output_message(MSG_DISP, L"<<< ------------- PAT ---------------");
		for (i = 0; i < n; i++) {
			pn = payload[i * 4] * 256 + payload[i * 4 + 1];
			pid = (payload[i * 4 + 2] & 0x1f) * 256 + payload[i * 4 + 3];
			if (pn == 0) {
				output_message(MSG_DISP, L"network_PID:%d(0x%X)", pid, pid);
			} else {
				n_progs++;
				output_message(MSG_DISP, L"program_number:%d(0x%X), program_map_PID:%d(0x%X)", pn, pn, pid, pid);
			}
		}
		output_message(MSG_DISP, L"---------------------------------- >>>");
		tps->n_programs = n_progs;
		tps->payload_PMTs = (PSI_parse_t*)malloc(n_progs*sizeof(PSI_parse_t));
		tps->programs = (program_pid_info_t*)malloc(n_progs*sizeof(program_pid_info_t));
		n_progs = 0;
		for (i = 0; i < n; i++) {
			pn = payload[i * 4] * 256 + payload[i * 4 + 1];
			pid = (payload[i * 4 + 2] & 0x1f) * 256 + payload[i * 4 + 3];
			if (pn != 0) {
				tps->payload_PMTs[n_progs].pid = pid;
				//tps->payload_PMTs[n_progs].stat = PAYLOAD_STAT_INIT;
				tps->programs[n_progs].service_id = payload[i * 4] * 256 + payload[i * 4 + 1];
				tps->programs[n_progs].content_pids = NULL;
				tps->programs[n_progs].n_pids = 0;
				n_progs++;
			}
		}
		tps->payload_crc32_PAT = tps->payload_PAT.crc32;
	}
}
