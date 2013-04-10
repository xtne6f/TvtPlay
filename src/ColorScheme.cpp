#include <Windows.h>
#include "Util.h"
#include "ColorScheme.h"
#include "Settings.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

#define lengthof _countof


static const LPCTSTR GradientDirectionList[] = {
	TEXT("horizontal"),
	TEXT("vertical"),
	TEXT("horizontal-mirror"),
	TEXT("vertical-mirror"),
};


#define HEXRGB(hex) RGB((hex)>>16,((hex)>>8)&0xFF,(hex)&0xFF)

const CColorScheme::ColorInfo CColorScheme::m_ColorInfoList[NUM_COLORS] = {
	{HEXRGB(0x777777),	TEXT("StatusBack"),							TEXT("ステータスバー背景1")},
	{HEXRGB(0x222222),	TEXT("StatusBack2"),						TEXT("ステータスバー背景2")},
	{HEXRGB(0xBBBBBB),	TEXT("StatusText"),							TEXT("ステータスバー文字")},
	{HEXRGB(0x777777),	TEXT("StatusItemBorder"),					TEXT("ステータスバー項目外枠")},
	{HEXRGB(0x222222),	TEXT("StatusBottomItemBack"),				TEXT("ステータスバー下段背景1")},
	{HEXRGB(0x222222),	TEXT("StatusBottomItemBack2"),				TEXT("ステータスバー下段背景2")},
	{HEXRGB(0xBBBBBB),	TEXT("StatusBottomItemText"),				TEXT("ステータスバー下段文字")},
	{HEXRGB(0x222222),	TEXT("StatusBottomItemBorder"),				TEXT("ステータスバー下段外枠")},
	{HEXRGB(0x3384FF),	TEXT("StatusHighlightBack"),				TEXT("ステータスバー選択背景1")},
	{HEXRGB(0x33D6FF),	TEXT("StatusHighlightBack2"),				TEXT("ステータスバー選択背景2")},
	{HEXRGB(0x333333),	TEXT("StatusHighlightText"),				TEXT("ステータスバー選択文字")},
	{HEXRGB(0x3384FF),	TEXT("StatusHighlightBorder"),				TEXT("ステータスバー選択外枠")},
	{HEXRGB(0x777777),	TEXT("StatusBorder"),						TEXT("ステータスバー外枠")},
	{HEXRGB(0xDF3F00),	TEXT("StatusRecordingCircle"),				TEXT("ステータスバー録画●")},
	{HEXRGB(0x777777),	TEXT("ScreenBorder"),						TEXT("画面の外枠")},
};

const CColorScheme::GradientInfo CColorScheme::m_GradientInfoList[NUM_GRADIENTS] = {
	{TEXT("StatusBackGradient"),						Theme::DIRECTION_VERT,	false,
		COLOR_STATUSBACK1,						COLOR_STATUSBACK2},
	{TEXT("StatusBottomItemBackGradient"),				Theme::DIRECTION_VERT,	false,
		COLOR_STATUSBOTTOMITEMBACK1,			COLOR_STATUSBOTTOMITEMBACK2},
	{TEXT("StatusHighlightBackGradient"),				Theme::DIRECTION_VERT,	true,
		COLOR_STATUSHIGHLIGHTBACK1,				COLOR_STATUSHIGHLIGHTBACK2},
};

const CColorScheme::BorderInfo CColorScheme::m_BorderInfoList[NUM_BORDERS] = {
	{TEXT("ScreenBorder"),						Theme::BORDER_SUNKEN,
		COLOR_SCREENBORDER,							true},
	{TEXT("StatusBorder"),						Theme::BORDER_RAISED,
		COLOR_STATUSBORDER,							false},
	{TEXT("StatusItemBorder"),					Theme::BORDER_NONE,
		COLOR_STATUSITEMBORDER,						false},
	{TEXT("StatusBottomItemBorder"),			Theme::BORDER_NONE,
		COLOR_STATUSBOTTOMITEMBORDER,				false},
	{TEXT("StatusHighlightBorder"),				Theme::BORDER_NONE,
		COLOR_STATUSHIGHLIGHTBORDER,				false},
};

