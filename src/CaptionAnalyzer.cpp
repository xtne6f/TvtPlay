#include <Windows.h>
#include <Shlwapi.h>
#include <vector>
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

CCaptionAnalyzer::CCaptionAnalyzer()
    : m_hCaptionDll(NULL)
    , m_hBregonigDll(NULL)
    , m_showLate(0)
    , m_clearEarly(0)
    , m_fShowing(false)
    , m_commandRear(0)
    , m_commandFront(0)
    , m_pcr(0)
    , m_pcrPid(-1)
    , m_pcrPidsLen(0)
    , m_captionPts(0)
    , m_fEnCaptionPts(false)
    , m_captionPid(-1)
    , m_pfnInitializeUNICODE(NULL)
    , m_pfnUnInitializeCP(NULL)
    , m_pfnAddTSPacketCP(NULL)
    , m_pfnClearCP(NULL)
    , m_pfnGetTagInfoCP(NULL)
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
    UnInitialize();

    if (captionDllPath[0]) {
        m_hCaptionDll = ::LoadLibrary(captionDllPath);
    }
    if (bregonigDllPath[0]) {
        m_hBregonigDll = ::LoadLibrary(bregonigDllPath);
    }
    if (m_hCaptionDll) {
        m_pfnInitializeUNICODE   = reinterpret_cast<InitializeUNICODE*>(::GetProcAddress(m_hCaptionDll, "InitializeUNICODE"));
        m_pfnUnInitializeCP      = reinterpret_cast<UnInitializeCP*>(::GetProcAddress(m_hCaptionDll, "UnInitializeCP"));
        m_pfnAddTSPacketCP       = reinterpret_cast<AddTSPacketCP*>(::GetProcAddress(m_hCaptionDll, "AddTSPacketCP"));
        m_pfnClearCP             = reinterpret_cast<ClearCP*>(::GetProcAddress(m_hCaptionDll, "ClearCP"));
        m_pfnGetTagInfoCP        = reinterpret_cast<GetTagInfoCP*>(::GetProcAddress(m_hCaptionDll, "GetTagInfoCP"));
        m_pfnGetCaptionDataCP    = reinterpret_cast<GetCaptionDataCP*>(::GetProcAddress(m_hCaptionDll, "GetCaptionDataCP"));
        if (m_hBregonigDll) {
            m_pfnBMatch          = reinterpret_cast<BMatch*>(::GetProcAddress(m_hBregonigDll, BMatch_NAME));
            m_pfnBRegfree        = reinterpret_cast<BRegfree*>(::GetProcAddress(m_hBregonigDll, BRegfree_NAME));
        }
        if (m_pfnInitializeUNICODE &&
            m_pfnUnInitializeCP &&
            m_pfnAddTSPacketCP &&
            m_pfnClearCP &&
            m_pfnGetTagInfoCP &&
            m_pfnGetCaptionDataCP &&
            (!m_hBregonigDll || m_pfnBMatch && m_pfnBRegfree) &&
            m_pfnInitializeUNICODE() == CP_NO_ERR)
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
                                TCHAR debug[128 + _countof(pattern)];
                                ::wsprintf(debug, TEXT("CCaptionAnalyzer::Initialize(): trex_compile %.31s: %s\n"), error, pattern);
                                ::OutputDebugString(debug);
                            }
                        }
                        else {
                            ::OutputDebugString(TEXT("CCaptionAnalyzer::Initialize(): _Blacklist format error\n"));
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
    if (m_hCaptionDll) {
        m_pfnUnInitializeCP();
        ::FreeLibrary(m_hCaptionDll);
        m_hCaptionDll = NULL;
    }
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
    ClearShowState();
}

// 字幕表示状態をクリアする
// PCRが連続でなくなったとき(シークなど)は呼ぶべき
void CCaptionAnalyzer::ClearShowState()
{
    m_fShowing = false;
    m_commandFront = m_commandRear;
    m_fEnCaptionPts = false;

    // PCR参照PIDをクリア
    m_pcrPid = -1;
    m_pcrPidsLen = 0;
}

