typedef struct
{
	const WCHAR *fn;
	int delay_remain;
	int close_remain;
	int close_flag;
	int64_t closetime;
	void **modulestats;
	//ProgInfo final_pi;

	proginfo_t final_pi;
	int initial_pi_status;

} pgoutput_stat_t;

typedef struct
{
	int64_t time;
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

	//ProgInfo pi;
	//ProgInfo pi_last;

	proginfo_t *proginfo;
	proginfo_t last_proginfo;
	int64_t last_nopi_time;

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
void ts_check_pi(ts_output_stat_t *tos, int64_t nowtime, ch_info_t *ch_info);
void ts_minimize_buf(ts_output_stat_t *tos);
void ts_require_buf(ts_output_stat_t *tos, int require);
void ts_copy_backward(ts_output_stat_t *tos, int64_t nowtime);
void ts_output(ts_output_stat_t *tos, int64_t nowtime, int);
int ts_wait_pgoutput(ts_output_stat_t *tos);
void ts_check_pgoutput(ts_output_stat_t *tos);
int create_tos_per_service(ts_output_stat_t **ptos, ts_parse_stat_t *tps, ch_info_t *ch_info);

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

static inline void ts_update_transfer_history(ts_output_stat_t *tos, int64_t nowtime, int bytes)
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

static inline int ts_simplify_PAT_packet(uint8_t *new_packet, const uint8_t *old_packet, unsigned int target_sid, unsigned int continuity_counter)
{
	int payload_pos = ts_get_payload_data_pos(old_packet);
	int table_pos = payload_pos + 8 + 4;
	int section_len = ts_get_section_length(old_packet);
	int n = (section_len - 5 - 4 - 4) / 4;

	/* 複数パケットにまたがるPATには未対応 */
	if (!ts_get_payload_unit_start_indicator(old_packet)) {
		return 0; /* pass */
	}

	/* 不正なパケットかどうかをチェック */
	if ( table_pos + 8 > 188 || n <= 0 || table_pos + n*4 + 2 > 188 ) {
		output_message(MSG_PACKETERROR, L"Invalid packet!");
		return 0; /* pass */
	}

	memcpy(new_packet, old_packet, 188);

	int i;
	unsigned int sid;

	unsigned __int32 crc32_set;

	for (i = 0; i < n; i++) {
		sid = new_packet[table_pos + i * 4] * 256 + new_packet[table_pos + i * 4 + 1];
		if (sid == target_sid) {
			memmove(&new_packet[table_pos], &new_packet[table_pos + i * 4], 4);
			break;
		}
	}
	/* set new section_length */
	new_packet[payload_pos + 1] = new_packet[payload_pos + 1] & 0xf0;
	new_packet[payload_pos + 2] = 5 + 8 + 4;

	/* set new CRC32 */
	crc32_set = crc32(&new_packet[payload_pos], 8 + 4 + 4);
	new_packet[table_pos + 4] = (crc32_set / 0x1000000) & 0xFF;
	new_packet[table_pos + 4 + 1] = (crc32_set / 0x10000) & 0xFF;
	new_packet[table_pos + 4 + 2] = (crc32_set / 0x100) & 0xFF;
	new_packet[table_pos + 4 + 3] = crc32_set & 0xFF;

	/* set new continuity_counter */
	new_packet[3] = (new_packet[3] & 0xF0) + (continuity_counter & 0x0F);

	return 1;
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
		if ( !ts_simplify_PAT_packet(new_packet, packet, tps->programs[tos->tps_index].service_id, tos->PAT_packet_counter) ) {
			return;
		}
		p = new_packet;
		tos->PAT_packet_counter++;
	} else if (ismypid != 2) { /* 他サービス"のみ"に属するパケットはスルー */
		p = packet;
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