const CColorScheme::StyleInfo CColorScheme::m_StyleList[NUM_STYLES] = {
	{GRADIENT_STATUSBACK,						BORDER_STATUSITEM,
		COLOR_STATUSTEXT},
	{GRADIENT_STATUSBOTTOMITEMBACK,				BORDER_STATUSBOTTOMITEM,
		COLOR_STATUSBOTTOMITEMTEXT},
	{GRADIENT_STATUSHIGHLIGHTBACK,				BORDER_STATUSHIGHLIGHT,
		COLOR_STATUSHIGHLIGHTTEXT},
};


CColorScheme::CColorScheme()
{
	SetDefault();
	::ZeroMemory(m_LoadedFlags,sizeof(m_LoadedFlags));
}


CColorScheme::CColorScheme(const CColorScheme &ColorScheme)
{
	*this=ColorScheme;
}


CColorScheme::~CColorScheme()
{
}


CColorScheme &CColorScheme::operator=(const CColorScheme &ColorScheme)
{
	if (&ColorScheme!=this) {
		::CopyMemory(m_ColorList,ColorScheme.m_ColorList,sizeof(m_ColorList));
		::CopyMemory(m_GradientList,ColorScheme.m_GradientList,sizeof(m_GradientList));
		::CopyMemory(m_BorderList,ColorScheme.m_BorderList,sizeof(m_BorderList));
		m_Name=ColorScheme.m_Name;
		m_FileName=ColorScheme.m_FileName;
		::CopyMemory(m_LoadedFlags,ColorScheme.m_LoadedFlags,sizeof(m_LoadedFlags));
	}
	return *this;
}


COLORREF CColorScheme::GetColor(int Type) const
{
	if (Type<0 || Type>=NUM_COLORS)
		return CLR_INVALID;
	return m_ColorList[Type];
}


COLORREF CColorScheme::GetColor(LPCTSTR pszText) const
{
	for (int i=0;i<NUM_COLORS;i++) {
		if (::lstrcmpi(m_ColorInfoList[i].pszText,pszText)==0)
			return m_ColorList[i];
	}
	return CLR_INVALID;
}


bool CColorScheme::SetColor(int Type,COLORREF Color)
{
	if (Type<0 || Type>=NUM_COLORS)
		return false;
	m_ColorList[Type]=Color;
	return true;
}


Theme::GradientType CColorScheme::GetGradientType(int Gradient) const
{
	if (Gradient<0 || Gradient>=NUM_GRADIENTS)
		return Theme::GRADIENT_NORMAL;
	return m_GradientList[Gradient].Type;
}


Theme::GradientType CColorScheme::GetGradientType(LPCTSTR pszText) const
{
	for (int i=0;i<NUM_GRADIENTS;i++) {
		if (::lstrcmpi(m_GradientInfoList[i].pszText,pszText)==0)
			return m_GradientList[i].Type;
	}
	return Theme::GRADIENT_NORMAL;
}


bool CColorScheme::SetGradientStyle(int Gradient,const GradientStyle &Style)
{
	if (Gradient<0 || Gradient>=NUM_GRADIENTS)
		return false;
	m_GradientList[Gradient].Type=Style.Type;
	m_GradientList[Gradient].Direction=Style.Direction;
	return true;
}


bool CColorScheme::GetGradientStyle(int Gradient,GradientStyle *pStyle) const
{
	if (Gradient<0 || Gradient>=NUM_GRADIENTS)
		return false;
	*pStyle=m_GradientList[Gradient];
	return true;
}


bool CColorScheme::GetGradientInfo(int Gradient,Theme::GradientInfo *pInfo) const
{
	if (Gradient<0 || Gradient>=NUM_GRADIENTS)
		return false;
	pInfo->Type=m_GradientList[Gradient].Type;
	pInfo->Direction=m_GradientList[Gradient].Direction;
	pInfo->Color1=m_ColorList[m_GradientInfoList[Gradient].Color1];
	pInfo->Color2=m_ColorList[m_GradientInfoList[Gradient].Color2];
	return true;
}


Theme::BorderType CColorScheme::GetBorderType(int Border) const
{
	if (Border<0 || Border>=NUM_BORDERS)
		return Theme::BORDER_NONE;
	return m_BorderList[Border];
}


bool CColorScheme::SetBorderType(int Border,Theme::BorderType Type)
{
	if (Border<0 || Border>=NUM_BORDERS
			|| Type<Theme::BORDER_NONE || Type>Theme::BORDER_RAISED)
		return false;
	m_BorderList[Border]=Type;
	return true;
}


