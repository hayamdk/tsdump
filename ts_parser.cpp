#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ts_parser.h"

__int64 ts_n_drops = 0;
int ts_counter[0x2000] = {};

static inline void get_PSI_payload(unsigned char *packet, payload_procstat_t *ps)
{
	int pos, remain;

	if (ps->pid != ts_get_pid(packet)) {
		return;
	}

	pos = ts_get_payload_pos(packet);
	if (ps->stat == PAYLOAD_STAT_INIT) {
		if (!ts_get_payload_unit_start_indicator(packet)) {
			return;
		}
		ps->stat = PAYLOAD_STAT_PROC;
		ps->n_payload = ts_get_section_length(packet) + 3;
		ps->recv_payload = 0;
		ps->continuity_counter = ts_get_continuity_counter(packet);
		remain = 188 - pos;
		if (remain > ps->n_payload) {
			remain = ps->n_payload;
			ps->stat = PAYLOAD_STAT_FINISH;
		}
		memcpy(ps->payload, &packet[pos], remain);
		ps->recv_payload += remain;
	}
	else if (ps->stat == PAYLOAD_STAT_PROC) {
		if ((ps->continuity_counter + 1) % 16 != ts_get_continuity_counter(packet)) {
			/* drop! */
			ps->n_payload = ps->recv_payload = 0;
			ps->stat = PAYLOAD_STAT_INIT;
			return;
		}
		ps->continuity_counter = ts_get_continuity_counter(packet);
		remain = 188 - pos;
		if (remain >= ps->n_payload - ps->recv_payload) {
			remain = ps->n_payload - ps->recv_payload;
			ps->stat = PAYLOAD_STAT_FINISH;
		}
		memcpy(&(ps->payload[ps->recv_payload]), &packet[pos], remain);
		ps->recv_payload += remain;
	}

	if (ps->stat == PAYLOAD_STAT_FINISH) {
		ps->crc32 = get_payload_crc32(ps);
		unsigned __int32 crc = crc32(ps->payload, ps->n_payload - 4);
		if (ps->crc32 != crc) {
			ps->stat = PAYLOAD_STAT_INIT;
			printf("Payload CRC32 mismatch!\n");
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
	for (i = 0; i < tps->n_programs; i++) {
		get_PSI_payload(packet, &(tps->payload_PMTs[i]));
		if (tps->payload_PMTs[i].stat == PAYLOAD_STAT_FINISH) {
			/* parse PMT */
			int pid, stype, n_pids;
			unsigned char *payload = tps->payload_PMTs[i].payload;
			int pos = 12 + payload[11];
			n_pids = 0;
			if (tps->programs[i].payload_crc32 != tps->payload_PMTs[i].crc32) { /* CRCが前回と違ったときのみ表示 */
				printf("<<<-------------PMT---------------\n"
					"program_number: %d(0x%X), payload crc32: 0x%08X\n",
					tps->programs[i].service_id, tps->programs[i].service_id, tps->payload_PMTs[i].crc32);
			}
			while (pos < tps->payload_PMTs[i].n_payload - 4/*crc32*/) {
				stype = payload[pos];
				pid = (payload[pos + 1] & 0x1f) * 256 + payload[pos + 2];
				pos += (payload[pos + 3] & 0x0f) * 256 + payload[pos + 4] + 5;
				n_pids++;
				if (tps->programs[i].payload_crc32 != tps->payload_PMTs[i].crc32) { /* CRCが前回と違ったときのみ表示 */
					printf("stream_type:0x%x(%s), elementary_PID:%d(0x%X)\n",
						stype, get_stream_type_str(stype), pid, pid);
				}
			}
			if (tps->programs[i].payload_crc32 != tps->payload_PMTs[i].crc32) {
				printf("------------------------------->>>\n");
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

	if (tps->payload_PAT.stat == PAYLOAD_STAT_FINISH && tps->payload_PMTs == NULL) {
		/* parse PAT */
		int n = (tps->payload_PAT.n_payload - 4/*crc32*/ - 8/*fixed length*/) / 4;
		int pn, pid, n_progs = 0;
		unsigned char *payload = &(tps->payload_PAT.payload[8]);
		printf("<<<-------------PAT---------------\n");
		for (i = 0; i < n; i++) {
			pn = payload[i * 4] * 256 + payload[i * 4 + 1];
			pid = (payload[i * 4 + 2] & 0x1f) * 256 + payload[i * 4 + 3];
			if (pn == 0) {
				printf("network_PID:%d(0x%X)\n", pid, pid);
			} else {
				n_progs++;
				printf("program_number:%d(0x%X), program_map_PID:%d(0x%X)\n", pn, pn, pid, pid);
			}
		}
		printf("------------------------------->>>\n");
		tps->n_programs = n_progs;
		tps->payload_PMTs = (payload_procstat_t*)malloc(n_progs*sizeof(payload_procstat_t));
		tps->programs = (program_pid_info_t*)malloc(n_progs*sizeof(program_pid_info_t));
		n_progs = 0;
		for (i = 0; i < n; i++) {
			pn = payload[i * 4] * 256 + payload[i * 4 + 1];
			pid = (payload[i * 4 + 2] & 0x1f) * 256 + payload[i * 4 + 3];
			if (pn != 0) {
				tps->payload_PMTs[n_progs].pid = pid;
				tps->payload_PMTs[n_progs].stat = PAYLOAD_STAT_INIT;
				tps->programs[n_progs].service_id = payload[i * 4] * 256 + payload[i * 4 + 1];
				tps->programs[n_progs].content_pids = NULL;
				tps->programs[n_progs].n_pids = 0;
				n_progs++;
			}
		}
		tps->payload_crc32_PAT = tps->payload_PAT.crc32;
	}
}
