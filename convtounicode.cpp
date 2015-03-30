#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <locale.h>

#include "convtounicode.h"

#define		IF_NSZ_TO_MSZ		if(bCharSize&&sta.bNormalSize){charbuf(dbuf,maxbufsize,dst++,0x89);sta.bNormalSize=FALSE;}
#define		IF_MSZ_TO_NSZ		if(bCharSize&&!sta.bNormalSize){charbuf(dbuf,maxbufsize,dst++,0x8A);sta.bNormalSize=TRUE;}
#define		WCHARBUF(x)			dst+=wcharbuf(dbuf,maxbufsize,dst,x,sta.bXCS)
#define		CHARBUF(x)			charbuf(dbuf,maxbufsize,dst++,x)

#define		NORMAL_NEWLINE_CODE				TRUE			// TRUEなら改行コードに0x0Dを使用, FALSEなら0x0Aを使用する

// #define		USE_CONV_FROM_UNICODE3						// conv_from_unicode3()を使用する


int conv_to_unicode(WCHAR *dbuf, const int maxbufsize, const unsigned char *sbuf, const int total_length, const BOOL bCharSize)
{

//		8単位符号文字列 -> UNICODE(UTF-16LE)文字列への変換
//
//		sbuf				変換元buf
//		total_length		その長さ(byte単位, NULL文字分を含まない)
//		dbuf				変換先buf
//		maxbufsize			変換先bufの最大サイズ(WCHAR単位), 越えた分は書き込まれず無視される
//		bCharSize			スペース及び英数文字の変換に文字サイズ指定(NSZ, MSZ)を反映させるか否か．TRUEなら反映させる
//		戻り値				変換して生成したWCHAR文字列の長さ(WCHAR単位)
//							dbufにNULLを指定すると変換して生成した文字列の長さ(WCHAR単位)だけ返す

	ConvStatus	sta;
	initConvStatus(&sta);

	int		src = 0;
	int		dst = 0;


	// 変換メイン

	while(src < total_length)
	{
		if(isControlChar(sbuf[src]))
		{
			// 0x00～0x20, 0x7F～0xA0, 0xFFの場合

			switch(sbuf[src])
			{
				case 0x08:				// APB (BS)
					WCHARBUF(0x0008);						// BS出力
					src++;
					break;
				case 0x09:				// APF (TAB)
					WCHARBUF(0x0009);						// TAB出力
					src++;
					break;
				case 0x0A:				// APD (LF)
					WCHARBUF(0x000D);						// CR+LF出力
					WCHARBUF(0x000A);
					src++;
					break;
				case 0x0D:				// APR (CR)
					if(sbuf[src + 1] != 0x0A) {
						WCHARBUF(0x000D);					// CR+LF出力	
						WCHARBUF(0x000A);
					}
					src++;
					break;
				case 0x20:				// SP
					if( bCharSize && sta.bNormalSize ) {
						WCHARBUF(0x3000);					// 全角SP出力
					} else {
						WCHARBUF(0x0020);					// 半角SP出力
					}
					src++;
					break;
				case 0x7F:				// DEL
					WCHARBUF(0x007F);						// DEL出力
					src++;
					break;
				case 0x9B:				// CSI処理
					csiProc(sbuf, &src, &sta);
					break;
				default:				// それ以外の制御コード
					changeConvStatus(sbuf, &src, &sta);
					break;
			}
		}
		else
		{
			// GL, GRに対応する文字出力

			int		jis, uc, len;
			WCHAR	utfstr[UTF16TABLELEN];

			int		regionLR = (sbuf[src] >= 0x80) ? REGION_GR : REGION_GL;

			switch(sta.bank[sta.region[regionLR]])
			{
				case F_KANJI:
				case F_JIS1KANJI:
					jis = (sbuf[src] & 0x7F) * 256 + (sbuf[src + 1] & 0x7F);
					uc = 0;
					if( bCharSize && !sta.bNormalSize ) uc = charsize1conv(jis, FALSE);			// MSZ指定の英数文字の場合は半角文字に変換する
					if(uc == 0) uc = jis12conv(jis, TRUE);
					if(uc == 0) uc = jis3conv(jis, TRUE);
					if(uc != 0) {
                        WCHARBUF(uc);
                    } 
                    else if( (len = jis3combconv(jis, utfstr, UTF16TABLELEN)) != 0 ) {
                        for(int i = 0; i < len; i++) WCHARBUF(utfstr[i]);
                    }
					src += 2;
					break;
				case F_JIS2KANJI:
					jis = (sbuf[src] & 0x7F) * 256 + (sbuf[src + 1] & 0x7F);
					uc = jis4conv(jis, TRUE);
					if(uc != 0) WCHARBUF(uc);
					src += 2;
					break;
				case F_ALPHA:
				case F_P_ALPHA:
					jis = sbuf[src] & 0x7F;
					if( bCharSize && sta.bNormalSize ) {
						uc = jis12conv(charsize1conv(jis, TRUE), TRUE);							// NSZ指定の場合は全角文字に変換する
					} else {
						uc = jis;																// tildeでoverlineを代用
					}
					if(uc != 0) WCHARBUF(uc);
					src++;
					break;
				case F_HIRAGANA:
				case F_P_HIRAGANA:
					uc = hiragana1conv(sbuf[src] & 0x7F, TRUE);
					if(uc != 0) WCHARBUF(uc);
					src++;
					break;
				case F_KATAKANA:
				case F_P_KATAKANA:
					uc = katakana1conv(sbuf[src] & 0x7F, TRUE);
					if(uc != 0) WCHARBUF(uc);
					src++;
					break;
				case F_HANKAKU:
					uc = hankaku1conv(sbuf[src] & 0x7F, TRUE);
					if(uc != 0) WCHARBUF(uc);
					src++;
					break;
				case F_TUIKAKIGOU:
					jis = (sbuf[src] & 0x7F) * 256 + (sbuf[src + 1] & 0x7F);
					uc = tuikakigou1conv(jis, TRUE);
					if( (uc == 0) && bCharSize && !sta.bNormalSize ) uc = charsize1conv(jis, TRUE);		// 追加記号集合の1～84区は未定義だと思うのですが、パナ製レコではこの部分で
					if(uc == 0) uc = jis12conv(jis, TRUE);												// 漢字系集合と同一の文字を出力をしている例があったのでこうしてあります（ソニー製レコにもって行くと化けます）
					if(uc != 0) {
						WCHARBUF(uc);
					}
                    else if( (len = tuikakigou2conv(jis, utfstr, UTF16TABLELEN)) != 0 ) {
                        for(int i = 0; i < len; i++) WCHARBUF(utfstr[i]);
                    }
					src += 2;
					break;
				case F_MOSAICA:
				case F_MOSAICB:
				case F_MOSAICC:
				case F_MOSAICD:
				case F_DRCS0:
				case F_DRCS1A:
				case F_DRCS2A:
				case F_DRCS3A:
				case F_DRCS4A:
				case F_DRCS5A:
				case F_DRCS6A:
				case F_DRCS7A:
				case F_DRCS8A:
				case F_DRCS9A:
				case F_DRCS10A:
				case F_DRCS11A:
				case F_DRCS12A:
				case F_DRCS13A:
				case F_DRCS14A:
				case F_DRCS15A:
					WCHARBUF(0x003F);						// '?'出力
					src++;
					break;
				case F_MACROA:
					defaultMacroProc(sbuf[src] & 0x7F, &sta);
					src++;
					break;
				default:
					break;
			}

			if(sta.bSingleShift) {
				sta.region[REGION_GL] = sta.region_GL_backup;
				sta.bSingleShift = FALSE;
			}
		}
	}

	WCHARBUF(0x0000);

	dst--;
	if(dst > maxbufsize) dst = maxbufsize; 

	return dst;			// 変換後の長さを返す(WCHAR単位), 終端のヌル文字分を含まない
}


int conv_from_unicode(unsigned char *dbuf, const int maxbufsize, const WCHAR *sbuf, const int total_length, const BOOL bCharSize)
{

//		UNICODE(UTF-16LE)文字列 -> 8単位符号文字列への変換
//
//		sbuf				変換元buf
//		total_length		その長さ(WCHAR単位, NULL文字分を含まない)
//		dbuf				変換先buf
//		maxbufsize			変換先bufの最大サイズ(byte単位), 越えた分は書き込まれず無視される
//		bCharSize			文字サイズ指定(MSZ:0x89, NSZ:0x8A)を付与するか否か
//							FALSEを指定すると文字サイズ指定を省略する(すべて全角で表示)
//		戻り値				変換して生成した文字列の長さ(BYTE単位)
//							dbufにNULLを指定すると変換して生成した文字列の長さ(BYTE単位)だけ返す

//		conv_from_unicode1とconv_from_unicode2を呼んで，変換結果の短い方を採用する
//		変換結果の長さに拘らないなら直接どちらかを直接呼んでも可
//		conv_from_unicode3は他の二つより短い変換結果を得られる場合があるが長いテキストでは変換時間が非常に長くなってしまう可能性あり


	int		len1 = conv_from_unicode1(NULL, maxbufsize, sbuf, total_length, bCharSize, NORMAL_NEWLINE_CODE);
	int		len2 = conv_from_unicode2(NULL, maxbufsize, sbuf, total_length, bCharSize, NORMAL_NEWLINE_CODE);
#ifdef USE_CONV_FROM_UNICODE3
	int		len3 = conv_from_unicode3(NULL, maxbufsize, sbuf, total_length, bCharSize, NORMAL_NEWLINE_CODE);
#endif

	int		len;

#ifndef USE_CONV_FROM_UNICODE3
	if(len1 <= len2) {
		len = conv_from_unicode1(dbuf, maxbufsize, sbuf, total_length, bCharSize, NORMAL_NEWLINE_CODE);
	} else {
		len = conv_from_unicode2(dbuf, maxbufsize, sbuf, total_length, bCharSize, NORMAL_NEWLINE_CODE);
	}
#else
	if( (len1 <= len2) && (len1 <= len3) ) {
		len = conv_from_unicode1(dbuf, maxbufsize, sbuf, total_length, bCharSize, NORMAL_NEWLINE_CODE);
	} else if( (len2 <= len1) && (len2 <= len3) ) {
		len = conv_from_unicode2(dbuf, maxbufsize, sbuf, total_length, bCharSize, NORMAL_NEWLINE_CODE);
	} else {
		len = conv_from_unicode3(dbuf, maxbufsize, sbuf, total_length, bCharSize, NORMAL_NEWLINE_CODE);
	}
#endif

	return len;
}


