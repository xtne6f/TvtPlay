#include <Windows.h>
#include <Shlwapi.h>
#include <vector>
#include <tchar.h>
#include "Util.h"

#ifdef EN_SWC

#include "Caption.h"
#include "bregdef.h"
#include "trex.h"
#include "CaptionAnalyzer.h"

#define MSB(x) ((x) & 0x80000000)

// C1制御符号名(ARIB STD-B24)
static const char *CP_STRING_SIZE_NAME[] = {
    "SSZ", "MSZ", "NSZ", "SZX60", "SZX41", "SZX44", "SZX45", "SZX6B", "SZX64"
};

static void OutputDebugFormat(LPCTSTR format, ...)
{
    TCHAR debug[256];
    va_list args;
    va_start(args, format);
    _vsntprintf_s(debug, _countof(debug), _TRUNCATE, format, args);
    va_end(args);
    ::OutputDebugString(debug);
}

CCaptionAnalyzer::CCaptionAnalyzer()
    : m_hCaptionDll(NULL)
    , m_hBregonigDll(NULL)
    , m_showLate(0)
    , m_clearEarly(0)
    , m_fShowing(false)
    , m_commandRear(0)
    , m_commandFront(0)
    , m_pts(0)
    , m_fEnPts(false)
    , m_pfnUnInitializeCP(NULL)
    , m_pfnAddTSPacketCP(NULL)
    , m_pfnClearCP(NULL)
    , m_pfnGetCaptionDataCP(NULL)
    , m_pfnBMatch(NULL)
    , m_pfnBRegfree(NULL)
{
}

CCaptionAnalyzer::~CCaptionAnalyzer()
{
    UnInitialize();
}

bool CCaptionAnalyzer::Initialize(LPCTSTR captionDllPath, LPCTSTR bregonigDllPath, LPCTSTR blacklistPath, int showLateMsec, int clearEarlyMsec)
{
    if (IsInitialized()) return true;

    if (captionDllPath[0]) {
        m_hCaptionDll = ::LoadLibrary(captionDllPath);
    }
    if (bregonigDllPath[0]) {
        m_hBregonigDll = ::LoadLibrary(bregonigDllPath);
    }
    if (m_hCaptionDll) {
        InitializeUNICODE *pfnInitializeUNICODE;
        pfnInitializeUNICODE     = reinterpret_cast<InitializeUNICODE*>(::GetProcAddress(m_hCaptionDll, "InitializeUNICODE"));
        m_pfnUnInitializeCP      = reinterpret_cast<UnInitializeCP*>(::GetProcAddress(m_hCaptionDll, "UnInitializeCP"));
        m_pfnAddTSPacketCP       = reinterpret_cast<AddTSPacketCP*>(::GetProcAddress(m_hCaptionDll, "AddTSPacketCP"));
        m_pfnClearCP             = reinterpret_cast<ClearCP*>(::GetProcAddress(m_hCaptionDll, "ClearCP"));
        m_pfnGetCaptionDataCP    = reinterpret_cast<GetCaptionDataCP*>(::GetProcAddress(m_hCaptionDll, "GetCaptionDataCP"));
        if (m_hBregonigDll) {
            m_pfnBMatch          = reinterpret_cast<BMatch*>(::GetProcAddress(m_hBregonigDll, BMatch_NAME));
            m_pfnBRegfree        = reinterpret_cast<BRegfree*>(::GetProcAddress(m_hBregonigDll, BRegfree_NAME));
        }
        if (pfnInitializeUNICODE &&
            m_pfnUnInitializeCP &&
            m_pfnAddTSPacketCP &&
            m_pfnClearCP &&
            m_pfnGetCaptionDataCP &&
            (!m_hBregonigDll || m_pfnBMatch && m_pfnBRegfree) &&
            pfnInitializeUNICODE() == TRUE)
        {
            TCHAR *pPatterns = NewReadUtfFileToEnd(blacklistPath, FILE_SHARE_READ);
            if (pPatterns) {
                // コンパイルブロックのリストを作成しておく
                for (const TCHAR *p = pPatterns; *p;) {
                    int len = ::StrCSpn(p, TEXT("\r\n"));
                    if (m_hBregonigDll) {
                        // bregonig
                        if (len < PATTERN_MAX) {
                            RXP_PATTERN pattern;
                            ::lstrcpyn(pattern.str, p, len + 1);
                            pattern.rxp = NULL;
                            m_rxpList.push_back(pattern);
                        }
                    }
                    else {
                        // T-Rex: "m?/pattern/s"となってる場合のみ受けいれる
                        if (len >= 1 && p[0] == TEXT('m')) { ++p; --len; }

                        if (3 <= len && len < PATTERN_MAX+3 && p[0] == p[len-2] && p[len-1] == TEXT('s')) {
                            TCHAR pattern[PATTERN_MAX];
                            ::lstrcpyn(pattern, p + 1, len - 2);
                            LPCTSTR error = NULL;
                            TRex *trex = trex_compile(pattern, &error);
                            if (trex) {
                                m_trexList.push_back(trex);
                            }
                            else {
                                OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): trex_compile %s: %s\n"), error, pattern);
                            }
                        }
                        else {
                            OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): _Blacklist format error\n"));
                        }
                    }
                    p += len;
                    if (*p == TEXT('\r') && *(p+1) == TEXT('\n')) ++p;
                    if (*p) ++p;
                }
                delete [] pPatterns;
            }
            m_showLate = showLateMsec * PCR_PER_MSEC;
            m_clearEarly = clearEarlyMsec * PCR_PER_MSEC;
            ClearShowState();
            return true;
        }
    }

    if (m_hCaptionDll) {
        ::FreeLibrary(m_hCaptionDll);
        m_hCaptionDll = NULL;
    }
    if (m_hBregonigDll) {
        ::FreeLibrary(m_hBregonigDll);
        m_hBregonigDll = NULL;
    }
    return false;
}

