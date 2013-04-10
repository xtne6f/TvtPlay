#include <Windows.h>
#include <Shlwapi.h>
#include "Settings.h"

#if 0
#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif
#endif


#ifndef _T
#define _T(x) TEXT(x)
#endif


static unsigned int StrToUInt(LPCTSTR pszValue)
{
	unsigned int uValue;
	LPCTSTR p;

	uValue=0;
	p=pszValue;
	while (*p>=_T('0') && *p<=_T('9')) {
		uValue=uValue*10+(*p-_T('0'));
		p++;
	}
	return uValue;
}


static int HexToNum(TCHAR cCode)
{
	if (cCode>=_T('0') && cCode<=_T('9'))
		return cCode-_T('0');
	if (cCode>=_T('A') && cCode<=_T('F'))
		return cCode-_T('A')+10;
	if (cCode>=_T('a') && cCode<=_T('f'))
		return cCode-_T('a')+10;
	return 0;
}


CSettings::CSettings()
	: m_OpenFlags(0)
{
}


CSettings::~CSettings()
{
	Close();
}


bool CSettings::Open(LPCTSTR pszFileName,LPCTSTR pszSection,unsigned int Flags)
{
	if (pszFileName==NULL || ::lstrlen(pszFileName)>=MAX_PATH
			|| pszSection==NULL || pszSection[0]==_T('\0')
			|| Flags==0)
		return false;
	lstrcpy(m_szFileName,pszFileName);
	lstrcpyn(m_szSection,pszSection,MAX_SECTION);
	m_OpenFlags=Flags;
	return true;
}


void CSettings::Close()
{
	if ((m_OpenFlags&OPEN_WRITE)!=0)
		WritePrivateProfileString(NULL,NULL,NULL,m_szFileName);	// Flush
	m_OpenFlags=0;
}


bool CSettings::Clear()
{
	if ((m_OpenFlags&OPEN_WRITE)==0)
		return false;

	return WritePrivateProfileString(m_szSection,NULL,NULL,m_szFileName)!=FALSE;
}


bool CSettings::Read(LPCTSTR pszValueName,int *pData)
{
	TCHAR szValue[16];

	if (!Read(pszValueName,szValue,16) || szValue[0]=='\0')
		return false;
	*pData=::StrToInt(szValue);
	return true;
}


bool CSettings::Write(LPCTSTR pszValueName,int Data)
{
	TCHAR szValue[16];

	wsprintf(szValue,TEXT("%d"),Data);
	return Write(pszValueName,szValue);
}


bool CSettings::Read(LPCTSTR pszValueName,unsigned int *pData)
{
	TCHAR szValue[16];

	if (!Read(pszValueName,szValue,16) || szValue[0]=='\0')
		return false;
	*pData=StrToUInt(szValue);
	return true;
}


bool CSettings::Write(LPCTSTR pszValueName,unsigned int Data)
{
	TCHAR szValue[16];

	wsprintf(szValue,TEXT("%u"),Data);
	return Write(pszValueName,szValue);
}


bool CSettings::Read(LPCTSTR pszValueName,LPTSTR pszData,unsigned int Max)
{
	if ((m_OpenFlags&OPEN_READ)==0)
		return false;

	TCHAR cBack[2];

	if (pszData==NULL)
		return false;
	cBack[0]=pszData[0];
	if (Max>1)
		cBack[1]=pszData[1];
	GetPrivateProfileString(m_szSection,pszValueName,TEXT("\x1A"),pszData,Max,
																m_szFileName);
	if (pszData[0]=='\x1A') {
		pszData[0]=cBack[0];
		if (Max>1)
			pszData[1]=cBack[1];
		return false;
	}
	return true;
}


bool CSettings::Write(LPCTSTR pszValueName,LPCTSTR pszData)
{
	if ((m_OpenFlags&OPEN_WRITE)==0)
		return false;

	if (pszData==NULL)
		return false;
	// 文字列が ' か " で囲まれていると読み込み時に除去されてしまうので、
	// 余分に " で囲っておく。
	if (pszData[0]=='"' || pszData[0]=='\'') {
		LPCTSTR p;
		TCHAR Quote;

		p=pszData;
		Quote=*p++;
		while (*p!='\0') {
			if (*p==Quote && *(p+1)=='\0')
				break;
#ifndef UNICODE
			if (IsDBCSLeadByteEx(CP_ACP,*p))
				p++;
#endif
			p++;
		}
		if (*p==Quote) {
			LPTSTR pszBuff;

			pszBuff=new TCHAR[::lstrlen(pszData)+3];
			::wsprintf(pszBuff,TEXT("\"%s\""),pszData);
			bool fOK=WritePrivateProfileString(m_szSection,pszValueName,
											   pszBuff,m_szFileName)!=0;
			delete [] pszBuff;
			return fOK;
		}
	}
	return WritePrivateProfileString(m_szSection,pszValueName,pszData,m_szFileName)!=FALSE;
}