int conv_from_unicode1(unsigned char *dbuf, const int maxbufsize, const WCHAR *sbuf, const int total_length, const BOOL bCharSize, const BOOL bNLChar)
{

//		UNICODE(UTF-16LE)文字列 -> 8単位符号文字列への変換　その1
//
//		sbuf				変換元buf
//		total_length		その長さ(WCHAR単位, NULL文字分を含まない)
//		dbuf				変換先buf
//		maxbufsize			変換先bufの最大サイズ(byte単位), 越えた分は書き込まれず無視される
//		bCharSize			文字サイズ指定(MSZ:0x89, NSZ:0x8A)を付与するか否か
//							FALSEを指定すると文字サイズ指定を省略する(すべて全角で表示)
//		bNLChar				TRUEなら改行コードに0x0Dを使用, FALSEなら0x0Aを使用する
//		戻り値				変換して生成した文字列の長さ(BYTE単位)
//							dbufにNULLを指定すると変換して生成した文字列の長さ(BYTE単位)だけ返す
//
//		REGION_GL			BANK_G0, BANK_G1で切り替え
//		REGION_GR			BANK_G2, BANK_G3で切り替え
//		BANK_G0				F_KANJI, F_JIS1KANJI, F_JIS2KANJI, F_TUIKAKIGOUで切り替え
//		BANK_G1				F_ALPHA, F_HANKAKUで切り替え
//		BANK_G2				F_HIRAGANA固定
//		BANK_G3				F_KATAKANA固定

	ConvStatus	sta;
	initConvStatus(&sta);

	int		src = 0;
	int		dst = 0;


	// 変換メイン

	while(src < total_length)
	{
		int		jiscode;
		int		src_char_len;

		int		fcode = charclass(sbuf + src, &jiscode, &src_char_len);

		switch(fcode)
		{
			case F_CONTROL:
				switch(jiscode)
				{
					case 0x09:	// TAB
						CHARBUF(0x09);
						break;
					case 0x0A:	// LF
						if(bNLChar) {
							CHARBUF(0x0D);
						} else {
							CHARBUF(0x0A);
						}
						break;
					case 0x20:	// SP
						IF_NSZ_TO_MSZ;
						CHARBUF(0x20);
						break;
					default:
						break;
				}
				break;

			case F_ALPHA:
				if(sta.region[REGION_GL] != BANK_G1) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_NSZ_TO_MSZ;
				if(sta.bank[BANK_G1] != F_ALPHA) {
					CHARBUF(0x1B);
					CHARBUF(0x29);
					CHARBUF(F_ALPHA);						// F_ALPHA -> G1
					sta.bank[BANK_G1] = F_ALPHA;
				}
				CHARBUF(jiscode);
				break;

			case F_KANJI:
				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL	(LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G0] != F_KANJI) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(F_KANJI);						// F_KANJI -> G0
					sta.bank[BANK_G0] = F_KANJI;
				}
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_JIS1KANJI:
				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G0] != F_JIS1KANJI) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(F_JIS1KANJI);					// F_JIS1KANJI -> G0
					sta.bank[BANK_G0] = F_JIS1KANJI;
				}
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_JIS2KANJI:
				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G0] != F_JIS2KANJI) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(F_JIS2KANJI);					// F_JIS2KANJI -> G0
					sta.bank[BANK_G0] = F_JIS2KANJI;
				}
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_HANKAKU:
				if(sta.region[REGION_GL] != BANK_G1) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_NSZ_TO_MSZ;
				if(sta.bank[BANK_G1] != F_HANKAKU) {
					CHARBUF(0x1B);
					CHARBUF(0x29);
					CHARBUF(F_HANKAKU);						// F_HANKAKU -> G1
					sta.bank[BANK_G1] = F_HANKAKU;
				}
				CHARBUF(jiscode);
				break;

			case F_TUIKAKIGOU:
				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G0] != F_TUIKAKIGOU) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(F_TUIKAKIGOU);					// F_TUIKAKIGOU -> G0
					sta.bank[BANK_G0] = F_TUIKAKIGOU;
				}
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_KANACOMMON:
				IF_MSZ_TO_NSZ;
				CHARBUF(jiscode + 0x80);					// REGION_GRはBANK_G2(F_HIRAGANA),BANK_G3(F_KATAKANA)のどちらでも良い				
				break;

			case F_HIRAGANA:

				if(sta.region[REGION_GR] != BANK_G2) {

					// REGION_GRをBANK_G2(F_HIRAGANA)に切り替えるべきか判断、必要なら切り替え

					if(kanaChange(sbuf, src, total_length, F_HIRAGANA)) {
						CHARBUF(0x1B);
						CHARBUF(0x7D);							// G2 -> GR (LS2R)
						sta.region[REGION_GR] = BANK_G2;
					}
				}

				// REGION_GRがBANK_G2ならそのまま出力する

				if(sta.region[REGION_GR] == BANK_G2) {
					IF_MSZ_TO_NSZ;
					CHARBUF(jiscode + 0x80);
					break;
				}

				// それ以外の場合はBANK_G0を使ってF_KANJIで出力

				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G0] != F_KANJI) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(F_KANJI);						// F_KANJI -> G0
					sta.bank[BANK_G0] = F_KANJI;
				}
				jiscode = jis12conv(hiragana1conv(jiscode, TRUE), FALSE);
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_KATAKANA:

				if(sta.region[REGION_GR] != BANK_G3) {

					// REGION_GRをBANK_G3(F_KATAKANA)に切り替えるべきか判断、必要なら切り替え

					if(kanaChange(sbuf, src, total_length, F_KATAKANA)) {
						CHARBUF(0x1B);
						CHARBUF(0x7C);							// G3 -> GR (LS3R)
						sta.region[REGION_GR] = BANK_G3;
					}
				}

				// REGION_GRがBANK_G3ならそのまま出力する

				if(sta.region[REGION_GR] == BANK_G3) {
					IF_MSZ_TO_NSZ;
					CHARBUF(jiscode + 0x80);
					break;
				}

				// それ以外の場合はBANK_G0を使ってF_KANJIで出力

				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G0] != F_KANJI) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(F_KANJI);						// F_KANJI -> G0
					sta.bank[BANK_G0] = F_KANJI;
				}
				jiscode = jis12conv(katakana1conv(jiscode, TRUE), FALSE);
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			default:
				break;
		}

		src += src_char_len;

	}

	CHARBUF(0x00);

	dst--;
	if(dst > maxbufsize) dst = maxbufsize; 

	return dst;			// 変換後の長さを返す(byte単位), 終端のヌル文字分を含まない
}


