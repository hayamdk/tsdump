#include "core/tsdump_def.h"

#ifdef TSD_PLATFORM_MSVC
#include <Windows.h>
#endif

#include <inttypes.h>
#include <stdlib.h>

#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"

static int hook_postconfig()
{
}

static void hook_stream_generator(void *param, uint8_t **buf, int *size)
{
}

static int hook_stream_generator_open(void **param, ch_info_t *chinfo)
{
}

static double hook_stream_generator_siglevel(void *param)
{
}

static void hook_stream_generator_close(void *param)
{
}

static hooks_stream_generator_t hooks_stream_generator = {
	hook_stream_generator_open,
	hook_stream_generator,
	hook_stream_generator_siglevel,
	hook_stream_generator_close
};

static void register_hooks()
{
	//register_hooks_stream_generator(&hooks_stream_generator);
	register_hook_postconfig(hook_postconfig);
}

static const TSDCHAR *set_file(const TSDCHAR* param)
{
	return NULL;
}

static cmd_def_t cmds[] = {
	{ TSD_TEXT("--filein"), TSD_TEXT("“ü—Íƒtƒ@ƒCƒ‹"), 0, set_file },
	NULL,
};

MODULE_DEF module_def_t mod_filein = {
	TSDUMP_MODULE_V4,
	TSD_TEXT("mod_filein"),
	register_hooks,
	cmds
};