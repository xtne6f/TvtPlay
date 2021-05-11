#ifndef COLOR_SCHEME_H
#define COLOR_SCHEME_H


#include "Theme.h"
#include <string>


class CColorScheme
{
public:
	enum {
		COLOR_STATUSBACK1,
		COLOR_STATUSBACK2,
		COLOR_STATUSTEXT,
		COLOR_STATUSITEMBORDER,
		COLOR_STATUSBOTTOMITEMBACK1,
		COLOR_STATUSBOTTOMITEMBACK2,
		COLOR_STATUSBOTTOMITEMTEXT,
		COLOR_STATUSBOTTOMITEMBORDER,
		COLOR_STATUSHIGHLIGHTBACK1,
		COLOR_STATUSHIGHLIGHTBACK2,
		COLOR_STATUSHIGHLIGHTTEXT,
		COLOR_STATUSHIGHLIGHTBORDER,
		COLOR_STATUSBORDER,
		COLOR_STATUSRECORDINGCIRCLE,
		COLOR_SCREENBORDER,
		COLOR_LAST=COLOR_SCREENBORDER,
		NUM_COLORS
	};
	enum {
		GRADIENT_STATUSBACK,
		GRADIENT_STATUSBOTTOMITEMBACK,
		GRADIENT_STATUSHIGHLIGHTBACK,
		NUM_GRADIENTS
	};
	enum {
		BORDER_SCREEN,
		BORDER_STATUS,
		BORDER_STATUSITEM,
		BORDER_STATUSBOTTOMITEM,
		BORDER_STATUSHIGHLIGHT,
		NUM_BORDERS
	};
	enum {
		STYLE_STATUSITEM,
		STYLE_STATUSBOTTOMITEM,
		STYLE_STATUSHIGHLIGHTITEM,
		NUM_STYLES
	};
	struct GradientStyle {
		Theme::GradientType Type;
		Theme::GradientDirection Direction;
	};

	CColorScheme();
	CColorScheme(const CColorScheme &ColorScheme);
	~CColorScheme();
	CColorScheme &operator=(const CColorScheme &ColorScheme);
	COLORREF GetColor(int Type) const;
	COLORREF GetColor(LPCTSTR pszText) const;
	bool SetColor(int Type,COLORREF Color);
	Theme::GradientType GetGradientType(int Gradient) const;
	Theme::GradientType GetGradientType(LPCTSTR pszText) const;
	bool SetGradientStyle(int Gradient,const GradientStyle &Style);
	bool GetGradientStyle(int Gradient,GradientStyle *pStyle) const;
	bool GetGradientInfo(int Gradient,Theme::GradientInfo *pInfo) const;
	Theme::BorderType GetBorderType(int Border) const;
	bool SetBorderType(int Border,Theme::BorderType Type);
	bool GetBorderInfo(int Border,Theme::BorderInfo *pInfo) const;
	bool GetStyle(int Type,Theme::Style *pStyle) const;
	LPCTSTR GetName() const { return m_Name.c_str(); }
	bool SetName(LPCTSTR pszName);
	LPCTSTR GetFileName() const { return m_FileName.c_str(); }
	bool Load(LPCTSTR pszFileName,bool fLegacy=false);
	bool SetFileName(LPCTSTR pszFileName);
	void SetDefault();
	bool IsLoaded(int Type) const;
	void SetLoaded();

	static COLORREF GetDefaultColor(int Type);
	static Theme::GradientType GetDefaultGradientType(int Gradient);
	static bool GetDefaultGradientStyle(int Gradient,GradientStyle *pStyle);
	static bool IsGradientDirectionEnabled(int Gradient);
	static Theme::BorderType GetDefaultBorderType(int Border);
	static int GetColorGradient(int Type);
	static int GetColorBorder(int Type);
	static bool IsBorderAlways(int Border);

private:
	COLORREF m_ColorList[NUM_COLORS];
	GradientStyle m_GradientList[NUM_GRADIENTS];
	Theme::BorderType m_BorderList[NUM_BORDERS];
	std::basic_string<TCHAR> m_Name;
	std::basic_string<TCHAR> m_FileName;
	struct ColorInfo {
		COLORREF DefaultColor;
		LPCTSTR pszText;
	};
	struct GradientInfo {
		LPCTSTR pszText;
		Theme::GradientDirection Direction;
		bool fEnableDirection;
		int Color1;
		int Color2;
	};
	struct BorderInfo {
		LPCTSTR pszText;
		Theme::BorderType DefaultType;
		int Color;
		bool fAlways;
	};
	struct StyleInfo {
		int Gradient;
		int Border;
		int TextColor;
	};

	DWORD m_LoadedFlags[(NUM_COLORS+31)/32];
	void SetLoadedFlag(int Color);
	static const ColorInfo m_ColorInfoList[NUM_COLORS];
	static const ColorInfo m_ColorInfoLegacyList[NUM_COLORS];
	static const GradientInfo m_GradientInfoList[NUM_GRADIENTS];
	static const BorderInfo m_BorderInfoList[NUM_BORDERS];
	static const StyleInfo m_StyleList[NUM_STYLES];
};

#endif
