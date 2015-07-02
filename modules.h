extern module_def_t mod_core;
extern module_def_t mod_bondriver;
extern module_def_t mod_b25decoder;
extern module_def_t mod_fileoutput_win;
extern module_def_t mod_fileoutput_stdio;
extern module_def_t mod_pipeoutput_win;
extern module_def_t mod_cmdexec;
extern module_def_t mod_path_resolver;

module_def_t *static_modules[] = {
	&mod_core,
	&mod_bondriver,
	&mod_b25decoder,
	&mod_fileoutput_win,
	//&mod_fileoutput_stdio,
	&mod_pipeoutput_win,
	&mod_cmdexec,
	&mod_path_resolver,
};