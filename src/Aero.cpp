#include <Windows.h>
#if _WIN32_WINNT<0x0600 || _WINVER<0x0600
#undef _WIN32_WINNT
#undef _WINVER
#define _WIN32_WINNT	0x0600
#define _WINVER			0x0600
#endif
#include <uxtheme.h>
#include "Aero.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif


template<typename T> bool ProcAddress(T &func,HMODULE lib,const char *symbol)
{
	return (func=reinterpret_cast<T>(::GetProcAddress(lib,symbol)))!=NULL;
}


typedef HRESULT (WINAPI *BufferedPaintInitFunc)();
typedef HRESULT (WINAPI *BufferedPaintUnInitFunc)();
typedef HPAINTBUFFER (WINAPI *BeginBufferedPaintFunc)(HDC hdcTarget,const RECT *prcTarget,BP_BUFFERFORMAT dwFormat,BP_PAINTPARAMS *pPaintParams,HDC *phdc);
typedef HRESULT (WINAPI *EndBufferedPaintFunc)(HPAINTBUFFER hBufferedPaint,BOOL fUpdateTarget);
typedef HRESULT (WINAPI *BufferedPaintClearFunc)(HPAINTBUFFER hBufferedPaint,const RECT *prc);
typedef HRESULT (WINAPI *BufferedPaintSetAlphaFunc)(HPAINTBUFFER hBufferedPaint,const RECT *prc,BYTE alpha);


static HMODULE g_hThemeLib=NULL;

class CBufferedPaintInitializer
{
public:
	CBufferedPaintInitializer()
	{
		g_hThemeLib=::LoadLibrary(TEXT("uxtheme.dll"));
		if (g_hThemeLib!=NULL) {
			BufferedPaintInitFunc pBufferedPaintInit;
			if (!ProcAddress(pBufferedPaintInit,g_hThemeLib,"BufferedPaintInit")
					|| pBufferedPaintInit()!=S_OK) {
				//TRACE(TEXT("BufferedPaintInit() Failed\n"));
				::FreeLibrary(g_hThemeLib);
				g_hThemeLib=NULL;
			}
		}
	}

	~CBufferedPaintInitializer()
	{
		if (g_hThemeLib!=NULL) {
			BufferedPaintUnInitFunc pBufferedPaintUnInit;
			if (ProcAddress(pBufferedPaintUnInit,g_hThemeLib,"BufferedPaintUnInit"))
				pBufferedPaintUnInit();
			::FreeLibrary(g_hThemeLib);
		}
	}
};

static CBufferedPaintInitializer BufferedPaintInitializer;


CBufferedPaint::CBufferedPaint()
	: m_hPaintBuffer(NULL)
{
}


CBufferedPaint::~CBufferedPaint()
{
	End(false);
}


HDC CBufferedPaint::Begin(HDC hdc,const RECT *pRect,bool fErase)
{
	if (g_hThemeLib==NULL)
		return NULL;

	if (m_hPaintBuffer!=NULL) {
		if (!End(false))
			return NULL;
	}

	BeginBufferedPaintFunc pBeginBufferedPaint;
	if (!ProcAddress(pBeginBufferedPaint,g_hThemeLib,"BeginBufferedPaint"))
		return NULL;

	BP_PAINTPARAMS Params={sizeof(BP_PAINTPARAMS),0,NULL,NULL};
	if (fErase)
		Params.dwFlags|=BPPF_ERASE;
	HDC hdcBuffer;
	m_hPaintBuffer=pBeginBufferedPaint(hdc,pRect,BPBF_TOPDOWNDIB,&Params,&hdcBuffer);
	if (m_hPaintBuffer==NULL)
		return NULL;
	return hdcBuffer;
}


bool CBufferedPaint::End(bool fUpdate)
{
	if (m_hPaintBuffer!=NULL) {
		EndBufferedPaintFunc pEndBufferedPaint;
		if (!ProcAddress(pEndBufferedPaint,g_hThemeLib,"EndBufferedPaint"))
			return false;
		pEndBufferedPaint(m_hPaintBuffer,fUpdate);
		m_hPaintBuffer=NULL;
	}
	return true;
}


bool CBufferedPaint::Clear(const RECT *pRect)
{
	if (m_hPaintBuffer==NULL)
		return false;
	BufferedPaintClearFunc pBufferedPaintClear;
	if (!ProcAddress(pBufferedPaintClear,g_hThemeLib,"BufferedPaintClear"))
		return false;
	return pBufferedPaintClear(m_hPaintBuffer,pRect)==S_OK;
}


bool CBufferedPaint::SetAlpha(BYTE Alpha)
{
	if (m_hPaintBuffer==NULL)
		return false;
	BufferedPaintSetAlphaFunc pBufferedPaintSetAlpha;
	if (!ProcAddress(pBufferedPaintSetAlpha,g_hThemeLib,"BufferedPaintSetAlpha"))
		return false;
	return pBufferedPaintSetAlpha(m_hPaintBuffer,NULL,Alpha)==S_OK;
}


bool CBufferedPaint::IsSupported()
{
	return g_hThemeLib!=NULL;
}