void CCaptionAnalyzer::UnInitialize()
{
    if (!IsInitialized()) return;

    if (m_hBregonigDll) {
        std::vector<RXP_PATTERN>::iterator it = m_rxpList.begin();
        for (; it != m_rxpList.end(); ++it) {
            if (it->rxp) m_pfnBRegfree(it->rxp);
        }
        m_rxpList.clear();
        ::FreeLibrary(m_hBregonigDll);
        m_hBregonigDll = NULL;
    }
    else {
        std::vector<TRex*>::iterator it = m_trexList.begin();
        for (; it != m_trexList.end(); ++it) {
            trex_free(*it);
        }
        m_trexList.clear();
    }
    m_pfnUnInitializeCP();
    ::FreeLibrary(m_hCaptionDll);
    m_hCaptionDll = NULL;
}

// PCRが連続でなくなったとき(シークなど)は呼ぶべき
void CCaptionAnalyzer::Clear()
{
    if (!IsInitialized()) return;
    m_pfnClearCP();
    ClearShowState();
}

// 字幕表示状態をクリアする
void CCaptionAnalyzer::ClearShowState()
{
    m_fShowing = false;
    m_commandFront = m_commandRear;
    m_fEnPts = false;
}

// 字幕表示状態かどうか調べる
bool CCaptionAnalyzer::CheckShowState(DWORD currentPcr)
{
    if (!IsInitialized()) return false;

    while (m_commandFront != m_commandRear && !MSB(currentPcr - m_commandPcr[m_commandFront])) {
        if (m_fCommandShow[m_commandFront]) {
            // 字幕表示タイミングに達した
            m_fShowing = true;
        }
        else {
            // 画面消去タイミングに達した
            m_fShowing = false;
        }
        m_commandFront = (m_commandFront + 1) % _countof(m_fCommandShow);
    }
    return m_fShowing;
}

