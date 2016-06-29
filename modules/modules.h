/* 静的リンクするモジュールの記述 */

extern module_def_t mod_core;
extern module_def_t mod_path_resolver;
extern module_def_t mod_log;
extern module_def_t mod_filein;
extern module_def_t mod_fileout;

#ifdef TSD_PLATFORM_MSVC
extern module_def_t mod_bondriver;
extern module_def_t mod_b25decoder;
extern module_def_t mod_pipeoutput_win;
extern module_def_t mod_cmdexec_win;
#else
extern module_def_t mod_dvb;
extern module_def_t mod_arib25;
#endif

module_def_t *static_modules[] = {
	&mod_core,
	&mod_path_resolver,
	&mod_log,
	&mod_filein,
	&mod_fileout,
#ifdef TSD_PLATFORM_MSVC
	&mod_bondriver,
	&mod_b25decoder,
	&mod_pipeoutput_win,
	&mod_cmdexec_win,
#else
	&mod_dvb,
	&mod_arib25,
#endif
};
