#ifndef DRAW_UTIL_H
#define DRAW_UTIL_H


namespace DrawUtil {

struct RGBA {
	BYTE Red;
	BYTE Green;
	BYTE Blue;
	BYTE Alpha;

	RGBA() : Red(0), Green(0), Blue(0), Alpha(0) {}
	RGBA(BYTE r,BYTE g,BYTE b,BYTE a=255) : Red(r), Green(g), Blue(b), Alpha(a) {}
	RGBA(COLORREF c) : Red(GetRValue(c)), Green(GetGValue(c)), Blue(GetBValue(c)), Alpha(255) {}
	COLORREF GetCOLORREF() const { return RGB(Red,Green,Blue); }
};

// 塗りつぶしの方向
enum FillDirection {
	DIRECTION_HORZ,			// 水平方向
	DIRECTION_VERT,			// 垂直方向
	DIRECTION_HORZMIRROR,	// 左右対称
	DIRECTION_VERTMIRROR	// 上下対称
};

bool Fill(HDC hdc,const RECT *pRect,COLORREF Color);
bool FillGradient(HDC hdc,const RECT *pRect,COLORREF Color1,COLORREF Color2,
				  FillDirection Direction=DIRECTION_HORZ);
bool FillGradient(HDC hdc,const RECT *pRect,const RGBA &Color1,const RGBA &Color2,
				  FillDirection Direction=DIRECTION_HORZ);
bool FillGlossyGradient(HDC hdc,const RECT *pRect,
						COLORREF Color1,COLORREF Color2,
						FillDirection Direction=DIRECTION_HORZ,
						int GlossRatio1=96,int GlossRatio2=48);
bool FillInterlacedGradient(HDC hdc,const RECT *pRect,
							COLORREF Color1,COLORREF Color2,
							FillDirection Direction=DIRECTION_HORZ,
							COLORREF LineColor=RGB(0,0,0),int LineOpacity=48);
bool DrawMonoColorDIB(HDC hdcDst,int DstX,int DstY,
					  HDC hdcSrc,int SrcX,int SrcY,int Width,int Height,COLORREF Color);
bool DrawMonoColorDIB(HDC hdcDst,int DstX,int DstY,
					  HBITMAP hbm,int SrcX,int SrcY,int Width,int Height,COLORREF Color);
HBITMAP CreateDIB(int Width,int Height,int BitCount);

enum FontType {
	FONT_DEFAULT,
	FONT_MESSAGE,
	FONT_MENU,
	FONT_CAPTION,
	FONT_SMALLCAPTION,
	FONT_STATUS
};

bool GetSystemFont(FontType Type,LOGFONT *pLogFont);

class CFont {
	HFONT m_hfont;
public:
	CFont();
	CFont(const CFont &Font);
	CFont(const LOGFONT &Font);
	CFont(FontType Type);
	~CFont();
	CFont &operator=(const CFont &Font);
	bool operator==(const CFont &Font) const;
	bool operator!=(const CFont &Font) const;
	bool Create(const LOGFONT *pLogFont);
	bool Create(FontType Type);
	bool IsCreated() const { return m_hfont!=NULL; }
	void Destroy();
	bool GetLogFont(LOGFONT *pLogFont) const;
	HFONT GetHandle() const { return m_hfont; }
	int GetHeight(bool fCell=true) const;
	int GetHeight(HDC hdc,bool fCell=true) const;
};

class CBitmap {
	HBITMAP m_hbm;
public:
	CBitmap();
	CBitmap(const CBitmap &Src);
	~CBitmap();
	CBitmap &operator=(const CBitmap &Src);
	bool Create(int Width,int Height,int BitCount);
	bool Load(HINSTANCE hinst,LPCTSTR pszName,UINT Flags=LR_CREATEDIBSECTION);
	bool Load(HINSTANCE hinst,int ID,UINT Flags=LR_CREATEDIBSECTION) {
		return Load(hinst,MAKEINTRESOURCE(ID),Flags);
	}
	bool Attach(HBITMAP hbm);
	bool IsCreated() const { return m_hbm!=NULL; }
	void Destroy();
	HBITMAP GetHandle() const { return m_hbm; }
	bool IsDIB() const;
	int GetWidth() const;
	int GetHeight() const;
};

inline HFONT SelectObject(HDC hdc,const CFont &Font) {
	return static_cast<HFONT>(::SelectObject(hdc,Font.GetHandle()));
}

class COffscreen {
	HDC m_hdc;
	HBITMAP m_hbm;
	HBITMAP m_hbmOld;
	int m_Width;
	int m_Height;

public:
	COffscreen();
	~COffscreen();
	bool Create(int Width,int Height,HDC hdc=NULL);
	void Destroy();
	bool IsCreated() const { return m_hdc!=NULL; }
	HDC GetDC() const { return m_hdc; }
	int GetWidth() const { return m_Width; }
	int GetHeight() const { return m_Height; }
	bool CopyTo(HDC hdc,const RECT *pDstRect=NULL);
};

}	// namespace DrawUtil


#endif