// 字幕ストリームを追加する
void CCaptionAnalyzer::AddPacket(BYTE *pPacket)
{
    if (!IsInitialized()) return;

    TS_HEADER header;
    extract_ts_header(&header, pPacket);

    // 字幕PTSを取得
    if (header.payload_unit_start_indicator &&
        (header.adaptation_field_control&1)/*1,3*/)
    {
        BYTE *pPayload = pPacket + 4;
        if (header.adaptation_field_control == 3) {
            // アダプテーションに続けてペイロードがある
            ADAPTATION_FIELD adapt;
            extract_adaptation_field(&adapt, pPayload);
            pPayload = adapt.adaptation_field_length >= 0 ? pPayload + adapt.adaptation_field_length + 1 : NULL;
        }
        if (pPayload) {
            int payloadSize = 188 - static_cast<int>(pPayload - pPacket);
            PES_HEADER pesHeader;
            extract_pes_header(&pesHeader, pPayload, payloadSize);
            if (pesHeader.packet_start_code_prefix && pesHeader.pts_dts_flags >= 2) {
                m_pts = (DWORD)pesHeader.pts_45khz;
                m_fEnPts = true;
            }
        }
    }

    // 字幕ストリームを解析
    if (m_fEnPts) {
        DWORD dwRet = m_pfnAddTSPacketCP(pPacket);
        if (dwRet == CP_NO_ERR_CAPTION) {
            // 字幕文データ
            CAPTION_DATA_DLL *pList;
            DWORD dwListCount;
            if (m_pfnGetCaptionDataCP(0, &pList, &dwListCount) == TRUE) {
                OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): GetCaptionDataCP Succeeded\n"));
                for (DWORD i = 0; i < dwListCount; ++i, ++pList) {
                    if (pList->bClear) {
                        // 画面消去
                        m_fCommandShow[m_commandRear] = false;
                        m_commandPcr[m_commandRear] = m_clearEarly<0 ? m_pts + pList->dwWaitTime * PCR_PER_MSEC + (DWORD)(-m_clearEarly) :
                                                                       m_pts + pList->dwWaitTime * PCR_PER_MSEC - (DWORD)m_clearEarly;
                        m_commandRear = (m_commandRear + 1) % _countof(m_fCommandShow);
                        OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): %5umsec [CS]\n"), pList->dwWaitTime);
                    }
                    else {
                        // データユニットを連結
                        char utf8[CAPTION_MAX * 3]; // 入力文字すべて3バイト表現と仮定
                        int utf8Count = 0;
                        utf8[0] = 0;
                        for (DWORD j = 0; j < pList->dwListCount; ++j) {
                            // 中型でも標準でもなければ文字サイズを出力
                            DWORD mode = pList->pstCharList[j].wCharSizeMode;
                            bool fPrintMode = mode != CP_STR_MEDIUM && mode != CP_STR_NORMAL && mode < _countof(CP_STRING_SIZE_NAME);

                            if (fPrintMode && utf8Count < (int)_countof(utf8) - 16) {
                                utf8Count += ::wsprintfA(utf8 + utf8Count, "\\<%s>", CP_STRING_SIZE_NAME[mode]);
                            }
                            if (pList->pstCharList[j].pszDecode) {
                                int len = ::lstrlenA(pList->pstCharList[j].pszDecode);
                                int copyLen = min(len, (int)_countof(utf8) - utf8Count - 1);
                                ::lstrcpynA(utf8 + utf8Count, pList->pstCharList[j].pszDecode, copyLen + 1);
                                utf8Count += copyLen;
                            }
                            if (fPrintMode && utf8Count < (int)_countof(utf8) - 16) {
                                utf8Count += ::wsprintfA(utf8 + utf8Count, "\\</%s>", CP_STRING_SIZE_NAME[mode]);
                            }
                        }
                        // UTF-16に変換
                        WCHAR utf16[_countof(utf8)]; // 入力文字すべて1バイト表現と仮定
                        if (::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, utf16, _countof(utf16))) {
                            // パターンマッチした字幕は除外する
                            bool fMatch = false;
                            if (m_hBregonigDll) {
                                // bregonig
                                std::vector<RXP_PATTERN>::iterator it = m_rxpList.begin();
                                for (; it != m_rxpList.end(); ++it) {
                                    TCHAR msg[BREGEXP_MAX_ERROR_MESSAGE_LEN];
                                    int rv = m_pfnBMatch(it->str, utf16, utf16 + ::lstrlen(utf16), &it->rxp, msg);
                                    if (rv > 0) {
                                        fMatch = true;
                                        break;
                                    }
                                    else if (rv < 0) {
                                        OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): BMatch %s: %s\n"), msg, it->str);
                                        break;
                                    }
                                }
                            }
                            else {
                                // T-Rex
                                std::vector<TRex*>::iterator it = m_trexList.begin();
                                for (; it != m_trexList.end(); ++it) {
                                    LPCTSTR outBegin, outEnd;
                                    if (trex_search(*it, utf16, &outBegin, &outEnd) != TRex_False) {
                                        fMatch = true;
                                        break;
                                    }
                                }
                            }
                            if (!fMatch) {
                                // 字幕表示
                                m_fCommandShow[m_commandRear] = true;
                                m_commandPcr[m_commandRear] = m_showLate<0 ? m_pts + pList->dwWaitTime * PCR_PER_MSEC - (DWORD)(-m_showLate) :
                                                                             m_pts + pList->dwWaitTime * PCR_PER_MSEC + (DWORD)m_showLate;
                                m_commandRear = (m_commandRear + 1) % _countof(m_fCommandShow);
                            }
                            OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): %5umsec %s %s\n"), pList->dwWaitTime, fMatch?TEXT("[X]"):TEXT(""), utf16);
                        }
                        else {
                            OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): MultiByteToWideChar Failed!\n"));
                        }
                    }
                }
            }
        }
    }
}

#endif // EN_SWC
