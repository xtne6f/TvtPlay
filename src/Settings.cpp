#include <Windows.h>
#include <Shlwapi.h>
#include <tchar.h>
#include "Util.h"
#include "Settings.h"

#if 0
#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif
#endif

#define lengthof _countof

static inline bool IsStringEmpty(LPCTSTR pszString)
{
	return pszString==NULL || pszString[0]==TEXT('\0');
}


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
	Close();
	if (IsStringEmpty(pszFileName) || ::lstrlen(pszFileName)>=MAX_PATH
			|| IsStringEmpty(pszSection)
			|| Flags==0)
		return false;
	lstrcpy(m_szFileName,pszFileName);
	lstrcpyn(m_szSection,pszSection,MAX_SECTION);
	m_OpenFlags=Flags;
	return true;
}


void CSettings::Close()
{
	m_OpenFlags=0;
}


bool CSettings::Clear()
{
	if ((m_OpenFlags&OPEN_WRITE)==0)
		return false;

	return WritePrivateProfileString(m_szSection,NULL,NULL,m_szFileName)!=FALSE;
}


void CSettings::Flush()
{
	if ((m_OpenFlags&OPEN_WRITE)!=0)
		WritePrivateProfileString(NULL,NULL,NULL,m_szFileName);
}


bool CSettings::Read(LPCTSTR pszValueName,int *pData)
{
	TCHAR szValue[16];

	if (!Read(pszValueName,szValue,lengthof(szValue)) || szValue[0]==_T('\0'))
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

	if (!Read(pszValueName,szValue,16) || szValue[0]==_T('\0'))
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
	GetPrivateProfileString(m_szSection,pszValueName,TEXT("\x1A"),pszData,Max,m_szFileName);
	if (pszData[0]==_T('\x1A')) {
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
	if (pszData[0]==_T('"') || pszData[0]==_T('\'')) {
		LPCTSTR p;
		TCHAR Quote;

		p=pszData;
		Quote=*p++;
		while (*p!=_T('\0')) {
			if (*p==Quote && *(p+1)==_T('\0'))
				break;
#ifndef UNICODE
			if (IsDBCSLeadByteEx(CP_ACP,*p))
				p++;
#endif
			p++;
		}
		if (*p==Quote) {
			size_t Length=::lstrlen(pszData);
			LPTSTR pszBuff=new TCHAR[Length+3];

			pszBuff[0]=_T('\"');
			::CopyMemory(&pszBuff[1],pszData,Length*sizeof(TCHAR));
			pszBuff[Length+1]=_T('\"');
			pszBuff[Length+2]=_T('\0');
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
	TCHAR szData[8];

	if (!Read(pszValueName,szData,lengthof(szData)))
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

	if (!Read(pszValueName,szText,lengthof(szText)) || szText[0]!=_T('#') || lstrlen(szText)!=7)
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
	for (int i=0;*p!=_T('\0');i++) {
		while (*p==_T(' '))
			p++;
		q=p;
		while (*p!=_T('\0') && *p!=_T(','))
			p++;
		if (*p!=_T('\0'))
			*p++=_T('\0');
		if (*q!=_T('\0')) {
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




CSettingsFile::CSettingsFile()
	: m_OpenFlags(0)
	, m_LastError(ERROR_SUCCESS)
{
	m_szFileName[0]=_T('\0');
}


CSettingsFile::~CSettingsFile()
{
	//Close();
}


bool CSettingsFile::Open(LPCTSTR pszFileName,unsigned int Flags)
{
	Close();

	if (IsStringEmpty(pszFileName) || ::lstrlen(pszFileName)>=MAX_PATH
			|| Flags==0) {
		m_LastError=ERROR_INVALID_PARAMETER;
		return false;
	}

	::lstrcpy(m_szFileName,pszFileName);
	m_OpenFlags=Flags;
	m_LastError=ERROR_SUCCESS;

#ifdef UNICODE
	if ((m_OpenFlags&OPEN_WRITE)!=0 && !::PathFileExists(pszFileName)) {
		HANDLE hFile=::CreateFile(pszFileName,GENERIC_WRITE,0,NULL,
								  CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
		if (hFile!=INVALID_HANDLE_VALUE) {
			// Unicode(UTF-16 LE)のiniファイルになるようにBOMを書き出す
			static const WORD BOM=0xFEFF;
			DWORD Write;
			::WriteFile(hFile,&BOM,2,&Write,NULL);
			::FlushFileBuffers(hFile);
			::CloseHandle(hFile);
		} else {
			DWORD Error=::GetLastError();
			if (Error!=ERROR_ALREADY_EXISTS) {
				m_LastError=Error;
			}
		}
	}
#endif

	return true;
}


void CSettingsFile::Close()
{
	m_szFileName[0]=_T('\0');
	m_OpenFlags=0;
	m_LastError=ERROR_SUCCESS;
}


void CSettingsFile::Flush()
{
	if ((m_OpenFlags&OPEN_WRITE)!=0)
		::WritePrivateProfileString(NULL,NULL,NULL,m_szFileName);
}


bool CSettingsFile::OpenSection(CSettings *pSettings,LPCTSTR pszSection)
{
	if (m_OpenFlags==0 || pSettings==NULL)
		return false;
	return pSettings->Open(m_szFileName,pszSection,m_OpenFlags);
}


bool CSettingsFile::GetFileName(LPTSTR pszFileName,int MaxLength) const
{
	if (pszFileName==NULL || MaxLength<=::lstrlen(m_szFileName))
		return false;
	::lstrcpy(pszFileName,m_szFileName);
	return true;
}




CSettingsBase::CSettingsBase()
	: m_pszSection(TEXT("Settings"))
	, m_fChanged(false)
{
}


CSettingsBase::CSettingsBase(LPCTSTR pszSection)
	: m_pszSection(pszSection)
	, m_fChanged(false)
{
}


CSettingsBase::~CSettingsBase()
{
}


bool CSettingsBase::LoadSettings(CSettingsFile &File)
{
	CSettings Settings;

	if (!File.OpenSection(&Settings,m_pszSection))
		return false;
	return ReadSettings(Settings);
}


bool CSettingsBase::SaveSettings(CSettingsFile &File)
{
	CSettings Settings;

	if (!File.OpenSection(&Settings,m_pszSection))
		return false;
	return WriteSettings(Settings);
}


bool CSettingsBase::LoadSettings(LPCTSTR pszFileName)
{
	CSettingsFile File;

	if (!File.Open(pszFileName,CSettingsFile::OPEN_READ))
		return false;
	return LoadSettings(File);
}


bool CSettingsBase::SaveSettings(LPCTSTR pszFileName)
{
	CSettingsFile File;

	if (!File.Open(pszFileName,CSettingsFile::OPEN_WRITE))
		return false;
	return LoadSettings(File);
}
