extern int64_t ts_n_drops;
extern int64_t ts_n_total;
extern int64_t ts_n_scrambled;

typedef struct
{
	int remain;
	int skip;
	int bytes;
	int buf_size;
	uint8_t *buf;
} ts_alignment_filter_t;

void ts_packet_counter(ts_header_t *tsh);
void create_ts_alignment_filter(ts_alignment_filter_t *filter);
void delete_ts_alignment_filter(ts_alignment_filter_t *filter);
void ts_alignment_filter(ts_alignment_filter_t *filter, uint8_t **out_buf, int *out_bytes, const uint8_t *in_buf, int in_bytes);
void default_decoder(uint8_t **out_buf, int *out_bytes, const uint8_t *in_buf, int in_bytes);