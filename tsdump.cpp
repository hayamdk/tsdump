#define _CRT_SECURE_NO_WARNINGS

#pragma comment(lib, "shlwapi.lib")

#include <stdio.h>
#include <windows.h>
#include <tchar.h>
#include <locale.h>
#include <signal.h>
#include <time.h>
#include <shlwapi.h>
#include <sys/types.h>
#include <sys/timeb.h>

#include "IBonDriver2.h"
#include "IB25Decoder.h"

#include "modules_def.h"
#include "tsdump.h"
#include "ts_parser.h"
#include "ts_output.h"
#include "load_modules.h"

//#define HAVE_TIMECALC_DECLARATION
//#include "timecalc.h"

int BUFSIZE = BUFSIZE_DEFAULT * 1024 * 1024;
int OVERLAP_SEC = OVERLAP_SEC_DEFAULT;
int CHECK_INTERVAL = CHECK_INTERVAL_DEFAULT;
int MAX_PGOVERLAP = MAX_PGOVERLAP_DEFAULT;

int termflag = 0;

const WCHAR *bon_ch_name = NULL;
const WCHAR *bon_sp_name = NULL;
const WCHAR *bon_tuner_name = NULL;

const WCHAR *param_bon_dll_name = NULL;
int param_sp_num = -1;
int param_ch_num = -1;
WCHAR param_base_dir[MAX_PATH_LEN];
int param_all_services;
int param_services[MAX_SERVICES];
int param_n_services = 0;
int param_nodec = 0;
int param_nowait = 0;

void signal_handler(int)
{
	termflag = 1;
	printf("\n終了シグナルをキャッチ\n");
}

FILE *logfp;

void open_log(FILE **fp)
{
	TCHAR fn[MAX_PATH_LEN];
	_stprintf_s(fn, MAX_PATH_LEN - 1, _T("%s%s.log"), param_base_dir, bon_ch_name);
	*fp = _tfopen(fn, _T("a+"));
}

int decode_dummy(BYTE *buf, DWORD n_buf, BYTE **decbuf, DWORD *n_decbuf)
{
	static BYTE *tmpbuf = NULL;
	static DWORD n_tmpbuf = 0;
	static BYTE rem[188];
	static DWORD n_rem = 0;

	//*decbuf = buf;
	//*n_decbuf = n_buf;
	//return 1;

	//n_rem = 0;

	DWORD n = n_rem + n_buf;

	if (n > n_tmpbuf) {
		if (tmpbuf != NULL) {
			free(tmpbuf);
		}
		tmpbuf = (BYTE*)malloc(n);
		n_tmpbuf = n;
	}

	memcpy(tmpbuf, rem, n_rem);
	memcpy(&tmpbuf[n_rem], buf, n_buf);

	DWORD i = 0;
	while (tmpbuf[i] != 0x47 && i < n && i < 188) {
		i++;
	}

	DWORD n_dec = (n - i) / 188 * 188;

	n_rem = n - i - n_dec;
	memcpy(rem, &tmpbuf[i + n_dec], n_rem);

	*decbuf = &tmpbuf[i];
	*n_decbuf = n_dec;

	if (i != 0) {
		printf("[WARN] skipped %d bytes\n", i);
	}

	/* DROP数をカウント */
	int c;
	for (c = 0; c < (int)n_dec; c += 188) {
		ts_drop_counter(&(*decbuf)[c]);
	}

//	if (n_rem != 0) {
		//printf("n=%d rem=%d\n", n, n_rem);
//	}

	return 1;
}

void print_buf(ts_output_stat_t *tos, int n_tos)
{
	int n, i, j, backward_size;
	char line[100];
	char *p = line;
	static int cnt = 0;

	if(!tos) {
		return;
	}

	int width = (74-(n_tos-1)) / n_tos;

	for (i = 0; i < n_tos; i++) {
		for (backward_size = j = 0; j < tos[i].n_th; j++) {
			backward_size += tos[i].th[j].bytes;
		}

		for (n = 0; n < width; n++) {
			int pos = int( (double)BUFSIZE / width * (n+0.5) );
			if (pos < tos[i].pos_write) {
				*p = '-';
			} else if (pos < tos[i].pos_filled) {
				*p = '!';
			} else {
				*p = '_';
			}
			if (pos > tos[i].pos_filled - backward_size) {
				if (*p == '-') {
					*p = '+';
				} else if(*p == '/') {
					*p = '|';
				}
			}
			p++;
		}
		if (i != n_tos - 1) {
			*(p++) = ' ';
		}
	}
	*p = '\0';
	printf("buf: %s\r", line);
}