bool CColorScheme::GetBorderInfo(int Border,Theme::BorderInfo *pInfo) const
{
	if (Border<0 || Border>=NUM_BORDERS)
		return false;
	pInfo->Type=m_BorderList[Border];
	pInfo->Color=m_ColorList[m_BorderInfoList[Border].Color];
	return true;
}


bool CColorScheme::GetStyle(int Type,Theme::Style *pStyle) const
{
	if (Type<0 || Type>=NUM_STYLES)
		return false;

	const StyleInfo &Info=m_StyleList[Type];
	GetGradientInfo(Info.Gradient,&pStyle->Gradient);
	if (Info.Border>=0)
		GetBorderInfo(Info.Border,&pStyle->Border);
	else
		pStyle->Border.Type=Theme::BORDER_NONE;
	if (Info.TextColor>=0)
		pStyle->TextColor=m_ColorList[Info.TextColor];
	return true;
}


bool CColorScheme::SetName(LPCTSTR pszName)
{
	return m_Name.Set(pszName);
}


bool CColorScheme::Load(LPCTSTR pszFileName)
{
	CSettings Settings;
	TCHAR szText[128];
	int i;

	if (!Settings.Open(pszFileName,TEXT("ColorScheme"),CSettings::OPEN_READ))
		return false;
	if (Settings.Read(TEXT("Name"),szText,lengthof(szText)))
		SetName(szText);
	::ZeroMemory(m_LoadedFlags,sizeof(m_LoadedFlags));
	for (i=0;i<NUM_COLORS;i++) {
		if (Settings.ReadColor(m_ColorInfoList[i].pszText,&m_ColorList[i]))
			SetLoadedFlag(i);
	}
	for (i=0;i<NUM_COLORS;i++) {
		if (IsLoaded(i)) {
			for (int j=0;j<NUM_GRADIENTS;j++) {
				if (m_GradientInfoList[j].Color1==i
						|| m_GradientInfoList[j].Color2==i) {
					if (m_GradientInfoList[j].Color1==i
							&& !IsLoaded(m_GradientInfoList[j].Color2)) {
						m_ColorList[m_GradientInfoList[j].Color2]=m_ColorList[i];
						SetLoadedFlag(m_GradientInfoList[j].Color2);
					}
					m_GradientList[j].Type=Theme::GRADIENT_NORMAL;
					break;
				}
			}
		} else {
			static const struct {
				int To,From;
			} Map[] = {
				{COLOR_STATUSBORDER,						COLOR_STATUSBACK1},
				{COLOR_STATUSBOTTOMITEMBACK1,				COLOR_STATUSBACK2},
				{COLOR_STATUSBOTTOMITEMBACK2,				COLOR_STATUSBOTTOMITEMBACK1},
				{COLOR_STATUSBOTTOMITEMTEXT,				COLOR_STATUSTEXT},
				{COLOR_STATUSBOTTOMITEMBORDER,				COLOR_STATUSBOTTOMITEMBACK1},
			};

			for (int j=0;j<lengthof(Map);j++) {
				if (Map[j].To==i && IsLoaded(Map[j].From)) {
					m_ColorList[i]=m_ColorList[Map[j].From];
					SetLoadedFlag(i);
					break;
				}
			}
		}
	}

	for (i=0;i<NUM_GRADIENTS;i++) {
		if (Settings.Read(m_GradientInfoList[i].pszText,szText,lengthof(szText))) {
			if (szText[0]=='\0' || ::lstrcmpi(szText,TEXT("normal"))==0)
				m_GradientList[i].Type=Theme::GRADIENT_NORMAL;
			else if (::lstrcmpi(szText,TEXT("glossy"))==0)
				m_GradientList[i].Type=Theme::GRADIENT_GLOSSY;
			else if (::lstrcmpi(szText,TEXT("interlaced"))==0)
				m_GradientList[i].Type=Theme::GRADIENT_INTERLACED;
		}

		TCHAR szName[128];
		::wsprintf(szName,TEXT("%sDirection"),m_GradientInfoList[i].pszText);
		m_GradientList[i].Direction=m_GradientInfoList[i].Direction;
		if (Settings.Read(szName,szText,lengthof(szText))) {
			for (int j=0;j<lengthof(GradientDirectionList);j++) {
				if (::lstrcmpi(szText,GradientDirectionList[j])==0) {
					m_GradientList[i].Direction=(Theme::GradientDirection)j;
					break;
				}
			}
		}
	}

	Settings.Close();

	for (i=0;i<NUM_BORDERS;i++)
		m_BorderList[i]=m_BorderInfoList[i].DefaultType;
	if (Settings.Open(pszFileName,TEXT("Style"),CSettings::OPEN_READ)) {
		for (i=0;i<NUM_BORDERS;i++) {
			if (Settings.Read(m_BorderInfoList[i].pszText,szText,lengthof(szText))) {
				if (::lstrcmpi(szText,TEXT("none"))==0) {
					if (!m_BorderInfoList[i].fAlways)
						m_BorderList[i]=Theme::BORDER_NONE;
				} else if (::lstrcmpi(szText,TEXT("solid"))==0)
					m_BorderList[i]=Theme::BORDER_SOLID;
				else if (::lstrcmpi(szText,TEXT("sunken"))==0)
					m_BorderList[i]=Theme::BORDER_SUNKEN;
				else if (::lstrcmpi(szText,TEXT("raised"))==0)
					m_BorderList[i]=Theme::BORDER_RAISED;
			}
		}
		Settings.Close();
	}

	SetFileName(pszFileName);
	return true;
}