int conv_from_unicode2(unsigned char *dbuf, const int maxbufsize, const WCHAR *sbuf, const int total_length, const BOOL bCharSize, const BOOL bNLChar)
{

//		UNICODE(UTF-16LE)文字列 -> 8単位符号文字列への変換　その2
//
//		sbuf				変換元buf
//		total_length		その長さ(WCHAR単位, NULL文字分を含まない)
//		dbuf				変換先buf
//		maxbufsize			変換先bufの最大サイズ(byte単位), 越えた分は書き込まれず無視される
//		bCharSize			文字サイズ指定(MSZ:0x89, NSZ:0x8A)を付与するか否か
//							FALSEを指定すると文字サイズ指定を省略する(すべて全角で表示)
//		bNLChar				TRUEなら改行コードに0x0Dを使用, FALSEなら0x0Aを使用する
//		戻り値				変換して生成した文字列の長さ(BYTE単位)
//							dbufにNULLを指定すると変換して生成した文字列の長さ(BYTE単位)だけ返す
//
//		REGION_GL			BANK_G0, BANK_G1で切り替え
//		REGION_GR			BANK_G2, BANK_G3で切り替え
//		BANK_G0				F_KANJI(ほぼ)固定
//		BANK_G1				F_ALPHA, F_JIS1KANJI, F_JIS2KANJI, F_TUIKAKIGOU, F_HANKAKUで切り替え
//		BANK_G2				F_HIRAGANA固定
//		BANK_G3				F_KATAKANA固定

	ConvStatus	sta;
	initConvStatus(&sta);

	int		src = 0;
	int		dst = 0;


	// 変換メイン

	while(src < total_length)
	{
		int		jiscode;
		int		src_char_len;

		int		fcode = charclass(sbuf + src, &jiscode, &src_char_len);

		switch(fcode)
		{
			case F_CONTROL:
				switch(jiscode)
				{
					case 0x09:	// TAB
						CHARBUF(0x09);
						break;
					case 0x0A:	// LF
						if(bNLChar) {
							CHARBUF(0x0D);
						} else {
							CHARBUF(0x0A);
						}
						break;
					case 0x20:	// SP
						IF_NSZ_TO_MSZ;
						CHARBUF(0x20);
						break;
					default:
						break;
				}
				break;

			case F_ALPHA:
				if(sta.region[REGION_GL] != BANK_G1) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_NSZ_TO_MSZ;
				if(sta.bank[BANK_G1] != F_ALPHA) {
					CHARBUF(0x1B);
					CHARBUF(0x29);
					CHARBUF(F_ALPHA);						// F_ALPHA -> G1
					sta.bank[BANK_G1] = F_ALPHA;
				}
				CHARBUF(jiscode);
				break;

			case F_KANJI:
				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL	(LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_JIS1KANJI:
				if(sta.region[REGION_GL] != BANK_G1) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G1] != F_JIS1KANJI) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(0x29);
					CHARBUF(F_JIS1KANJI);					// F_JIS1KANJI -> G1
					sta.bank[BANK_G1] = F_JIS1KANJI;
				}
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_JIS2KANJI:
				if(sta.region[REGION_GL] != BANK_G1) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G1] != F_JIS2KANJI) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(0x29);
					CHARBUF(F_JIS2KANJI);					// F_JIS2KANJI -> G1
					sta.bank[BANK_G1] = F_JIS2KANJI;
				}
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_HANKAKU:
				if(sta.region[REGION_GL] != BANK_G1) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_NSZ_TO_MSZ;
				if(sta.bank[BANK_G1] != F_HANKAKU) {
					CHARBUF(0x1B);
					CHARBUF(0x29);
					CHARBUF(F_HANKAKU);						// F_HANKAKU -> G1
					sta.bank[BANK_G1] = F_HANKAKU;
				}
				CHARBUF(jiscode);
				break;

			case F_TUIKAKIGOU:

				if( (sta.bank[BANK_G0] != F_TUIKAKIGOU) && (sta.bank[BANK_G1] != F_TUIKAKIGOU) ) {

					// BANK_G0をF_TUIKAKIGOUに切り替えるべきか判断
					// 以降にF_TUIKAKIGOU, F_ALPHA, F_CONTROL以外の文字が無ければ切り替え

					if(kigouChange(sbuf, src, total_length)) {
						CHARBUF(0x1B);
						CHARBUF(0x24);
						CHARBUF(F_TUIKAKIGOU);				// F_TUIKAKIGOU -> G0
						sta.bank[BANK_G0] = F_TUIKAKIGOU;
					}
				}

				// BANK_G0がF_TUIKAKIGOUになっていればBANK_G0で出力

				if(sta.bank[BANK_G0] == F_TUIKAKIGOU) {
					if(sta.region[REGION_GL] != BANK_G0) {
						CHARBUF(0x0F);							// G0 -> GL (LS0)
						sta.region[REGION_GL] = BANK_G0;
					}
					IF_MSZ_TO_NSZ;
					CHARBUF(jiscode >> 8);
					CHARBUF(jiscode & 0xFF);
					break;
				}
				
				// そうでない場合は通常のBANK_G1で出力

				if(sta.region[REGION_GL] != BANK_G1) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_MSZ_TO_NSZ;
				if(sta.bank[BANK_G1] != F_TUIKAKIGOU) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(0x29);
					CHARBUF(F_TUIKAKIGOU);					// F_TUIKAKIGOU -> G1
					sta.bank[BANK_G1] = F_TUIKAKIGOU;
				}
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_KANACOMMON:
				IF_MSZ_TO_NSZ;
				CHARBUF(jiscode + 0x80);					// REGION_GRはBANK_G2(F_HIRAGANA),BANK_G3(F_KATAKANA)のどちらでも良い				
				break;

			case F_HIRAGANA:

				if(sta.region[REGION_GR] != BANK_G2) {

					// REGION_GRをBANK_G2(F_HIRAGANA)に切り替えるべきか判断、必要なら切り替え

					if(kanaChange(sbuf, src, total_length, F_HIRAGANA)) {
						CHARBUF(0x1B);
						CHARBUF(0x7D);							// G2 -> GR (LS2R)
						sta.region[REGION_GR] = BANK_G2;
					}
				}

				// REGION_GRがBANK_G2ならそのまま出力する

				if(sta.region[REGION_GR] == BANK_G2) {
					IF_MSZ_TO_NSZ;
					CHARBUF(jiscode + 0x80);
					break;
				}

				// それ以外の場合はBANK_G0を使ってF_KANJIで出力

				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				jiscode = jis12conv(hiragana1conv(jiscode, TRUE), FALSE);
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_KATAKANA:

				if(sta.region[REGION_GR] != BANK_G3) {

					// REGION_GRをBANK_G3(F_KATAKANA)に切り替えるべきか判断、必要なら切り替え

					if(kanaChange(sbuf, src, total_length, F_KATAKANA)) {
						CHARBUF(0x1B);
						CHARBUF(0x7C);							// G3 -> GR (LS3R)
						sta.region[REGION_GR] = BANK_G3;
					}
				}

				// REGION_GRがBANK_G3ならそのまま出力する

				if(sta.region[REGION_GR] == BANK_G3) {
					IF_MSZ_TO_NSZ;
					CHARBUF(jiscode + 0x80);
					break;
				}

				// それ以外の場合はBANK_G0を使ってF_KANJIで出力

				if(sta.region[REGION_GL] != BANK_G0) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				IF_MSZ_TO_NSZ;
				jiscode = jis12conv(katakana1conv(jiscode, TRUE), FALSE);
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			default:
				break;
		}

		src += src_char_len;

	}

	CHARBUF(0x00);

	dst--;
	if(dst > maxbufsize) dst = maxbufsize; 

	return dst;			// 変換後の長さを返す(byte単位), 終端のヌル文字分を含まない
}


