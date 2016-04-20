// IB25Decoder.h: IB25Decoder クラスのインターフェイス
//
//////////////////////////////////////////////////////////////////////


#pragma once


/////////////////////////////////////////////////////////////////////////////
// 定数定義
/////////////////////////////////////////////////////////////////////////////

#define TS_INVALID_PID	0xFFFFU		// 無効PID


/////////////////////////////////////////////////////////////////////////////
// B25デコーダインタフェース
/////////////////////////////////////////////////////////////////////////////

class IB25Decoder
{
public:
	virtual const BOOL Initialize(DWORD dwRound = 4) = 0;
	virtual void Release(void) = 0;

	virtual const BOOL Decode(BYTE *pSrcBuf, const DWORD dwSrcSize, BYTE **ppDstBuf, DWORD *pdwDstSize) = 0;
	virtual const BOOL Flush(BYTE **ppDstBuf, DWORD *pdwDstSize) = 0;
	virtual const BOOL Reset(void) = 0;
};


/////////////////////////////////////////////////////////////////////////////
// B25デコーダインタフェース2
/////////////////////////////////////////////////////////////////////////////

class IB25Decoder2	: public IB25Decoder
{
public:
	enum	// GetDescramblerState() リターンコード
	{
		DS_NO_ERROR			= 0x00000000UL,		// エラーなし正常
		DS_BCAS_ERROR		= 0x00000001UL,		// B-CASカードエラー
		DS_NOT_CONTRACTED	= 0x00000002UL		// 視聴未契約
	};

	virtual void DiscardNullPacket(const bool bEnable = true) = 0;
	virtual void DiscardScramblePacket(const bool bEnable = true) = 0;
	virtual void EnableEmmProcess(const bool bEnable = true) = 0;

	virtual const DWORD GetDescramblingState(const WORD wProgramID) = 0;

	virtual void ResetStatistics(void) = 0;

	virtual const DWORD GetPacketStride(void) = 0;
	virtual const DWORD GetInputPacketNum(const WORD wPID = TS_INVALID_PID) = 0;
	virtual const DWORD GetOutputPacketNum(const WORD wPID = TS_INVALID_PID) = 0;
	virtual const DWORD GetSyncErrNum(void) = 0;
	virtual const DWORD GetFormatErrNum(void) = 0;
	virtual const DWORD GetTransportErrNum(void) = 0;
	virtual const DWORD GetContinuityErrNum(const WORD wPID = TS_INVALID_PID) = 0;
	virtual const DWORD GetScramblePacketNum(const WORD wPID = TS_INVALID_PID) = 0;
	virtual const DWORD GetEcmProcessNum(void) = 0;
	virtual const DWORD GetEmmProcessNum(void) = 0;
};


/////////////////////////////////////////////////////////////////////////////
// For Delphi and General Non C++ Laguages
// From MobileHackerz (http://mobilehackerz.jp/contents/Software/cap_hdus)
/////////////////////////////////////////////////////////////////////////////

#ifdef B25SDK_IMPLEMENT
	#define B25DECAPI	__declspec(dllexport)
#else
	#define B25DECAPI	__declspec(dllimport)
#endif


extern "C"
{
	// クラスファクトリー
	B25DECAPI IB25Decoder * CreateB25Decoder(void);

	// IB25Decoder
	B25DECAPI const BOOL B25Decoder_Initialize(IB25Decoder *pB25, DWORD dwRound);
	B25DECAPI void B25Decoder_Release(IB25Decoder *pB25);
	B25DECAPI const BOOL B25Decoder_Decode(IB25Decoder *pB25, BYTE *pSrcBuf, const DWORD dwSrcSize, BYTE **ppDstBuf, DWORD *pdwDstSize);
	B25DECAPI const BOOL B25Decoder_Flush(IB25Decoder *pB25, BYTE **ppDstBuf, DWORD *pdwDstSize);
	B25DECAPI const BOOL B25Decoder_Reset(IB25Decoder *pB25);
}

extern "C"
{
	// クラスファクトリー
	B25DECAPI IB25Decoder2 * CreateB25Decoder2(void);

	// IB25Decoder
	B25DECAPI const BOOL B25Decoder2_Initialize(IB25Decoder2 *pB25, DWORD dwRound);
	B25DECAPI void B25Decoder2_Release(IB25Decoder2 *pB25);
	B25DECAPI const BOOL B25Decoder2_Decode(IB25Decoder2 *pB25, BYTE *pSrcBuf, const DWORD dwSrcSize, BYTE **ppDstBuf, DWORD *pdwDstSize);
	B25DECAPI const BOOL B25Decoder2_Flush(IB25Decoder2 *pB25, BYTE **ppDstBuf, DWORD *pdwDstSize);
	B25DECAPI const BOOL B25Decoder2_Reset(IB25Decoder2 *pB25);

	// IB25Decoder2
	B25DECAPI void B25Decoder2_DiscardNullPacket(IB25Decoder2 *pB25, const bool bEnable);
	B25DECAPI void B25Decoder2_DiscardScramblePacket(IB25Decoder2 *pB25, const bool bEnable);
	B25DECAPI void B25Decoder2_EnableEmmProcess(IB25Decoder2 *pB25, const bool bEnable);
	B25DECAPI const DWORD B25Decoder2_GetDescramblingState(IB25Decoder2 *pB25, const WORD wProgramID);
	B25DECAPI void B25Decoder2_ResetStatistics(IB25Decoder2 *pB25);
	B25DECAPI const DWORD B25Decoder2_GetPacketStride(IB25Decoder2 *pB25);
	B25DECAPI const DWORD B25Decoder2_GetInputPacketNum(IB25Decoder2 *pB25, const WORD wPID);
	B25DECAPI const DWORD B25Decoder2_GetOutputPacketNum(IB25Decoder2 *pB25, const WORD wPID);
	B25DECAPI const DWORD B25Decoder2_GetSyncErrNum(IB25Decoder2 *pB25);
	B25DECAPI const DWORD B25Decoder2_GetFormatErrNum(IB25Decoder2 *pB25);
	B25DECAPI const DWORD B25Decoder2_GetTransportErrNum(IB25Decoder2 *pB25);
	B25DECAPI const DWORD B25Decoder2_GetContinuityErrNum(IB25Decoder2 *pB25, const WORD wPID);
	B25DECAPI const DWORD B25Decoder2_GetScramblePacketNum(IB25Decoder2 *pB25, const WORD wPID);
	B25DECAPI const DWORD B25Decoder2_GetEcmProcessNum(IB25Decoder2 *pB25);
	B25DECAPI const DWORD B25Decoder2_GetEmmProcessNum(IB25Decoder2 *pB25);
}
