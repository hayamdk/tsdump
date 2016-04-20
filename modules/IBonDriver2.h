class IBonDriver
{
public:
	virtual const BOOL OpenTuner(void) = 0;
	virtual void CloseTuner(void) = 0;

	virtual const BOOL SetChannel(const BYTE bCh) = 0;
	virtual const float GetSignalLevel(void) = 0;

	virtual const DWORD WaitTsStream(const DWORD dwTimeOut = 0) = 0;
	virtual const DWORD GetReadyCount(void) = 0;

	virtual const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) = 0;
	virtual const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) = 0;

	virtual void PurgeTsStream(void) = 0;

	virtual void Release(void) = 0;
};


// インスタンス生成メソッド
extern "C" __declspec(dllimport) IBonDriver * CreateBonDriver();

class IBonDriver2 : public IBonDriver
{
public:
	virtual LPCTSTR GetTunerName(void) = 0;

	virtual const BOOL IsTunerOpening(void) = 0;
	
	virtual LPCTSTR EnumTuningSpace(const DWORD dwSpace) = 0;
	virtual LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel) = 0;

	virtual const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel) = 0;
	
	virtual const DWORD GetCurSpace(void) = 0;
	virtual const DWORD GetCurChannel(void) = 0;
	
// IBonDriver
	virtual void Release(void) = 0;
};

typedef IBonDriver* (pCreateBonDriver_t)(void);