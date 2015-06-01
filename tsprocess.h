#pragma once

#include <stdio.h>
#include <windows.h>


// 定数など

#define			READBUFSIZE				65536
#define			READBUFMERGIN			1024


// ディスク入出力用

typedef struct {
	HANDLE			hFile;
	int				psize;
	int				poffset;
	unsigned char	databuf[READBUFMERGIN + READBUFSIZE];
	unsigned char	*buf;
	int				datasize;
	int				pos;
	BOOL			bShowError;
} TsReadProcess;


// プロトタイプ宣言

int				getPid(unsigned char*);
int				getPidValue(unsigned char*);
BOOL			isPsiTop(unsigned char*);
BOOL			isScrambled(unsigned char*);
int				getAdapFieldLength(unsigned char*);
int				getPointerFieldLength(unsigned char*);
int				getSectionLength(unsigned char*);
int				getLength(unsigned char*);
int				getPsiLength(unsigned char*);

int				parsePat(unsigned char*);
void			parsePmt(unsigned char*, int*, int*, int*, int*, BOOL, BOOL);
unsigned int	calc_crc32(unsigned char*, int);
BOOL			isPcrData(unsigned char*);
int64_t			getPcrValue(unsigned char*);

void			initTsFileRead(TsReadProcess*, HANDLE, int);
void			setPointerTsFileRead(TsReadProcess*, int64_t);
void			setPosTsFileRead(TsReadProcess*, int);
void			showErrorTsFileRead(TsReadProcess*, BOOL);
unsigned char*	getPacketTsFileRead(TsReadProcess*);


