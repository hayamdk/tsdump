#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <new.h>
#include <locale.h>
#include <tchar.h>
#include <inttypes.h>

#include "ts_parser.h"
#include "modules_def.h"

#include "rplsinfo.h"
#include "proginfo.h"
#include "convtounicode.h"

#include <conio.h>


// 定数など

#define		MAXFLAGNUM				256
#define		FILE_INVALID			0
#define		FILE_RPLS				1
#define		FILE_188TS				188
#define		FILE_192TS				192
#define		DEFAULTTSFILEPOS		50

#define		NAMESTRING				L"\nrplsinfo version 1.3 "

#ifdef _WIN64
	#define		NAMESTRING2				L"(64bit)\n"
#else
	#define		NAMESTRING2				L"(32bit)\n"
#endif


// 構造体宣言

typedef struct {
	int		argSrc;
	int		argDest;
	int		separator;
	int		flags[MAXFLAGNUM + 1];
	BOOL	bNoControl;
	BOOL	bNoComma;
	BOOL	bDQuot;
	BOOL	bItemName;
	BOOL	bDisplay;
	BOOL	bCharSize;
	int		tsfilepos;
} CopyParams;


// プロトタイプ宣言

void	initCopyParams(CopyParams*);
BOOL	parseCopyParams(int, _TCHAR*[], CopyParams*);
int		rplsTsCheck(BYTE*);
BOOL	rplsMakerCheck(unsigned char*, int);
void	readRplsProgInfo(HANDLE, ProgInfo*, BOOL);
void	outputProgInfo(HANDLE, ProgInfo*, CopyParams*);
//int		putGenreStr(WCHAR*, int, int*, int*);
int		getRecSrcIndex(int);
int		putRecSrcStr(WCHAR*, int, int);
int		convforcsv(WCHAR*, int, WCHAR*, int, BOOL, BOOL, BOOL, BOOL);


// メイン

int get_proginfo( ProgInfo *proginfo, BYTE *buf, int size )
{
	int i;
	BYTE *buf_t;
	int	sfiletype;

	proginfo->isok = FALSE;

	if (size < 188 * 4) {
		return 0;
	}

	/* bufが188バイトにアラインメントされない実装(例:BonDriver_Friio)に対応 */
	for( i=0; i<188; i++ ) {
		sfiletype = rplsTsCheck(&buf[i]);
		if(sfiletype == FILE_188TS) {
			break;
		}
	}
	if(sfiletype != FILE_188TS) {
		//fprintf(stderr, "\n正常なtsストリームではありません\n");
		output_message(MSG_ERROR, L"正常なtsストリームではありません");
		return 0;
	}

	buf_t = &buf[i];

	// 番組情報の読み込み
	memset(proginfo, 0, sizeof(ProgInfo));
	BOOL bResult = readTsProgInfo( buf_t, size-i, proginfo, sfiletype, 1 );
	if(!bResult) {
		//wprintf(L"有効な番組情報を検出できませんでした.\n");
		return 0;
	}

	proginfo->isok = TRUE;
	return 1;
}

int rplsTsCheck( BYTE *buf )
{
	if( (buf[188 * 0] == 0x47) && (buf[188 * 1] == 0x47) && (buf[188 * 2] == 0x47) && (buf[188 * 3] == 0x47) ){
		return	FILE_188TS;
	}
	return	FILE_INVALID;
}


BOOL rplsMakerCheck(unsigned char *buf, int idMaker)
{
	unsigned int	mpadr = (buf[ADR_MPDATA] << 24) + (buf[ADR_MPDATA + 1] << 16) + (buf[ADR_MPDATA + 2] << 8) + buf[ADR_MPDATA + 3];
	if(mpadr == 0) return FALSE;

	unsigned char	*mpdata = buf + mpadr;
	int		makerid = (mpdata[ADR_MPMAKERID] << 8) + mpdata[ADR_MPMAKERID + 1];

	if(makerid != idMaker) return FALSE;

	return TRUE;
}