void main_loop(IBonDriver2 *pBon2, IB25Decoder2 *pB25Decoder2)
{
	BYTE *recvbuf, *decbuf;
	DWORD n_recv=0, n_dec, rem_recv;

	__int64 total = 0;
	__int64 subtotal = 0;

	__int64 nowtime, lasttime;

	ts_output_stat_t *tos = NULL;
	int n_tos = 0;
	ts_parse_stat_t tps = {};

	lasttime = nowtime = gettime();

	double tdiff, Mbps=0.0;

	int i;
	int single_mode = 0;

	int pos;

	//open_log(&logfp);

	if ( !param_all_services && param_n_services == 0 ) {
		single_mode = 1;
	}

	int gettscount = 0;

	do_open_stream();

	while ( !termflag ) {
		nowtime = gettime();

		/* 前回空取得だった場合少し待ってみる */
		if (n_recv == 0) {
			pBon2->WaitTsStream(100);
		}
		/* tsをチューナーから取得 */
		pBon2->GetTsStream(&recvbuf, &n_recv, &rem_recv);
		gettscount++;

		do_encrypted_stream(recvbuf, n_recv);

		/* tsをデコード */
		//tc_start("decode");
		if(n_recv > 0) {
			int ret;
			if (param_nodec) {
				ret = decode_dummy(recvbuf, n_recv, &decbuf, &n_dec);
			} else {
				ret = pB25Decoder2->Decode(recvbuf, n_recv, &decbuf, &n_dec);
			}

			if (ret == 0 || n_dec == 0) {
				//fprintf(logfp, "[INFO] ret=%d n_dec=%d\n", ret, n_dec);
				if (ret == 0) {
					printf("[ERROR] Decoder returns 0\n");
				}
			}
		} else {
			n_dec = 0;
			decbuf = recvbuf;
		}
		//tc_end();

		do_stream(decbuf, n_dec, !param_nodec);

		//tc_start("bufcopy");
		if ( single_mode ) { /* 単一書き出しモード */
			/* tosを生成 */
			if ( ! tos ) {
				n_tos = param_n_services = 1;
				tos = (ts_output_stat_t*)malloc(1 * sizeof(ts_output_stat_t));
				init_tos(tos);
			}

			/* パケットをバッファにコピー */
			ts_copybuf(tos, decbuf, n_dec);
			ts_update_transfer_history(tos, nowtime, n_dec);

		} else {  /* サービスごと書き出しモード */
			/* pos_filledをコピー */
			for (i = 0; i < n_tos; i++) {
				tos[i].pos_filled_old = tos[i].pos_filled;
			}

			/* パケットを処理 */
			for (pos = 0; pos < (int)n_dec; pos += 188) {
				BYTE *packet = &decbuf[pos];

				/* PAT, PMTを取得 */
				parse_ts_packet(&tps, packet);

				/* tosを生成 */
				if ( ! tos && tps.payload_PAT.stat == PAYLOAD_STAT_FINISH ) {
					n_tos = param_n_services = create_tos_per_service(&tos, &tps);
				}

				/* サービスごとにパケットをバッファにコピー */
				for (i = 0; i < n_tos; i++) {
					copy_current_service_packet(&tos[i], &tps, packet);
				}
			}

			/* コピーしたバイト数の履歴を保存 */
			for (i = 0; i < n_tos; i++) {
				ts_update_transfer_history( &tos[i], nowtime, tos[i].pos_filled - tos[i].pos_filled_old );
			}
		}
		//tc_end();

		//tc_start("output");
		/* バッファを出力 */
		/* 100回に一回、または空取得だったとき書き出しを行う */
		/*if(gettscount > 100 || n_recv == 0) {
			for (i = 0; i < n_tos; i++) {
				ts_output(&tos[i], nowtime);
			}
			gettscount = 0;
		}*/
		for (i = 0; i < n_tos; i++) {
			//if ( tos[i].pos_filled - tos[i].pos_write > 1024*1024 ) {
				ts_output(&tos[i], nowtime, 0);
			//}
		}
		//tc_end();

		//fprintf(logfp, "%I64d n_recv=%d, n_dec=%d, filled=%d\n", nowtime, n_recv, n_dec, filled);

		//tc_start("proginfo");
		/* 定期的に番組情報をチェック */
		if ( nowtime / CHECK_INTERVAL != lasttime / CHECK_INTERVAL ) {
			tdiff = (double)(nowtime - lasttime) / 1000;
			Mbps = (double)subtotal * 8 / 1024 / 1024 / tdiff;

			TCHAR title[256];

			__int64 n_drops, n_srmbs;

			if (param_nodec) {
				n_drops = ts_n_drops;
				n_srmbs = 0;
			} else {
				n_drops = pB25Decoder2->GetContinuityErrNum();
				n_srmbs = pB25Decoder2->GetScramblePacketNum();
			}

			_stprintf_s(title, 256, _T("%s:%s:%s|%.1fdb %.1fMbps D:%I64d S:%I64d %.1fGB"),
				bon_tuner_name, bon_sp_name, bon_ch_name, pBon2->GetSignalLevel(), Mbps,
				n_drops, n_srmbs,
				(double)total / 1024 / 1024 / 1024 );
			SetConsoleTitle(title);

			lasttime = nowtime;
			subtotal = 0;

			/* 番組情報のチェック */
			for (i = 0; i < n_tos; i++) {
				if (tos[i].th[1].bytes > 0) { /* 前のintervalで何も受信できてない時は番組情報のチェックをパスする */
					ts_check_pi(&tos[i], nowtime);
				}
			}

			//tc_start("printbuf");
			print_buf(tos, n_tos);
			//tc_end();

			/* 溢れたバイト数を表示 */
			for (i = 0; i < n_tos; i++) {
				if (tos[i].dropped_bytes > 0) {
					if ( tos[i].service_id != -1 ) {
						fprintf(stderr, "[WARN] バッファフルのためデータが溢れました(サービス%d, %dバイト)\n", tos[i].service_id, tos[i].dropped_bytes);
					} else {
						fprintf(stderr, "[WARN] バッファフルのためデータが溢れました(%dバイト)\n", tos[i].dropped_bytes);
					}
					tos[i].dropped_bytes = 0;
				}
			}
		}
		//tc_end();

		//tc_start("minimize");
		/* minimize */
		for (i = 0; i < n_tos; i++) {
			ts_minimize_buf(&tos[i]);
		}
		//tc_end();

		subtotal += n_dec;
		total += n_dec;
	}

	int err;
	/* 終了処理 */
	printf("まだ書き出していないバッファを書き出ています\n");
	for (i = 0; i < n_tos; i++) {
		/* まだ書き出していないバッファを書き出し */
		err = ts_wait_pgoutput(&tos[i]);
		while (tos[i].pos_filled - tos[i].pos_write > 0 && !err) {
			ts_output(&tos[i], gettime(), 1);
			err = ts_wait_pgoutput(&tos[i]);
			print_buf(&tos[i], n_tos-i);
		}
		close_tos(&tos[i]);
	}
	free(tos);

	do_close_stream();

	//tc_report_id(123);
}

