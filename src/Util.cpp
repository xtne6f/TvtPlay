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
WCHAR *NewReadUtfFileToEnd(LPCTSTR fileName, DWORD dwShareMode)
{
    BYTE *pBuf = NULL;
    WCHAR *pRet = NULL;

    HANDLE hFile = ::CreateFile(fileName, GENERIC_READ, dwShareMode,
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


// 文字列をBOM付きUTF-8テキストファイルとして書き込む
bool WriteUtfFileToEnd(LPCTSTR fileName, DWORD dwShareMode, const WCHAR *pStr)
{
    BYTE *pBuf = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;
    bool rv = false;

    // 出力サイズ算出
    int bufSize = ::WideCharToMultiByte(CP_UTF8, 0, pStr, -1, NULL, 0, NULL, NULL);
    if (bufSize <= 0) goto EXIT;

    // 文字コード変換(NULL文字含む)
    pBuf = new BYTE[3 + bufSize];
    pBuf[0] = 0xEF; pBuf[1] = 0xBB; pBuf[2] = 0xBF;
    bufSize = ::WideCharToMultiByte(CP_UTF8, 0, pStr, -1, reinterpret_cast<LPSTR>(pBuf + 3), bufSize, NULL, NULL);
    if (bufSize <= 0) goto EXIT;

    hFile = ::CreateFile(fileName, GENERIC_WRITE, dwShareMode,
                         NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) goto EXIT;

    DWORD written;
    if (!::WriteFile(hFile, pBuf, bufSize + 3 - 1, &written, NULL)) goto EXIT;

    rv = true;
EXIT:
    if (hFile != INVALID_HANDLE_VALUE) ::CloseHandle(hFile);
    delete [] pBuf;
    return rv;
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


static void extract_psi(PSI *psi, const unsigned char *payload, int payload_size, int unit_start, int counter)
{
    int pointer;
    int section_length;
    const unsigned char *table;

    if (unit_start) {
        psi->continuity_counter = 0x20|counter;
        psi->data_count = psi->version_number = 0;
    }
    else {
        psi->continuity_counter = (psi->continuity_counter+1)&0x2f;
        if (psi->continuity_counter != (0x20|counter)) {
            psi->continuity_counter = psi->data_count = psi->version_number = 0;
            return;
        }
    }
    if (psi->data_count + payload_size <= sizeof(psi->data)) {
        memcpy(psi->data + psi->data_count, payload, payload_size);
        psi->data_count += payload_size;
    }
    // TODO: CRC32

    // psi->version_number != 0 のとき各フィールドは有効
    if (psi->data_count >= 1) {
        pointer = psi->data[0];
        if (psi->data_count >= pointer + 4) {
            section_length = ((psi->data[pointer+2]&0x03)<<8) | psi->data[pointer+3];
            if (section_length >= 3 && psi->data_count >= pointer + 4 + section_length) {
                table = psi->data + 1 + pointer;
                psi->pointer_field          = pointer;
                psi->table_id               = table[0];
                psi->section_length         = section_length;
                psi->version_number         = 0x20 | ((table[5]>>1)&0x1f);
                psi->current_next_indicator = table[5] & 0x01;
            }
        }
    }
}


// 参考: ITU-T H.222.0 Sec.2.4.4.3 および ARIB TR-B14 第一分冊第二編8.2
void extract_pat(PAT *pat, const unsigned char *payload, int payload_size, int unit_start, int counter)
{
    int program_number;
    int found;
    int pos, i, j, n;
    int pmt_exists[PAT_PID_MAX];
    unsigned short pid;
    const unsigned char *table;

    extract_psi(&pat->psi, payload, payload_size, unit_start, counter);

    if (pat->psi.version_number &&
        pat->psi.version_number != pat->version_number &&
        pat->psi.current_next_indicator &&
        pat->psi.table_id == 0 &&
        pat->psi.section_length >= 5)
    {
        // PAT更新
        table = pat->psi.data + 1 + pat->psi.pointer_field;
        pat->transport_stream_id = (table[3]<<8) | table[4];
        pat->version_number = pat->psi.version_number;

        // 受信済みPMTを調べ、必要ならば新規に生成する
        memset(pmt_exists, 0, sizeof(pmt_exists));
        pos = 3 + 5;
        while (pos < 3 + pat->psi.section_length - 4) {
            program_number = (table[pos]<<8) | (table[pos+1]);
            if (program_number != 0) {
                pid = ((table[pos+2]&0x1f)<<8) | table[pos+3];
                for (found=0, i=0; i < pat->pid_count; ++i) {
                    if (pat->pid[i] == pid) {
                        pmt_exists[i] = found = 1;
                        break;
                    }
                }
                if (!found && pat->pid_count < PAT_PID_MAX) {
                    //pat->program_number[pat->pid_count] = program_number;
                    pat->pid[pat->pid_count] = pid;
                    pat->pmt[pat->pid_count] = new PMT;
                    memset(pat->pmt[pat->pid_count], 0, sizeof(PMT));
                    pmt_exists[pat->pid_count++] = 1;
                }
            }
            pos += 4;
        }
        // PATから消えたPMTを破棄する
        n = pat->pid_count;
        for (i=0, j=0; i < n; ++i) {
            if (!pmt_exists[i]) {
                delete pat->pmt[i];
                --pat->pid_count;
            }
            else {
                pat->pid[j] = pat->pid[i];
                pat->pmt[j++] = pat->pmt[i];
            }
        }
    }
}


// 地上波+BSで指定可能なID(ARIB TR-B14):0x01,0x02,0x06,0x0D,0x0F,0x1B
#define H_262_VIDEO         0x02
#define PES_PRIVATE_DATA    0x06
#define ADTS_TRANSPORT      0x0F
#define AVC_VIDEO           0x1B
// for スカパーSD
#define MPEG2_AUDIO         0x04
// for Blu-ray
#define PS_BD_AC3_AUDIO     0x81


// 参考: ITU-T H.222.0 Sec.2.4.4.8
void extract_pmt(PMT *pmt, const unsigned char *payload, int payload_size, int unit_start, int counter)
{
    int program_info_length;
    int es_info_length;
    int stream_type;
    int pos;
    const unsigned char *table;

    extract_psi(&pmt->psi, payload, payload_size, unit_start, counter);

    if (pmt->psi.version_number &&
        pmt->psi.version_number != pmt->version_number &&
        pmt->psi.current_next_indicator &&
        pmt->psi.table_id == 2 &&
        pmt->psi.section_length >= 9)
    {
        // PMT更新
        table = pmt->psi.data + 1 + pmt->psi.pointer_field;
        pmt->program_number = (table[3]<<8) | table[4];
        pmt->version_number = pmt->psi.version_number;
        pmt->pcr_pid        = ((table[8]&0x1f)<<8) | table[9];
        program_info_length = ((table[10]&0x03)<<8) | table[11];

        pmt->pid_count = 0;
        pos = 3 + 9 + program_info_length;
        while (pos < 3 + pmt->psi.section_length - 5) {
            stream_type = table[pos];
            if (stream_type == H_262_VIDEO ||
                stream_type == PES_PRIVATE_DATA ||
                stream_type == ADTS_TRANSPORT ||
                stream_type == AVC_VIDEO ||
                stream_type == MPEG2_AUDIO ||
                stream_type == PS_BD_AC3_AUDIO)
            {
                //pmt->stream_type[pmt->pid_count] = stream_type;
                pmt->pid[pmt->pid_count++] = (table[pos+1]&0x1f)<<8 | table[pos+2];
            }
            es_info_length = (table[pos+3]&0x03)<<8 | table[pos+4];
            pos += 5 + es_info_length;
        }
    }
}


#define PROGRAM_STREAM_MAP          0xBC
#define PADDING_STREAM              0xBE
#define PRIVATE_STREAM_2            0xBF
#define ECM                         0xF0
#define EMM                         0xF1
#define PROGRAM_STREAM_DIRECTORY    0xFF
#define DSMCC_STREAM                0xF2
#define ITU_T_REC_TYPE_E_STREAM     0xF8


// 参考: ITU-T H.222.0 Sec.2.4.3.6
void extract_pes_header(PES_HEADER *dst, const unsigned char *payload, int payload_size/*, int stream_type*/)
{
    const unsigned char *p;

    dst->packet_start_code_prefix = 0;
    if (payload_size < 19) return;

    p = payload;

    dst->packet_start_code_prefix = (p[0]<<16) | (p[1]<<8) | p[2];
    if (dst->packet_start_code_prefix != 1) {
        dst->packet_start_code_prefix = 0;
        return;
    }

    dst->stream_id         = p[3];
    dst->pes_packet_length = (p[4]<<8) | p[5];
    dst->pts_dts_flags     = 0;
    if (dst->stream_id != PROGRAM_STREAM_MAP &&
        dst->stream_id != PADDING_STREAM &&
        dst->stream_id != PRIVATE_STREAM_2 &&
        dst->stream_id != ECM &&
        dst->stream_id != EMM &&
        dst->stream_id != PROGRAM_STREAM_DIRECTORY &&
        dst->stream_id != DSMCC_STREAM &&
        dst->stream_id != ITU_T_REC_TYPE_E_STREAM)
    {
        dst->pts_dts_flags = (p[7]>>6) & 0x03;
        if (dst->pts_dts_flags >= 2) {
            dst->pts_45khz = ((unsigned int)((p[9]>>1)&7)<<29)|(p[10]<<21)|(((p[11]>>1)&0x7f)<<14)|(p[12]<<6)|((p[13]>>2)&0x3f);
            if (dst->pts_dts_flags == 3) {
                dst->dts_45khz = ((unsigned int)((p[14]>>1)&7)<<29)|(p[15]<<21)|(((p[16]>>1)&0x7f)<<14)|(p[17]<<6)|((p[18]>>2)&0x3f);
            }
        }
    }
    // スタフィング(Sec.2.4.3.5)によりPESのうちここまでは必ず読める
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
//////////////////////////////////////////////////////////////////////
// CCriticalLock クラスの構築/消滅
//////////////////////////////////////////////////////////////////////

CCriticalLock::CCriticalLock()
{
	// クリティカルセクション初期化
	::InitializeCriticalSection(&m_CriticalSection);
}

CCriticalLock::~CCriticalLock()
{
	// クリティカルセクション削除
	::DeleteCriticalSection(&m_CriticalSection);
}

void CCriticalLock::Lock(void)
{
	// クリティカルセクション取得
	::EnterCriticalSection(&m_CriticalSection);
}

void CCriticalLock::Unlock(void)
{
	// クリティカルセクション開放
	::LeaveCriticalSection(&m_CriticalSection);
}

//////////////////////////////////////////////////////////////////////
// CBlockLock クラスの構築/消滅
//////////////////////////////////////////////////////////////////////

CBlockLock::CBlockLock(CCriticalLock *pCriticalLock)
	: m_pCriticalLock(pCriticalLock)
{
	// ロック取得
	m_pCriticalLock->Lock();
}

CBlockLock::~CBlockLock()
{
	// ロック開放
	m_pCriticalLock->Unlock();
}

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