int conv_from_unicode3(unsigned char *dbuf, const int maxbufsize, const WCHAR *sbuf, const int total_length, const BOOL bCharSize, const BOOL bNLChar)
{

//		UNICODE(UTF-16LE)文字列 -> 8単位符号文字列への変換　その3
//
//		sbuf				変換元buf
//		total_length		その長さ(WCHAR単位, NULL文字分を含まない)
//		dbuf				変換先buf
//		maxbufsize			変換先bufの最大サイズ(byte単位), 越えた分は書き込まれず無視される
//		bCharSize			文字サイズ指定(MSZ:0x89, NSZ:0x8A)を付与するか否か
//							FALSEを指定すると文字サイズ指定を省略する(すべて全角で表示)
//		bNLChar				TRUEなら改行コードに0x0Dを使用, FALSEなら0x0Aを使用する
//		戻り値				変換して生成した文字列の長さ(BYTE単位)
//							dbufにNULLを指定すると変換して生成した文字列の長さ(BYTE単位)だけ返す
//
//		REGION_GL			BANK_G0, BANK_G1で切り替え
//		REGION_GR			BANK_G2, BANK_G3で切り替え
//		BANK_G0, BANK_G1 	F_KANJI, F_ALPHA, F_JIS1KANJI, F_JIS2KANJI, F_TUIKAKIGOU, F_HANKAKUで切り替え
//		BANK_G2				F_HIRAGANA固定
//		BANK_G3				F_KATAKANA固定

	ConvStatus	sta;
	initConvStatus(&sta);

	int		src = 0;
	int		dst = 0;


	// 変換メイン

	while(src < total_length)
	{
		int		jiscode;
		int		src_char_len;

		int		fcode = charclass(sbuf + src, &jiscode, &src_char_len);

		switch(fcode)
		{
			case F_CONTROL:
				switch(jiscode)
				{
					case 0x09:	// TAB
						CHARBUF(0x09);
						break;
					case 0x0A:	// LF
						if(bNLChar) {
							CHARBUF(0x0D);
						} else {
							CHARBUF(0x0A);
						}
						break;
					case 0x20:	// SP
						IF_NSZ_TO_MSZ;
						CHARBUF(0x20);
						break;
					default:
						break;
				}
				break;

			case F_ALPHA:
			case F_HANKAKU:

				// BANK_G0, BANK_G1のどちらも必要なfcodeでない場合、どちらを切り替えるか判断する

				if( (sta.bank[BANK_G0] != fcode) && (sta.bank[BANK_G1] != fcode) ) {
					if(bankChange(sbuf, src, total_length, &sta, fcode)) {
						CHARBUF(0x1B);
						CHARBUF(0x28);
						CHARBUF(fcode);					// 1 Byte G Set -> G0
						sta.bank[BANK_G0] = fcode;
					} else {
						CHARBUF(0x1B);
						CHARBUF(0x29);
						CHARBUF(fcode);					// 1 Byte G Set -> G1
						sta.bank[BANK_G1] = fcode;
					}
				}

				// 必要ならLS0, LS1

				if( (sta.bank[BANK_G0] == fcode) && (sta.region[REGION_GL] != BANK_G0) ) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				if( (sta.bank[BANK_G1] == fcode) && (sta.region[REGION_GL] != BANK_G1) ) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_NSZ_TO_MSZ;
				CHARBUF(jiscode);
				break;

			case F_KANJI:
			case F_JIS1KANJI:
			case F_JIS2KANJI:
			case F_TUIKAKIGOU:

				// BANK_G0, BANK_G1のどちらも必要なfcodeでない場合、どちらを切り替えるか判断する

				if( (sta.bank[BANK_G0] != fcode) && (sta.bank[BANK_G1] != fcode) ) {
					if(bankChange(sbuf, src, total_length, &sta, fcode)) {
						CHARBUF(0x1B);
						CHARBUF(0x24);
						CHARBUF(fcode);					// 2 Byte G Set -> G0
						sta.bank[BANK_G0] = fcode;
					} else {
						CHARBUF(0x1B);
						CHARBUF(0x24);
						CHARBUF(0x29);
						CHARBUF(fcode);					// 2 Byte G Set -> G1
						sta.bank[BANK_G1] = fcode;
					}
				}

				// 必要ならLS0, LS1

				if( (sta.bank[BANK_G0] == fcode) && (sta.region[REGION_GL] != BANK_G0) ) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				if( (sta.bank[BANK_G1] == fcode) && (sta.region[REGION_GL] != BANK_G1) ) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_MSZ_TO_NSZ;
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_KANACOMMON:
				IF_MSZ_TO_NSZ;
				CHARBUF(jiscode + 0x80);					// REGION_GRはBANK_G2(F_HIRAGANA),BANK_G3(F_KATAKANA)のどちらでも良い				
				break;

			case F_HIRAGANA:

				if(sta.region[REGION_GR] != BANK_G2) {

					// REGION_GRをBANK_G2(F_HIRAGANA)に切り替えるべきか判断、必要なら切り替え

					if(kanaChange(sbuf, src, total_length, F_HIRAGANA)) {
						CHARBUF(0x1B);
						CHARBUF(0x7D);							// G2 -> GR (LS2R)
						sta.region[REGION_GR] = BANK_G2;
					}
				}

				// REGION_GRがBANK_G2ならそのまま出力する

				if(sta.region[REGION_GR] == BANK_G2) {
					IF_MSZ_TO_NSZ;
					CHARBUF(jiscode + 0x80);
					break;
				}

				// それ以外の場合はBANK_G0かBANK_G1を使ってF_KANJIで出力

				if( (sta.bank[BANK_G0] != F_KANJI) && (sta.bank[BANK_G1] != F_KANJI) ) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(F_KANJI);						// F_KANJI -> G0
					sta.bank[BANK_G0] = F_KANJI;
				}
				if( (sta.bank[BANK_G0] == F_KANJI) && (sta.region[REGION_GL] != BANK_G0) ) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				if( (sta.bank[BANK_G1] == F_KANJI) && (sta.region[REGION_GL] != BANK_G1) ) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_MSZ_TO_NSZ;
				jiscode = jis12conv(hiragana1conv(jiscode, TRUE), FALSE);
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			case F_KATAKANA:

				if(sta.region[REGION_GR] != BANK_G3) {

					// REGION_GRをBANK_G3(F_KATAKANA)に切り替えるべきか判断、必要なら切り替え

					if(kanaChange(sbuf, src, total_length, F_KATAKANA)) {
						CHARBUF(0x1B);
						CHARBUF(0x7C);							// G3 -> GR (LS3R)
						sta.region[REGION_GR] = BANK_G3;
					}
				}

				// REGION_GRがBANK_G3ならそのまま出力する

				if(sta.region[REGION_GR] == BANK_G3) {
					IF_MSZ_TO_NSZ;
					CHARBUF(jiscode + 0x80);
					break;
				}

				// それ以外の場合はBANK_G0かBANK_G1を使ってF_KANJIで出力

				if( (sta.bank[BANK_G0] != F_KANJI) && (sta.bank[BANK_G1] != F_KANJI) ) {
					CHARBUF(0x1B);
					CHARBUF(0x24);
					CHARBUF(F_KANJI);						// F_KANJI -> G0
					sta.bank[BANK_G0] = F_KANJI;
				}
				if( (sta.bank[BANK_G0] == F_KANJI) && (sta.region[REGION_GL] != BANK_G0) ) {
					CHARBUF(0x0F);							// G0 -> GL (LS0)
					sta.region[REGION_GL] = BANK_G0;
				}
				if( (sta.bank[BANK_G1] == F_KANJI) && (sta.region[REGION_GL] != BANK_G1) ) {
					CHARBUF(0x0E);							// G1 -> GL (LS1)
					sta.region[REGION_GL] = BANK_G1;
				}
				IF_MSZ_TO_NSZ;
				jiscode = jis12conv(katakana1conv(jiscode, TRUE), FALSE);
				CHARBUF(jiscode >> 8);
				CHARBUF(jiscode & 0xFF);
				break;

			default:
				break;
		}

		src += src_char_len;

	}

	CHARBUF(0x00);

	dst--;
	if(dst > maxbufsize) dst = maxbufsize; 

	return dst;			// 変換後の長さを返す(byte単位), 終端のヌル文字分を含まない
}


