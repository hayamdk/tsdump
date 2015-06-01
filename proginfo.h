#pragma once

// 定数

#define		MEDIATYPE_TB		0x5442
#define		MEDIATYPE_BS		0x4253
#define		MEDIATYPE_CS		0x4353
#define		MEDIATYPE_UNKNOWN	0xFFFF

#define		PID_PAT				0x0000
#define		PID_SIT				0x001f
#define		PID_EIT				0x0012
#define		PID_SDT				0x0011
#define		PID_INVALID			0xffff

#define		PSIBUFSIZE			65536


// プロトタイプ宣言

BOOL			readTsProgInfo( BYTE*, int, ProgInfo*, int, BOOL );
int				getSitEit( BYTE*, int, unsigned char*, int );
BOOL			getSdt( BYTE*, int, unsigned char*, int, int );
void			mjd_dec(int, int*, int*, int*);
int				comparefornidtable(const void*, const void*);
int				getTbChannelNum(int, int, int);
void			parseSit(unsigned char*, ProgInfo*, BOOL);
int				parseEit(unsigned char*, ProgInfo*, BOOL);
void			parseSdt(unsigned char*, ProgInfo*, int, BOOL);
