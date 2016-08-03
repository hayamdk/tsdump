#include <Windows.h>
#include <stdio.h>
#include <inttypes.h>

#include "core/tsdump_def.h"
#include "utils/arib_proginfo.h"
#include "core/module_hooks.h"

#include "IBonDriver2.h"

typedef struct {
	HMODULE hdll;
	pCreateBonDriver_t *pCreateBonDriver;
	IBonDriver *pBon;
	IBonDriver2 *pBon2;
	DWORD n_rem;
} bondriver_stat_t;

//static WCHAR errmsg[1024];

static const WCHAR *bon_dll_name = NULL;
static int sp_num = -1;
static int ch_num = -1;

//static const WCHAR *reg_hook_msg;
static int reg_hook = 0;

LPWSTR lasterr_msg()
{
	LPWSTR msg;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		GetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)(&msg),
		0,
		NULL
	);
	return msg;
}

static int hook_postconfig()
{
	if (bon_dll_name == NULL) {
		return 1;
	}

	if (!reg_hook) {
		output_message(MSG_ERROR, L"generatorフックの登録に失敗しました");
		return 0;
	}

	if (ch_num < 0) {
		output_message(MSG_ERROR, L"チャンネルが指定されていないか、または不正です");
		return 0;
	}
	if (sp_num < 0) {
		output_message(MSG_ERROR, L"チューナー空間が指定されていないか、または不正です");
		return 0;
	}

	return 1;
}

static void hook_stream_generator(void *param, unsigned char **buf, int *size)
{
	DWORD n_recv;
	bondriver_stat_t *pstat = (bondriver_stat_t*)param;

	/* tsをチューナーから取得 */
	if (!pstat->pBon2->GetTsStream(buf, &n_recv, &pstat->n_rem)) {
		*size = 0;
		*buf = NULL;
		return;
	}
	*size = n_recv;
}

static int hook_stream_generator_wait(void *param, int timeout_ms)
{
	DWORD ret;
	bondriver_stat_t *pstat = (bondriver_stat_t*)param;

	if (pstat->n_rem > 0) {
		return 1;
	}
	if (timeout_ms > 0) {
		ret = pstat->pBon2->WaitTsStream(timeout_ms);
		if (ret == WAIT_OBJECT_0) {
			return 1;
		}
	}
	return 0;
}

static int hook_stream_generator_open(void **param, ch_info_t *chinfo)
{
	bondriver_stat_t *pstat, stat;
	ch_info_t ci;

	stat.hdll = LoadLibrary(bon_dll_name);
	if (stat.hdll == NULL) {
		output_message(MSG_SYSERROR, L"BonDriverをロードできませんでした(LoadLibrary): %s", bon_dll_name);
		return 0;
	}

	stat.pCreateBonDriver = (pCreateBonDriver_t*)GetProcAddress(stat.hdll, "CreateBonDriver");
	if (stat.pCreateBonDriver == NULL) {
		FreeLibrary(stat.hdll);
		output_message(MSG_SYSERROR, L"CreateBonDriver()のポインタを取得できませんでした(GetProcAddress): %s", bon_dll_name);
		return 0;
	}

	stat.pBon = stat.pCreateBonDriver();
	if (stat.pBon == NULL) {
		FreeLibrary(stat.hdll);
		output_message(MSG_ERROR, L"CreateBonDriver()に失敗しました: %s", bon_dll_name);
		return 0;
	}

	stat.pBon2 = dynamic_cast<IBonDriver2 *>(stat.pBon);

	if (! stat.pBon2->OpenTuner()) {
		FreeLibrary(stat.hdll);
		output_message(MSG_ERROR, L"OpenTuner()に失敗しました");
		return 0;
	}

	ci.ch_str = stat.pBon2->EnumChannelName(sp_num, ch_num);
	ci.sp_str = stat.pBon2->EnumTuningSpace(sp_num);
	ci.tuner_name = stat.pBon2->GetTunerName();
	ci.ch_num = ch_num;
	ci.sp_num = sp_num;

	if (!ci.tuner_name) {
		ci.tuner_name = L"NullTuner";
	}
	if (!ci.ch_str) {
		ci.ch_str = L"Null";
	}
	if (!ci.sp_str) {
		ci.sp_str = L"Null";
	}

	/* これを入れておかないとSetChannelに失敗するBonDriverが存在する e.g. BonDriver PT-ST 人柱版3 */
	Sleep(500);

	output_message(MSG_NOTIFY, L"BonTuner: %s\nSpace: %s\nChannel: %s",
		ci.tuner_name, ci.sp_str, ci.ch_str);
	if (!stat.pBon2->SetChannel(sp_num, ch_num)) {
		stat.pBon2->CloseTuner();
		FreeLibrary(stat.hdll);
		output_message(MSG_ERROR, L"SetChannel()に失敗しました");
		return 0;
	}

	stat.n_rem = 1;

	*chinfo = ci;
	pstat = (bondriver_stat_t*)malloc(sizeof(bondriver_stat_t));
	*pstat = stat;
	*param = pstat;
	return 1;
}

static void hook_stream_generator_cnr(void *param, double *cnr, signal_value_scale_t *scale)
{
	bondriver_stat_t *pstat = (bondriver_stat_t*)param;
	*cnr = pstat->pBon2->GetSignalLevel();
	*scale = TSDUMP_SCALE_DECIBEL;
}

static void hook_stream_generator_close(void *param)
{
	bondriver_stat_t *pstat = (bondriver_stat_t*)param;
	pstat->pBon2->CloseTuner();
	pstat->pBon2->Release();
	FreeLibrary(pstat->hdll);
}

static hooks_stream_generator_t hooks_stream_generator = {
	hook_stream_generator_open,
	hook_stream_generator,
	hook_stream_generator_wait,
	NULL,
	hook_stream_generator_cnr,
	hook_stream_generator_close
};

static void register_hooks()
{
	if (bon_dll_name) {
		reg_hook = register_hooks_stream_generator(&hooks_stream_generator);
	}
	register_hook_postconfig(hook_postconfig);
}

static const WCHAR *set_bon(const WCHAR* param)
{
	bon_dll_name = _wcsdup(param);
	return NULL;
}

static const WCHAR* set_sp(const WCHAR *param)
{
	sp_num = _wtoi(param);
	if (sp_num < 0) {
		return L"スペース番号が不正です";
	}
	return NULL;
}

static const WCHAR* set_ch(const WCHAR *param)
{
	ch_num = _wtoi(param);
	if (ch_num < 0) {
		return L"チャンネル番号が不正です";
	}
	return NULL;
}

static cmd_def_t cmds[] = {
	{ L"--bon", L"BonDriverのDLL *", 1, set_bon },
	{ L"--sp", L"チューナー空間番号 *", 1, set_sp },
	{ L"--ch", L"チャンネル番号 *", 1, set_ch },
	NULL,
};

TSD_MODULE_DEF(
	mod_bondriver,
	register_hooks,
	cmds,
	NULL
);
