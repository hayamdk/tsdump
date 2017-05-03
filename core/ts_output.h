typedef struct output_status_struct				output_status_t;
typedef struct output_status_module_struct		output_status_module_t;
typedef struct output_status_prog_struct		output_status_prog_t;
typedef struct output_status_stream_struct		output_status_stream_t;

struct output_status_struct {
	unsigned int closed : 1;
	unsigned int close_waiting : 1;
	int64_t closetime;
	int downstream_id;
	void *param;
	output_status_module_t *parent;
};

struct output_status_module_struct {
	module_load_t *module;
	void *param;
	output_status_t *client_array;
	output_status_prog_t *parent;
	int refcount;
	int n_clients;
};

struct output_status_prog_struct {
	unsigned int close_flag1 : 1;
	unsigned int close_flag2 : 1;
	const TSDCHAR *fn;
	int close_remain;
	int64_t closetime;
	proginfo_t final_pi;
	int initial_pi_status;

	output_status_module_t *client_array;
	output_status_stream_t *parent;
	int refcount;
};

struct output_status_stream_struct {
	ab_buffer_t *ab;
	ab_history_t *ab_history;

	proginfo_t *proginfo;
	proginfo_t last_proginfo;
	int64_t last_checkpi_time;
	int proginfo_retry_count;
	int pcr_retry_count;

	int64_t last_bufminimize_time;

	int n_pgos;
	output_status_prog_t *pgos;
	int tps_index;
	int singlemode;
	int PAT_packet_counter;
	int dropped_bytes;

	output_status_prog_t *curr_pgos;

};

void init_tos(output_status_stream_t *tos);
void close_tos(output_status_stream_t *tos);
void ts_check_pi(output_status_stream_t *tos, int64_t nowtime, ch_info_t *ch_info);
void ts_minimize_buf(output_status_stream_t *tos);
void ts_require_buf(output_status_stream_t *tos, int require);
void ts_output(output_status_stream_t *tos, int64_t nowtime);
int ts_wait_pgoutput(output_status_stream_t *tos);
void ts_check_pgoutput(output_status_stream_t *tos);
int create_tos_per_service(output_status_stream_t **ptos, ts_service_list_t *service_list, ch_info_t *ch_info);

static int ts_is_mypid(unsigned int pid, output_status_stream_t *tos, ts_service_list_t *service_list)
{
	int i, j, found = 0, my = 0;
	for (i = 0; i < service_list->n_services; i++) {
		if (pid == service_list->PMT_payloads[i].pid ) {
			if (i == tos->tps_index) {
				my = 1;
			} else {
				found = 1;
			}
		}
		for (j = 0; j < service_list->proginfos[i].n_service_pids; j++) {
			if (pid == service_list->proginfos[i].service_pids[j].pid) {
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

static inline int ts_simplify_PAT_packet(uint8_t *new_packet, const uint8_t *old_packet, unsigned int target_sid, unsigned int continuity_counter)
{
	ts_header_t tsh;
	int payload_pos, table_pos, section_len, n;

	if (!parse_ts_header(old_packet, &tsh)) {
		if (tsh.valid_sync_byte) {
			/* PESパケットでここに来る場合があるので警告はひとまずOFF  e.g. NHK BS1
				PESパケットの規格を要調査 */
			//output_message(MSG_PACKETERROR, L"Invalid ts header! pid=0x%x(%d)", tsh.pid, tsh.pid);
			return 0; /* pass */
		} else {
			output_message(MSG_PACKETERROR, TSD_TEXT("Invalid ts packet!"));
			return 0; /* pass */
		}
	}

	/* 複数パケットにまたがるPATには未対応 */
	if (!tsh.payload_unit_start_indicator) {
		return 0; /* pass */
	}

	/* 不正なパケットかどうかをチェック */
	section_len = ts_get_section_length(old_packet, &tsh);
	if (section_len < 0) {
		output_message(MSG_PACKETERROR, TSD_TEXT("Invalid payload pos!"));
		return 0; /* pass */
	}
	payload_pos = tsh.payload_data_pos;
	table_pos = payload_pos + 8 + 4;
	n = (section_len - 5 - 4 - 4) / 4;
	if ( table_pos + 8 > 188 || n <= 0 || table_pos + n*4 + 2 > 188 ) {
		output_message(MSG_PACKETERROR, TSD_TEXT("Invalid packet!"));
		return 0; /* pass */
	}

	memcpy(new_packet, old_packet, 188);

	int i;
	unsigned int sid;

	uint32_t crc32_set;

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

static inline void copy_current_service_packet(output_status_stream_t *tos, ts_service_list_t *service_list, const uint8_t *packet)
{
	unsigned int pid;
	int ismypid;
	uint8_t new_packet[188];
	const uint8_t *p;
	ts_header_t tsh;

	if (!parse_ts_header(packet, &tsh)) {
		if (tsh.valid_sync_byte) {
			/* PESパケットでここに来る場合があるので警告はひとまずOFF  e.g. NHK BS1
				PESパケットの規格を要調査 */
			//output_message(MSG_PACKETERROR, L"Invalid ts header! pid=0x%x(%d)", tsh.pid, tsh.pid);
		} else {
			output_message(MSG_PACKETERROR, TSD_TEXT("Invalid ts packet!"));
			return;
		}
	}

	pid = tsh.pid;
	ismypid = ts_is_mypid(pid, tos, service_list);
	if (pid == 0) {
		/* PATの内容を当該サービスだけにする */
		if ( !ts_simplify_PAT_packet(new_packet, packet, service_list->proginfos[tos->tps_index].service_id, tos->PAT_packet_counter) ) {
			return;
		}
		p = new_packet;
		tos->PAT_packet_counter++;
	} else if (ismypid != 2) { /* 他サービス"のみ"に属するパケットはスルー */
		p = packet;
	} else {
		return;
	}

	ab_input_buf(tos->ab, p, 188);
}
