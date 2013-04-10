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