bool CColorScheme::SetFileName(LPCTSTR pszFileName)
{
	return m_FileName.Set(pszFileName);
}


void CColorScheme::SetDefault()
{
	int i;

	for (i=0;i<NUM_COLORS;i++)
		m_ColorList[i]=m_ColorInfoList[i].DefaultColor;
	for (i=0;i<NUM_GRADIENTS;i++) {
		m_GradientList[i].Type=Theme::GRADIENT_NORMAL;
		m_GradientList[i].Direction=m_GradientInfoList[i].Direction;
	}
	for (i=0;i<NUM_BORDERS;i++)
		m_BorderList[i]=m_BorderInfoList[i].DefaultType;
}


LPCTSTR CColorScheme::GetColorName(int Type)
{
	if (Type<0 || Type>=NUM_COLORS)
		return NULL;
	return m_ColorInfoList[Type].pszName;
}


COLORREF CColorScheme::GetDefaultColor(int Type)
{
	if (Type<0 || Type>=NUM_COLORS)
		return CLR_INVALID;
	return m_ColorInfoList[Type].DefaultColor;
}


Theme::GradientType CColorScheme::GetDefaultGradientType(int Gradient)
{
	return Theme::GRADIENT_NORMAL;
}


bool CColorScheme::GetDefaultGradientStyle(int Gradient,GradientStyle *pStyle)
{
	if (Gradient<0 || Gradient>=NUM_GRADIENTS)
		return false;
	pStyle->Type=Theme::GRADIENT_NORMAL;
	pStyle->Direction=m_GradientInfoList[Gradient].Direction;
	return true;
}


bool CColorScheme::IsGradientDirectionEnabled(int Gradient)
{
	if (Gradient<0 || Gradient>=NUM_GRADIENTS)
		return false;
	return m_GradientInfoList[Gradient].fEnableDirection;
}


Theme::BorderType CColorScheme::GetDefaultBorderType(int Border)
{
	if (Border<0 || Border>=NUM_BORDERS)
		return Theme::BORDER_NONE;
	return m_BorderInfoList[Border].DefaultType;
}


bool CColorScheme::IsLoaded(int Type) const
{
	if (Type<0 || Type>=NUM_COLORS)
		return false;
	return (m_LoadedFlags[Type/32]&(1<<(Type%32)))!=0;
}


void CColorScheme::SetLoaded()
{
	::FillMemory(m_LoadedFlags,sizeof(m_LoadedFlags),0xFF);
}


int CColorScheme::GetColorGradient(int Type)
{
	for (int i=0;i<NUM_GRADIENTS;i++) {
		if (m_GradientInfoList[i].Color1==Type
				|| m_GradientInfoList[i].Color2==Type)
			return i;
	}
	return -1;
}


int CColorScheme::GetColorBorder(int Type)
{
	for (int i=0;i<NUM_BORDERS;i++) {
		if (m_BorderInfoList[i].Color==Type)
			return i;
	}
	return -1;
}


bool CColorScheme::IsBorderAlways(int Border)
{
	if (Border<0 || Border>=NUM_BORDERS)
		return false;
	return m_BorderInfoList[Border].fAlways;
}


void CColorScheme::SetLoadedFlag(int Color)
{
	m_LoadedFlags[Color/32]|=1<<(Color%32);
}
