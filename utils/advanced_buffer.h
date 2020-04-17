typedef struct ab_buffer_struct ab_buffer_t;
typedef struct ab_downstream_struct ab_downstream_t;
typedef struct ab_history_struct ab_history_t;

extern const int ab_use_magic_ring_buffer;

typedef struct {
	int (*output)(ab_buffer_t*, void*, const uint8_t*, int);
	void (*notify_skip)(ab_buffer_t*, void*, int);
	void (*close)(ab_buffer_t*, void*, const uint8_t*, int);
	int (*pre_output)(ab_buffer_t*, void*, int*);
} ab_downstream_handler_t;

int ab_next_downstream(ab_buffer_t *ab, int id);
int ab_first_downstream(ab_buffer_t *ab);

void ab_init(ab_buffer_t *ab, int buf_size);
ab_buffer_t* ab_create(int buf_size);
void ab_delete(ab_buffer_t *ab);
int ab_connect_downstream_backward(ab_buffer_t *ab, const ab_downstream_handler_t *handler, int alignment_size, void *param, int backward_size);
int ab_connect_downstream_history_backward(ab_buffer_t *ab, const ab_downstream_handler_t *handler, int alignment_size, void *param, ab_history_t *history);
int ab_connect_downstream(ab_buffer_t *ab, const ab_downstream_handler_t *handler, int alignment_size, void *param);
void ab_set_maxsize(ab_buffer_t *ab, int id, int maxsize);
void ab_set_minsize(ab_buffer_t *ab, int id, int minsize);
void ab_set_realtime(ab_buffer_t *ab, int id);
void ab_set_use_retval(ab_buffer_t *ab, int id);
void ab_clear_buf(ab_buffer_t *ab, int require_size);
void ab_input_buf(ab_buffer_t *ab, const uint8_t *buf, int size);
void ab_output_buf(ab_buffer_t *ab);
void ab_close_buf(ab_buffer_t *ab);

void ab_get_status(ab_buffer_t *ab, int *buf_used, int *buf_offset);
int ab_get_downstream_status(ab_buffer_t *ab, int id, int *buf_pos, int *remain_size);
void ab_disconnect_downstream(ab_buffer_t *ab, int id, int immediate);

int ab_set_history(ab_buffer_t *ab, ab_history_t **history, int resolution_ms, int backward_ms);
int ab_get_history_backward_bytes(ab_history_t *history);
int ab_get_history_bytes(ab_history_t *history, int n);
