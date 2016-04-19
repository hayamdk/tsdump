#include <Windows.h>
#include <stdio.h>
#include <inttypes.h>

#include "module_def.h"
#include "ts_proginfo.h"
#include "module_hooks.h"
#include "IB25Decoder.h"

typedef IB25Decoder2* (pCreateB25Decoder2_t)(void);

typedef struct {
	HMODULE hdll;
	pCreateB25Decoder2_t *pCreateB25Decoder2;
	IB25Decoder2 *pB25Decoder2;
} b25decoder_stat_t;

static int use_b25dll = 0;
static int reg_hook = 0;

static int hook_stream_decoder_open(void **param, int *encrypted)
{
	b25decoder_stat_t *pstat, stat;

	stat.hdll = LoadLibrary(L"B25Decoder.dll");
	if (stat.hdll == NULL) {
		output_message(MSG_SYSERROR, L"B25Decoder.dllをロードできませんでした(LoadLibrary)");
		return 0;
	}

	stat.pCreateB25Decoder2 = (pCreateB25Decoder2_t*)GetProcAddress(stat.hdll, "CreateB25Decoder2");
	if (stat.pCreateB25Decoder2 == NULL) {
		FreeLibrary(stat.hdll);
		output_message(MSG_SYSERROR, L"CreateB25Decoder2()のポインタを取得できませんでした(GetProcAddress)");
		return 0;
	}

	stat.pB25Decoder2 = stat.pCreateB25Decoder2();

	if (stat.pB25Decoder2 == NULL) {
		FreeLibrary(stat.hdll);
		output_message(MSG_ERROR, L"CreateB25Decoder2()に失敗");
		return 0;
	}

	if ( ! stat.pB25Decoder2->Initialize() ) {
		FreeLibrary(stat.hdll);
		output_message(MSG_ERROR, L"pB25Decoder2->Initialize()に失敗");
		return 0;
	}

	pstat = (b25decoder_stat_t*)malloc(sizeof(b25decoder_stat_t));
	*pstat = stat;
	*param = pstat;
	*encrypted = 0;
	return 1;
}

static void hook_stream_decoder(void *param, unsigned char **dst_buf, int *dst_size, const unsigned char *src_buf, int src_size)
{
	DWORD dw_dst_size;
	BOOL ret;
	b25decoder_stat_t *pstat = (b25decoder_stat_t*)param;

	if (src_size > 0) {
		ret = pstat->pB25Decoder2->Decode((BYTE*)src_buf, src_size, dst_buf, &dw_dst_size);
		if (ret) {
			*dst_size = dw_dst_size;
			return;
		}
	}
	/* else */
	*dst_size = 0;
	*dst_buf = NULL;
}

static void hook_stream_decoder_stats(void *param, decoder_stats_t *stats)
{
	b25decoder_stat_t *pstat = (b25decoder_stat_t*)param;
	stats->n_dropped = pstat->pB25Decoder2->GetContinuityErrNum();
	stats->n_input = pstat->pB25Decoder2->GetInputPacketNum();
	stats->n_output = pstat->pB25Decoder2->GetOutputPacketNum();
	stats->n_scrambled = pstat->pB25Decoder2->GetScramblePacketNum();
}

static void hook_stream_decoder_close(void *param)
{
	b25decoder_stat_t *pstat = (b25decoder_stat_t*)param;
	pstat->pB25Decoder2->Release();
	FreeLibrary(pstat->hdll);
}

static hooks_stream_decoder_t hooks_stream_decoder = {
	hook_stream_decoder_open,
	hook_stream_decoder,
	hook_stream_decoder_stats,
	hook_stream_decoder_close
};

static int hook_postconfig()
{
	if (!use_b25dll) {
		return 1;
	}

	if (!reg_hook) {
		output_message(MSG_ERROR, L"generatorフックの登録に失敗しました");
		return 0;
	}

	return 1;
}

static void register_hooks()
{
	if (use_b25dll) {
		reg_hook = register_hooks_stream_decoder(&hooks_stream_decoder);
	}
	register_hook_postconfig(hook_postconfig);
}

static const WCHAR *set_b25dll(const WCHAR*)
{
	use_b25dll = 1;
	return NULL;
}

static cmd_def_t cmds[] = {
	{ L"--b25dec", L"B25Decoderによってデコードを行う", 0, set_b25dll },
	NULL,
};

MODULE_DEF module_def_t mod_b25decoder = {
	TSDUMP_MODULE_V4,
	L"mod_b25decoder",
	register_hooks,
	cmds
};
