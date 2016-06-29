#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <arib25/arib_std_b25.h>
#include <arib25/b_cas_card.h>

#include "core/tsdump_def.h"
#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"

static int use_arib25 = 0;
static int reg_hook = 0;

typedef struct {
	ARIB_STD_B25 *b25;
	B_CAS_CARD *bcas;
} arib25_stat_t;

static int hook_stream_decoder_open(void **param, int *encrypted)
{
	int code;
	arib25_stat_t stat, *pstat;

	stat.b25 = create_arib_std_b25();
	if (!stat.b25) {
		output_message(MSG_ERROR, "failed: create_arib_std_b25()");
		return 0;
	}

	code = stat.b25->set_multi2_round(stat.b25, 4);
	if (code < 0) {
		output_message(MSG_ERROR, "failed: b25->set_multi2_round(), errcode=%d", code);
		return 0;
	}

	code = stat.b25->set_strip(stat.b25, 0);
	if (code < 0) {
		output_message(MSG_ERROR, "failed: b25->set_strip(), errcode=%d", code);
		return 0;
	}

	stat.bcas = create_b_cas_card();
	if (!stat.bcas) {
		output_message(MSG_ERROR, "failed: create_b_cas_card()");
		return 0;
	}

	code = stat.bcas->init(stat.bcas);
	if (code < 0) {
		output_message(MSG_ERROR, "failed: bcas->init(), errcode=%d", code);
		return 0;
	}

	code = stat.b25->set_b_cas_card(stat.b25, stat.bcas);
	if (code < 0) {
		output_message(MSG_ERROR, "failed: b25->set_b_cas_card(), errcode=%d", code);
		return 0;
	}

	pstat = (arib25_stat_t*)malloc(sizeof(arib25_stat_t));
	*pstat = stat;
	*param = pstat;

	return 1;
}

static void hook_stream_decoder(void *param, uint8_t **dst_buf, int *dst_size, const uint8_t *src_buf, int src_size)
{
	int code;
	arib25_stat_t *pstat = (arib25_stat_t*)param;

	ARIB_STD_B25_BUFFER buf_in, buf_out;

	buf_in.data = (uint8_t*)src_buf;
	buf_in.size = src_size;

	code = pstat->b25->put(pstat->b25, &buf_in);
	if (code < 0) {
		output_message(MSG_ERROR, "failed: b25->put(), errcode=%d", code);
		*dst_size = 0;
		*dst_buf = NULL;
		return;
	}

	code = pstat->b25->get(pstat->b25, &buf_out);
	if (code < 0) {
		output_message(MSG_ERROR, "failed: b25->get(), errcode=%d", code);
		*dst_size = 0;
		*dst_buf = NULL;
		return;
	}
	*dst_buf = buf_out.data;
	*dst_size = buf_out.size;
	//printf("%d -> %d\n", buf_in.size, buf_out.size);
}

static void hook_stream_decoder_close(void *param)
{
	arib25_stat_t *pstat = (arib25_stat_t*)param;
	pstat->b25->release(pstat->b25);
	pstat->bcas->release(pstat->bcas);
	free(pstat);
}

static int hook_postconfig()
{
	if (!use_arib25) {
		return 1;
	}

	if (!reg_hook) {
		output_message(MSG_ERROR, "decoderフックの登録に失敗しました");
		return 0;
	}
	return 1;
}

static hooks_stream_decoder_t hooks_stream_decoder = {
	hook_stream_decoder_open,
	hook_stream_decoder,
	NULL, /* not implemented */
	hook_stream_decoder_close
};

static void register_hooks()
{
	if (use_arib25) {
		reg_hook = register_hooks_stream_decoder(&hooks_stream_decoder);
	}
	register_hook_postconfig(hook_postconfig);
}

static const char *set_arib25(const char *param)
{
	UNREF_ARG(param);
	use_arib25 = 1;
	return NULL;
}

static cmd_def_t cmds[] = {
	{ "--arib25", "arib25によってデコードを行う", 0, set_arib25 },
	NULL,
};

MODULE_DEF module_def_t mod_arib25 = {
	TSDUMP_MODULE_V4,
	"mod_arib25",
	register_hooks,
	cmds
};
