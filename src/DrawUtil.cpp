#include <Windows.h>
#include <CommCtrl.h>
#include "DrawUtil.h"
#include "Util.h"

// このマクロを使うとGDI+のヘッダでエラーが出る
/*
#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif
*/


namespace DrawUtil {


// 単色で塗りつぶす
bool Fill(HDC hdc,const RECT *pRect,COLORREF Color)
{
	HBRUSH hbr=::CreateSolidBrush(Color);

	if (hbr==NULL)
		return false;
	::FillRect(hdc,pRect,hbr);
	::DeleteObject(hbr);
	return true;
}


// グラデーションで塗りつぶす
bool FillGradient(HDC hdc,const RECT *pRect,COLORREF Color1,COLORREF Color2,
				  FillDirection Direction)
{
	if (hdc==NULL || pRect==NULL
			|| pRect->left>pRect->right || pRect->top>pRect->bottom)
		return false;

	if (Direction==DIRECTION_HORZMIRROR || Direction==DIRECTION_VERTMIRROR) {
		RECT rc;

		rc=*pRect;
		if (Direction==DIRECTION_HORZMIRROR) {
			rc.right=(pRect->left+pRect->right)/2;
			if (rc.right>rc.left) {
				FillGradient(hdc,&rc,Color1,Color2,DIRECTION_HORZ);
				rc.left=rc.right;
			}
			rc.right=pRect->right;
			FillGradient(hdc,&rc,Color2,Color1,DIRECTION_HORZ);
		} else {
			rc.bottom=(pRect->top+pRect->bottom)/2;
			if (rc.bottom>rc.top) {
				FillGradient(hdc,&rc,Color1,Color2,DIRECTION_VERT);
				rc.top=rc.bottom;
			}
			rc.bottom=pRect->bottom;
			FillGradient(hdc,&rc,Color2,Color1,DIRECTION_VERT);
		}
		return true;
	}

	TRIVERTEX vert[2];
	GRADIENT_RECT rect={0,1};

	vert[0].x=pRect->left;
	vert[0].y=pRect->top;
	vert[0].Red=GetRValue(Color1)<<8;
	vert[0].Green=GetGValue(Color1)<<8;
	vert[0].Blue=GetBValue(Color1)<<8;
	vert[0].Alpha=0x0000;
	vert[1].x=pRect->right;
	vert[1].y=pRect->bottom;
	vert[1].Red=GetRValue(Color2)<<8;
	vert[1].Green=GetGValue(Color2)<<8;
	vert[1].Blue=GetBValue(Color2)<<8;
	vert[1].Alpha=0x0000;
	return ::GdiGradientFill(hdc,vert,2,&rect,1,
		Direction==DIRECTION_HORZ?GRADIENT_FILL_RECT_H:GRADIENT_FILL_RECT_V)!=FALSE;
}


bool FillGradient(HDC hdc,const RECT *pRect,const RGBA &Color1,const RGBA &Color2,
				  FillDirection Direction)
{
	if (hdc==NULL || pRect==NULL
			|| pRect->left>pRect->right || pRect->top>pRect->bottom)
		return false;

	if (Direction==DIRECTION_HORZMIRROR || Direction==DIRECTION_VERTMIRROR) {
		RECT rc;

		rc=*pRect;
		if (Direction==DIRECTION_HORZMIRROR) {
			rc.right=(pRect->left+pRect->right)/2;
			if (rc.right>rc.left) {
				FillGradient(hdc,&rc,Color1,Color2,DIRECTION_HORZ);
				rc.left=rc.right;
			}
			rc.right=pRect->right;
			FillGradient(hdc,&rc,Color2,Color1,DIRECTION_HORZ);
		} else {
			rc.bottom=(pRect->top+pRect->bottom)/2;
			if (rc.bottom>rc.top) {
				FillGradient(hdc,&rc,Color1,Color2,DIRECTION_VERT);
				rc.top=rc.bottom;
			}
			rc.bottom=pRect->bottom;
			FillGradient(hdc,&rc,Color2,Color1,DIRECTION_VERT);
		}
		return true;
	}

	const int Width=pRect->right-pRect->left;
	const int Height=pRect->bottom-pRect->top;
	HBITMAP hbm=::CreateCompatibleBitmap(hdc,Width,Height);
	if (hbm==NULL)
		return false;
	HDC hdcMem=::CreateCompatibleDC(hdc);
	HGDIOBJ hOldBmp=::SelectObject(hdcMem,hbm);

	RECT rc={0,0,Width,Height};
	FillGradient(hdcMem,&rc,Color1.GetCOLORREF(),Color2.GetCOLORREF(),Direction);

	BLENDFUNCTION BlendFunc={AC_SRC_OVER,0,0,0};
	if (Direction==DIRECTION_HORZ) {
		for (int x=0;x<Width;x++) {
			BlendFunc.SourceConstantAlpha=
				(BYTE)(((Width-1-x)*Color1.Alpha+x*Color2.Alpha)/(Width-1));
			if (BlendFunc.SourceConstantAlpha!=0) {
				::GdiAlphaBlend(hdc,x+pRect->left,pRect->top,1,Height,
								hdcMem,x,0,1,Height,
								BlendFunc);
			}
		}
	} else {
		for (int y=0;y<Height;y++) {
			BlendFunc.SourceConstantAlpha=
				(BYTE)(((Height-1-y)*Color1.Alpha+y*Color2.Alpha)/(Height-1));
			if (BlendFunc.SourceConstantAlpha!=0) {
				::GdiAlphaBlend(hdc,pRect->left,y+pRect->top,Width,1,
								hdcMem,0,y,Width,1,
								BlendFunc);
			}
		}
	}

	::SelectObject(hdcMem,hOldBmp);
	::DeleteDC(hdcMem);
	::DeleteObject(hbm);

	return true;
}


// 光沢のあるグラデーションで塗りつぶす
bool FillGlossyGradient(HDC hdc,const RECT *pRect,
						COLORREF Color1,COLORREF Color2,
						FillDirection Direction,int GlossRatio1,int GlossRatio2)
{
	RECT rc;
	COLORREF crCenter,crEnd;
	FillDirection Dir;

	rc.left=pRect->left;
	rc.top=pRect->top;
	if (Direction==DIRECTION_HORZ || Direction==DIRECTION_HORZMIRROR) {
		rc.right=(rc.left+pRect->right)/2;
		rc.bottom=pRect->bottom;
		Dir=DIRECTION_HORZ;
	} else {
		rc.right=pRect->right;
		rc.bottom=(rc.top+pRect->bottom)/2;
		Dir=DIRECTION_VERT;
	}
	if (Direction==DIRECTION_HORZ || Direction==DIRECTION_VERT) {
		crCenter=MixColor(Color1,Color2,128);
		crEnd=Color2;
	} else {
		crCenter=Color2;
		crEnd=Color1;
	}
	DrawUtil::FillGradient(hdc,&rc,
						   MixColor(RGB(255,255,255),Color1,GlossRatio1),
						   MixColor(RGB(255,255,255),crCenter,GlossRatio2),
						   Dir);
	if (Direction==DIRECTION_HORZ || Direction==DIRECTION_HORZMIRROR) {
		rc.left=rc.right;
		rc.right=pRect->right;
	} else {
		rc.top=rc.bottom;
		rc.bottom=pRect->bottom;
	}
	DrawUtil::FillGradient(hdc,&rc,crCenter,crEnd,Dir);
	return true;
}


// 縞々のグラデーションで塗りつぶす
bool FillInterlacedGradient(HDC hdc,const RECT *pRect,
							COLORREF Color1,COLORREF Color2,FillDirection Direction,
							COLORREF LineColor,int LineOpacity)
{
	if (hdc==NULL || pRect==NULL)
		return false;

	int Width=pRect->right-pRect->left;
	int Height=pRect->bottom-pRect->top;
	if (Width<=0 || Height<=0)
		return false;
	if (Width==1 || Height==1)
		return Fill(hdc,pRect,MixColor(Color1,Color2));

	HPEN hpenOld=static_cast<HPEN>(::SelectObject(hdc,::GetStockObject(DC_PEN)));
	COLORREF OldPenColor=::GetDCPenColor(hdc);

	if (Direction==DIRECTION_HORZ || Direction==DIRECTION_HORZMIRROR) {
		int Center=pRect->left*2+Width-1;

		for (int x=pRect->left;x<pRect->right;x++) {
			COLORREF Color;

			Color=MixColor(Color1,Color2,
						   (BYTE)(Direction==DIRECTION_HORZ?
								  (pRect->right-1-x)*255/(Width-1):
								  abs(Center-x*2)*255/(Width-1)));
			if ((x-pRect->left)%2==1)
				Color=MixColor(LineColor,Color,LineOpacity);
			::SetDCPenColor(hdc,Color);
			::MoveToEx(hdc,x,pRect->top,NULL);
			::LineTo(hdc,x,pRect->bottom);
		}
	} else {
		int Center=pRect->top*2+Height-1;

		for (int y=pRect->top;y<pRect->bottom;y++) {
			COLORREF Color;

			Color=MixColor(Color1,Color2,
						   (BYTE)(Direction==DIRECTION_VERT?
								  (pRect->bottom-1-y)*255/(Height-1):
								  abs(Center-y*2)*255/(Height-1)));
			if ((y-pRect->top)%2==1)
				Color=MixColor(LineColor,Color,LineOpacity);
			::SetDCPenColor(hdc,Color);
			::MoveToEx(hdc,pRect->left,y,NULL);
			::LineTo(hdc,pRect->right,y);
		}
	}

	::SetDCPenColor(hdc,OldPenColor);
	::SelectObject(hdc,hpenOld);

	return true;
}


// 単色で画像を描画する
bool DrawMonoColorDIB(HDC hdcDst,int DstX,int DstY,
					  HDC hdcSrc,int SrcX,int SrcY,int Width,int Height,COLORREF Color)
{
	if (hdcDst==NULL || hdcSrc==NULL)
		return false;

	COLORREF TransColor=Color^0x00FFFFFF;
	RGBQUAD Palette[2];

	Palette[0].rgbBlue=GetBValue(Color);
	Palette[0].rgbGreen=GetGValue(Color);
	Palette[0].rgbRed=GetRValue(Color);
	Palette[0].rgbReserved=0;
	Palette[1].rgbBlue=GetBValue(TransColor);
	Palette[1].rgbGreen=GetGValue(TransColor);
	Palette[1].rgbRed=GetRValue(TransColor);
	Palette[1].rgbReserved=0;
	::SetDIBColorTable(hdcSrc,0,2,Palette);
	::GdiTransparentBlt(hdcDst,DstX,DstY,Width,Height,
						hdcSrc,SrcX,SrcY,Width,Height,TransColor);
	return true;
}


bool DrawMonoColorDIB(HDC hdcDst,int DstX,int DstY,
					  HBITMAP hbm,int SrcX,int SrcY,int Width,int Height,COLORREF Color)
{
	if (hdcDst==NULL || hbm==NULL)
		return false;

	HDC hdcMem=::CreateCompatibleDC(hdcDst);
	if (hdcMem==NULL)
		return false;

	HBITMAP hbmOld=static_cast<HBITMAP>(::SelectObject(hdcMem,hbm));
	DrawMonoColorDIB(hdcDst,DstX,DstY,
					 hdcMem,SrcX,SrcY,Width,Height,Color);
	::SelectObject(hdcMem,hbmOld);
	::DeleteDC(hdcMem);

	return true;
}


HBITMAP CreateDIB(int Width,int Height,int BitCount,void **ppBits)
{
	struct {
		BITMAPINFOHEADER bmiHeader;
		RGBQUAD bmiColors[256];
	} bmi;
	void *pBits;

	::ZeroMemory(&bmi,sizeof(bmi));
	bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth=Width;
	bmi.bmiHeader.biHeight=Height;
	bmi.bmiHeader.biPlanes=1;
	bmi.bmiHeader.biBitCount=BitCount;
	bmi.bmiHeader.biCompression=BI_RGB;
	HBITMAP hbm=::CreateDIBSection(NULL,(BITMAPINFO*)&bmi,DIB_RGB_COLORS,&pBits,NULL,0);
	if (hbm==NULL)
		return NULL;
	if (ppBits!=NULL)
		*ppBits=pBits;
	return hbm;
}


// システムフォントを取得する
bool GetSystemFont(FontType Type,LOGFONT *pLogFont)
{
	if (pLogFont==NULL)
		return false;
	if (Type==FONT_DEFAULT) {
		return ::GetObject(::GetStockObject(DEFAULT_GUI_FONT),sizeof(LOGFONT),pLogFont)==sizeof(LOGFONT);
	} else {
		NONCLIENTMETRICS ncm;
		LOGFONT *plf;
		ncm.cbSize=CCSIZEOF_STRUCT(NONCLIENTMETRICS,lfMessageFont);
		::SystemParametersInfo(SPI_GETNONCLIENTMETRICS,ncm.cbSize,&ncm,0);
		switch (Type) {
		case FONT_MESSAGE:		plf=&ncm.lfMessageFont;		break;
		case FONT_MENU:			plf=&ncm.lfMenuFont;		break;
		case FONT_CAPTION:		plf=&ncm.lfCaptionFont;		break;
		case FONT_SMALLCAPTION:	plf=&ncm.lfSmCaptionFont;	break;
		case FONT_STATUS:		plf=&ncm.lfStatusFont;		break;
		default:
			return false;
		}
		*pLogFont=*plf;
	}
	return true;
}


CFont::CFont()
	: m_hfont(NULL)
{
}

CFont::CFont(const CFont &Font)
	: m_hfont(NULL)
{
	*this=Font;
}

CFont::CFont(const LOGFONT &Font)
	: m_hfont(NULL)
{
	Create(&Font);
}

CFont::CFont(FontType Type)
	: m_hfont(NULL)
{
	Create(Type);
}

CFont::~CFont()
{
	Destroy();
}

CFont &CFont::operator=(const CFont &Font)
{
	if (Font.m_hfont) {
		LOGFONT lf;
		Font.GetLogFont(&lf);
		Create(&lf);
	} else {
		if (m_hfont)
			::DeleteObject(m_hfont);
		m_hfont=NULL;
	}
	return *this;
}

bool CFont::operator==(const CFont &Font) const
{
	if (m_hfont==NULL)
		return Font.m_hfont==NULL;
	if (Font.m_hfont==NULL)
		return m_hfont==NULL;
	LOGFONT lf1,lf2;
	GetLogFont(&lf1);
	Font.GetLogFont(&lf2);
	return CompareLogFont(&lf1,&lf2);
}

bool CFont::operator!=(const CFont &Font) const
{
	return !(*this==Font);
}

bool CFont::Create(const LOGFONT *pLogFont)
{
	if (pLogFont==NULL)
		return false;
	HFONT hfont=::CreateFontIndirect(pLogFont);
	if (hfont==NULL)
		return false;
	if (m_hfont)
		::DeleteObject(m_hfont);
	m_hfont=hfont;
	return true;
}

bool CFont::Create(FontType Type)
{
	LOGFONT lf;

	if (!GetSystemFont(Type,&lf))
		return false;
	return Create(&lf);
}

void CFont::Destroy()
{
	if (m_hfont) {
		::DeleteObject(m_hfont);
		m_hfont=NULL;
	}
}

bool CFont::GetLogFont(LOGFONT *pLogFont) const
{
	if (m_hfont==NULL || pLogFont==NULL)
		return false;
	return ::GetObject(m_hfont,sizeof(LOGFONT),pLogFont)==sizeof(LOGFONT);
}

int CFont::GetHeight(bool fCell) const
{
	if (m_hfont==NULL)
		return 0;

	HDC hdc=::CreateCompatibleDC(NULL);
	int Height;
	if (hdc==NULL) {
		LOGFONT lf;
		if (!GetLogFont(&lf))
			return 0;
		Height=abs(lf.lfHeight);
	} else {
		Height=GetHeight(hdc,fCell);
		::DeleteDC(hdc);
	}
	return Height;
}

int CFont::GetHeight(HDC hdc,bool fCell) const
{
	if (m_hfont==NULL || hdc==NULL)
		return 0;
	HGDIOBJ hOldFont=::SelectObject(hdc,m_hfont);
	TEXTMETRIC tm;
	::GetTextMetrics(hdc,&tm);
	::SelectObject(hdc,hOldFont);
	if (!fCell)
		tm.tmHeight-=tm.tmInternalLeading;
	return tm.tmHeight;
}


CBitmap::CBitmap()
	: m_hbm(NULL)
{
}

CBitmap::CBitmap(const CBitmap &Src)
	: m_hbm(NULL)
{
	*this=Src;
}

CBitmap::~CBitmap()
{
	Destroy();
}

CBitmap &CBitmap::operator=(const CBitmap &Src)
{
	if (&Src!=this) {
		Destroy();
		if (Src.m_hbm!=NULL)
			m_hbm=static_cast<HBITMAP>(::CopyImage(Src.m_hbm,IMAGE_BITMAP,0,0,
												   Src.IsDIB()?LR_CREATEDIBSECTION:0));
	}
	return *this;
}

bool CBitmap::Create(int Width,int Height,int BitCount)
{
	Destroy();
	m_hbm=CreateDIB(Width,Height,BitCount);
	return m_hbm!=NULL;
}

bool CBitmap::Load(HINSTANCE hinst,LPCTSTR pszName,UINT Flags)
{
	Destroy();
	m_hbm=static_cast<HBITMAP>(::LoadImage(hinst,pszName,IMAGE_BITMAP,0,0,Flags));
	return m_hbm!=NULL;
}

bool CBitmap::Attach(HBITMAP hbm)
{
	if (hbm==NULL)
		return false;
	Destroy();
	m_hbm=hbm;
	return true;
}

void CBitmap::Destroy()
{
	if (m_hbm!=NULL) {
		::DeleteObject(m_hbm);
		m_hbm=NULL;
	}
}

bool CBitmap::IsDIB() const
{
	if (m_hbm!=NULL) {
		DIBSECTION ds;
		if (::GetObject(m_hbm,sizeof(ds),&ds)==sizeof(ds))
			return true;
	}
	return false;
}

int CBitmap::GetWidth() const
{
	if (m_hbm!=NULL) {
		BITMAP bm;
		if (::GetObject(m_hbm,sizeof(bm),&bm)==sizeof(bm))
			return bm.bmWidth;
	}
	return 0;
}

int CBitmap::GetHeight() const
{
	if (m_hbm!=NULL) {
		BITMAP bm;
		if (::GetObject(m_hbm,sizeof(bm),&bm)==sizeof(bm))
			return bm.bmHeight;
	}
	return 0;
}


COffscreen::COffscreen()
	: m_hdc(NULL)
	, m_hbm(NULL)
	, m_hbmOld(NULL)
	, m_Width(0)
	, m_Height(0)
{
}

COffscreen::~COffscreen()
{
	Destroy();
}

bool COffscreen::Create(int Width,int Height,HDC hdc)
{
	if (Width<=0 || Height<=0)
		return false;
	Destroy();
	HDC hdcScreen;
	if (hdc==NULL) {
		hdcScreen=::GetDC(NULL);
		if (hdcScreen==NULL)
			return false;
		hdc=hdcScreen;
	} else {
		hdcScreen=NULL;
	}
	m_hdc=::CreateCompatibleDC(hdc);
	if (m_hdc==NULL) {
		if (hdcScreen!=NULL)
			::ReleaseDC(NULL,hdcScreen);
		return false;
	}
	m_hbm=::CreateCompatibleBitmap(hdc,Width,Height);
	if (hdcScreen!=NULL)
		::ReleaseDC(NULL,hdcScreen);
	if (m_hbm==NULL) {
		Destroy();
		return false;
	}
	m_hbmOld=static_cast<HBITMAP>(::SelectObject(m_hdc,m_hbm));
	m_Width=Width;
	m_Height=Height;
	return true;
}

void COffscreen::Destroy()
{
	if (m_hbmOld!=NULL) {
		::SelectObject(m_hdc,m_hbmOld);
		m_hbmOld=NULL;
	}
	if (m_hdc!=NULL) {
		::DeleteDC(m_hdc);
		m_hdc=NULL;
	}
	if (m_hbm!=NULL) {
		::DeleteObject(m_hbm);
		m_hbm=NULL;
		m_Width=0;
		m_Height=0;
	}
}

bool COffscreen::CopyTo(HDC hdc,const RECT *pDstRect)
{
	int DstX,DstY,Width,Height;

	if (m_hdc==NULL || hdc==NULL)
		return false;
	if (pDstRect!=NULL) {
		DstX=pDstRect->left;
		DstY=pDstRect->top;
		Width=pDstRect->right-pDstRect->left;
		Height=pDstRect->bottom-pDstRect->top;
		if (Width<=0 || Height<=0)
			return false;
		if (Width>m_Width)
			Width=m_Width;
		if (Height>m_Height)
			Height=m_Height;
	} else {
		DstX=DstY=0;
		Width=m_Width;
		Height=m_Height;
	}
	::BitBlt(hdc,DstX,DstY,Width,Height,m_hdc,0,0,SRCCOPY);
	return true;
}


}	// namespace DrawUtil
