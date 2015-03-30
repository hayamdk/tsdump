#pragma once


// 定数

#define		REGION_GL		0
#define		REGION_GR		1

#define		BANK_G0			0
#define		BANK_G1			1
#define		BANK_G2			2
#define		BANK_G3			3

#define		F_KANJI			0x42		// Gセットの終端符号
#define		F_ALPHA			0x4a
#define		F_HIRAGANA		0x30
#define		F_KATAKANA		0x31
#define		F_MOSAICA		0x32
#define		F_MOSAICB		0x33
#define		F_MOSAICC		0x34
#define		F_MOSAICD		0x35
#define		F_P_ALPHA		0x36
#define		F_P_HIRAGANA	0x37
#define		F_P_KATAKANA	0x38
#define		F_HANKAKU		0x49
#define		F_JIS1KANJI		0x39
#define		F_JIS2KANJI		0x3a
#define		F_TUIKAKIGOU	0x3b

#define		F_DRCS0			0x40		// DRCSの終端符号
#define		F_DRCS1			0x41
#define		F_DRCS2			0x42
#define		F_DRCS3			0x43
#define		F_DRCS4			0x44
#define		F_DRCS5			0x45
#define		F_DRCS6			0x46
#define		F_DRCS7			0x47
#define		F_DRCS8			0x48
#define		F_DRCS9			0x49
#define		F_DRCS10		0x4a
#define		F_DRCS11		0x4b
#define		F_DRCS12		0x4c
#define		F_DRCS13		0x4d
#define		F_DRCS14		0x4e
#define		F_DRCS15		0x4f
#define		F_MACRO			0x70	

#define		F_DRCS1A		0x51		// Gセットの終端符号と被らないように誤魔化した
#define		F_DRCS2A		0x52		// 1バイトDRCSの偽終端符号
#define		F_DRCS3A		0x53
#define		F_DRCS4A		0x54
#define		F_DRCS5A		0x55
#define		F_DRCS6A		0x56
#define		F_DRCS7A		0x57
#define		F_DRCS8A		0x58
#define		F_DRCS9A		0x59
#define		F_DRCS10A		0x5a
#define		F_DRCS11A		0x5b
#define		F_DRCS12A		0x5c
#define		F_DRCS13A		0x5d
#define		F_DRCS14A		0x5e
#define		F_DRCS15A		0x5f
#define		F_MACROA		0x80

#define		F_UNKNOWN		0x00		// 内部処理用偽終端符号
#define		F_CONTROL		0x01
#define		F_KANACOMMON	0x02

#define		UTF16TABLELEN	16


// 構造体宣言

typedef struct {
	int		region[2];
	int		bank[4];
	BOOL	bSingleShift;
	int		region_GL_backup;
	BOOL	bXCS;
	BOOL	bNormalSize;
} ConvStatus;

typedef struct {
	int				count;
	int				num[256];
	int				mincost;
} KanaSequence;

typedef struct {
	int				count;
	int				fcode[256];
	int				mincost;
} CharSequence;

typedef struct {
	int		regionGL;
	int		bankG0;
	int		bankG1;
	int		count;
	int		cost;
} BankStatus;


	// プロトタイプ宣言

int			conv_to_unicode(WCHAR*, const int, const unsigned char*, const int, const BOOL);
int			conv_from_unicode(unsigned char*, const int, const WCHAR*, const int, const BOOL);

int			conv_from_unicode1(unsigned char*, const int, const WCHAR*, const int, const BOOL, const BOOL);
int			conv_from_unicode2(unsigned char*, const int, const WCHAR*, const int, const BOOL, const BOOL);
int			conv_from_unicode3(unsigned char*, const int, const WCHAR*, const int, const BOOL, const BOOL);

int			jis12conv(const int, const BOOL);
int			jis12winconv(const int, const BOOL);
int			jis3conv(const int, const BOOL);
int			jis3combconv(const int, WCHAR*, const int);
int			jis3combrevconv(const WCHAR*, int*);
int			jis4conv(const int, const BOOL);
int			hiragana1conv(const int, const BOOL);
int			katakana1conv(const int, const BOOL);
int			kanacommon1conv(const int, const BOOL);
int			hankaku1conv(const int, const BOOL);
int			tuikakigou1conv(const int, const BOOL);
int			tuikakigou2conv(const int, WCHAR*, const int);
int			tuikakigou2revconv(const WCHAR*, int*);
int			charsize1conv(const int, const BOOL);

void		initrevtable(const int*, int*, const int);
int			comparefortable(const void*, const void*);
int			comparefortable2(const void*, const void*);
int			comparefortable2str(const void*, const void*);

void		initConvStatus(ConvStatus*);
BOOL		isControlChar(const unsigned char);
BOOL		isOneByteGSET(const unsigned char);
BOOL		isTwoByteGSET(const unsigned char);
BOOL		isOneByteDRCS(const unsigned char);
BOOL		isTwoByteDRCS(const unsigned char);
int			numgbank(const unsigned char);

int			wcharbuf(WCHAR*, const int, const int, const int, const BOOL);
void		charbuf(unsigned char*, const int, const int, const int);
void		changeConvStatus(const unsigned char*, int*, ConvStatus*);
void		csiProc(const unsigned char*, int*, ConvStatus*);
void		defaultMacroProc(const unsigned char, ConvStatus*);

int			charclass(const WCHAR*, int*, int*);

BOOL		kanaChange(const WCHAR*, const int, const int, const int);
void		kanacostc(KanaSequence*, const int, const int);
void		kanacostn(KanaSequence*, const int, const int);
void		kanacostp(KanaSequence*, const int, const int);
BOOL		kigouChange(const WCHAR*, const int, const int);

BOOL		bankChange(const WCHAR*, const int, const int, const ConvStatus*, const int);
void		bankG0cost(CharSequence*, BankStatus, const int );
void		bankG1cost(CharSequence*, BankStatus, const int );
