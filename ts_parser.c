#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

typedef wchar_t			WCHAR;
typedef long			BOOL;
typedef unsigned long	DWORD;

#include "modules_def.h"
#include "ts_parser.h"
#include "aribstr.h"

static inline void parse_PSI(unsigned char *packet, PSI_parse_t *ps)
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

int parse_Sed(const uint8_t *desc, Sed_t *sed)
{
	sed->descriptor_tag					= desc[0];
	sed->descriptor_length				= desc[1];
	memcpy(sed->ISO_639_language_code,	  &desc[2], 3);
	sed->ISO_639_language_code[3]		= '\0';
	
	sed->event_name_length				= desc[5];
	sed->event_name_char				= &desc[6];
	if ( &sed->event_name_char[sed->event_name_length] > &desc[2 + sed->descriptor_length] ) {
		return 0;
	}

	sed->text_length					= sed->event_name_char[sed->event_name_length];
	sed->text_char						= &sed->event_name_char[sed->event_name_length + 1];
	if ( &sed->text_char[sed->text_length] > &desc[2 + sed->descriptor_length] ) {
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

void parse_EIT(PSI_parse_t *payload_stat, uint8_t *packet)
{
	int len;
	const char *rs[] = {"undef", "not running", "coming", "stopped", "running", "reserved", "reserved", "reserved" };
	WCHAR s1[256], s2[256];
	EIT_header_t eit_h;
	EIT_body_t eit_b;
	Sed_t sed;
	uint8_t *p_eit_b, *p_eit_end;
	uint8_t *p_Sed, *p_Sed_end;

	parse_PSI(packet, payload_stat);

	if (payload_stat->stat != PAYLOAD_STAT_FINISHED || payload_stat->payload[0] != 0x4e) {
		return;
	}

	parse_EIT_header(payload_stat->payload, &eit_h);

	printf("----------------------------------------------------------------------------"
		"\ntable_id = 0x%02x, pid=0x%02x, service_id=0x%02x, len=%d, section_number=%d   \n",
		eit_h.table_id, payload_stat->pid, eit_h.service_id, payload_stat->n_payload, eit_h.section_number);
		
	len = payload_stat->n_payload - 14 - 4/*=sizeof(crc32)*/;
	p_eit_b = &payload_stat->payload[14];
	p_eit_end = &p_eit_b[len];
	while(p_eit_b < p_eit_end) {
		parse_EIT_body(p_eit_b, &eit_b);
		printf(" eid=0x%04x start=%d|%8x dur=%8x dlen=%d running_status=%s(%d) \n",
			eit_b.event_id, eit_b.start_time_mjd, eit_b.start_time_jtc, eit_b.duration, eit_b.descriptors_loop_length,
			rs[eit_b.running_status], eit_b.running_status);

		p_Sed = &p_eit_b[12];
		p_Sed_end = &p_Sed[eit_b.descriptors_loop_length];
		while( p_Sed < p_Sed_end && parse_Sed(p_Sed, &sed) ) {
			if (sed.descriptor_tag == 0x4d) {
				AribToString(s1, 256, sed.event_name_char, sed.event_name_length);
				AribToString(s2, 256, sed.text_char, sed.text_length);

				printf(" \n tag=0x%02x dlen2=%d code=%s     \n", sed.descriptor_tag, eit_b.descriptors_loop_length, sed.ISO_639_language_code);
				wprintf(L"%s\n%s\n\n", s1, s2);
			}
			p_Sed += (1 + sed.descriptor_length);
		}
		p_eit_b = p_Sed_end;
	}
}

int parse_Sd(uint8_t *desc, Sd_t *sd)
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

void parse_SDT_header(uint8_t *payload, SDT_header_t *sdt_h)
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

void parse_SDT_body(uint8_t *body, SDT_body_t *sdt_b)
{
	sdt_b->service_id					= get_bits(body,  0, 16);
	sdt_b->EIT_user_defined_flags		= get_bits(body, 19,  3);
	sdt_b->EIT_schedule_flag			= get_bits(body, 22,  1);
	sdt_b->EIT_present_following_flag	= get_bits(body, 23,  1);
	sdt_b->running_status				= get_bits(body, 24,  3);
	sdt_b->free_CA_mode					= get_bits(body, 27,  1);
	sdt_b->descriptors_loop_length 		= get_bits(body, 28, 12);
}

void parse_SDT(PSI_parse_t *payload_stat, uint8_t *packet)
{
	int len;
	WCHAR sp[1024], s[1024];
	SDT_header_t sdt_h;
	SDT_body_t sdt_b;
	Sd_t sd;
	uint8_t *p_sdt_b, *p_sdt_end;
	uint8_t *p_sd, *p_sd_end;

	parse_PSI(packet, payload_stat);

	if (payload_stat->stat != PAYLOAD_STAT_FINISHED || payload_stat->payload[0] != 0x42) {
		return;
	}

	parse_SDT_header(payload_stat->payload, &sdt_h);

	printf("table_id = 0x%02x, pid=0x%02x, tsid=0x%02x, len=%d \n", 
		sdt_h.table_id , payload_stat->pid, sdt_h.transport_stream_id, payload_stat->n_payload);

	len = payload_stat->n_payload - 11 - 4/*=sizeof(crc32)*/;
	p_sdt_b = &payload_stat->payload[11];
	p_sdt_end = &p_sdt_b[len];
	while(p_sdt_b < p_sdt_end) {
		parse_SDT_body(p_sdt_b, &sdt_b);
		printf(" service_id=0x%04x dlen=%d \n", sdt_b.service_id, sdt_b.descriptors_loop_length);

		p_sd = &p_sdt_b[5];
		p_sd_end = &p_sd[sdt_b.descriptors_loop_length];
		while( p_sd < p_sd_end && parse_Sd(p_sd, &sd) ) {
			printf("  tag=0x%02x dlen2=%d \n", sd.descriptor_tag, sd.descriptor_length);
			if (sd.descriptor_tag == 0x48) {
				AribToString(sp, 1024, sd.service_provider_name_char, sd.service_provider_name_length);
				AribToString(s, 1024, sd.service_name_char, sd.service_name_length);
				wprintf(L"%s|%s   \n", sp, s);
			}
			p_sd += (1 + sd.descriptor_length);
		}
		p_sdt_b = p_sd_end;
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
