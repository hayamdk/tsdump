#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

typedef wchar_t			WCHAR;
typedef long			BOOL;
typedef unsigned long	DWORD;

#include "modules_def.h"
#include "ts_parser.h"

static inline void get_PSI_payload(unsigned char *packet, payload_procstat_t *ps)
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

void parse_EIT(payload_procstat_t *payload_stat, uint8_t *packet)
{
	int sid;
	get_PSI_payload(packet, payload_stat);
	uint8_t *p, *q;
	int len, dlen, eid, i, dtag, dlen2;
	uint64_t start, dur;

	if (payload_stat->payload[0] != 0x4e) {
		return;
	}

	if (payload_stat->stat == PAYLOAD_STAT_FINISHED) {
		//payload_stat->stat = PAYLOAD_STAT_INIT;
		sid = payload_stat->payload[3] * 0x100 + payload_stat->payload[4];
		printf("table_id = 0x%02x, pid=0x%02x, service_id=0x%02x, len=%d \n", (int)payload_stat->payload[0], payload_stat->pid, sid, payload_stat->n_payload);
		
		len = payload_stat->n_payload - 14 - 4/*crc32*/;
		for (i=0; i < len; ) {
			p = &payload_stat->payload[14+i];
			eid = p[0]*0x100 + p[1];
			start = p[2];
			start = start * 0x100 + p[3];
			start = start * 0x100 + p[4];
			start = start * 0x100 + p[5];
			start = start * 0x100 + p[6];
			dur = p[7];
			dur = dur * 0x100 + p[8];
			dur = dur * 0x100 + p[9];
			dlen = (p[10] & 0x0f) * 0x100 + p[11];
			p = &p[12];
			printf(" eid=0x%04x start=%I64x dur=%8x dlen=%d \n", eid, start, (uint32_t)dur, dlen);
			for (q = p; q < &p[dlen]; ) {
				dtag = q[0];
				dlen2 = q[1];
				printf("  tag=0x%02x dlen2=%d \n", dtag, dlen2);
				q += (2 + dlen2);
			}
			i += (12+dlen);
		}
	}
}

void parse_SDT(payload_procstat_t *payload_stat, uint8_t *packet)
{
	int tsid, sid;
	get_PSI_payload(packet, payload_stat);
	uint8_t *p, *q;
	int len, dlen, i, dtag, dlen2;

	if (payload_stat->payload[0] != 0x42) {
		return;
	}

	if (payload_stat->stat == PAYLOAD_STAT_FINISHED) {
		//payload_stat->stat = PAYLOAD_STAT_INIT;
		tsid = payload_stat->payload[3] * 0x100 + payload_stat->payload[4];
		printf("table_id = 0x%02x, pid=0x%02x, tsid=0x%02x, len=%d \n", (int)payload_stat->payload[0], payload_stat->pid, tsid, payload_stat->n_payload);

		len = payload_stat->n_payload - 11 - 4/*crc32*/;
		for (i = 0; i < len; ) {
			p = &payload_stat->payload[11 + i];
			sid = p[0] * 0x100 + p[1];
			dlen = (p[3] & 0x0f) * 0x100 + p[4];
			p = &p[5];
			printf(" service_id=0x%04x dlen=%d \n", sid, dlen);
			for (q = p; q < &p[dlen]; ) {
				dtag = q[0];
				dlen2 = q[1];
				printf("  tag=0x%02x dlen2=%d \n", dtag, dlen2);
				if (dtag == 0x48) {
					int k = 0;
				}
				q += (2 + dlen2);
			}
			i += (5 + dlen);
		}
	}
}

void parse_ts_packet(ts_parse_stat_t *tps, unsigned char *packet)
{
	int i;

	if (packet[0] != 0x47) {
		return;
	}

	get_PSI_payload(packet, &(tps->payload_PAT));
	for (i = 0; i < tps->n_programs/* should be initialized to 0 */; i++) {
		get_PSI_payload(packet, &(tps->payload_PMTs[i]));
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
		tps->payload_PMTs = (payload_procstat_t*)malloc(n_progs*sizeof(payload_procstat_t));
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
