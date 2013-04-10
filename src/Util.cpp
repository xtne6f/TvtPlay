#include <Windows.h>
#include <Shlwapi.h>
#include <tchar.h>
#include "Util.h"


#if 1 // 同一プロセスからTvtAudioStretchFilterへのメッセージ送信コード
#define ASFLT_FILTER_NAME   TEXT("TvtAudioStretchFilter")

static HWND ASFilterFindWindow()
{
    TCHAR szName[128];
    ::wsprintf(szName, TEXT("%s,%lu"), ASFLT_FILTER_NAME, ::GetCurrentProcessId());
    return ::FindWindowEx(HWND_MESSAGE, NULL, ASFLT_FILTER_NAME, szName);
}

LRESULT ASFilterSendMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    HWND hwnd = ASFilterFindWindow();
    return hwnd ? ::SendMessage(hwnd, Msg, wParam, lParam) : FALSE;
}

BOOL ASFilterPostMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    HWND hwnd = ASFilterFindWindow();
    return hwnd ? ::PostMessage(hwnd, Msg, wParam, lParam) : FALSE;
}
#endif


// UTF-16またはUTF-8テキストファイルを文字列として全て読む
// 成功するとnewされた配列のポインタが返るので、必ずdeleteすること
WCHAR *NewReadUtfFileToEnd(LPCTSTR fileName)
{
    BYTE *pBuf = NULL;
    WCHAR *pRet = NULL;

    HANDLE hFile = ::CreateFile(fileName, GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) goto EXIT;

    DWORD fileBytes = ::GetFileSize(hFile, NULL);
    if (fileBytes == 0xFFFFFFFF || fileBytes >= READ_FILE_MAX_SIZE) goto EXIT;

    pBuf = new BYTE[fileBytes + 2];
    DWORD readBytes;
    if (!::ReadFile(hFile, pBuf, fileBytes, &readBytes, NULL)) goto EXIT;
    pBuf[readBytes] = pBuf[readBytes+1] = 0;

    // BOM付きUTF-16LE、UTF-16BE、UTF-8、またはBOM無しUTF-8を判別する
    int codeType = pBuf[0]==0xFF && pBuf[1]==0xFE ? 1 :
                   pBuf[0]==0xFE && pBuf[1]==0xFF ? 2 : 0;
    int bomOffset = codeType ? 2 : pBuf[0]==0xEF && pBuf[1]==0xBB && pBuf[2]==0xBF ? 3 : 0;

    // 出力サイズ算出
    int retSize;
    if (codeType) {
        retSize = ::lstrlenW(reinterpret_cast<LPCWSTR>(pBuf+bomOffset)) + 1;
    }
    else {
        retSize = ::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(pBuf+bomOffset), -1, NULL, 0);
    }
    if (retSize <= 0) goto EXIT;

    // 文字コード変換
    pRet = new WCHAR[retSize];
    if (codeType == 1) {
        ::lstrcpyW(pRet, reinterpret_cast<LPCWSTR>(pBuf+bomOffset));
    }
    else if (codeType == 2) {
        for (int i = 0; i < retSize; ++i) {
            pRet[i] = (pBuf[bomOffset+i*2] << 8) | pBuf[bomOffset+i*2+1];
        }
    }
    else {
        if (!::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<LPCSTR>(pBuf+bomOffset), -1, pRet, retSize)) {
            delete [] pRet;
            pRet = NULL;
            goto EXIT;
        }
    }

EXIT:
    if (hFile != INVALID_HANDLE_VALUE) ::CloseHandle(hFile);
    delete [] pBuf;
    return pRet;
}


static void IconIndexToPos(int *pX, int *pW, HDC hdc, int idx)
{
    *pX = 0;
    *pW = ICON_SIZE;
    for (int x=0, i=0; i <= idx; i++) {
        if (::GetPixel(hdc, x, 0) == RGB(0,0,0)) {
            *pW = 1;
            while (*pW < ICON_SIZE && ::GetPixel(hdc, x + (*pW)++, 0) == RGB(0,0,0));
        }
        else {
            *pW = ICON_SIZE;
        }
        *pX = x;
        x += *pW;
    }
}


