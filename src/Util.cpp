﻿#include <Windows.h>
#include "Util.h"


#if 1 // 同一プロセスからTvtAudioStretchFilterへのメッセージ送信コード
#define ASFLT_FILTER_NAME   TEXT("TvtAudioStretchFilter")

HWND ASFilterFindWindow()
{
    TCHAR szName[128];
    _stprintf_s(szName, TEXT("%s,%lu"), ASFLT_FILTER_NAME, ::GetCurrentProcessId());
    return ::FindWindowEx(HWND_MESSAGE, nullptr, ASFLT_FILTER_NAME, szName);
}

LRESULT ASFilterSendMessageTimeout(UINT Msg, WPARAM wParam, LPARAM lParam, UINT uTimeout)
{
    HWND hwnd = ASFilterFindWindow();
    DWORD_PTR dwResult;
    return hwnd && ::SendMessageTimeout(hwnd, Msg, wParam, lParam, SMTO_NORMAL,
                                        uTimeout, &dwResult) ? dwResult : FALSE;
}

BOOL ASFilterSendNotifyMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    HWND hwnd = ASFilterFindWindow();
    return hwnd ? ::SendNotifyMessage(hwnd, Msg, wParam, lParam) : FALSE;
}
#endif


// 必要なバッファを確保してGetPrivateProfileSection()を呼ぶ
std::vector<TCHAR> GetPrivateProfileSectionBuffer(LPCTSTR lpAppName, LPCTSTR lpFileName)
{
    std::vector<TCHAR> buf(4096);
    for (;;) {
        DWORD len = GetPrivateProfileSection(lpAppName, buf.data(), static_cast<DWORD>(buf.size()), lpFileName);
        if (len < buf.size() - 2) {
            buf.resize(len + 1);
            break;
        }
        if (buf.size() >= READ_FILE_MAX_SIZE / 2) {
            buf.assign(1, TEXT('\0'));
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return buf;
}


// GetPrivateProfileSection()で取得したバッファから、キーに対応する文字列を取得する
void GetBufferedProfileString(LPCTSTR lpBuff, LPCTSTR lpKeyName, LPCTSTR lpDefault, LPTSTR lpReturnedString, DWORD nSize)
{
    size_t nKeyLen = _tcslen(lpKeyName);
    while (*lpBuff) {
        size_t nLen = _tcslen(lpBuff);
        if (!_tcsnicmp(lpBuff, lpKeyName, nKeyLen) && lpBuff[nKeyLen] == TEXT('=')) {
            if ((lpBuff[nKeyLen + 1] == TEXT('\'') || lpBuff[nKeyLen + 1] == TEXT('"')) &&
                nLen >= nKeyLen + 3 && lpBuff[nKeyLen + 1] == lpBuff[nLen - 1])
            {
                _tcsncpy_s(lpReturnedString, nSize, lpBuff + nKeyLen + 2, min(nLen - nKeyLen - 3, static_cast<size_t>(nSize - 1)));
            }
            else {
                _tcsncpy_s(lpReturnedString, nSize, lpBuff + nKeyLen + 1, _TRUNCATE);
            }
            return;
        }
        lpBuff += nLen + 1;
    }
    _tcsncpy_s(lpReturnedString, nSize, lpDefault, _TRUNCATE);
}


// GetPrivateProfileSection()で取得したバッファから、キーに対応する数値を取得する
int GetBufferedProfileInt(LPCTSTR lpBuff, LPCTSTR lpKeyName, int nDefault)
{
    TCHAR sz[16];
    GetBufferedProfileString(lpBuff, lpKeyName, TEXT(""), sz, _countof(sz));
    LPTSTR endp;
    int nRet = _tcstol(sz, &endp, 10);
    return endp == sz ? nDefault : nRet;
}


// UTF-16またはUTF-8テキストファイルを文字列として全て読む
std::vector<WCHAR> ReadUtfFileToEnd(LPCTSTR fileName, DWORD dwShareMode, bool fNoBomUseAcp)
{
    std::vector<BYTE> buf;
    HANDLE hFile = ::CreateFile(fileName, GENERIC_READ, dwShareMode,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD size = ::GetFileSize(hFile, nullptr);
        if (size != INVALID_FILE_SIZE && size < READ_FILE_MAX_SIZE) {
            buf.resize(size + 3);
            DWORD numRead;
            if (size == 0 || ::ReadFile(hFile, buf.data(), size, &numRead, nullptr) && numRead == size) {
                buf[size] = buf[size + 1] = buf[size + 2] = 0;
            }
            else {
                buf.clear();
            }
        }
        ::CloseHandle(hFile);
    }
    std::vector<WCHAR> ret;
    if (buf.empty()) return ret;

    // BOM付きUTF-16LE、UTF-16BE、UTF-8、またはBOM無しを判別する
    int codeType = buf[0]==0xFF && buf[1]==0xFE ? 1 :
                   buf[0]==0xFE && buf[1]==0xFF ? 2 : 0;
    int bomOffset = codeType ? 2 : buf[0]==0xEF && buf[1]==0xBB && buf[2]==0xBF ? 3 : 0;

    // 文字コード変換
    if (codeType) {
        for (size_t i = 0; ; ++i) {
            if (!buf[bomOffset + i * 2] && !buf[bomOffset + i * 2 + 1]) {
                ret.resize(i + 1);
                break;
            }
        }
        for (size_t i = 0; i < ret.size(); ++i) {
            ret[i] = codeType == 1 ? buf[bomOffset + i * 2] | buf[bomOffset + i * 2 + 1] << 8
                                   : buf[bomOffset + i * 2] << 8 | buf[bomOffset + i * 2 + 1];
        }
    }
    else {
        int retSize = ::MultiByteToWideChar(fNoBomUseAcp && !bomOffset ? CP_ACP : CP_UTF8, 0,
                                            reinterpret_cast<LPCSTR>(&buf[bomOffset]), -1, nullptr, 0);
        if (retSize > 0) {
            ret.resize(retSize);
            if (!::MultiByteToWideChar(fNoBomUseAcp && !bomOffset ? CP_ACP : CP_UTF8, 0,
                                       reinterpret_cast<LPCSTR>(&buf[bomOffset]), -1, ret.data(), retSize))
            {
                ret.clear();
            }
        }
    }
    return ret;
}


// 文字列をBOM付きUTF-8テキストファイルとして書き込む
bool WriteUtfFileToEnd(LPCTSTR fileName, DWORD dwShareMode, const WCHAR *pStr)
{
    // 出力サイズ算出
    int bufSize = ::WideCharToMultiByte(CP_UTF8, 0, pStr, -1, nullptr, 0, nullptr, nullptr);
    if (bufSize <= 0) return false;

    // 文字コード変換(NULL文字含む)
    std::vector<BYTE> buf(3 + bufSize);
    buf[0] = 0xEF; buf[1] = 0xBB; buf[2] = 0xBF;
    bufSize = ::WideCharToMultiByte(CP_UTF8, 0, pStr, -1, reinterpret_cast<LPSTR>(&buf[3]), bufSize, nullptr, nullptr);
    if (bufSize <= 0) return false;

    HANDLE hFile = ::CreateFile(fileName, GENERIC_WRITE, dwShareMode,
                                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written;
    if (!::WriteFile(hFile, buf.data(), bufSize + 3 - 1, &written, nullptr)) {
        ::CloseHandle(hFile);
        return false;
    }
    ::CloseHandle(hFile);
    return true;
}


static void IconIndexToPos(int *pX, int *pW, HDC hdc, int idx, int iconMaxWidth)
{
    *pX = 0;
    *pW = iconMaxWidth;
    for (int x=0, i=0; i <= idx; i++) {
        if (::GetPixel(hdc, x, 0) == RGB(0,0,0)) {
            *pW = 1;
            while (*pW < iconMaxWidth && ::GetPixel(hdc, x + (*pW)++, 0) == RGB(0,0,0));
        }
        else {
            *pW = iconMaxWidth;
        }
        *pX = x;
        x += *pW;
    }
}


static int DrawMonoColorIcon(HDC hdcDest, int destX, int destY, HDC hdcSrc, int idx, int iconSize, bool fInvert)
{
    int x, width;
    IconIndexToPos(&x, &width, hdcSrc, idx, iconSize);
    int ofsY = width == iconSize ? 0 : 1;

    if (fInvert) {
        ::BitBlt(hdcDest, destX, destY+ofsY, width, iconSize-ofsY, hdcSrc, x, ofsY, MERGEPAINT);
    }
    else {
        ::PatBlt(hdcDest, destX, destY+ofsY, width, iconSize-ofsY, DSTINVERT);
        ::BitBlt(hdcDest, destX, destY+ofsY, width, iconSize-ofsY, hdcSrc, x, ofsY, MERGEPAINT);
        ::PatBlt(hdcDest, destX, destY+ofsY, width, iconSize-ofsY, DSTINVERT);
    }
    return width;
}


bool ComposeMonoColorIcon(HDC hdcDest, int destX, int destY, HBITMAP hbm, LPCTSTR pIdxList, int iconSize)
{
    if (!hdcDest || !hbm) return false;

    HDC hdcMem = ::CreateCompatibleDC(hdcDest);
    if (!hdcMem) return false;
    HGDIOBJ hgdiOld = ::SelectObject(hdcMem, hbm);

    LPCTSTR p = pIdxList + _tcscspn(pIdxList, TEXT(",:-~'"));
    if (!*p || *p == TEXT(',')) {
        // indexが1つだけ指定されている場合
        int idx = _tcstol(pIdxList, nullptr, 10);
        ::PatBlt(hdcDest, destX, destY, iconSize, iconSize, WHITENESS);
        DrawMonoColorIcon(hdcDest, destX, destY, hdcMem, min(max(idx,0),255), iconSize, false);

        ::PatBlt(hdcDest, destX + iconSize, destY, iconSize, iconSize, WHITENESS);
        DrawMonoColorIcon(hdcDest, destX + iconSize, destY, hdcMem, min(max(idx+1,0),255), iconSize, false);
    }
    else {
        p = pIdxList;
        for (int i = 0; *p && *p != TEXT(','); i++) {
            bool fInvert = false;
            int x = destX + i * iconSize;
            ::PatBlt(hdcDest, x, destY, iconSize, iconSize, WHITENESS);
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
                    idx = _tcstol(p, nullptr, 10);
                    p += _tcscspn(p, TEXT(",:-~'"));
                }
                width = DrawMonoColorIcon(hdcDest, x, destY, hdcMem, min(max(idx,0),255), iconSize, fInvert);
                x += width == iconSize ? 0 : width - 1;
                if (*p == TEXT('-')) p++;
            }
            if (*p == TEXT(':')) p++;
        }
    }
    ::SelectObject(hdcMem, hgdiOld);
    ::DeleteDC(hdcMem);
    return true;
}


BOOL WritePrivateProfileInt(LPCTSTR lpAppName, LPCTSTR lpKeyName, int value, LPCTSTR lpFileName)
{
    TCHAR sz[16];
    _stprintf_s(sz, TEXT("%d"), value);
    return ::WritePrivateProfileString(lpAppName, lpKeyName, sz, lpFileName);
}


// MD5ハッシュ値を計算して先頭56bitを返す
// 失敗時は負を返す
LONGLONG CalcHash(const LPBYTE pbData, DWORD dwDataLen, DWORD dwSalt, LONGLONG *pHead2kbHash)
{
    HCRYPTPROV hProv;
    LONGLONG llRet = -1;
    if (pHead2kbHash) {
        *pHead2kbHash = -1;
    }
    if (::CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        HCRYPTHASH hHash;
        if (::CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            BYTE pbHash[16];
            DWORD dwHashLen = 16;
            // リトルエンディアンを仮定
            if (::CryptHashData(hHash, (LPBYTE)&dwSalt, sizeof(dwSalt), 0) &&
                ::CryptHashData(hHash, pbData, dwDataLen, 0) &&
                ::CryptGetHashParam(hHash, HP_HASHVAL, pbHash, &dwHashLen, 0)) {
                llRet = ((LONGLONG)pbHash[0]<<48) | ((LONGLONG)pbHash[1]<<40) | ((LONGLONG)pbHash[2]<<32) |
                        ((LONGLONG)pbHash[3]<<24) | (pbHash[4]<<16) | (pbHash[5]<<8) | pbHash[6];
            }
            ::CryptDestroyHash(hHash);
        }
        if (pHead2kbHash && ::CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
            // 先頭2KBのハッシュ値も返す
            BYTE pbHash[16];
            DWORD dwHashLen = 16;
            if (::CryptHashData(hHash, (LPBYTE)&dwSalt, sizeof(dwSalt), 0) &&
                ::CryptHashData(hHash, pbData, min(dwDataLen, 2048), 0) &&
                ::CryptGetHashParam(hHash, HP_HASHVAL, pbHash, &dwHashLen, 0)) {
                *pHead2kbHash = ((LONGLONG)pbHash[0]<<48) | ((LONGLONG)pbHash[1]<<40) | ((LONGLONG)pbHash[2]<<32) |
                                ((LONGLONG)pbHash[3]<<24) | (pbHash[4]<<16) | (pbHash[5]<<8) | pbHash[6];
            }
            ::CryptDestroyHash(hHash);
        }
        ::CryptReleaseContext(hProv, 0);
    }
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
    int pos;
    size_t pmt_pos, i;
    int pid;
    const unsigned char *table;

    extract_psi(&pat->psi, payload, payload_size, unit_start, counter);

    if (pat->psi.version_number &&
        pat->psi.current_next_indicator &&
        pat->psi.table_id == 0 &&
        pat->psi.section_length >= 5)
    {
        // PAT更新
        table = pat->psi.data + 1 + pat->psi.pointer_field;
        pat->transport_stream_id = (table[3]<<8) | table[4];
        pat->version_number = pat->psi.version_number;

        // 受信済みPMTを調べ、必要ならば新規に生成する
        pmt_pos = 0;
        pos = 3 + 5;
        while (pos + 3 < 3 + pat->psi.section_length - 4/*CRC32*/) {
            program_number = (table[pos]<<8) | (table[pos+1]);
            if (program_number != 0) {
                pid = ((table[pos+2]&0x1f)<<8) | table[pos+3];
                for (i = pmt_pos; i < pat->pmt.size(); ++i) {
                    if (pat->pmt[i].pmt_pid == pid) {
                        if (i != pmt_pos) {
                            PMT sw = pat->pmt[i];
                            pat->pmt[i] = pat->pmt[pmt_pos];
                            pat->pmt[pmt_pos] = sw;
                        }
                        ++pmt_pos;
                        break;
                    }
                }
                if (i == pat->pmt.size()) {
                    pat->pmt.insert(pat->pmt.begin() + pmt_pos, PMT());
                    pat->pmt[pmt_pos++].pmt_pid = pid;
                }
            }
            pos += 4;
        }
        // PATから消えたPMTを破棄する
        pat->pmt.resize(pmt_pos);
    }
}


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
        while (pos + 4 < 3 + pmt->psi.section_length - 4/*CRC32*/) {
            stream_type = table[pos];
            if (stream_type == H_262_VIDEO ||
                stream_type == PES_PRIVATE_DATA ||
                stream_type == ADTS_TRANSPORT ||
                stream_type == AVC_VIDEO ||
                stream_type == H_265_VIDEO ||
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
	while( tail-buf > 188 ){
		if(buf[0] != 0x47){
			buf += 1;
			continue;
		}
		m = 320;
		if( tail-buf < m){
			m = (int)(tail-buf);
		}
		for(i=188;i<m;i++){
			if(buf[i] == 0x47 && (tail-buf <= i+i || buf[i+i] == 0x47)){
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

	return nullptr;
}

void extract_adaptation_field(ADAPTATION_FIELD *dst, const unsigned char *data)
{
	const unsigned char *p;
	const unsigned char *tail;

	p = data;
	
	memset(dst, 0, sizeof(ADAPTATION_FIELD));
	if( p[0] == 0 ){
		// a single stuffing byte
		dst->adaptation_field_length = 0;
		return;
	}
	if( p[0] > 183 ){
		dst->adaptation_field_length = -1;
		return;
	}

	dst->adaptation_field_length = p[0];
	p += 1;
	tail = p + dst->adaptation_field_length;
	if( (p+1) > tail ){
		memset(dst, 0, sizeof(ADAPTATION_FIELD));
		dst->adaptation_field_length = -1;
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
			dst->adaptation_field_length = -1;
			return;
		}
		dst->pcr_45khz = ((unsigned int)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
		p += 6;
	}
}
#endif


#if 1 // From: TVTest_0.7.23_Src/Util.cpp
COLORREF MixColor(COLORREF Color1,COLORREF Color2,BYTE Ratio)
{
	return RGB((GetRValue(Color1)*Ratio+GetRValue(Color2)*(255-Ratio))/255,
			   (GetGValue(Color1)*Ratio+GetGValue(Color2)*(255-Ratio))/255,
			   (GetBValue(Color1)*Ratio+GetBValue(Color2)*(255-Ratio))/255);
}
#endif
