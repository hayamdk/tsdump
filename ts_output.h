typedef struct
{
	WCHAR *fn;
	int delay_remain;
	int close_remain;
	int close_flag;
	__int64 closetime;
	void **modulestats;
	ProgInfo final_pi;
} pgoutput_stat_t;

typedef struct
{
	__int64 time;
	int bytes;
} transfer_history_t;

typedef struct
{
	BYTE *buf;
	int pos_filled;
	int pos_write;
	int pos_pi;
	int pos_filled_old;
	transfer_history_t *th;
	ProgInfo pi;
	ProgInfo pi_last;
	int n_pgos;
	pgoutput_stat_t *pgos;
	int write_busy;
	int retry_count;
	int n_th;
	int tps_index;
	int service_id;
	int PAT_packet_counter;
	int dropped_bytes;
} ts_output_stat_t;

void init_tos(ts_output_stat_t *tos);
void close_tos(ts_output_stat_t *tos);
void ts_copybuf(ts_output_stat_t *tos, BYTE *buf, int n_buf);
void ts_check_pi(ts_output_stat_t *tos, __int64 nowtime);
void ts_minimize_buf(ts_output_stat_t *tos);
void ts_require_buf(ts_output_stat_t *tos, int require);
void ts_copy_backward(ts_output_stat_t *tos, __int64 nowtime);
void ts_output(ts_output_stat_t *tos, __int64 nowtime, int);
int ts_wait_pgoutput(ts_output_stat_t *tos);
void ts_check_pgoutput(ts_output_stat_t *tos);
int create_tos_per_service(ts_output_stat_t **ptos, ts_parse_stat_t *tps);

static int ts_is_mypid(unsigned int pid, ts_output_stat_t *tos, ts_parse_stat_t *tps)
{
	int i, j, found = 0, my = 0;
	for (i = 0; i < tps->n_programs; i++) {
		if (pid == tps->payload_PMTs[i].pid) {
			if (i == tos->tps_index) {
				my = 1;
			} else {
				found = 1;
			}
		}
		for (j = 0; j < tps->programs[i].n_pids; j++) {
			if (pid == tps->programs[i].content_pids[j]) {
				if (i == tos->tps_index) {
					my = 1;
				} else {
					found = 1;
				}
			}
		}
	}
	return found * 2 + my;
}

static inline void ts_update_transfer_history(ts_output_stat_t *tos, __int64 nowtime, int bytes)
{
	int i;
	if (nowtime / CHECK_INTERVAL != tos->th[0].time / CHECK_INTERVAL)
	{
		for (i = tos->n_th - 1; i > 0; i--) {
			tos->th[i] = tos->th[i - 1];
		}
		tos->th[0].bytes = 0;
	}
	tos->th[0].bytes += bytes;
	tos->th[0].time = nowtime;
}

static inline void ts_simplify_PAT_packet(BYTE *packet, unsigned int target_sid, unsigned int continuity_counter)
{
	int payload_pos = ts_get_payload_pos(packet);
	int section_len = ts_get_section_length(packet);

	int n = (section_len - 5 - 4 - 4) / 4;
	int table_pos = payload_pos + 8 + 4;

	int i;
	unsigned int sid;

	unsigned __int32 crc32_set;

	for (i = 0; i < n; i++) {
		sid = packet[table_pos + i * 4] * 256 + packet[table_pos + i * 4 + 1];
		if (sid == target_sid) {
			memmove(&packet[table_pos], &packet[table_pos + i * 4], 4);
			break;
		}
	}
	/* set new section_length */
	packet[payload_pos + 1] = packet[payload_pos + 1] & 0xf0;
	packet[payload_pos + 2] = 5 + 8 + 4;

	/* set new CRC32 */
	crc32_set = crc32(&packet[payload_pos], 8 + 4 + 4);
	packet[payload_pos + 8 + 4 + 4] = (crc32_set / 0x1000000) & 0xFF;
	packet[payload_pos + 8 + 4 + 4 + 1] = (crc32_set / 0x10000) & 0xFF;
	packet[payload_pos + 8 + 4 + 4 + 2] = (crc32_set / 0x100) & 0xFF;
	packet[payload_pos + 8 + 4 + 4 + 3] = crc32_set & 0xFF;

	/* set new continuity_counter */
	packet[3] = (packet[3] & 0xF0) + (continuity_counter & 0x0F);
}

static inline void ts_giveup_pibuf(ts_output_stat_t *tos)
{
	tos->pos_pi = tos->pos_filled;
}

static inline void copy_current_service_packet(ts_output_stat_t *tos, ts_parse_stat_t *tps, BYTE *packet)
{
	unsigned int pid;
	int ismypid;
	BYTE new_packet[188], *p;

	pid = ts_get_pid(packet);
	ismypid = ts_is_mypid(pid, tos, tps);
	if (pid == 0) {
		/* PATの内容を当該サービスだけにする */
		if (!ts_get_payload_unit_start_indicator(packet)) {
			return; /* ignore */
		}
		memcpy(new_packet, packet, 188);
		ts_simplify_PAT_packet(new_packet, tps->programs[tos->tps_index].service_id, tos->PAT_packet_counter);
		p = new_packet;
		//memcpy(&(tos->buf[tos->pos_filled]), new_packet, 188);
		//tos->pos_filled += 188;
		tos->PAT_packet_counter++;
	} else if (ismypid != 2) { /* 他サービス"のみ"に属するパケットはスルー */
		p = packet;
		//memcpy(&(tos->buf[tos->pos_filled]), packet, 188);
		//tos->pos_filled += 188;
	} else {
		return;
	}

	/* バッファが足りるかどうかのチェック */
	if (tos->pos_filled + 188 > BUFSIZE) {
		if ( ! param_nowait ) {
			ts_wait_pgoutput(tos);
		}
		ts_minimize_buf(tos);
	}
	if (tos->pos_filled + 188 > BUFSIZE) {
		if (param_nowait) {
			tos->dropped_bytes += 188;
			return; /* データを捨てる */
		} else {
			ts_require_buf(tos, 188);
		}
	}

	/* コピー */
	memcpy(&(tos->buf[tos->pos_filled]), p, 188);
	tos->pos_filled += 188;
}