static int DrawMonoColorIcon(HDC hdcDest, int destX, int destY, HDC hdcSrc, int idx, bool fInvert)
{
    int x, width;
    IconIndexToPos(&x, &width, hdcSrc, idx);
    int ofsY = width == ICON_SIZE ? 0 : 1;

    if (fInvert) {
        ::BitBlt(hdcDest, destX, destY+ofsY, width, ICON_SIZE-ofsY, hdcSrc, x, ofsY, MERGEPAINT);
    }
    else {
        ::PatBlt(hdcDest, destX, destY+ofsY, width, ICON_SIZE-ofsY, DSTINVERT);
        ::BitBlt(hdcDest, destX, destY+ofsY, width, ICON_SIZE-ofsY, hdcSrc, x, ofsY, MERGEPAINT);
        ::PatBlt(hdcDest, destX, destY+ofsY, width, ICON_SIZE-ofsY, DSTINVERT);
    }
    return width;
}


bool ComposeMonoColorIcon(HDC hdcDest, int destX, int destY, HBITMAP hbm, LPCTSTR pIdxList)
{
    if (!hdcDest || !hbm) return false;

    HDC hdcMem = ::CreateCompatibleDC(hdcDest);
    if (!hdcMem) return false;
    HGDIOBJ hgdiOld = ::SelectObject(hdcMem, hbm);

    LPCTSTR p = pIdxList + ::StrCSpn(pIdxList, TEXT(",:-~'"));
    if (!*p || *p == TEXT(',')) {
        // indexが1つだけ指定されている場合
        int idx = ::StrToInt(pIdxList);
        ::PatBlt(hdcDest, destX, destY, ICON_SIZE, ICON_SIZE, WHITENESS);
        DrawMonoColorIcon(hdcDest, destX, destY, hdcMem, min(max(idx,0),255), false);

        ::PatBlt(hdcDest, destX+ICON_SIZE, destY, ICON_SIZE, ICON_SIZE, WHITENESS);
        DrawMonoColorIcon(hdcDest, destX+ICON_SIZE, destY, hdcMem, min(max(idx+1,0),255), false);
    }
    else {
        p = pIdxList;
        for (int i = 0; *p && *p != TEXT(','); i++) {
            bool fInvert = false;
            int x = destX + i * ICON_SIZE;
            ::PatBlt(hdcDest, x, destY, ICON_SIZE, ICON_SIZE, WHITENESS);
            while (*p && *p != TEXT(',') && *p != TEXT(':')) {
                int idx, width;
                if (*p == TEXT('~')) {
                    fInvert = !fInvert;
                    p++;
                }
                if (*p == TEXT('\'')) {
                    idx = (int)*(++p);
                    if (*p) p++;
                }
                else {
                    idx = ::StrToInt(p);
                    p += ::StrCSpn(p, TEXT(",:-~'"));
                }
                width = DrawMonoColorIcon(hdcDest, x, destY, hdcMem, min(max(idx,0),255), fInvert);
                x += width == ICON_SIZE ? 0 : width - 1;
                if (*p == TEXT('-')) p++;
            }
            if (*p == TEXT(':')) p++;
        }
    }
    ::SelectObject(hdcMem, hgdiOld);
    ::DeleteDC(hdcMem);
    return true;
}


int CompareTStrI(const void *str1, const void *str2)
{
    return ::lstrcmpi(*(LPCTSTR*)str1, *(LPCTSTR*)str2);
}


int CompareTStrIX(const void *str1, const void *str2)
{
    return ::lstrcmpi(*(LPCTSTR*)str2, *(LPCTSTR*)str1);
}


BOOL WritePrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, int value, LPCTSTR lpFileName)
{
    TCHAR szValue[32];
    ::wsprintf(szValue, TEXT("%d"), value);
    return ::WritePrivateProfileString(lpAppName, lpKeyName, szValue, lpFileName);
}


