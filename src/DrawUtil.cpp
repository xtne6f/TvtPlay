#include <Windows.h>
#include "DrawUtil.h"


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


}	// namespace DrawUtil
