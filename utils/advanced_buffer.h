#define GB_MAX_DOWNSTREAMS	32

typedef struct {
	void (*output)(void*, const uint8_t*, int);
	void (*notify_skip)(void*, int);
	void (*close)(void*, const uint8_t*, int);
	void (*start_hook)(void*, int i);
	int (*pre_output)(void*, int*);
} ab_downstream_handler_t;

typedef struct {
	ab_downstream_handler_t handler;
	void *param;
	int busy : 1;
	int close_flg : 1;
	int pos;
	int remain_to_close;
	int alignment_size;
	int max_size;
} ab_downstream_t;

typedef struct {
	ab_downstream_t downstreams[GB_MAX_DOWNSTREAMS];
	uint64_t input_total;
	int n_downstreams;
	int buf_used;
	int buf_size;
	uint8_t *buf;
} ab_buffer_t;

typedef struct {
	int64_t timenum;
	int bytes;
} ab_history_record_t;

typedef struct {
	ab_buffer_t *buffer;
	ab_history_record_t *records;
	int resolution;
	int num;
	int used;
	int backward_ms;
	int buf_remain_bytes;
	int backward_bytes;
} ab_history_t;

void ab_init_buf(ab_buffer_t *gb, int buf_size);
int ab_connect_downstream_backward(ab_buffer_t *gb, const ab_downstream_handler_t *handler, int alignment_size, int max_size, void *param, int backward_size);
int ab_connect_downstream(ab_buffer_t *gb, const ab_downstream_handler_t *handler, int alignment_size, int max_size, void *param);
void ab_clear_buf(ab_buffer_t *gb, int require_size);
void ab_input_buf(ab_buffer_t *gb, const uint8_t *buf, int size);
void ab_output_buf(ab_buffer_t *gb);
void ab_close_buf(ab_buffer_t *gb);

void ab_get_bufinfo(ab_buffer_t *gb, int id, int *buf_size, int *buf_used, int *buf_pos);
void ab_disconnect_downstream(ab_buffer_t *gb, int id, int immediate);

int ab_set_history(ab_buffer_t *gb, ab_history_t *history, int resolution_ms, int backward_ms);
int ab_get_history_backward_bytes(ab_history_t *history);