// MD5ハッシュ値を計算して先頭56bitを返す
// 失敗時は負を返す
LONGLONG CalcHash(const LPBYTE pbData, DWORD dwDataLen, DWORD dwSalt)
{
    HCRYPTPROV hProv = NULL;
    HCRYPTHASH hHash = NULL;
    LONGLONG llRet = -1;

    if (!::CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        hProv = NULL;
        goto EXIT;
    }

    if (!::CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        hHash = NULL;
        goto EXIT;
    }

    // リトルエンディアンを仮定
    if (!::CryptHashData(hHash, (LPBYTE)&dwSalt, sizeof(dwSalt), 0)) goto EXIT;
    if (!::CryptHashData(hHash, pbData, dwDataLen, 0)) goto EXIT;

    BYTE pbHash[16];
    DWORD dwHashLen = 16;
    if (!::CryptGetHashParam(hHash, HP_HASHVAL, pbHash, &dwHashLen, 0)) goto EXIT;

    llRet = ((LONGLONG)pbHash[0]<<48) | ((LONGLONG)pbHash[1]<<40) | ((LONGLONG)pbHash[2]<<32) |
            ((LONGLONG)pbHash[3]<<24) | (pbHash[4]<<16) | (pbHash[5]<<8) | pbHash[6];

EXIT:
    if (hHash) ::CryptDestroyHash(hHash);
    if (hProv) ::CryptReleaseContext(hProv, 0);
    return llRet;
}


#if 1 // From: tsselect-0.1.8/tsselect.c (一部改変)
int select_unit_size(unsigned char *head, unsigned char *tail)
{
	int i;
	int m,n,w;
	int count[320-188];

	unsigned char *buf;

	buf = head;
	memset(count, 0, sizeof(count));

	// 1st step, count up 0x47 interval
	while( buf+188 < tail ){
		if(buf[0] != 0x47){
			buf += 1;
			continue;
		}
		m = 320;
		if( buf+m > tail){
			m = tail-buf;
		}
		for(i=188;i<m;i++){
			if(buf[i] == 0x47){
				count[i-188] += 1;
			}
		}
		buf += 1;
	}

	// 2nd step, select maximum appeared interval
	m = 0;
	n = 0;
	for(i=188;i<320;i++){
		if(m < count[i-188]){
			m = count[i-188];
			n = i;
		}
	}

	// 3rd step, verify unit_size
	w = m*n;
	if( (m < 8) || ((w+2*n) < (tail-head)) ){
		return 0;
	}

	return n;
}

unsigned char *resync(unsigned char *head, unsigned char *tail, int unit_size)
{
	int i;
	unsigned char *buf;

	buf = head;
	tail -= unit_size * 8;
	while( buf < tail ){
		if(buf[0] == 0x47){
			for(i=1;i<8;i++){
				if(buf[unit_size*i] != 0x47){
					break;
				}
			}
			if(i == 8){
				return buf;
			}
		}
		buf += 1;
	}

	return NULL;
}

void extract_ts_header(TS_HEADER *dst, const unsigned char *packet)
{
	dst->sync                         =  packet[0];
	dst->transport_error_indicator    = (packet[1] >> 7) & 0x01;
	dst->payload_unit_start_indicator = (packet[1] >> 6) & 0x01;
	dst->transport_priority           = (packet[1] >> 5) & 0x01;
	dst->pid = ((packet[1] & 0x1f) << 8) | packet[2];
	dst->transport_scrambling_control = (packet[3] >> 6) & 0x03;
	dst->adaptation_field_control     = (packet[3] >> 4) & 0x03;
	dst->continuity_counter           =  packet[3]       & 0x0f;
}