int jis12conv(const int jiscode, const BOOL bConvDir)
{
//	JIS第一第二水準漢字，非漢字についての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode (例 0x2121 -> U+3000)
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int jis12table[] = 
	{
		#include "jis12table.h"
	};

	static int		jis12revtable[sizeof(jis12table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(jis12table, jis12revtable, sizeof(jis12table));
		bTableInitialized = TRUE;
	}

	int		winresult = jis12winconv(jiscode, bConvDir);
	if(winresult != 0) return winresult;
	
	void	*result;
	if(bConvDir) {
		result = bsearch(&jiscode, jis12table, sizeof(jis12table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&jiscode, jis12revtable, sizeof(jis12revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}


int jis12winconv(const int jiscode, const BOOL bConvDir)
{
//	JIS第一第二水準非漢字でwindows固有のマッピングを有するものについての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int jis12wintable[] = 
	{
		#include "jis12wintable.h"
	};

	static int		jis12winrevtable[sizeof(jis12wintable) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(jis12wintable, jis12winrevtable, sizeof(jis12wintable));
		bTableInitialized = TRUE;
	}
	
	void	*result;
	if(bConvDir) {
		result = bsearch(&jiscode, jis12wintable, sizeof(jis12wintable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&jiscode, jis12winrevtable, sizeof(jis12winrevtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}


int jis3conv(const int jiscode, const BOOL bConvDir)
{
//	JIS第三水準漢字非漢字ついての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int jis3table[] = 
	{
		#include "jis3table.h"
	};

	static int		jis3revtable[sizeof(jis3table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(jis3table, jis3revtable, sizeof(jis3table));
		bTableInitialized = TRUE;
	}
	
	void	*result;
	if(bConvDir) {
		result = bsearch(&jiscode, jis3table, sizeof(jis3table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&jiscode, jis3revtable, sizeof(jis3revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}


int jis3combconv(const int jiscode, WCHAR* dbuf, const int bufsize)
{
//	JIS第三水準漢字非漢字でunicode合成文字に対応するものについての jiscode -> UTF-16文字列変換
//	対応する文字コードがなければ戻り値0を返す
//
//	jis         変換元jiscode
//	dbuf		変換した文字列が書き込まれるbuf
//  bufsize     変換先dbufの最大サイズ(WCHAR単位)
//	戻り値		変換したUTF-16文字長(WCHAR単位)

	static WCHAR jis3combtable[][UTF16TABLELEN] =
	{
		#include "jis3combiningtable.h"
	};
    
	int		len = 0;
	WCHAR	jisbuf[UTF16TABLELEN];
 
	jisbuf[0] = (WCHAR)jiscode;

	void	*result = bsearch(jisbuf, jis3combtable, sizeof(jis3combtable) / sizeof(WCHAR) / UTF16TABLELEN / 2, sizeof(WCHAR) * UTF16TABLELEN * 2, comparefortable2);

	if(result != NULL) len = swprintf_s(dbuf, bufsize, L"%s", (WCHAR*)result + UTF16TABLELEN);

	return len;
}


int jis3combrevconv(const WCHAR* sbuf, int *jis)
{
//	JIS第三水準漢字非漢字でunicode合成文字に対応するものについての UTF-16 -> jiscode変換
//	対応する文字コードがなければ戻り値0を返す
//
//	sbuf		変換元buf
//  jis         変換されたjisコードが入る   
//	戻り値		変換元のUTF-16文字列長(WCHAR単位)

    static WCHAR jis3combtable[][UTF16TABLELEN] =
	{
        #include "jis3combiningtable.h"
	};    
    
	static BOOL		bTableInitialized = FALSE;
    
	if(!bTableInitialized)
    {
        qsort(jis3combtable, sizeof(jis3combtable) / sizeof(WCHAR) / UTF16TABLELEN / 2, sizeof(WCHAR) * UTF16TABLELEN * 2, comparefortable2str);
		bTableInitialized = TRUE;
	}
    
    int     len;    
	void*   result = bsearch(sbuf - UTF16TABLELEN, jis3combtable, sizeof(jis3combtable) / sizeof(WCHAR) / UTF16TABLELEN / 2, sizeof(WCHAR) * UTF16TABLELEN * 2, comparefortable2str);
    
	if(result != NULL)
    {
		len = (int)wcsnlen_s((WCHAR*)result + UTF16TABLELEN, UTF16TABLELEN);

        if( !wcsncmp(sbuf, (WCHAR*)result + UTF16TABLELEN, len) ) {    // 部分一致の排除
			*jis = (int)(*(WCHAR*)result);
            return len;
        }
	}
 
    *jis = 0;
    
    return 0;
}


int jis4conv(const int jiscode, const BOOL bConvDir)
{
//	JIS第四水準漢字についての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int jis4table[] = 
	{
		#include "jis4table.h"
	};

	static int		jis4revtable[sizeof(jis4table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(jis4table, jis4revtable, sizeof(jis4table));
		bTableInitialized = TRUE;
	}
	
	void	*result;
	if(bConvDir) {
		result = bsearch(&jiscode, jis4table, sizeof(jis4table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&jiscode, jis4revtable, sizeof(jis4revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}


int hiragana1conv(const int code, const BOOL bConvDir)
{
//	全角ひらがな文字についての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int hiragana1table[] = 
	{
		#include "hiragana1table.h"
	};

	static int		hiragana1revtable[sizeof(hiragana1table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(hiragana1table, hiragana1revtable, sizeof(hiragana1table));
		bTableInitialized = TRUE;
	}

	void	*result;
	if(bConvDir) {
		result = bsearch(&code, hiragana1table, sizeof(hiragana1table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&code, hiragana1revtable, sizeof(hiragana1revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}


int katakana1conv(const int code, const BOOL bConvDir)
{
//	全角カタカナ文字についての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int katakana1table[] = 
	{
		#include "katakana1table.h"
	};

	static int		katakana1revtable[sizeof(katakana1table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(katakana1table, katakana1revtable, sizeof(katakana1table));
		bTableInitialized = TRUE;
	}

	void	*result;
	if(bConvDir) {
		result = bsearch(&code, katakana1table, sizeof(katakana1table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&code, katakana1revtable, sizeof(katakana1revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}

int kanacommon1conv(const int code, const BOOL bConvDir)
{
//	全角ひらがな，カタカナ集合の共通文字についての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int kanacommon1table[] = 
	{
		#include "kanacommon1table.h"
	};

	static int		kanacommon1revtable[sizeof(kanacommon1table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(kanacommon1table, kanacommon1revtable, sizeof(kanacommon1table));
		bTableInitialized = TRUE;
	}

	void	*result;
	if(bConvDir) {
		result = bsearch(&code, kanacommon1table, sizeof(kanacommon1table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&code, kanacommon1revtable, sizeof(kanacommon1revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}


int hankaku1conv(const int code, const BOOL bConvDir)
{
//	JIS X0201カタカナ文字集合についての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int hankaku1table[] = 
	{
		#include "hankaku1table.h"
	};

	static int		hankaku1revtable[sizeof(hankaku1table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(hankaku1table, hankaku1revtable, sizeof(hankaku1table));
		bTableInitialized = TRUE;
	}

	void	*result;
	if(bConvDir) {
		result = bsearch(&code, hankaku1table, sizeof(hankaku1table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&code, hankaku1revtable, sizeof(hankaku1revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}


int tuikakigou1conv(const int code, const BOOL bConvDir)
{
//	追加記号，追加漢字集合のうち、対応するunicode文字が存在する文字についての jiscode <-> unicode変換
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数jiscode -> 戻り値unicode
//	bConvDir : FALSE	引数unicode -> 戻り値jiscode

	static int kigou1table[] = 
	{
		#include "tuikakigou1table.h"
	};

#if	defined(__TOOL_VISTA_7__) || defined(__TOOL_XP__)
	static int kigou1excludetable[] =
	{
		#include "kigou1excludetable.h"
	};

	static BOOL		bExcludeTableInitialized = FALSE;
#endif

	static int		kigou1revtable[sizeof(kigou1table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(kigou1table, kigou1revtable, sizeof(kigou1table));
		bTableInitialized = TRUE;
	}

#if	defined(__TOOL_VISTA_7__) || defined(__TOOL_XP__)
	if(bConvDir && !bExcludeTableInitialized) {
		qsort(kigou1excludetable, sizeof(kigou1excludetable) / sizeof(int), sizeof(int), comparefortable);
		bExcludeTableInitialized = TRUE;
	}
#endif

	void	*result;
	if(bConvDir) {
		result = bsearch(&code, kigou1table, sizeof(kigou1table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&code, kigou1revtable, sizeof(kigou1revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	int		resultcode = *((int*)result + 1);

#if	defined(__TOOL_VISTA_7__) || defined(__TOOL_XP__)
	result = bsearch(&resultcode, kigou1excludetable, sizeof(kigou1excludetable) / sizeof(int), sizeof(int), comparefortable);
	if(result != NULL) resultcode = 0;
#endif

	return resultcode;
}


int tuikakigou2conv(const int jis, WCHAR *dbuf, const int bufsize)
{
//	追加記号集合のうち、対応するunicode文字が存在する文字が存在せず
//	[...]あるいは[#xx#xx]で表される文字についての jiscode -> UTF-16文字列変換
//
//	jis         変換元jiscode
//	dbuf		変換した文字列が書き込まれるbuf
//  bufsize     変換先dbufのサイズ(WCHAR単位)
//	戻り値		変換したunicode文字長(WCHAR単位)

	static WCHAR kigou2table[][UTF16TABLELEN] =
	{
        #include "tuikakigou2table.h"
	};
    
	int			len;
	WCHAR		jisbuf[UTF16TABLELEN];
    
    jisbuf[0] = (WCHAR)jis;
    
	void	*result = bsearch(jisbuf, kigou2table, sizeof(kigou2table) / sizeof(WCHAR) / UTF16TABLELEN / 2, sizeof(WCHAR) * UTF16TABLELEN * 2, comparefortable2);
    
	if(result != NULL) {
        len = swprintf_s(dbuf, bufsize, L"%s", (WCHAR*)result + UTF16TABLELEN);
	} else {
		len = swprintf_s(dbuf, bufsize, L"[#%.2d#%.2d]", (jis / 256) - 32, (jis % 256) - 32);
	}
    
	return len;
}


int tuikakigou2revconv(const WCHAR *sbuf, int *jis)
{
//	追加記号集合のうち、対応するunicode文字が存在する文字が存在せず
//	[...]あるいは[#xx#xx]で表される文字についての UTF-16文字列 -> jiscode変換
//
//	sbuf		変換元の文字列を有するbuf
//  jis         変換されたjisコードが入る   
//	戻り値		変換元のUTF-16文字列長(WCHAR単位)

    static WCHAR kigou2table[][UTF16TABLELEN] =
	{
        #include "tuikakigou2table.h"
	};    
    
	static BOOL		bTableInitialized = FALSE;
    
	if(!bTableInitialized)
    {
        qsort(kigou2table, sizeof(kigou2table) / sizeof(WCHAR) / UTF16TABLELEN / 2, sizeof(WCHAR) * UTF16TABLELEN * 2, comparefortable2str);
		bTableInitialized = TRUE;
	}
    
    int     len;    
	void*   result = bsearch(sbuf - UTF16TABLELEN, kigou2table, sizeof(kigou2table) / sizeof(WCHAR) / UTF16TABLELEN / 2, sizeof(WCHAR) * UTF16TABLELEN * 2, comparefortable2str);

    if(result != NULL)
    {
		len = (int)wcsnlen_s((WCHAR*)result + UTF16TABLELEN, UTF16TABLELEN);
        
        if( !wcsncmp(sbuf, (WCHAR*)result + UTF16TABLELEN, len) ) {							// 部分一致の排除
            *jis = *(WCHAR*)result;
            return len;
        }
	}
    
    if( (sbuf[0]==L'[') && (sbuf[1]==L'#') && isdigit(sbuf[2]) && isdigit(sbuf[3]) && (sbuf[4]==L'#') && isdigit(sbuf[5]) && isdigit(sbuf[6]) && (sbuf[7]==L']') )
    {
        *jis = ((sbuf[2] - L'0') * 10 + (sbuf[3] - L'0')) * 256 + (sbuf[5] - L'0') * 10 + (sbuf[6] - L'0') + 0x2020;
        return 8;
    } 

    *jis = 0;
    
    return 0;
}


int charsize1conv(const int jiscode, const BOOL bConvDir)
{
//　空白文字及び英数文字の 半角jiscode <-> 全角jiscode 変換
//　例：A(0x41) <-> Ａ(0x2341)
//	対応する文字コードがなければ0を返す
//
//	bConvDir : TRUE		引数半角jiscode -> 戻り値全角jiscode
//	bConvDir : FALSE	引数全角jiscode -> 戻り値半角jiscode

	static int charsize1table[] = 
	{
		#include "charsize1table.h"
	};

	static int		charsize1revtable[sizeof(charsize1table) / sizeof(int)];
	static BOOL		bTableInitialized = FALSE;

	if(!bConvDir && !bTableInitialized) {
		initrevtable(charsize1table, charsize1revtable, sizeof(charsize1table));
		bTableInitialized = TRUE;
	}

	void	*result;
	if(bConvDir) {
		result = bsearch(&jiscode, charsize1table, sizeof(charsize1table) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	} else {
		result = bsearch(&jiscode, charsize1revtable, sizeof(charsize1revtable) / sizeof(int) / 2, sizeof(int) * 2, comparefortable);
	}

	if(result == NULL) return 0;

	return *((int*)result + 1);
}


void initrevtable(const int *tabletop, int *revtop, const int tablesize)	// 変換テーブル初期化用
{
	int		itemnum = tablesize / sizeof(int) / 2;
		
	for(int i = 0; i < itemnum; i++) {
		revtop[i * 2]		= tabletop[i * 2 + 1];
		revtop[i * 2 + 1]	= tabletop[i * 2];
	}

	qsort(revtop, itemnum, sizeof(int) * 2, comparefortable);
	
	return;
}


int comparefortable(const void *item1, const void *item2)					// 変換テーブルソート，検索用関数
{
	return *(int*)item1 - *(int*)item2;
}


int comparefortable2(const void *item1, const void *item2)					// 変換テーブルソート，検索用関数
{
	return *(WCHAR*)item1 - *(WCHAR*)item2;
}


int comparefortable2str(const void *item1, const void *item2)				// 変換テーブルソート，検索用関数
{ 
    WCHAR *str1 = ((WCHAR*)item1) + UTF16TABLELEN;
    WCHAR *str2 = ((WCHAR*)item2) + UTF16TABLELEN;
    
    int		i = 0;
    
	while( (str1[i] != 0) && (str2[i] != 0) ) {
		if(str1[i] != str2[i]) return (int)(str1[i]) - (int)(str2[i]);
		i++;
	}
    
	return 0;
}


void initConvStatus(ConvStatus *status)
{
	status->region[REGION_GL]	= BANK_G0;
	status->region[REGION_GR]	= BANK_G2;
	
	status->bank[BANK_G0]		= F_KANJI;
	status->bank[BANK_G1]		= F_ALPHA;
	status->bank[BANK_G2]		= F_HIRAGANA;
	status->bank[BANK_G3]		= F_KATAKANA;

	status->bSingleShift		= FALSE;
	status->region_GL_backup	= BANK_G0;		// backup for singleshift

	status->bXCS				= FALSE;
	status->bNormalSize			= TRUE;			// TRUE: NSZ, FALSE: MSZ

	return;
}


BOOL isControlChar(const unsigned char c)
{
	if( (c >= 0x00) && (c <= 0x20) ) return TRUE;

	if( (c >= 0x7f) && (c <= 0xa0) ) return TRUE;

	if( c == 0xff ) return TRUE;

	return FALSE;
}


BOOL isOneByteGSET(const unsigned char c)
{
	BOOL	bResult;

	switch(c)
	{
		case F_ALPHA:
		case F_HIRAGANA:
		case F_KATAKANA:
		case F_MOSAICA:
		case F_MOSAICB:
		case F_MOSAICC:
		case F_MOSAICD:
		case F_P_ALPHA:
		case F_P_HIRAGANA:
		case F_P_KATAKANA:
		case F_HANKAKU:
			bResult = TRUE;
			break;
		default:
			bResult = FALSE;
	}

	return bResult;
}


BOOL isTwoByteGSET(const unsigned char c)
{
	BOOL	bResult;

	switch(c)
	{
		case F_KANJI:
		case F_JIS1KANJI:
		case F_JIS2KANJI:
		case F_TUIKAKIGOU:
			bResult = TRUE;
			break;
		default:
			bResult = FALSE;
	}

	return bResult;
}


BOOL isOneByteDRCS(const unsigned char c)
{
	BOOL	bResult;

	switch(c)
	{
		case F_DRCS1:
		case F_DRCS2:
		case F_DRCS3:
		case F_DRCS4:
		case F_DRCS5:
		case F_DRCS6:
		case F_DRCS7:
		case F_DRCS8:
		case F_DRCS9:
		case F_DRCS10:
		case F_DRCS11:
		case F_DRCS12:
		case F_DRCS13:
		case F_DRCS14:
		case F_DRCS15:
		case F_MACRO:
			bResult = TRUE;
			break;
		default:
			bResult = FALSE;
	}

	return bResult;
}


BOOL isTwoByteDRCS(const unsigned char c)
{
	BOOL	bResult;

	switch(c)
	{
		case F_DRCS0:
			bResult = TRUE;
			break;
		default:
			bResult = FALSE;
	}

	return bResult;
}


int numgbank(const unsigned char c)
{
	int		banknum;

	switch(c)
	{
		case 0x28:
			banknum = BANK_G0;
			break;
		case 0x29:
			banknum = BANK_G1;
			break;
		case 0x2A:
			banknum = BANK_G2;
			break;
		case 0x2B:
			banknum = BANK_G3;
			break;
		default:
			banknum = BANK_G0;
	}

	return banknum;
}


int wcharbuf(WCHAR *dbuf, const int maxbufsize, const int dst, const int uc, const BOOL bXCS)
{
	if(bXCS) return 0;

	if(uc >= 0x10000)
	{
		if( (dbuf != NULL) && (dst + 0 < maxbufsize) ) dbuf[dst + 0] = (WCHAR)(0xD800 + ((uc - 0x10000) / 0x400));
		if( (dbuf != NULL) && (dst + 1 < maxbufsize) ) dbuf[dst + 1] = (WCHAR)(0xDC00 + ((uc - 0x10000) % 0x400));

		return 2;
	}
	
	if( (dbuf != NULL) && (dst < maxbufsize) ) dbuf[dst] = (WCHAR)uc;

	return 1;
}


void charbuf(unsigned char *dbuf, const int maxbufsize, const int dst, const int code)
{
	if( (dbuf != NULL) && (dst < maxbufsize) ) dbuf[dst] = code;

	return;
}


void changeConvStatus(const unsigned char *sbuf, int *srcpos, ConvStatus *sta)
{
// 制御コードに従って符号の呼び出し、指示制御する
// srcposは制御コードサイズ分進む

	int		src = *srcpos;

	switch(sbuf[src])
	{
		// 文字サイズ指定

		case 0x89:
			sta->bNormalSize = FALSE;					// 文字サイズ半角(MSZ)指定
			src++;
			break;
		case 0x8A:
			sta->bNormalSize = TRUE;					// 文字サイズ全角(NSZ)指定
			src++;
			break;

		// 符号の呼び出し

		case 0x0F:										// LS0 (0F), G0->GL ロッキングシフト
			sta->region[REGION_GL] = BANK_G0;
			src++;
			break;
		case 0x0E:										// LS1 (0E), G1->GL ロッキングシフト
			sta->region[REGION_GL] = BANK_G1;
			src++;
			break;
		case 0x19:										// SS2 (19), G2->GL シングルシフト
			sta->region_GL_backup = sta->region[REGION_GL];
			sta->region[REGION_GL] = BANK_G2;
			sta->bSingleShift = TRUE;
			src++;
			break;
		case 0x1D:										// SS3 (1D), G3->GL シングルシフト
			sta->region_GL_backup = sta->region[REGION_GL];
			sta->region[REGION_GL] = BANK_G3;
			sta->bSingleShift = TRUE;
			src++;
			break;

		case 0x1B:		// ESCに続く制御コード

			switch(sbuf[src + 1])
			{
				// ESCに続く符号の呼び出し
				
				case 0x6E:								// LS2 (ESC 6E), G2->GL ロッキングシフト
					sta->region[REGION_GL] = BANK_G2;
					src += 2;
					break;
				case 0x6F:								// LS3 (ESC 6F), G3->GL ロッキングシフト
					sta->region[REGION_GL] = BANK_G3;
					src += 2;
					break;
				case 0x7E:								// LS1R (ESC 7E), G1->GR ロッキングシフト
					sta->region[REGION_GR] = BANK_G1;
					src += 2;
					break;
				case 0x7D:								// LS2R (ESC 7D), G2->GR ロッキングシフト
					sta->region[REGION_GR] = BANK_G2;
					src += 2;
					break;
				case 0x7C:								// LS3R (ESC 7C), G3->GR ロッキングシフト
					sta->region[REGION_GR] = BANK_G3;
					src += 2;
					break;

				// ESCに続く符号の指示制御

				case 0x28:	// ESC 28
				case 0x29:	// ESC 29
				case 0x2A:	// ESC 2A
				case 0x2B:	// ESC 2B
					if(isOneByteGSET(sbuf[src + 2])) {
						sta->bank[numgbank(sbuf[src + 1])] = sbuf[src + 2];					// 1バイトGSET指示 (ESC 28|29|2A|2B [F]) -> G0,G1,G2,G3
						src += 3;
					} else {
						if(sbuf[src + 2] == 0x20) {
							if(isOneByteDRCS(sbuf[src + 3])) {
								sta->bank[numgbank(sbuf[src + 1])] = sbuf[src + 3] + 0x10;	// 1バイトDRCS指示 (ESC 28|29|2A|2B 20 [F]) -> G0,G1,G2,G3		
								src += 4;														// + 0x10は終端符号が被らないようにするための細工
							} else {
								src += 4;														// 不明な1バイトDRCS指示 (ESC 28|29|2A|2B 20 XX)
							}
						} else {
							src += 3;															// 不明な1バイトGSET指示 (ESC 28|29|2A|2B XX)
						}
					}
					break;

				case 0x24:	// ESC 24
					if(isTwoByteGSET(sbuf[src + 2])) {
						sta->bank[BANK_G0] = sbuf[src + 2];									// 2バイトGSET指示 (ESC 24 [F]) ->G0
						src += 3;
					} else {
						switch(sbuf[src + 2])
						{
							case 0x28:	// ESC 24 28
								if(sbuf[src + 3] == 0x20) {
									if(isTwoByteDRCS(sbuf[src + 4])) {
										sta->bank[BANK_G0] = sbuf[src + 4];					// 2バイトDRCS指示 (ESC 24 28 20 [F]) ->G0
										src += 5;
									} else {
										src += 5;												// 不明な2バイトDRCS指示 (ESC 24 28 20 XX)
									}
								} else {
									src += 4;													// 不明な指示 (ESC 24 28 XX)
								}
								break;
							case 0x29:	// ESC 24 29
							case 0x2A:	// ESC 24 2A
							case 0x2B:	// ESC 24 2B
								if(isTwoByteGSET(sbuf[src + 3])) {
									sta->bank[numgbank(sbuf[src + 2])] = sbuf[src + 3];		// 2バイトGSET指示 (ESC 24 29|2A|2B [F]) ->G1,G2,G3
									src += 4;
								} else {
									if(sbuf[src + 3] == 0x20) {
										if(isTwoByteDRCS(sbuf[src + 4])) {
											sta->bank[numgbank(sbuf[src + 2])] = sbuf[src + 4];	// 2バイトDRCS指示 (ESC 24 29|2A|2B 20 [F]) ->G1,G2,G3
											src += 5;
										} else {
											src += 5;												// 不明な2バイトDRCS指示 (ESC 24 29|2A|2B 20 XX)
										}
									} else {
										src += 4;													// 不明な2バイトGSET指示 (ESC 24 29|2A|2B XX)
									}
								}
								break;
							default:
								src += 3;															// 不明な指示 (ESC 24 XX)
						}
					}
					break;

				default:
					src += 2;																		// 不明な指示 (ESC XX)
			}
			break;

		default:	// 上記以外の制御コード
			src++;
	}

	*srcpos = src;

	return;
}


void csiProc(const unsigned char *sbuf, int *srcpos, ConvStatus *sta)
{
// CSI制御コード処理
// srcposは制御コードサイズ分進む
// 
// XCSの処理のみ

	int			src = *srcpos + 1;

	int			param[4];
	memset(param, 0, sizeof(param));	

	int			pcount = 0;
	int			fcode = 0;
	
	while(TRUE) {

		if(isdigit(sbuf[src]) != 0) {										// パラメータ?
			param[pcount] *= 10;
			param[pcount] += (sbuf[src] - '0');
			src++;
		}
		else if(sbuf[src] == 0x3B) {										// パラメータ区切り?
			pcount++;
			src++;
			if(pcount == 4) break;											// パラメータ多すぎ
		}
		else if(sbuf[src] == 0x20) {										// パラメータ終了?
			if( (sbuf[src + 1] >= 0x42) && (sbuf[src + 1] <= 0x6F) ) fcode = sbuf[src + 1];		// 終端文字?
			src += 2;
			pcount++;
			break;
		}
		else if( (sbuf[src] >= 0x42) && (sbuf[src] <= 0x6F) ) {				// 終端文字?
			fcode = sbuf[src];
			src++;
			break;
		}
		else {																// 不正なコード
			src++;
			break;
		}
	}

	switch(fcode) {

		case	0x66:									// XCS
			if( (pcount == 1) && (param[0] == 0) ) {
				sta->bXCS = TRUE;						// 外字代替符号定義開始
				break;
			}
			if( (pcount == 1) && (param[0] == 1) ) {
				sta->bXCS = FALSE;						// 外字代替符号定義終了
				break;
			}
			break;

		default:
			break;
	}
	
	*srcpos = src;

	return;
}


void defaultMacroProc(const unsigned char c, ConvStatus *sta)
{
// デフォルトマクロの処理
// 番組情報(SI)では使用されない

	switch(c)
	{
		case 0x60:
			sta->bank[BANK_G0] = F_KANJI;
			sta->bank[BANK_G1] = F_ALPHA;
			sta->bank[BANK_G2] = F_HIRAGANA;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x61:
			sta->bank[BANK_G0] = F_KANJI;
			sta->bank[BANK_G1] = F_KATAKANA;
			sta->bank[BANK_G2] = F_HIRAGANA;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x62:
			sta->bank[BANK_G0] = F_KANJI;
			sta->bank[BANK_G1] = F_DRCS1A;
			sta->bank[BANK_G2] = F_HIRAGANA;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x63:
			sta->bank[BANK_G0] = F_MOSAICA;
			sta->bank[BANK_G1] = F_MOSAICC;
			sta->bank[BANK_G2] = F_MOSAICD;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x64:
			sta->bank[BANK_G0] = F_MOSAICA;
			sta->bank[BANK_G1] = F_MOSAICB;
			sta->bank[BANK_G2] = F_MOSAICD;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x65:
			sta->bank[BANK_G0] = F_MOSAICA;
			sta->bank[BANK_G1] = F_DRCS1A;
			sta->bank[BANK_G2] = F_MOSAICD;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x66:
			sta->bank[BANK_G0] = F_DRCS1A;
			sta->bank[BANK_G1] = F_DRCS2A;
			sta->bank[BANK_G2] = F_DRCS3A;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x67:
			sta->bank[BANK_G0] = F_DRCS4A;
			sta->bank[BANK_G1] = F_DRCS5A;
			sta->bank[BANK_G2] = F_DRCS6A;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x68:
			sta->bank[BANK_G0] = F_DRCS7A;
			sta->bank[BANK_G1] = F_DRCS8A;
			sta->bank[BANK_G2] = F_DRCS9A;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x69:
			sta->bank[BANK_G0] = F_DRCS10A;
			sta->bank[BANK_G1] = F_DRCS11A;
			sta->bank[BANK_G2] = F_DRCS12A;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x6a:
			sta->bank[BANK_G0] = F_DRCS13A;
			sta->bank[BANK_G1] = F_DRCS14A;
			sta->bank[BANK_G2] = F_DRCS15A;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x6b:
			sta->bank[BANK_G0] = F_KANJI;
			sta->bank[BANK_G1] = F_DRCS2A;
			sta->bank[BANK_G2] = F_HIRAGANA;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x6c:
			sta->bank[BANK_G0] = F_KANJI;
			sta->bank[BANK_G1] = F_DRCS3A;
			sta->bank[BANK_G2] = F_HIRAGANA;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x6d:
			sta->bank[BANK_G0] = F_KANJI;
			sta->bank[BANK_G1] = F_DRCS4A;
			sta->bank[BANK_G2] = F_HIRAGANA;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x6e:
			sta->bank[BANK_G0] = F_KATAKANA;
			sta->bank[BANK_G1] = F_HIRAGANA;
			sta->bank[BANK_G2] = F_ALPHA;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		case 0x6f:
			sta->bank[BANK_G0] = F_ALPHA;
			sta->bank[BANK_G1] = F_MOSAICA;
			sta->bank[BANK_G2] = F_DRCS1A;
			sta->bank[BANK_G3] = F_MACROA;
			sta->region[REGION_GL] = BANK_G0;
			sta->region[REGION_GR] = BANK_G2;
			break;
		default:
			break;
	}

	return;
}


int charclass(const WCHAR *sbuf, int *jiscode, int *slen)
{
//		unicode文字種を判定する
//		
//		sbuf		判断する文字buf
//		戻り値		判断結果の文字種(F_CONTROL, F_ALPHA, …)
//		jiscode		結果のjiscode
//		slen		結果のunicode文字長(WCHAR単位)

	int		len = 1;
	int		uc = sbuf[0];

	if( (uc >= 0xD800) && (uc <= 0xDBFF) ) {		// サロゲートペア？
		int uc2 = sbuf[1];
		uc = ((uc - 0xD800) << 10) + (uc2 - 0xDC00) + 0x10000;
		len = 2;
	}


	// 制御コード (SP含む)

	if(uc <= 0x20) {
		*jiscode = uc;
		*slen = len;
		return F_CONTROL;
	}

	int		code;
	int		templen;


	// '['から始まる追加記号集合

	if(uc == L'[') {
		templen = tuikakigou2revconv(sbuf, &code);
		if(templen != 0) {
			*jiscode = code;
			*slen = templen;
			return F_TUIKAKIGOU;
		}
	}


	// 英数集合

	if( (uc >= 0x21) && (uc <= 0x7E) ) {
		*jiscode = uc;
		*slen = len;
		return F_ALPHA;
	}


	// 第三水準非漢字のunicode合成文字に該当するもの

	templen = jis3combrevconv(sbuf, &code);
	if(templen != 0) {
		*jiscode = code;
		*slen = templen;
		return F_JIS1KANJI;
	}	


	// 平仮名集合･片仮名集合の共通文字

	code = kanacommon1conv(uc, FALSE);
	if(code != 0) {
		*jiscode = code;
		*slen = len;
		return F_KANACOMMON;
	}


	// 平仮名集合

	code = hiragana1conv(uc, FALSE);
	if(code != 0) {
		*jiscode = code;
		*slen = len;
		return F_HIRAGANA;
	}


	// 片仮名集合

	code = katakana1conv(uc, FALSE);
	if(code != 0) {
		*jiscode = code;
		*slen = len;
		return F_KATAKANA;
	}


	// JIS X0201 片仮名集合

	code = hankaku1conv(uc, FALSE);
	if(code != 0) {
		*jiscode = code;
		*slen = len;
		return F_HANKAKU;
	}


	// 追加記号集合

	code = tuikakigou1conv(uc, FALSE);
	if(code != 0) {
		*jiscode = code;
		*slen = len;
		return F_TUIKAKIGOU;
	}


	// 第一･第二水準漢字集合 (漢字集合)

	code = jis12conv(uc, FALSE);
	if(code != 0) {
		*jiscode = code;
		*slen = len;
		return F_KANJI;
	}


	// 第三水準漢字集合 (JIS互換漢字1面)

	code = jis3conv(uc, FALSE);
	if(code != 0) {
		*jiscode = code;
		*slen = len;
		return F_JIS1KANJI;
	}


	// 第四水準漢字集合 (JIS互換漢字2面)

	code = jis4conv(uc, FALSE);
	if(code != 0) {
		*jiscode = code;
		*slen = len;
		return F_JIS2KANJI;
	}


	// その他の不明な文字

	*jiscode = 0;
	*slen = len;
	return F_UNKNOWN;
}


BOOL kanaChange(const WCHAR *sbuf, const int srcpos, const int total_length, const int fcode)
{
//	変換コスト(変換結果のバイト数)を考慮して
//	REGION_GRの設定をBANK_G2(F_HIRAGANA), BANK_G3(F_KATAKANA)間で切り替えるか判断する

	int		src = srcpos;
	int		charlen;
	int		jiscode;
	int		nfcode = fcode;

	KanaSequence	seq;
	memset(&seq, 0, sizeof(seq));

	while(src < total_length) {

		int f = charclass(sbuf + src, &jiscode, &charlen);

		if( (f == F_HIRAGANA) || (f == F_KATAKANA) ) {
			if(f == nfcode) {
				seq.num[seq.count]++;
				if(seq.num[seq.count] == 5) break;
			} else {
				seq.count++;
				if(seq.count == 20) break;
				nfcode = (nfcode == F_HIRAGANA) ? F_KATAKANA : F_HIRAGANA;
				seq.num[seq.count]++;
			}
		}

		src += charlen;
	}

	if(seq.num[0] == 1) return FALSE;
	if(seq.num[0] >= 5) return TRUE;

/*
	// 数回先の切り替えまで考慮しての判断
	// なるべく軽い判断が必要ならこちらで

	if(seq.count == 0) {
		if(seq.num[0] > 1) return TRUE;
		return FALSE;
	}

	if(seq.count == 1) {
		if( (seq.num[0] = 3) && (seq.num[1] == 1) ) return TRUE;
		return FALSE;
	}

	if(seq.count == 2) {
		if(seq.num[0] == 2) {
			if(seq.num[1] == 1) return TRUE;
			if( (seq.num[1] == 2) && (seq.num[2] > 1) ) return TRUE;
			return FALSE;
		}
		if(seq.num[1] <= 2) return TRUE;
		if( (seq.num[1] == 3) && (seq.num[2] >= 2) ) return TRUE;
		return FALSE;
	}

	return FALSE;
*/

	// 最大20回先の切り替えまで考慮しての判断

	if(seq.count == 20) return FALSE;

	kanacostc(&seq, 0, 0);
	int		ccost = seq.mincost;		// REGION_GRを切り替えた場合の最低コスト

	kanacostn(&seq, 0, 0);
	int		ncost = seq.mincost;		// REGION_GRを切り替えない場合の最低コスト

	if(ncost < ccost) return FALSE;

	return TRUE;
}


void kanacostc(KanaSequence *seq, const int count, const int cost)		// かな変換コスト計算用
{
	int		newcost = cost + 2 + seq->num[count];
	if(count == seq->count) {
		if( (newcost <= seq->mincost) || (seq->mincost == 0) ) seq->mincost = newcost;
		return;
	}
	kanacostc(seq, count + 1, newcost);
	kanacostn(seq, count + 1, newcost);
	return;
}


void kanacostn(KanaSequence *seq, const int count, const int cost)		// かな変換コスト計算用
{
	int		newcost = cost + 2 * seq->num[count];
	if(count == seq->count) {
		if( (newcost <= seq->mincost) || (seq->mincost == 0) ) seq->mincost = newcost;
		return;
	}
	kanacostp(seq, count + 1, newcost);
	return;
}


void kanacostp(KanaSequence *seq, const int count, const int cost)		// かな変換コスト計算用
{
	int		newcost = cost + seq->num[count];
	if(count == seq->count) {
		if( (newcost <= seq->mincost) || (seq->mincost == 0) ) seq->mincost = newcost;
		return;
	}
	kanacostc(seq, count + 1, newcost);
	kanacostn(seq, count + 1, newcost);
	return;
}


BOOL kigouChange(const WCHAR *sbuf, const int srcpos, const int total_length)
{
//	sbufの現在の場所srcposより後ろにF_TUIKAKIGOU, F_CONTROL, F_ALPHA以外の文字が無ければTRUEを返す

	int		src = srcpos;
	int		charlen;
	int		jiscode;

	BOOL	bKigouOnly = TRUE;

	while(src < total_length) {

		int f = charclass(sbuf + src, &jiscode, &charlen);
		if( (f != F_TUIKAKIGOU) && (f != F_CONTROL) && (f != F_ALPHA) ) {
			bKigouOnly = FALSE;
			break;
		}

		src += charlen;
	}

	return bKigouOnly;
}


BOOL bankChange(const WCHAR *sbuf, const int srcpos, const int total_length, const ConvStatus *convsta, const int fcode)
{
//	変換コスト(変換結果のバイト数)を考慮して
//	使いたい文字種をBANK_G0, BANK_G1のどちらに割り振るべきかを判断する
//
//	戻り値	TRUE -> BANK_G0, FALSE -> BANK_G1

	int		src = srcpos;
	int		charlen;
	int		jiscode;
	int		nfcode = fcode;

	CharSequence	seq;
	memset(&seq, 0, sizeof(seq));

	seq.fcode[0] = nfcode;

	while(src < total_length) {

		int f = charclass(sbuf + src, &jiscode, &charlen);

		if( (f == F_ALPHA) || (f == F_HANKAKU) || (f == F_KANJI) || (f == F_JIS1KANJI) || (f == F_JIS2KANJI) || (f == F_TUIKAKIGOU) ) {
			if(f != nfcode) {
				if(seq.count == 255) break;
				seq.count++;
				nfcode = f;
				seq.fcode[seq.count] = nfcode;
			}
		}

		src += charlen;
	}

	BankStatus sta;
	memset(&sta, 0, sizeof(sta));

	sta.bankG0 = convsta->bank[BANK_G0];
	sta.bankG1 = convsta->bank[BANK_G1];
	sta.regionGL = convsta->region[REGION_GL];

	bankG0cost(&seq, sta, fcode);
	int		costG0 = seq.mincost;					// BANK_G0に割り振った場合の最低コスト

	seq.mincost = 0;
	bankG1cost(&seq, sta, fcode);
	int		costG1 = seq.mincost;					// BANK_G1に割り振った場合の最低コスト

	if(costG0 < costG1) return TRUE;
	if(costG0 > costG1) return FALSE;

	if( (fcode == F_ALPHA) || (fcode == F_ALPHA) ) return FALSE;

	return TRUE;
}


void bankG0cost(CharSequence *seq, BankStatus sta, const int fcode)			// BANK切り替えコスト計算用
{
	switch(fcode)
	{
		case F_ALPHA:
		case F_HANKAKU:
			sta.cost += 3;				// ESC 28 [F]
			sta.bankG0 = fcode;
			break;
		case F_KANJI:
		case F_JIS1KANJI:
		case F_JIS2KANJI:
		case F_TUIKAKIGOU:
			sta.cost += 3;				// ESC 24 [F}
			sta.bankG0 = fcode;
			break;
		default:
			break;
	}

	while( (seq->fcode[sta.count] == sta.bankG0) || (seq->fcode[sta.count] == sta.bankG1) )
	{
		if( (seq->fcode[sta.count] == sta.bankG0) && (sta.regionGL != BANK_G0) ) {
			sta.cost++;						// LS0
			sta.regionGL = BANK_G0;
		}
		if( (seq->fcode[sta.count] == sta.bankG1) && (sta.regionGL != BANK_G1) ) {
			sta.cost++;						// LS1
			sta.regionGL = BANK_G1;
		}
		if(sta.count == seq->count) {
			if( (seq->mincost == 0) || (seq->mincost > sta.cost) ) {
				seq->mincost = sta.cost;
			}
			return;
		}
		sta.count++;
	}

	bankG0cost(seq, sta, seq->fcode[sta.count]);
	bankG1cost(seq, sta, seq->fcode[sta.count]);

	return;
}


void bankG1cost(CharSequence *seq, BankStatus sta, const int fcode)			// BANK切り替えコスト計算用
{
	switch(fcode)
	{
		case F_ALPHA:
		case F_HANKAKU:
			sta.cost += 3;				// ESC 29 [F]
			sta.bankG1 = fcode;
			break;
		case F_KANJI:
		case F_JIS1KANJI:
		case F_JIS2KANJI:
		case F_TUIKAKIGOU:
			sta.cost += 4;				// ESC 24 29 [F}
			sta.bankG1 = fcode;
			break;
		default:
			break;
	}

	while( (seq->fcode[sta.count] == sta.bankG0) || (seq->fcode[sta.count] == sta.bankG1) )
	{
		if( (seq->fcode[sta.count] == sta.bankG0) && (sta.regionGL != BANK_G0) ) {
			sta.cost++;						// LS0
			sta.regionGL = BANK_G0;
		}
		if( (seq->fcode[sta.count] == sta.bankG1) && (sta.regionGL != BANK_G1) ) {
			sta.cost++;						// LS1
			sta.regionGL = BANK_G1;
		}
		if(sta.count == seq->count) {
			if( (seq->mincost == 0) || (seq->mincost > sta.cost) ) {
				seq->mincost = sta.cost;
			}
			return;
		}
		sta.count++;
	}

	bankG0cost(seq, sta, seq->fcode[sta.count]);
	bankG1cost(seq, sta, seq->fcode[sta.count]);

	return;
}