// 字幕表示状態かどうか調べる
bool CCaptionAnalyzer::CheckShowState()
{
    if (!IsInitialized() || m_pcrPid < 0) return false;

    while (m_commandFront != m_commandRear && !MSB(m_pcr - m_commandPcr[m_commandFront])) {
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

void CCaptionAnalyzer::AddPacket(BYTE *pPacket)
{
    if (!IsInitialized()) return;

    TS_HEADER header;
    extract_ts_header(&header, pPacket);

    // PCRの取得手順はCTsSenderと同じ
    if ((header.adaptation_field_control&2)/*2,3*/ &&
        !header.transport_error_indicator)
    {
        // アダプテーションフィールドがある
        ADAPTATION_FIELD adapt;
        extract_adaptation_field(&adapt, pPacket + 4);

        if (adapt.pcr_flag) {
            // 参照PIDが決まっていないとき、最初に3回PCRが出現したPIDを参照PIDとする
            // 参照PIDのPCRが現れることなく5回別のPCRが出現すれば、参照PIDを変更する
            if (header.pid != m_pcrPid) {
                bool fFound = false;
                for (int i = 0; i < m_pcrPidsLen; i++) {
                    if (m_pcrPids[i] == header.pid) {
                        ++m_pcrPidCounts[i];
                        if (m_pcrPid < 0 && m_pcrPidCounts[i] >= 3 || m_pcrPidCounts[i] >= 5) m_pcrPid = header.pid;
                        fFound = true;
                        break;
                    }
                }
                if (!fFound && m_pcrPidsLen < PCR_PIDS_MAX) {
                    m_pcrPids[m_pcrPidsLen] = header.pid;
                    m_pcrPidCounts[m_pcrPidsLen] = 1;
                    m_pcrPidsLen++;
                }
            }
            // 参照PIDのときはPCRを取得する
            if (header.pid == m_pcrPid) {
                m_pcrPidsLen = 0;
                m_pcr = (DWORD)adapt.pcr_45khz;
            }
        }
    }

    // 字幕PTSを取得
    if (header.pid == m_captionPid &&
        header.payload_unit_start_indicator &&
        (header.adaptation_field_control&1)/*1,3*/ &&
        !header.transport_scrambling_control &&
        !header.transport_error_indicator)
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
                m_captionPts = (DWORD)pesHeader.pts_45khz;
                m_fEnCaptionPts = true;
            }
        }
    }

    // 字幕ストリームを解析
    if (header.pid == m_captionPid &&
        m_pcrPid >= 0 && m_fEnCaptionPts &&
        !header.transport_scrambling_control &&
        !header.transport_error_indicator)
    {
        DWORD dwRet = m_pfnAddTSPacketCP(pPacket);
        if (dwRet == CP_NO_ERR_CAPTION) {
            // 字幕文データ
            CAPTION_DATA_DLL *pList;
            DWORD dwListCount;
            if (m_pfnGetCaptionDataCP(0, &pList, &dwListCount) == TRUE) {
                ::OutputDebugString(TEXT("CCaptionAnalyzer::AddPacket(): GetCaptionDataCP Succeeded\n"));
                CheckShowState();
                for (DWORD i = 0; i < dwListCount; ++i, ++pList) {
                    if (pList->bClear) {
                        // 画面消去
                        m_fCommandShow[m_commandRear] = false;
                        m_commandPcr[m_commandRear] = m_captionPts + pList->dwWaitTime * PCR_PER_MSEC - m_clearEarly;
                        m_commandRear = (m_commandRear + 1) % _countof(m_fCommandShow);

                        TCHAR debug[128];
                        ::wsprintf(debug, TEXT("CCaptionAnalyzer::AddPacket(): %5umsec [CS]\n"), pList->dwWaitTime);
                        ::OutputDebugString(debug);
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
                                        TCHAR debug[128 + _countof(msg) + _countof(it->str)];
                                        ::wsprintf(debug, TEXT("CCaptionAnalyzer::AddPacket(): BMatch %s: %s\n"), msg, it->str);
                                        ::OutputDebugString(debug);
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
                                m_commandPcr[m_commandRear] = m_captionPts + pList->dwWaitTime * PCR_PER_MSEC + m_showLate;
                                m_commandRear = (m_commandRear + 1) % _countof(m_fCommandShow);
                            }
                            TCHAR debug[128 + _countof(utf16)];
                            ::wsprintf(debug, TEXT("CCaptionAnalyzer::AddPacket(): %5umsec %s "), pList->dwWaitTime, fMatch ? TEXT("[X]") : TEXT(""));
                            ::lstrcat(debug, utf16);
                            ::lstrcat(debug, TEXT("\n"));
                            ::OutputDebugString(debug);
                        }
                        else {
                            ::OutputDebugString(TEXT("CCaptionAnalyzer::AddPacket(): MultiByteToWideChar Failed!\n"));
                        }
                    }
                }
            }
        }
    }
}

#endif // EN_SWC