void extract_adaptation_field(ADAPTATION_FIELD *dst, const unsigned char *data)
{
	const unsigned char *p;
	const unsigned char *tail;

	p = data;
	
	memset(dst, 0, sizeof(ADAPTATION_FIELD));
	if( (p[0] == 0) || (p[0] > 183) ){
		return;
	}

	dst->adaptation_field_length = p[0];
	p += 1;
	tail = p + dst->adaptation_field_length;
	if( (p+1) > tail ){
		memset(dst, 0, sizeof(ADAPTATION_FIELD));
		return;
	}

	dst->discontinuity_counter = (p[0] >> 7) & 1;
	dst->random_access_indicator = (p[0] >> 6) & 1;
	dst->elementary_stream_priority_indicator = (p[0] >> 5) & 1;
	dst->pcr_flag = (p[0] >> 4) & 1;
	dst->opcr_flag = (p[0] >> 3) & 1;
	dst->splicing_point_flag = (p[0] >> 2) & 1;
	dst->transport_private_data_flag = (p[0] >> 1) & 1;
	dst->adaptation_field_extension_flag = p[0] & 1;
	
	p += 1;
	
	if(dst->pcr_flag != 0){
		if( (p+6) > tail ){
			memset(dst, 0, sizeof(ADAPTATION_FIELD));
			return;
		}
		dst->pcr_45khz = ((unsigned int)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
		p += 6;
	}
}
#endif


#if 1 // From: TVTest_0.7.23_Src/StdUtil.cpp
int StdUtil::vsnprintf(wchar_t *s,size_t n,const wchar_t *format,va_list args)
{
	int Length;

	if (n>0) {
		Length=::vswprintf_s(s,n,format,args);
	} else {
		Length=0;
	}
	return Length;
}
#endif


#if 1 // From: TVTest_0.7.23_Src/TsUtilClass.cpp
/////////////////////////////////////////////////////////////////////////////
// トレースクラス
/////////////////////////////////////////////////////////////////////////////

void CTracer::Trace(LPCTSTR pszOutput, ...)
{
	va_list Args;

	va_start(Args,pszOutput);
	TraceV(pszOutput,Args);
	va_end(Args);
}

void CTracer::TraceV(LPCTSTR pszOutput,va_list Args)
{
	StdUtil::vsnprintf(m_szBuffer,sizeof(m_szBuffer)/sizeof(TCHAR),pszOutput,Args);
	OnTrace(m_szBuffer);
}
#endif


#if 1 // From: TVTest_0.7.23_Src/StringUtility.cpp
__declspec(restrict) LPWSTR DuplicateString(LPCWSTR pszString)
{
	if (pszString==NULL)
		return NULL;

	const size_t Length=lstrlenW(pszString)+1;
	LPWSTR pszNewString=new WCHAR[Length];
	::CopyMemory(pszNewString,pszString,Length*sizeof(WCHAR));
	return pszNewString;
}


bool ReplaceString(LPWSTR *ppszString,LPCWSTR pszNewString)
{
	if (ppszString==NULL)
		return false;
	delete [] *ppszString;
	*ppszString=DuplicateString(pszNewString);
	return true;
}
#endif


#if 1 // From: TVTest_0.7.23_Src/Util.cpp
COLORREF MixColor(COLORREF Color1,COLORREF Color2,BYTE Ratio)
{
	return RGB((GetRValue(Color1)*Ratio+GetRValue(Color2)*(255-Ratio))/255,
			   (GetGValue(Color1)*Ratio+GetGValue(Color2)*(255-Ratio))/255,
			   (GetBValue(Color1)*Ratio+GetBValue(Color2)*(255-Ratio))/255);
}


bool CompareLogFont(const LOGFONT *pFont1,const LOGFONT *pFont2)
{
	return memcmp(pFont1,pFont2,28/*offsetof(LOGFONT,lfFaceName)*/)==0
		&& lstrcmp(pFont1->lfFaceName,pFont2->lfFaceName)==0;
}


CDynamicString::CDynamicString()
	: m_pszString(NULL)
{
}


CDynamicString::CDynamicString(const CDynamicString &String)
	: m_pszString(NULL)
{
	*this=String;
}


#ifdef MOVE_SEMANTICS_SUPPORTED
CDynamicString::CDynamicString(CDynamicString &&String)
	: m_pszString(String.m_pszString)
{
	String.m_pszString=NULL;
}
#endif


CDynamicString::CDynamicString(LPCTSTR pszString)
	: m_pszString(NULL)
{
	Set(pszString);
}


CDynamicString::~CDynamicString()
{
	Clear();
}


CDynamicString &CDynamicString::operator=(const CDynamicString &String)
{
	if (&String!=this) {
		ReplaceString(&m_pszString,String.m_pszString);
	}
	return *this;
}


#ifdef MOVE_SEMANTICS_SUPPORTED
CDynamicString &CDynamicString::operator=(CDynamicString &&String)
{
	if (&String!=this) {
		Clear();
		m_pszString=String.m_pszString;
		String.m_pszString=NULL;
	}
	return *this;
}
#endif


CDynamicString &CDynamicString::operator+=(const CDynamicString &String)
{
	return *this+=String.m_pszString;
}


CDynamicString &CDynamicString::operator=(LPCTSTR pszString)
{
	ReplaceString(&m_pszString,pszString);
	return *this;
}


CDynamicString &CDynamicString::operator+=(LPCTSTR pszString)
{
	int Length=0;
	if (m_pszString!=NULL)
		Length+=::lstrlen(m_pszString);
	if (pszString!=NULL)
		Length+=::lstrlen(pszString);
	if (Length>0) {
		LPTSTR pszOldString=m_pszString;

		m_pszString=new TCHAR[Length+1];
		m_pszString[0]='\0';
		if (pszOldString!=NULL) {
			::lstrcpy(m_pszString,pszOldString);
			delete [] pszOldString;
		}
		if (pszString!=NULL)
			::lstrcat(m_pszString,pszString);
	}
	return *this;
}


bool CDynamicString::operator==(const CDynamicString &String) const
{
	return Compare(String.m_pszString)==0;
}


bool CDynamicString::operator!=(const CDynamicString &String) const
{
	return Compare(String.m_pszString)!=0;
}


bool CDynamicString::Set(LPCTSTR pszString)
{
	return ReplaceString(&m_pszString,pszString);
}


bool CDynamicString::Set(LPCTSTR pszString,size_t Length)
{
	Clear();
	if (pszString!=NULL && Length>0) {
		Length=StdUtil::strnlen(pszString,Length);
		m_pszString=new TCHAR[Length+1];
		::CopyMemory(m_pszString,pszString,Length*sizeof(TCHAR));
		m_pszString[Length]=_T('\0');
	}
	return true;
}


bool CDynamicString::Attach(LPTSTR pszString)
{
	Clear();
	m_pszString=pszString;
	return true;
}


int CDynamicString::Length() const
{
	if (m_pszString==NULL)
		return 0;
	return ::lstrlen(m_pszString);
}


void CDynamicString::Clear()
{
	if (m_pszString!=NULL) {
		delete [] m_pszString;
		m_pszString=NULL;
	}
}


bool CDynamicString::IsEmpty() const
{
	return IsStringEmpty(m_pszString);
}


int CDynamicString::Compare(LPCTSTR pszString) const
{
	if (IsEmpty()) {
		if (IsStringEmpty(pszString))
			return 0;
		return -1;
	}
	if (IsStringEmpty(pszString))
		return 1;
	return ::lstrcmp(m_pszString,pszString);
}


int CDynamicString::CompareIgnoreCase(LPCTSTR pszString) const
{
	if (IsEmpty()) {
		if (IsStringEmpty(pszString))
			return 0;
		return -1;
	}
	if (IsStringEmpty(pszString))
		return 1;
	return ::lstrcmpi(m_pszString,pszString);
}


CGlobalLock::CGlobalLock()
	: m_hMutex(NULL)
	, m_fOwner(false)
{
}

CGlobalLock::~CGlobalLock()
{
	Close();
}

bool CGlobalLock::Create(LPCTSTR pszName)
{
	if (m_hMutex!=NULL)
		return false;
	SECURITY_DESCRIPTOR sd;
	SECURITY_ATTRIBUTES sa;
	::ZeroMemory(&sd,sizeof(sd));
	::InitializeSecurityDescriptor(&sd,SECURITY_DESCRIPTOR_REVISION);
	::SetSecurityDescriptorDacl(&sd,TRUE,NULL,FALSE);
	::ZeroMemory(&sa,sizeof(sa));
	sa.nLength=sizeof(sa);
	sa.lpSecurityDescriptor=&sd;
	m_hMutex=::CreateMutex(&sa,FALSE,pszName);
	m_fOwner=false;
	return m_hMutex!=NULL;
}

bool CGlobalLock::Wait(DWORD Timeout)
{
	if (m_hMutex==NULL)
		return false;
	if (::WaitForSingleObject(m_hMutex,Timeout)==WAIT_TIMEOUT)
		return false;
	m_fOwner=true;
	return true;
}

void CGlobalLock::Close()
{
	if (m_hMutex!=NULL) {
		Release();
		::CloseHandle(m_hMutex);
		m_hMutex=NULL;
	}
}

void CGlobalLock::Release()
{
	if (m_hMutex!=NULL && m_fOwner) {
		::ReleaseMutex(m_hMutex);
		m_fOwner=false;
	}
}
#endif
