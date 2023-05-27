typedef struct output_status_struct				output_status_t;
typedef struct output_status_module_struct		output_status_module_t;
typedef struct output_status_prog_struct		output_status_prog_t;
typedef struct output_status_stream_struct		output_status_stream_t;

struct output_status_struct {
	unsigned int closed : 1;
	unsigned int soft_closed : 1;
	unsigned int close_waiting : 1;
	unsigned int disconnect_tried : 1;
	unsigned int dropped : 1;
	int64_t closetime;
	int downstream_id;
	size_t dropped_bytes;
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
	TSDCHAR fn[MAX_PATH_LEN];
	int close_remain;
	int64_t closetime;
	proginfo_t final_pi;
	int initial_pi_status;
	output_status_module_t *client_array;
	output_status_stream_t *parent;
	int refcount;
};

struct output_status_stream_struct {
	unsigned int need_clear_buf : 1;
	ab_buffer_t *ab;
	ab_history_t *ab_history;
	proginfo_t *proginfo;
	proginfo_t last_proginfo;
	int64_t last_progchange_timenum;
	int proginfo_retry_count;
	int pcr_retry_count;
	int tot_retry_count;
	int64_t last_bufminimize_time;
	int n_pgos;
	output_status_prog_t *pgos;
	int tps_index;
	int singlemode;
	int PAT_packet_counter;
	output_status_prog_t *curr_pgos;
};

void init_tos(output_status_stream_t *tos);
void close_tos(output_status_stream_t *tos);
void prepare_close_tos(output_status_stream_t *tos);
void ts_check_si(output_status_stream_t* tos, int64_t nowtime, ch_info_t* ch_info);
void ts_check_pi(output_status_stream_t *tos, int64_t nowtime, ch_info_t *ch_info);
void ts_output(output_status_stream_t *tos, int64_t nowtime);
int create_tos_per_service(output_status_stream_t **ptos, ts_service_list_t *service_list, ch_info_t *ch_info);

int ts_is_mypid(unsigned int pid, output_status_stream_t *tos, ts_service_list_t *service_list);
int ts_simplify_PAT_packet(uint8_t *new_packet, const uint8_t *old_packet, unsigned int target_sid, unsigned int continuity_counter);
void require_a_few_buffer(ab_buffer_t *ab, ab_history_t *history);
void copy_current_service_packet(output_status_stream_t *tos, ts_service_list_t *service_list, const uint8_t *packet);
