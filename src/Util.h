#ifndef INCLUDE_UTIL_H
#define INCLUDE_UTIL_H

#ifdef _DEBUG
#define DEBUG_OUT(x) ::OutputDebugString(x)
#else
#define DEBUG_OUT(x)
#endif

#define WM_ASFLT_STRETCH    (WM_APP + 1)
LRESULT ASFilterSendMessage(UINT Msg, WPARAM wParam, LPARAM lParam);
BOOL ASFilterPostMessage(UINT Msg, WPARAM wParam, LPARAM lParam);

#define READ_FILE_MAX_SIZE (256 * 1024)
#define ICON_SIZE 16

WCHAR *NewReadUtfFileToEnd(LPCTSTR fileName, DWORD dwShareMode);
bool WriteUtfFileToEnd(LPCTSTR fileName, DWORD dwShareMode, const WCHAR *pStr);
bool ComposeMonoColorIcon(HDC hdcDest, int destX, int destY, HBITMAP hbm, LPCTSTR pIdxList);
int CompareTStrI(const void *str1, const void *str2);
int CompareTStrIX(const void *str1, const void *str2);
BOOL WritePrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, int value, LPCTSTR lpFileName);
LONGLONG CalcHash(const LPBYTE pbData, DWORD dwDataLen, DWORD dwSalt);

typedef struct {
	int           sync;
	int           transport_error_indicator;
	int           payload_unit_start_indicator;
	int           transport_priority;
	int           pid;
	int           transport_scrambling_control;
	int           adaptation_field_control;
	int           continuity_counter;
} TS_HEADER;

typedef struct {
	int           adaptation_field_length;
	int           discontinuity_counter;
	int           random_access_indicator;
	int           elementary_stream_priority_indicator;
	int           pcr_flag;
	int           opcr_flag;
	int           splicing_point_flag;
	int           transport_private_data_flag;
	int           adaptation_field_extension_flag;
	unsigned int  pcr_45khz;
} ADAPTATION_FIELD; // (partial)

typedef struct {
    int             pointer_field;
    int             table_id;
    int             section_length;
    int             version_number;
    int             current_next_indicator;
    int             continuity_counter;
    int             data_count;
    unsigned char   data[1025];
} PSI;

typedef struct {
    int             program_number;
    int             version_number;
    int             pcr_pid;
    int             pid_count;
    //unsigned char   stream_type[256];
    unsigned short  pid[256]; // PESの一部に限定
    PSI             psi;
} PMT;

#define PAT_PID_MAX 64

typedef struct {
    int             transport_stream_id;
    int             version_number;
    int             pid_count;
    //unsigned short  program_number[PAT_PID_MAX];
    unsigned short  pid[PAT_PID_MAX]; // NITを除く
    PMT             *pmt[PAT_PID_MAX];
    PSI             psi;
} PAT;

typedef struct {
    int           packet_start_code_prefix;
    int           stream_id;
    int           pes_packet_length;
    int           pts_dts_flags;
    unsigned int  pts_45khz;
    unsigned int  dts_45khz;
} PES_HEADER; // (partial)

void extract_pat(PAT *pat, const unsigned char *payload, int payload_size, int unit_start, int counter);
void extract_pmt(PMT *pmt, const unsigned char *payload, int payload_size, int unit_start, int counter);
void extract_pes_header(PES_HEADER *dst, const unsigned char *payload, int payload_size/*, int stream_type*/);

int select_unit_size(unsigned char *head, unsigned char *tail);
unsigned char *resync(unsigned char *head, unsigned char *tail, int unit_size);
void extract_adaptation_field(ADAPTATION_FIELD *dst, const unsigned char *data);

inline void extract_ts_header(TS_HEADER *dst, const unsigned char *packet)
{
    dst->sync                         = packet[0];
    dst->transport_error_indicator    = packet[1] & 0x80;
    dst->payload_unit_start_indicator = packet[1] & 0x40;
    dst->transport_priority           = packet[1] & 0x20;
    dst->pid = ((packet[1] & 0x1f) << 8) | packet[2];
    dst->transport_scrambling_control = (packet[3] >> 6) & 0x03;
    dst->adaptation_field_control     = (packet[3] >> 4) & 0x03;
    dst->continuity_counter           = packet[3] & 0x0f;
}

