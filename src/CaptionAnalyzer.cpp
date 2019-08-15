#include <Windows.h>
#include <Shlwapi.h>
#include <vector>
#include <tchar.h>
#include "Util.h"

#ifdef EN_SWC

#include "Caption.h"
#include <regex>
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
    : m_hCaptionDll(nullptr)
    , m_showLate(0)
    , m_clearEarly(0)
    , m_fShowing(false)
    , m_commandRear(0)
    , m_commandFront(0)
    , m_pts(0)
    , m_fEnPts(false)
    , m_pfnUnInitializeCP(nullptr)
    , m_pfnAddTSPacketCP(nullptr)
    , m_pfnClearCP(nullptr)
    , m_pfnGetCaptionDataCP(nullptr)
{
}

CCaptionAnalyzer::~CCaptionAnalyzer()
{
    UnInitialize();
}

bool CCaptionAnalyzer::Initialize(LPCTSTR captionDllPath, LPCTSTR blacklistPath, int showLateMsec, int clearEarlyMsec)
{
    if (IsInitialized()) return true;

    if (captionDllPath[0]) {
        m_hCaptionDll = ::LoadLibrary(captionDllPath);
    }
    if (m_hCaptionDll) {
        InitializeUNICODE *pfnInitializeUNICODE;
        pfnInitializeUNICODE     = reinterpret_cast<InitializeUNICODE*>(::GetProcAddress(m_hCaptionDll, "InitializeUNICODE"));
        m_pfnUnInitializeCP      = reinterpret_cast<UnInitializeCP*>(::GetProcAddress(m_hCaptionDll, "UnInitializeCP"));
        m_pfnAddTSPacketCP       = reinterpret_cast<AddTSPacketCP*>(::GetProcAddress(m_hCaptionDll, "AddTSPacketCP"));
        m_pfnClearCP             = reinterpret_cast<ClearCP*>(::GetProcAddress(m_hCaptionDll, "ClearCP"));
        m_pfnGetCaptionDataCP    = reinterpret_cast<GetCaptionDataCP*>(::GetProcAddress(m_hCaptionDll, "GetCaptionDataCP"));
        if (pfnInitializeUNICODE &&
            m_pfnUnInitializeCP &&
            m_pfnAddTSPacketCP &&
            m_pfnClearCP &&
            m_pfnGetCaptionDataCP &&
            pfnInitializeUNICODE() == TRUE)
        {
            std::vector<WCHAR> patterns = ReadUtfFileToEnd(blacklistPath, FILE_SHARE_READ);
            if (!patterns.empty()) {
                // コンパイルブロックのリストを作成しておく
                const std::basic_regex<TCHAR> rePattern(TEXT("m?(.)(.+?)\\1s"));
                std::match_results<LPCTSTR> m;
                for (const TCHAR *p = &patterns.front(); *p;) {
                    int len = ::StrCSpn(p, TEXT("\r\n"));
                    if (std::regex_match(p, &p[len], m, rePattern)) {
                        std::basic_regex<TCHAR> re;
                        try {
                            re.assign(m[2].first, m[2].length());
                            m_reList.push_back(re);
                        }
                        catch (std::regex_error&) {
                            OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): regex_assign error\n"));
                        }
                    }
                    else {
                        OutputDebugFormat(TEXT(__FUNCTION__) TEXT("(): _Blacklist format error\n"));
                    }
                    p += len;
                    if (*p == TEXT('\r') && *(p+1) == TEXT('\n')) ++p;
                    if (*p) ++p;
                }
            }
            m_showLate = showLateMsec * PCR_PER_MSEC;
            m_clearEarly = clearEarlyMsec * PCR_PER_MSEC;
            ClearShowState();
            return true;
        }
    }

    if (m_hCaptionDll) {
        ::FreeLibrary(m_hCaptionDll);
        m_hCaptionDll = nullptr;
    }
    return false;
}

void CCaptionAnalyzer::UnInitialize()
{
    if (!IsInitialized()) return;

    m_reList.clear();
    m_pfnUnInitializeCP();
    ::FreeLibrary(m_hCaptionDll);
    m_hCaptionDll = nullptr;
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
            pPayload = adapt.adaptation_field_length >= 0 ? pPayload + adapt.adaptation_field_length + 1 : nullptr;
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
                            std::vector<std::basic_regex<TCHAR>>::const_iterator it = m_reList.begin();
                            for (; it != m_reList.end(); ++it) {
                                if (std::regex_match(utf16, *it)) {
                                    fMatch = true;
                                    break;
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
