void print_cmd_usage();
int init_modules(int argc, WCHAR* argv[]);
int load_modules();
void free_modules();

void **do_pgoutput_create(const WCHAR *fname, const proginfo_t *pi, ch_info_t *ch_info);
void do_pgoutput(void **modulestats, unsigned char *buf, size_t size);
int do_pgoutput_check(void **modulestats);
int do_pgoutput_wait(void **modulestats);
void do_pgoutput_close(void **modulestats, const proginfo_t*);
int do_postconfig();
void do_close_module();
void do_open_stream();
void do_encrypted_stream(unsigned char *buf, size_t size);
void do_stream(unsigned char *buf, size_t size, int encrypted);
void do_close_stream();
int do_stream_generator_open(void **param, ch_info_t *chinfo);
void do_stream_generator(void *param, unsigned char **buf, int *size);
double do_stream_generator_siglevel(void *param);
void do_stream_generator_close(void *param);
void do_stream_decoder(void *param, unsigned char **dst_buf, int *dst_size, const unsigned char *src_buf, int src_size);
int do_stream_decoder_open(void **param, int *);
void do_stream_decoder_stats(void *param, decoder_stats_t *stats);
void do_stream_decoder_close(void *param);
void do_message(const WCHAR *modname, message_type_t msgtype, DWORD *err, const WCHAR *msg);
const WCHAR *do_path_resolver(const proginfo_t *proginfo, const ch_info_t *ch_info);