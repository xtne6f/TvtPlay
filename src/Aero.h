#ifndef AERO_H
#define AERO_H

class CBufferedPaint
{
public:
	CBufferedPaint();
	~CBufferedPaint();
	HDC Begin(HDC hdc,const RECT *pRect,bool fErase=false);
	bool End(bool fUpdate=true);
	bool Clear(const RECT *pRect=NULL);
	bool SetAlpha(BYTE Alpha);
	bool SetOpaque() { return SetAlpha(255); }

	static bool IsSupported();

private:
	HANDLE m_hPaintBuffer;
};

#endif