#define ABSTRACT_DECL			__declspec(novtable)
#define ABSTRACT_CLASS(name)	ABSTRACT_DECL name abstract

#define APP_NAME TEXT("TvtPlay")

namespace StdUtil {
int vsnprintf(wchar_t *s,size_t n,const wchar_t *format,va_list args);
inline size_t strnlen(const wchar_t *s,size_t n) { return ::wcsnlen(s,n); }
}

/////////////////////////////////////////////////////////////////////////////
// クリティカルセクションラッパークラス
/////////////////////////////////////////////////////////////////////////////

class CCriticalLock
{
public:
	CCriticalLock();
	virtual ~CCriticalLock();

	void Lock(void);
	void Unlock(void);
	//bool TryLock(DWORD TimeOut=0);
private:
	CRITICAL_SECTION m_CriticalSection;
};

/////////////////////////////////////////////////////////////////////////////
// ブロックスコープロッククラス
/////////////////////////////////////////////////////////////////////////////

class CBlockLock
{
public:
	CBlockLock(CCriticalLock *pCriticalLock);
	virtual ~CBlockLock();
		
private:
	CCriticalLock *m_pCriticalLock;
};

/////////////////////////////////////////////////////////////////////////////
// トレースクラス
/////////////////////////////////////////////////////////////////////////////

class CTracer
{
	TCHAR m_szBuffer[256];
public:
	virtual ~CTracer() {}
	void Trace(LPCTSTR pszOutput, ...);
	void TraceV(LPCTSTR pszOutput,va_list Args);
protected:
	virtual void OnTrace(LPCTSTR pszOutput)=0;
};

__declspec(restrict) LPWSTR DuplicateString(LPCWSTR pszString);
bool ReplaceString(LPWSTR *ppszString,LPCWSTR pszNewString);

inline bool IsStringEmpty(LPCWSTR pszString) {
	return pszString==NULL || pszString[0]==L'\0';
}
inline LPCWSTR NullToEmptyString(LPCWSTR pszString) {
	return pszString!=NULL?pszString:L"";
}

COLORREF MixColor(COLORREF Color1,COLORREF Color2,BYTE Ratio=128);
bool CompareLogFont(const LOGFONT *pFont1,const LOGFONT *pFont2);

class CDynamicString {
protected:
	LPTSTR m_pszString;
public:
	CDynamicString();
	CDynamicString(const CDynamicString &String);
#ifdef MOVE_SEMANTICS_SUPPORTED
	CDynamicString(CDynamicString &&String);
#endif
	explicit CDynamicString(LPCTSTR pszString);
	virtual ~CDynamicString();
	CDynamicString &operator=(const CDynamicString &String);
#ifdef MOVE_SEMANTICS_SUPPORTED
	CDynamicString &operator=(CDynamicString &&String);
#endif
	CDynamicString &operator+=(const CDynamicString &String);
	CDynamicString &operator=(LPCTSTR pszString);
	CDynamicString &operator+=(LPCTSTR pszString);
	bool operator==(const CDynamicString &String) const;
	bool operator!=(const CDynamicString &String) const;
	LPCTSTR Get() const { return m_pszString; }
	LPCTSTR GetSafe() const { return NullToEmptyString(m_pszString); }
	bool Set(LPCTSTR pszString);
	bool Set(LPCTSTR pszString,size_t Length);
	bool Attach(LPTSTR pszString);
	int Length() const;
	void Clear();
	bool IsEmpty() const;
	int Compare(LPCTSTR pszString) const;
	int CompareIgnoreCase(LPCTSTR pszString) const;
};

class CGlobalLock
{
	HANDLE m_hMutex;
	bool m_fOwner;

	// delete
	CGlobalLock(const CGlobalLock &);
	CGlobalLock &operator=(const CGlobalLock &);

public:
	CGlobalLock();
	~CGlobalLock();
	bool Create(LPCTSTR pszName);
	bool Wait(DWORD Timeout=INFINITE);
	void Close();
	void Release();
};

#endif // INCLUDE_UTIL_H