bool CSettings::Read(LPCTSTR pszValueName,bool *pfData)
{
	TCHAR szData[5];

	if (!Read(pszValueName,szData,5))
		return false;
	if (lstrcmpi(szData,TEXT("yes"))==0 || lstrcmpi(szData,TEXT("true"))==0)
		*pfData=true;
	else if (lstrcmpi(szData,TEXT("no"))==0 || lstrcmpi(szData,TEXT("false"))==0)
		*pfData=false;
	else
		return false;
	return true;
}


bool CSettings::Write(LPCTSTR pszValueName,bool fData)
{
	// よく考えたら否定文もあるので yes/no は変だが…
	// (その昔、iniファイルを直接編集して設定するようにしていた頃の名残)
	return Write(pszValueName,fData?TEXT("yes"):TEXT("no"));
}


bool CSettings::ReadColor(LPCTSTR pszValueName,COLORREF *pcrData)
{
	TCHAR szText[8];

	if (!Read(pszValueName,szText,8) || szText[0]!='#' || lstrlen(szText)!=7)
		return false;
	*pcrData=RGB((HexToNum(szText[1])<<4) | HexToNum(szText[2]),
				 (HexToNum(szText[3])<<4) | HexToNum(szText[4]),
				 (HexToNum(szText[5])<<4) | HexToNum(szText[6]));
	return true;
}


bool CSettings::WriteColor(LPCTSTR pszValueName,COLORREF crData)
{
	TCHAR szText[8];

	wsprintf(szText,TEXT("#%02x%02x%02x"),
			 GetRValue(crData),GetGValue(crData),GetBValue(crData));
	return Write(pszValueName,szText);
}


#define FONT_FLAG_ITALIC	0x0001U
#define FONT_FLAG_UNDERLINE	0x0002U
#define FONT_FLAG_STRIKEOUT	0x0004U

bool CSettings::Read(LPCTSTR pszValueName,LOGFONT *pFont)
{
	TCHAR szData[LF_FACESIZE+32];

	if (!Read(pszValueName,szData,sizeof(szData)/sizeof(TCHAR)) || szData[0]=='\0')
		return false;

	LPTSTR p=szData,q;
	for (int i=0;*p!='\0';i++) {
		while (*p==' ')
			p++;
		q=p;
		while (*p!='\0' && *p!=',')
			p++;
		if (*p!='\0')
			*p++='\0';
		if (*q!='\0') {
			switch (i) {
			case 0:
				::lstrcpyn(pFont->lfFaceName,q,LF_FACESIZE);
				pFont->lfWidth=0;
				pFont->lfEscapement=0;
				pFont->lfOrientation=0;
				pFont->lfWeight=FW_NORMAL;
				pFont->lfItalic=0;
				pFont->lfUnderline=0;
				pFont->lfStrikeOut=0;
				pFont->lfCharSet=DEFAULT_CHARSET;
				pFont->lfOutPrecision=OUT_DEFAULT_PRECIS;
				pFont->lfClipPrecision=CLIP_DEFAULT_PRECIS;
				pFont->lfQuality=DRAFT_QUALITY;
				pFont->lfPitchAndFamily=DEFAULT_PITCH | FF_DONTCARE;
				break;
			case 1:
				pFont->lfHeight=::StrToInt(q);
				break;
			case 2:
				pFont->lfWeight=::StrToInt(q);
				break;
			case 3:
				{
					unsigned int Flags=StrToUInt(q);
					pFont->lfItalic=(Flags&FONT_FLAG_ITALIC)!=0;
					pFont->lfUnderline=(Flags&FONT_FLAG_UNDERLINE)!=0;
					pFont->lfStrikeOut=(Flags&FONT_FLAG_STRIKEOUT)!=0;
				}
				break;
			}
		} else if (i==0) {
			return false;
		}
	}
	return true;
}


bool CSettings::Write(LPCTSTR pszValueName,const LOGFONT *pFont)
{
	TCHAR szData[LF_FACESIZE+32];
	unsigned int Flags=0;

	if (pFont->lfItalic)
		Flags|=FONT_FLAG_ITALIC;
	if (pFont->lfUnderline)
		Flags|=FONT_FLAG_UNDERLINE;
	if (pFont->lfStrikeOut)
		Flags|=FONT_FLAG_STRIKEOUT;
	::wsprintf(szData,TEXT("%s,%d,%d,%u"),
			   pFont->lfFaceName,pFont->lfHeight,pFont->lfWeight,Flags);
	return Write(pszValueName,szData);
}