int putGenreStr(WCHAR *buf, const int bufsize, const int *genretype, const int *genre)
{
	WCHAR	*str_genreL[] = {
		L"ニュース／報道",			L"スポーツ",	L"情報／ワイドショー",	L"ドラマ",
		L"音楽",					L"バラエティ",	L"映画",				L"アニメ／特撮",
		L"ドキュメンタリー／教養",	L"劇場／公演",	L"趣味／教育",			L"福祉",
		L"予備",					L"予備",		L"拡張",				L"その他"
	};

	WCHAR	*str_genreM[] = {
		L"定時・総合", L"天気", L"特集・ドキュメント", L"政治・国会", L"経済・市況", L"海外・国際", L"解説", L"討論・会談",
		L"報道特番", L"ローカル・地域", L"交通", L"-", L"-", L"-", L"-", L"その他",

		L"スポーツニュース", L"野球", L"サッカー", L"ゴルフ", L"その他の球技", L"相撲・格闘技", L"オリンピック・国際大会", L"マラソン・陸上・水泳",
		L"モータースポーツ", L"マリン・ウィンタースポーツ", L"競馬・公営競技", L"-", L"-", L"-", L"-", L"その他",

		L"芸能・ワイドショー", L"ファッション", L"暮らし・住まい", L"健康・医療", L"ショッピング・通販", L"グルメ・料理", L"イベント", L"番組紹介・お知らせ",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

		L"国内ドラマ", L"海外ドラマ", L"時代劇", L"-", L"-", L"-", L"-", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

		L"国内ロック・ポップス", L"海外ロック・ポップス", L"クラシック・オペラ", L"ジャズ・フュージョン", L"歌謡曲・演歌", L"ライブ・コンサート", L"ランキング・リクエスト", L"カラオケ・のど自慢",
		L"民謡・邦楽", L"童謡・キッズ", L"民族音楽・ワールドミュージック", L"-", L"-", L"-", L"-", L"その他",

		L"クイズ", L"ゲーム", L"トークバラエティ", L"お笑い・コメディ", L"音楽バラエティ", L"旅バラエティ", L"料理バラエティ", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

		L"洋画", L"邦画", L"アニメ", L"-", L"-", L"-", L"-", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

		L"国内アニメ", L"海外アニメ", L"特撮", L"-", L"-", L"-", L"-", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

		L"社会・時事", L"歴史・紀行", L"自然・動物・環境", L"宇宙・科学・医学", L"カルチャー・伝統芸能", L"文学・文芸", L"スポーツ", L"ドキュメンタリー全般",
		L"インタビュー・討論", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

		L"現代劇・新劇", L"ミュージカル", L"ダンス・バレエ", L"落語・演芸", L"歌舞伎・古典", L"-", L"-", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

		L"旅・釣り・アウトドア", L"園芸・ペット・手芸", L"音楽・美術・工芸", L"囲碁・将棋", L"麻雀・パチンコ", L"車・オートバイ", L"コンピュータ・ＴＶゲーム", L"会話・語学",
		L"幼児・小学生", L"中学生・高校生", L"大学生・受験", L"生涯教育・資格", L"教育問題", L"-", L"-", L"その他",

		L"高齢者", L"障害者", L"社会福祉", L"ボランティア", L"手話", L"文字（字幕）", L"音声解説", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他",

		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",

		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",

		L"BS/地上デジタル放送用番組付属情報", L"広帯域CSデジタル放送用拡張", L"衛星デジタル音声放送用拡張", L"サーバー型番組付属情報", L"-", L"-", L"-", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",

		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"-",
		L"-", L"-", L"-", L"-", L"-", L"-", L"-", L"その他"
	};

	int len;

	if(genretype[2] == 0x01) {
		len =  swprintf_s(buf, (size_t)bufsize, L"%s 〔%s〕　%s 〔%s〕　%s 〔%s〕", str_genreL[genre[0] >> 4], str_genreM[genre[0]], str_genreL[genre[1] >> 4], str_genreM[genre[1]], str_genreL[genre[2] >> 4], str_genreM[genre[2]]);
	} else if(genretype[1] == 0x01) {
		len =  swprintf_s(buf, (size_t)bufsize, L"%s 〔%s〕　%s 〔%s〕", str_genreL[genre[0] >> 4], str_genreM[genre[0]], str_genreL[genre[1] >> 4], str_genreM[genre[1]]);
	} else {
		len =  swprintf_s(buf, (size_t)bufsize, L"%s 〔%s〕", str_genreL[genre[0] >> 4],  str_genreM[genre[0]]);
	}

	return len;
}


int getRecSrcIndex(int num)
{
	int		recsrc_table[] = {		// 順序はputRecSrcStrと対応する必要あり
		0x5444,						// TD	地上デジタル
		0x4244,						// BD	BSデジタル
		0x4331,						// C1	CSデジタル1
		0x4332,						// C2	CSデジタル2
		0x694C,						// iL	i.LINK(TS)入力
		0x4D56,						// MV	AVCHD
		0x534B,						// SK	スカパー(DLNA)
		0x4456,						// DV	DV入力
		0x5441,						// TA	地上アナログ
		0x4E4C,						// NL	ライン入力
	};

	// 小さなテーブルなので順次探索で

	for(int i = 0; i < (sizeof(recsrc_table) / sizeof(int)); i++) {
		if(num == recsrc_table[i]) return i;
	}

	return	(sizeof(recsrc_table) / sizeof(int));			// 不明なrecsrc
}


int putRecSrcStr(WCHAR *buf, int bufsize, int index)
{
	WCHAR	*str_recsrc[] = {
		L"地上デジタル",
		L"BSデジタル",
		L"CSデジタル1",
		L"CSデジタル2",
		L"i.LINK(TS)",
		L"AVCHD",
		L"スカパー(DLNA)",
		L"DV入力",
		L"地上アナログ",
		L"ライン入力",
		L"unknown",
	};

	int	len = 0;

	if( (index >= 0) && (index < (sizeof(str_recsrc) / sizeof(WCHAR*))) ) {
		len = swprintf_s(buf, (size_t)bufsize, L"%s", str_recsrc[index]);
	}

	return len;
}


int convforcsv(WCHAR *dbuf, int bufsize, WCHAR *sbuf, int slen, BOOL bNoControl, BOOL bNoComma, BOOL bDQuot, BOOL bDisplay)
{
	int dst = 0;

	if( bDQuot && (dst < bufsize) ) dbuf[dst++] = 0x0022;		//  「"」						// CSV用出力なら項目の前後を「"」で囲む

	for(int	src = 0; src < slen; src++)
	{
		WCHAR	s = sbuf[src];
		BOOL	bOut = TRUE;

		if( bNoControl && (s < L' ') ) bOut = FALSE;											// 制御コードは出力しない
		if( bNoComma && (s == L',') ) bOut = FALSE;												// コンマは出力しない
		if( bDisplay && (s == 0x000D) ) bOut = FALSE;											// コンソール出力の場合は改行コードの0x000Dは省略する

		if( bDQuot && (s == 0x0022) && (dst < bufsize) ) dbuf[dst++] = 0x0022;					// CSV用出力なら「"」の前に「"」でエスケープ
		if( bOut && (dst < bufsize) ) dbuf[dst++] = s;											// 出力
	}

	if( bDQuot && (dst < bufsize) ) dbuf[dst++] = 0x0022;		//  「"」

	if(dst < bufsize) dbuf[dst] = 0x0000;

	return dst;
}