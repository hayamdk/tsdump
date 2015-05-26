int load_modules();
void free_modules();
int get_cmd_params(int argc, WCHAR* argv[]);
void print_cmd_usage();

void **do_pgoutput_create(WCHAR *fname, ProgInfo *pi, ch_info_t *ch_info);
void do_pgoutput(void **modulestats, unsigned char *buf, size_t size);
int do_pgoutput_check(void **modulestats);
int do_pgoutput_wait(void **modulestats);
void do_pgoutput_close(void **modulestats, ProgInfo *pi);
int do_postconfig();
void do_close_module();
void do_open_stream();
void do_encrypted_stream(unsigned char *buf, size_t size);
void do_stream(unsigned char *buf, size_t size, int encrypted);
void do_close_stream();
void do_stream_generator(unsigned char **buf, int *size);
void do_stream_decoder(unsigned char **dst_buf, int *dst_size, unsigned char *src_buf, int src_size);