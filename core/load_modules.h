typedef struct {
	hook_pgoutput_precreate_t hook_pgoutput_precreate;
	hook_pgoutput_changed_t hook_pgoutput_changed;
	hook_pgoutput_end_t hook_pgoutput_end;
	hook_pgoutput_postclose_t hook_pgoutput_postclose;
	hook_pgoutput_create_t hook_pgoutput_create;
	hook_pgoutput_t hook_pgoutput;
	hook_pgoutput_check_t hook_pgoutput_check;
	hook_pgoutput_wait_t hook_pgoutput_wait;
	hook_pgoutput_close_t hook_pgoutput_close;
	hook_pgoutput_forceclose_t hook_pgoutput_forceclose;
	hook_postconfig_t hook_postconfig;
	hook_preclose_module_t hook_preclose_module;
	hook_close_module_t hook_close_module;
	hook_open_stream_t hook_open_stream;
	hook_encrypted_stream_t hook_encrypted_stream;
	hook_stream_t hook_stream;
	hook_close_stream_t hook_close_stream;
	hook_message_t hook_message;
	hook_tick_t hook_tick;
	int output_block_size;
} module_hooks_t;

typedef struct {
	module_def_t *def;
#ifdef TSD_PLATFORM_MSVC
	HMODULE handle;
#else
	void *handle;
#endif
	module_hooks_t hooks;
} module_load_t;

#define MAX_MODULES			32

extern module_load_t modules[MAX_MODULES];
extern int n_modules;

void print_cmd_usage();
int init_modules(int argc, const TSDCHAR* argv[]);
int load_modules();
void free_modules();

void set_stream_stats_mbps(const double);
void add_stream_stats_total_bytes(const int);

int do_postconfig();
void do_close_module();
void do_open_stream();
void do_encrypted_stream(unsigned char *buf, size_t size);
void do_stream(unsigned char *buf, size_t size, int encrypted);
void do_close_stream();
void do_tick(int64_t);
void do_preclose_module();
void do_close_module();
int do_stream_generator_open(void **param, ch_info_t *chinfo);
void do_stream_generator(void *param, unsigned char **buf, int *size);
void do_stream_generator_cnr(void *param);
void do_stream_generator_siglevel(void *param);
void do_stream_generator_close(void *param);
void do_stream_decoder(void *param, unsigned char **dst_buf, int *dst_size, const unsigned char *src_buf, int src_size);
int do_stream_decoder_open(void **param, int *);
int do_stream_generator_wait(void *param, int timeout_ms);
int is_implemented_stream_decoder_stats();
void do_stream_decoder_stats(void *param);
void do_stream_decoder_close(void *param);
void do_message(const TSDCHAR *modname, message_type_t msgtype, tsd_syserr_t *err, const TSDCHAR *msg);
const TSDCHAR *do_path_resolver(const proginfo_t *proginfo, const ch_info_t *ch_info);