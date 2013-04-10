#ifndef SETTINGS_H
#define SETTINGS_H


class CSettings {
	enum { MAX_SECTION=MAX_PATH };
	TCHAR m_szFileName[MAX_PATH];
	TCHAR m_szSection[MAX_SECTION];
	unsigned int m_OpenFlags;
public:
	enum {
		OPEN_READ  = 0x00000001,
		OPEN_WRITE = 0x00000002
	};
	CSettings();
	~CSettings();
	bool Open(LPCTSTR pszFileName,LPCTSTR pszSection,unsigned int Flags);
	void Close();
	bool Clear();
	bool Read(LPCTSTR pszValueName,int *pData);
	bool Write(LPCTSTR pszValueName,int Data);
	bool Read(LPCTSTR pszValueName,unsigned int *pData);
	bool Write(LPCTSTR pszValueName,unsigned int Data);
	bool Read(LPCTSTR pszValueName,LPTSTR pszData,unsigned int Max);
	bool Write(LPCTSTR pszValueName,LPCTSTR pszData);
	bool Read(LPCTSTR pszValueName,bool *pfData);
	bool Write(LPCTSTR pszValueName,bool fData);
	bool ReadColor(LPCTSTR pszValueName,COLORREF *pcrData);
	bool WriteColor(LPCTSTR pszValueName,COLORREF crData);
	bool Read(LPCTSTR pszValueName,LOGFONT *pFont);
	bool Write(LPCTSTR pszValueName,const LOGFONT *pFont);
};


#endif
