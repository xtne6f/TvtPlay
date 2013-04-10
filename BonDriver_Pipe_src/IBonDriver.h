// IBonDriver.h: IBonDriver クラスのインターフェイス
//
/////////////////////////////////////////////////////////////////////////////

#pragma once


#ifdef BONSDK_IMPLEMENT
	#define BONAPI	__declspec(dllexport)
#else
	#define BONAPI	__declspec(dllimport)
#endif


/////////////////////////////////////////////////////////////////////////////
// Bonドライバインタフェース
/////////////////////////////////////////////////////////////////////////////

class IBonDriver
{
public:
// IBonDriver
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
extern "C" BONAPI IBonDriver * CreateBonDriver(void);
