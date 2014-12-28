#ifndef DRAW_UTIL_H
#define DRAW_UTIL_H


namespace DrawUtil {

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
bool DrawMonoColorDIB(HDC hdcDst,int DstX,int DstY,
					  HDC hdcSrc,int SrcX,int SrcY,int Width,int Height,COLORREF Color);
bool DrawMonoColorDIB(HDC hdcDst,int DstX,int DstY,
					  HBITMAP hbm,int SrcX,int SrcY,int Width,int Height,COLORREF Color);
HBITMAP CreateDIB(int Width,int Height,int BitCount,void **ppBits=NULL);

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

}	// namespace DrawUtil

#endif
