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

// IBonDriver->C互換構造体
struct STRUCT_IBONDRIVER
{
	void *pCtx;
	const void *pEnd;
	BOOL (*pF00)(void *);
	void (*pF01)(void *);
	BOOL (*pF02)(void *, BYTE);
	float (*pF03)(void *);
	DWORD (*pF04)(void *, DWORD);
	DWORD (*pF05)(void *);
	BOOL (*pF06)(void *, BYTE *, DWORD *, DWORD *);
	BOOL (*pF07)(void *, BYTE **, DWORD *, DWORD *);
	void (*pF08)(void *);
	void (*pF09)(void *);
	STRUCT_IBONDRIVER &Initialize(IBonDriver *pBon, const void *pEnd_) {
		pCtx = pBon;
		pEnd = pEnd_ ? pEnd_ : this + 1;
		pF00 = F00;
		pF01 = F01;
		pF02 = F02;
		pF03 = F03;
		pF04 = F04;
		pF05 = F05;
		pF06 = F06;
		pF07 = F07;
		pF08 = F08;
		pF09 = F09;
		return *this;
	}
	static BOOL F00(void *p) { return static_cast<IBonDriver *>(p)->OpenTuner(); }
	static void F01(void *p) { static_cast<IBonDriver *>(p)->CloseTuner(); }
	static BOOL F02(void *p, BYTE a0) { return static_cast<IBonDriver *>(p)->SetChannel(a0); }
	static float F03(void *p) { return static_cast<IBonDriver *>(p)->GetSignalLevel(); }
	static DWORD F04(void *p, DWORD a0) { return static_cast<IBonDriver *>(p)->WaitTsStream(a0); }
	static DWORD F05(void *p) { return static_cast<IBonDriver *>(p)->GetReadyCount(); }
	static BOOL F06(void *p, BYTE *a0, DWORD *a1, DWORD *a2) { return static_cast<IBonDriver *>(p)->GetTsStream(a0, a1, a2); }
	static BOOL F07(void *p, BYTE **a0, DWORD *a1, DWORD *a2) { return static_cast<IBonDriver *>(p)->GetTsStream(a0, a1, a2); }
	static void F08(void *p) { static_cast<IBonDriver *>(p)->PurgeTsStream(); }
	static void F09(void *p) { static_cast<IBonDriver *>(p)->Release(); }
};

// インスタンス生成メソッド
//extern "C" BONAPI IBonDriver * CreateBonDriver(void);
//extern "C" BONAPI const STRUCT_IBONDRIVER * CreateBonStruct(void);
