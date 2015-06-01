#include <Windows.h>
#include <stdio.h>
#include <inttypes.h>

#include "modules_def.h"
#include "IBonDriver2.h"

typedef struct {
	HMODULE hdll;
	pCreateBonDriver_t *pCreateBonDriver;
	IBonDriver *pBon;
	IBonDriver2 *pBon2;
} bondriver_stat_t;

static WCHAR errmsg[1024];

static const WCHAR *bon_dll_name = NULL;
static int sp_num = -1;
static int ch_num = -1;

static const WCHAR *reg_hook_msg;

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

static const WCHAR* hook_postconfig()
{
	if (bon_dll_name == NULL) {
		return NULL;
	}

	if (reg_hook_msg != NULL) {
		_snwprintf_s(errmsg, 1024 - 1, L"generatorフックの登録に失敗しました: %s", reg_hook_msg);
		return errmsg;
	}

	if (ch_num < 0) {
		return L"チャンネルが指定されていないか、または不正です";
	}
	if (sp_num < 0) {
		return L"チューナー空間が指定されていないか、または不正です";
	}

	return NULL;
}

static void hook_stream_generator(void*, unsigned char **buf, int *size)
{
}

static const WCHAR* hook_stream_generator_open(void **param, ch_info_t *chinfo)
{
	bondriver_stat_t *pstat, stat;
	ch_info_t ci;

	stat.hdll = LoadLibrary(bon_dll_name);
	if (stat.hdll == NULL) {
		print_err(L"LoadLibrary", GetLastError());
		return L"BonDriverをロードできませんでした";
	}

	stat.pCreateBonDriver = (pCreateBonDriver_t*)GetProcAddress(stat.hdll, "CreateBonDriver");
	if (stat.pCreateBonDriver == NULL) {
		print_err(L"GetProcAddress", GetLastError());
		FreeLibrary(stat.hdll);
		return L"CreateBonDriver()のポインタを取得できませんでした";
	}

	stat.pBon = stat.pCreateBonDriver();
	if (stat.pBon == NULL) {
		FreeLibrary(stat.hdll);
		return L"CreateBonDriver() returns NULL";
	}

	stat.pBon2 = dynamic_cast<IBonDriver2 *>(stat.pBon);

	if (! stat.pBon2->OpenTuner()) {
		FreeLibrary(stat.hdll);
		return L"OpenTuner() returns FALSE";
	}

	ci.ch_str = stat.pBon2->EnumChannelName(sp_num, ch_num);
	ci.sp_str = stat.pBon2->EnumTuningSpace(sp_num);
	ci.tuner_name = stat.pBon2->GetTunerName();
	ci.ch_num = ch_num;
	ci.sp_num = sp_num;

	wprintf(L"BonTuner: %s\n", ci.tuner_name);
	wprintf(L"Space: %s\n", ci.sp_str);
	wprintf(L"Channel: %s\n", ci.ch_str);
	if (!stat.pBon2->SetChannel(sp_num, ch_num)) {
		stat.pBon2->CloseTuner();
		FreeLibrary(stat.hdll);
		return L"SetChannel() returns FALSE";
	}

	*chinfo = ci;
	pstat = (bondriver_stat_t*)malloc(sizeof(bondriver_stat_t));
	*pstat = stat;
	*param = pstat;
	return NULL;
}

static double hook_stream_generator_siglevel(void*)
{
	return 0.0;
}

static void hook_stream_generator_close(void*)
{
}

static hooks_stream_generator_t hooks_stream_generator = {
	hook_stream_generator_open,
	hook_stream_generator,
	hook_stream_generator_siglevel,
	hook_stream_generator_close
};

static void hook_close_module()
{
}

static void register_hooks()
{
	if (bon_dll_name) {
		reg_hook_msg = register_hooks_stream_generator(&hooks_stream_generator);
	}
	register_hook_close_module(hook_close_module);
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
	{ L"-bon", L"BonDriverのDLL*", 1, set_bon },
	{ L"-sp", L"チューナー空間番号*", 1, set_sp },
	{ L"-ch", L"チャンネル番号*", 1, set_ch },
	NULL,
};

MODULE_DEF module_def_t mod_bondriver = {
	TSDUMP_MODULE_V2,
	L"mod_bondriver",
	register_hooks,
	cmds
};