void load_ini()
{
	int bufsize = GetPrivateProfileInt(_T("TSDUMP"), _T("BUFSIZE"), BUFSIZE_DEFAULT, _T(".\\tsdump.ini"));
	int overlap_sec = GetPrivateProfileInt(_T("TSDUMP"), _T("OVERLAP_SEC"), OVERLAP_SEC_DEFAULT, _T(".\\tsdump.ini"));
	int check_interval = GetPrivateProfileInt(_T("TSDUMP"), _T("CHECK_INTERVAL"), CHECK_INTERVAL_DEFAULT, _T(".\\tsdump.ini"));
	int max_pgoverlap = GetPrivateProfileInt(_T("TSDUMP"), _T("MAX_PGOVERLAP"), MAX_PGOVERLAP_DEFAULT, _T(".\\tsdump.ini"));

	BUFSIZE = bufsize * 1024 * 1024;
	OVERLAP_SEC = overlap_sec;
	CHECK_INTERVAL = check_interval;
	MAX_PGOVERLAP = max_pgoverlap;

	printf("BUFSIZE: %dMiB\nOVERLAP_SEC: %ds\nCHECK_INTERVAL: %dms\nMAX_PGOVERLAP: %d\n\n",
		bufsize, OVERLAP_SEC, CHECK_INTERVAL, MAX_PGOVERLAP);
}

