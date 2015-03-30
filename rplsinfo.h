#pragma once


// íËêî

#define		ADR_APPINFO				0x28
#define		ADR_TIMEZONE			0x08
#define		ADR_RECYEAR				0x0a
#define		ADR_RECMONTH			0x0c
#define		ADR_RECDAY				0x0d
#define		ADR_RECHOUR				0x0e
#define		ADR_RECMIN				0x0f
#define		ADR_RECSEC				0x10
#define		ADR_DURHOUR				0x11
#define		ADR_DURMIN				0x12
#define		ADR_DURSEC				0x13
#define		ADR_MAKERID				0x14
#define		ADR_MODELCODE			0x16
#define		ADR_CHANNELNUM			0x18
#define		ADR_CHANNELNAME			0x1b
#define		ADR_PNAME				0x30
#define		ADR_PDETAIL				0x130

#define		ADR_MPDATA				0x10
#define		ADR_MPMAKERID			0x0c
#define		ADR_MPMODELCODE			0x0e
#define		ADR_GENRE				0x1c
#define		ADR_PEXTENDLEN			0x38
#define		ADR_PCHECKSUM			0x3a

#define		ADR_RECMODE_PANA		0x98
#define		ADR_RECSRC_PANA			0xA8
#define		ADR_GENRE_PANA			0xB0
#define		ADR_MAXBITRATE_PANA		0xBC

#define		MAKERID_PANASONIC		0x0103
#define		MAKERID_SONY			0x0108

#define		F_FileName				1
#define		F_FileNameFullPath		2
#define		F_RecDate				3
#define		F_RecTime				4
#define		F_RecDuration			5
#define		F_RecTimeZone			6
#define		F_MakerID				7
#define		F_ModelCode				8
#define		F_RecSrc				9
#define		F_ChannelName			10
#define		F_ChannelNum			11
#define		F_ProgName				12
#define		F_ProgDetail			13
#define		F_ProgExtend			14
#define		F_ProgGenre				15

#define		S_NORMAL				0
#define		S_TAB					1
#define		S_SPACE					2
#define		S_CSV					3
#define		S_NEWLINE				4
#define		S_ITEMNAME				5

int get_proginfo( ProgInfo*, BYTE*, int );