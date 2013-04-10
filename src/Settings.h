#ifndef SETTINGS_H
#define SETTINGS_H


class CSettings
{
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
	void Flush();
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

class CSettingsFile
{
public:
	enum {
		OPEN_READ  = CSettings::OPEN_READ,
		OPEN_WRITE = CSettings::OPEN_WRITE
	};

	CSettingsFile();
	~CSettingsFile();
	bool Open(LPCTSTR pszFileName,unsigned int Flags);
	void Close();
	void Flush();
	bool OpenSection(CSettings *pSettings,LPCTSTR pszSection);
	bool GetFileName(LPTSTR pszFileName,int MaxLength) const;
	DWORD GetLastError() const { return m_LastError; }

private:
	TCHAR m_szFileName[MAX_PATH];
	unsigned int m_OpenFlags;
	DWORD m_LastError;
};

class ABSTRACT_CLASS(CSettingsBase)
{
public:
	CSettingsBase();
	CSettingsBase(LPCTSTR pszSection);
	virtual ~CSettingsBase();
	virtual bool LoadSettings(CSettingsFile &File);
	virtual bool SaveSettings(CSettingsFile &File);
	virtual bool ReadSettings(CSettings &Settings) { return false; }
	virtual bool WriteSettings(CSettings &Settings) { return false; }
	bool LoadSettings(LPCTSTR pszFileName);
	bool SaveSettings(LPCTSTR pszFileName);
	bool IsChanged() const { return m_fChanged; }
	void ClearChanged() { m_fChanged=false; }

protected:
	LPCTSTR m_pszSection;
	bool m_fChanged;
};


#endif
