// IBonDriver2.h: IBonDriver2 クラスのインターフェイス
//
/////////////////////////////////////////////////////////////////////////////

#pragma once


#include "IBonDriver.h"


/////////////////////////////////////////////////////////////////////////////
// Bonドライバインタフェース2
/////////////////////////////////////////////////////////////////////////////

class IBonDriver2 : public IBonDriver
{
public:
// IBonDriver2
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

// IBonDriver2->C互換構造体
struct STRUCT_IBONDRIVER2
{
	STRUCT_IBONDRIVER st;
	LPCTSTR (*pF10)(void *);
	BOOL (*pF11)(void *);
	LPCTSTR (*pF12)(void *, DWORD);
	LPCTSTR (*pF13)(void *, DWORD, DWORD);
	BOOL (*pF14)(void *, DWORD, DWORD);
	DWORD (*pF15)(void *);
	DWORD (*pF16)(void *);
	STRUCT_IBONDRIVER &Initialize(IBonDriver2 *pBon2, const void *pEnd) {
		pF10 = F10;
		pF11 = F11;
		pF12 = F12;
		pF13 = F13;
		pF14 = F14;
		pF15 = F15;
		pF16 = F16;
		return st.Initialize(pBon2, pEnd ? pEnd : this + 1);
	}
	static LPCTSTR F10(void *p) { return static_cast<IBonDriver2 *>(static_cast<IBonDriver *>(p))->GetTunerName(); }
	static BOOL F11(void *p) { return static_cast<IBonDriver2 *>(static_cast<IBonDriver *>(p))->IsTunerOpening(); }
	static LPCTSTR F12(void *p, DWORD a0) { return static_cast<IBonDriver2 *>(static_cast<IBonDriver *>(p))->EnumTuningSpace(a0); }
	static LPCTSTR F13(void *p, DWORD a0, DWORD a1) { return static_cast<IBonDriver2 *>(static_cast<IBonDriver *>(p))->EnumChannelName(a0, a1); }
	static BOOL F14(void *p, DWORD a0, DWORD a1) { return static_cast<IBonDriver2 *>(static_cast<IBonDriver *>(p))->SetChannel(a0, a1); }
	static DWORD F15(void *p) { return static_cast<IBonDriver2 *>(static_cast<IBonDriver *>(p))->GetCurSpace(); }
	static DWORD F16(void *p) { return static_cast<IBonDriver2 *>(static_cast<IBonDriver *>(p))->GetCurChannel(); }
};