typedef IB25Decoder2* (pCreateB25Decoder2_t)(void);

int _tmain(int argc, TCHAR* argv[])
{
	HMODULE hB25dll = NULL;
	HMODULE hDll = NULL;

	int ret=0;

	_tsetlocale(LC_ALL, _T("Japanese_Japan.932"));

	printf("tsdump ver%s (%s)\n\n", VERSION_STR, DATE_STR);

	load_ini();

	/* モジュールをロード */
	if (load_modules() < 0) {
		fwprintf(stderr, L"[ERROR] モジュールのロード時にエラーが発生しました!");
		ret = 1;
		goto END;
	}
	printf("\n");

	if ( !get_cmd_params(argc, argv) ) {
		print_cmd_usage();
		ret = 1;
		goto END;
	}

	if ( ! do_postconfig() ) {
		print_cmd_usage();
		ret = 1;
		goto END;
	}

	pCreateBonDriver_t *pCreateBonDriver;
	pCreateB25Decoder2_t *pCreateB25Decoder2;
	IB25Decoder2 *pB25Decoder2;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	hDll = LoadLibrary( param_bon_dll_name );
	if (hDll == NULL) {
		fprintf(stderr, "BonDriverをロードできませんでした\n");
		print_err(_T("LoadLibrary"), GetLastError());
		ret = 1;
		goto END;
	}

	pCreateBonDriver = (pCreateBonDriver_t*)GetProcAddress(hDll, "CreateBonDriver");
	if ( pCreateBonDriver == NULL) {
		fprintf(stderr, "CreateBonDriver()のポインタを取得できませんでした\n");
		print_err(_T("GetProcAddress"), GetLastError());
		ret = 1;
		goto END;
	}

	IBonDriver *pBon = pCreateBonDriver();
	if (pBon == NULL) {
		fprintf(stderr, "CreateBonDriver() returns NULL\n" );
		ret = 1;
		goto END;
	}

	IBonDriver2 *pBon2 = dynamic_cast<IBonDriver2 *>(pBon);

	if( ! pBon2->OpenTuner() ) {
		fprintf(stderr, "OpenTuner() returns FALSE\n" );
		ret = 1;
		goto END;
	}

	Sleep(500);

	bon_ch_name = pBon2->EnumChannelName( param_sp_num, param_ch_num );
	bon_tuner_name = pBon2->GetTunerName();
	bon_sp_name = pBon2->EnumTuningSpace( param_sp_num );
	_tprintf( _T("%s\n"), bon_tuner_name );
	_tprintf(_T("%s\n"), bon_sp_name);
	_tprintf( _T("%s\n"), bon_ch_name );
	if( ! pBon2->SetChannel( param_sp_num, param_ch_num ) ) {
		fprintf(stderr, "SetChannel() returns FALSE\n" );
		ret = 1;
		goto END;
	}

	if ( !param_nodec ) {
		hB25dll = LoadLibrary(_T("B25Decoder.dll"));
		if (hB25dll == NULL) {
			fprintf(stderr, "B25Decoder.dllをロードできませんでした\n");
			print_err(_T("LoadLibrary"), GetLastError());
			ret = 1;
			goto END;
		}

		pCreateB25Decoder2 = (pCreateB25Decoder2_t*)GetProcAddress(hB25dll, "CreateB25Decoder2");
		if (pCreateB25Decoder2 == NULL) {
			fprintf(stderr, "CreateB25Decoder2()のポインタを取得できませんでした\n");
			print_err(_T("GetProcAddress"), GetLastError());
			ret = 1;
			goto END;
		}

		pB25Decoder2 = pCreateB25Decoder2();

		if ( pB25Decoder2 == NULL ) {
			fprintf(stderr, "CreateB25Decoder2()に失敗\n");
			ret = 1;
			goto END;
		}

		if ( ! pB25Decoder2->Initialize() ) {
			fprintf(stderr, "pB25Decoder2->Initialize()に失敗\n");
			ret = 1;
			goto END;
		}
	} else {
		pB25Decoder2 = NULL;
	}

	main_loop(pBon2, pB25Decoder2);

	if (!param_nodec) {
		pB25Decoder2->Release();
	}

	pBon2->CloseTuner();
	//fclose(fp);
	printf("正常終了\n");

END:
	free_modules();
	if (hDll) {
		FreeLibrary(hDll);
	}
	if (!param_nodec && hB25dll) {
		FreeLibrary(hB25dll);
	}
	if( ret ) { getchar(); }
	return ret;
}

