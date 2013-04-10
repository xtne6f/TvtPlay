// BonTuner.h: CBonTuner クラスのインターフェイス
//
//////////////////////////////////////////////////////////////////////

#if !defined(_BONTUNER_H_)
#define _BONTUNER_H_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


#define dllimport dllexport
#include "IBonDriver2.h"


class CBonTuner : public IBonDriver2
{
public:
	CBonTuner();
	virtual ~CBonTuner();

// IBonDriver
	const BOOL OpenTuner(void);
	void CloseTuner(void);

	const BOOL SetChannel(const BYTE bCh);
	const float GetSignalLevel(void);

	const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
	const DWORD GetReadyCount(void);

	const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
	const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);

	void PurgeTsStream(void);

// IBonDriver2(暫定)
	LPCTSTR GetTunerName(void);

	const BOOL IsTunerOpening(void);

	LPCTSTR EnumTuningSpace(const DWORD dwSpace);
	LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);

	const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);

	const DWORD GetCurSpace(void);
	const DWORD GetCurChannel(void);

	void Release(void);

	static CBonTuner * m_pThis;
	static HINSTANCE m_hModule;

protected:
	// I/Oリクエストキューデータ
	struct AsyncIoReq
	{
		OVERLAPPED OverLapped;
		DWORD dwState;
		DWORD dwRxdSize;
		BYTE RxdBuff[48128];
		AsyncIoReq *pNext;
	};

	AsyncIoReq * AllocIoReqBuff(const DWORD dwBuffNum);
	void FreeIoReqBuff(AsyncIoReq *pBuff);

	static DWORD WINAPI PushIoThread(LPVOID pParam);
	static DWORD WINAPI PopIoThread(LPVOID pParam);

	const BOOL PushIoRequest();
	const BOOL PopIoRequest();
	

	HANDLE m_hMutex;

	AsyncIoReq *m_pIoReqBuff;
	AsyncIoReq *m_pIoPushReq;
	AsyncIoReq *m_pIoPopReq;
	AsyncIoReq *m_pIoGetReq;

	DWORD m_dwBusyReqNum;
	DWORD m_dwReadyReqNum;

	HANDLE m_hPushIoThread;
	HANDLE m_hPopIoThread;
	BOOL m_bLoopIoThread;

	HANDLE m_hOnStreamEvent;

	CRITICAL_SECTION m_CriticalSection;

	DWORD m_dwCurSpace;
	DWORD m_dwCurChannel;

	HANDLE m_hPipe;
};

#endif // !defined(_BONTUNER_H_)
