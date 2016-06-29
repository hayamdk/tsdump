extern int64_t ts_n_drops;
extern int64_t ts_n_total;
extern int64_t ts_n_scrambled;

void ts_statics_counter(ts_header_t *tsh);
void default_decoder(unsigned char **decbuf, int *n_decbuf, const unsigned char *buf, int n_buf);