static const WCHAR* set_bon(const WCHAR *param)
{
	param_bon_dll_name = _wcsdup(param);
	return NULL;
}

static const WCHAR* set_sp(const WCHAR *param)
{
	param_sp_num = _wtoi(param);
	return NULL;
}

static const WCHAR* set_ch(const WCHAR *param)
{
	param_ch_num = _wtoi(param);
	return NULL;
}

static const WCHAR* set_dir(const WCHAR *param)
{
	wcsncpy(param_base_dir, param, MAX_PATH_LEN);
	PathAddBackslash(param_base_dir);
	return NULL;
}

static const WCHAR* set_nodec(const WCHAR *)
{
	param_nodec = 1;
	return NULL;
}

static const WCHAR *set_nowait(const WCHAR *)
{
	param_nowait = 1;
	return NULL;
}

static const WCHAR* set_sv(const WCHAR *param)
{
	int sv;
	if (param_all_services) {
		return NULL;
	}
	if ( wcscmp(param, L"all") == 0 ) {
		param_all_services = 1;
	} else {
		if (param_n_services < MAX_SERVICES) {
			sv = _wtoi(param);
			if (sv <= 0 || sv > 65535) {
				return L"サービス番号が不正です";
			}
			param_services[param_n_services] = sv;
			param_n_services++;
		} else {
			return L"指定するサービスの数が多すぎます\n";
		}
	}
	return NULL;
}

static const WCHAR *hook_postconfig()
{
	if (param_bon_dll_name == NULL) {
		return L"BonDriverのDLLを指定してください";
	}
	if (param_ch_num < 0) {
		return L"チャンネルが指定されていないか、または不正です";
	}
	if (param_sp_num < 0) {
		return L"チューナー空間が指定されていないか、または不正です";
	}
	if ( ! param_base_dir ) {
		return L"出力ディレクトリが指定されていないか、または不正です";
	}
	return NULL;
}

static void register_hooks()
{
	register_hook_postconfig(hook_postconfig);
}

static cmd_def_t cmds[] = {
	{ L"-bon", L"BonDriverのDLL *", 1, set_bon },
	{ L"-sp", L"チューナー空間番号 *", 1, set_sp },
	{ L"-ch", L"チャンネル番号 *", 1, set_ch },
	{ L"-dir", L"出力先ディレクトリ *", 1, set_dir },
	{ L"-nodec", L"MULTI2のデコードを行わない", 0, set_nodec },
	{ L"-sv", L"サービス番号(複数指定可能)", 1, set_sv },
	{ L"-nowait", L"バッファフル時にあふれたデータは捨てる", 0, set_nowait },
	NULL,
};

module_def_t mod_core = {
	TSDUMP_MODULE_V1,
	L"mod_core",
	register_hooks,
	cmds
};
