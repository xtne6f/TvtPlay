// TVTestにtsファイル再生機能を追加するプラグイン
// 最終更新: 2012-05-08
// 署名: 849fa586809b0d16276cd644c6749503
#include <Windows.h>
#include <WindowsX.h>
#include <Shlwapi.h>
#include <algorithm>
#include <list>
#include <vector>
#include <map>
#include "Util.h"
#include "Settings.h"
#include "ColorScheme.h"
#include "StatusView.h"
#include "AsyncFileReader.h"
#include "TsSender.h"
#include "Playlist.h"
#include "ChapterMap.h"

#ifdef EN_SWC
#include "Caption.h"
#include "bregdef.h"
#include "trex.h"
#include "CaptionAnalyzer.h"
#endif

#include "resource.h"
#include "TvtPlayUtil.h"
#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0,0,13)
#include "TVTestPlugin.h"
#include "TvtPlay.h"

static LPCWSTR INFO_PLUGIN_NAME = L"TvtPlay_v.1.8.r2";
static LPCWSTR INFO_DESCRIPTION = L"ファイル再生機能を追加 (ver.1.8r2)";
static const int INFO_VERSION = 21;

#define WM_UPDATE_STATUS    (WM_APP + 1)
#define WM_QUERY_CLOSE_NEXT (WM_APP + 2)
#define WM_QUERY_SEEK_BGN   (WM_APP + 3)
#define WM_QUERY_RESET      (WM_APP + 4)
#define WM_SATISFIED_POS_GT (WM_APP + 5)

#define WM_TS_SET_UDP       (WM_APP + 1)
#define WM_TS_SET_PIPE      (WM_APP + 2)
#define WM_TS_PAUSE         (WM_APP + 3)
#define WM_TS_SEEK_BGN      (WM_APP + 4)
#define WM_TS_SEEK_END      (WM_APP + 5)
#define WM_TS_SEEK          (WM_APP + 6)
#define WM_TS_SEEK_ABSOLUTE (WM_APP + 7)
#define WM_TS_SET_SPEED     (WM_APP + 8)
#define WM_TS_SET_MOD_TS    (WM_APP + 9)
#define WM_TS_WATCH_POS_GT  (WM_APP + 10)

static LPCTSTR SETTINGS = TEXT("Settings");
static LPCTSTR TVTPLAY_FRAME_WINDOW_CLASS = TEXT("TvtPlay Frame");
static LPCSTR UDP_ADDR = "127.0.0.1";
static LPCTSTR PIPE_NAME = TEXT("\\\\.\\pipe\\BonDriver_Pipe%02d");

enum {
    TIMER_ID_AUTO_HIDE = 1,
    TIMER_ID_RESET_DROP,
    TIMER_ID_UPDATE_HASH_LIST,
    TIMER_ID_SYNC_CHAPTER,
    TIMER_ID_WATCH_POS_GT,
};

static const TVTest::CommandInfo COMMAND_LIST[] = {
    {ID_COMMAND_OPEN, L"Open", L"ファイルを開く"},
    {ID_COMMAND_OPEN_POPUP, L"OpenPopup", L"ファイルを開く(ポップアップ)"},
    {ID_COMMAND_LIST_POPUP, L"ListPopup", L"再生リストを開く(ポップアップ)"},
    {ID_COMMAND_CLOSE, L"Close", L"ファイルを閉じる"},
    {ID_COMMAND_PREV, L"Prev", L"前のファイル/先頭にシーク"},
    {ID_COMMAND_SEEK_BGN, L"SeekToBgn", L"先頭にシーク"},
    {ID_COMMAND_SEEK_END, L"SeekToEnd", L"次のファイル/末尾にシーク"},
    {ID_COMMAND_SEEK_PREV, L"SeekToPrev", L"前のチャプター"},
    {ID_COMMAND_SEEK_NEXT, L"SeekToNext", L"次のチャプター"},
    {ID_COMMAND_ADD_CHAPTER, L"AddChapter", L"チャプターを追加"},
    {ID_COMMAND_REPEAT_CHAPTER, L"RepeatChapter", L"チャプターリピート/しない"},
    {ID_COMMAND_SKIP_X_CHAPTER, L"SkipXChapter", L"チャプタースキップ/しない"},
    {ID_COMMAND_LOOP, L"Loop", L"全体/シングル/リピートしない"},
    {ID_COMMAND_PAUSE, L"Pause", L"一時停止/再生"},
    {ID_COMMAND_NOP, L"Nop", L"何もしない"},
    {ID_COMMAND_STRETCH, L"Stretch", L"倍速:切り替え"},
    {ID_COMMAND_STRETCH_RE, L"StretchRe", L"倍速:逆順切り替え"},
};

static const int DEFAULT_SEEK_LIST[COMMAND_S_MAX] = {
    -60000, -30000, -15000, -5000, 4000, 14000, 29000, 59000
};

static const int DEFAULT_STRETCH_LIST[COMMAND_S_MAX] = {
    400, 200, 50, 25
};

static LPCTSTR DEFAULT_BUTTON_LIST[] = {
    TEXT("0,Open"), TEXT(";1,Close"), TEXT("4:5:14,Loop"),
    TEXT(";9,Prev  2,SeekToBgn"),
    TEXT("'-'6'0,SeekA"), TEXT(";'-'3'0,SeekB"),
    TEXT("'-'1'5,SeekC"), TEXT("'-' '5,SeekD"),
    TEXT("6,Pause"),
    TEXT("'+' '5,SeekE"), TEXT("'+'1'5,SeekF"),
    TEXT(";'+'3'0,SeekG"), TEXT("'+'6'0,SeekH"),
    TEXT("3,SeekToEnd"),
    TEXT("'*'2'.'0:30~'*'2'.'0,StretchB"),
    TEXT("'*'0'.'5:30~'*'0'.'5,StretchC"),
    TEXT(";'*'1' '.'0:30~'*'4'.'0:30~'*'0'.'2,StretchD,StretchA"),
    TEXT(";'*'1' '.'0:30~'*'4'.'0:30~'*'2'.'0:30~'*'0'.'5:30~'*'0'.'2,Width=32,StretchZ,Stretch"),
    NULL
};


CTvtPlay::CTvtPlay()
    : m_fInitialized(false)
    , m_fSettingsLoaded(false)
    , m_fForceEnable(false)
    , m_fIgnoreExt(false)
    , m_fAutoEnUdp(false)
    , m_fAutoEnPipe(false)
    , m_fEventExecute(false)
    , m_fEventStartupDone(false)
    , m_fPausedOnPreviewChange(false)
    , m_specOffset(-1)
    , m_fShowOpenDialog(false)
    , m_fRaisePriority(false)
    , m_hwndFrame(NULL)
    , m_fAutoClose(false)
    , m_fAutoHide(false)
    , m_fAutoHideActive(false)
    , m_fHoveredFromOutside(false)
    , m_statusRow(0)
    , m_statusRowFull(0)
    , m_statusHeight(0)
    , m_fSeekDrawOfs(false)
    , m_fSeekDrawTot(false)
    , m_fPosDrawTot(false)
    , m_seekItemMinWidth(0)
    , m_posItemWidth(0)
    , m_timeoutOnCmd(0)
    , m_timeoutOnMove(0)
    , m_seekItemOrder(0)
    , m_posItemOrder(0)
    , m_dispCount(0)
    , m_lastDropCount(0)
    , m_resetDropInterval(0)
    , m_seekListNum(0)
    , m_stretchListNum(0)
    , m_popupMax(0)
    , m_fPopupDesc(false)
    , m_fPopuping(false)
    , m_fDialogOpen(false)
    , m_hThread(NULL)
    , m_hThreadEvent(NULL)
    , m_threadID(0)
    , m_threadPriority(THREAD_PRIORITY_NORMAL)
    , m_infoPos(0)
    , m_infoDur(0)
    , m_infoTot(-1)
    , m_infoExtMode(0)
    , m_infoSpeed(100)
    , m_fInfoPaused(false)
    , m_fHalt(false)
    , m_fAllRepeat(false)
    , m_fSingleRepeat(false)
    , m_fRepeatChapter(false)
    , m_fSkipXChapter(false)
    , m_readBufSizeKB(0)
    , m_supposedDispDelay(0)
    , m_resetMode(0)
    , m_stretchMode(0)
    , m_noMuteMax(0)
    , m_noMuteMin(0)
    , m_fConvTo188(false)
    , m_fUnderrunCtrl(false)
    , m_fUseQpc(false)
    , m_fModTimestamp(false)
    , m_pcrThresholdMsec(0)
    , m_salt(0)
    , m_hashListMax(0)
    , m_fUpdateHashList(false)
#ifdef EN_SWC
    , m_slowerWithCaption(0)
    , m_swcShowLate(0)
    , m_swcClearEarly(0)
#endif
{
    m_szIniFileName[0] = 0;
    m_szSpecFileName[0] = 0;
    m_szIconFileName[0] = 0;
    m_szPopupPattern[0] = 0;
    m_szChaptersDirName[0] = 0;
#ifdef EN_SWC
    m_szCaptionDllPath[0] = 0;
    m_szBregonigDllPath[0] = 0;
#endif
    m_lastCurPos.x = m_lastCurPos.y = 0;
    m_idleCurPos.x = m_idleCurPos.y = 0;
    CStatusView::Initialize(g_hinstDLL);
}


bool CTvtPlay::GetPluginInfo(TVTest::PluginInfo *pInfo)
{
    // プラグインの情報を返す
    pInfo->Type           = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags          = TVTest::PLUGIN_FLAG_DISABLEONSTART;
    pInfo->pszPluginName  = INFO_PLUGIN_NAME;
    pInfo->pszCopyright   = L"Public Domain";
    pInfo->pszDescription = INFO_DESCRIPTION;
    return true;
}


void CTvtPlay::AnalyzeCommandLine(LPCWSTR cmdLine, bool fIgnoreFirst)
{
    m_szSpecFileName[0] = 0;
    m_specOffset = -1;

    int argc;
    LPTSTR *argv = ::CommandLineToArgvW(cmdLine, &argc);
    if (argv) {
        for (int i = fIgnoreFirst; i < argc; ++i) {
            // オプションは複数起動禁止時に無効->有効にすることができる
            if (argv[i][0] == TEXT('/') || argv[i][0] == TEXT('-')) {
                if (!::lstrcmpi(argv[i]+1, TEXT("tvtplay"))) m_fForceEnable = m_fIgnoreExt = true;
                else if (!::lstrcmpi(argv[i]+1, TEXT("tvtpudp"))) m_fAutoEnUdp = true;
                else if (!::lstrcmpi(argv[i]+1, TEXT("tvtpipe"))) m_fAutoEnPipe = true;
                else if (!::lstrcmpi(argv[i]+1, TEXT("tvtpofs")) && i+1 < argc) {
                    ++i;
                    if (TEXT('0') <= argv[i][0] && argv[i][0] <= TEXT('9')) {
                        m_specOffset = ::StrToInt(argv[i]);
                        int n = ::StrCSpn(argv[i], TEXT("+-"));
                        if (argv[i][n] == TEXT('+')) ++n;
                        m_specOffset += ::StrToInt(argv[i] + n);
                        if (m_specOffset < 0) m_specOffset = 0;
                    }
                }
            }
        }

        if (argc >= 1 + fIgnoreFirst && argv[argc-1][0] != TEXT('/') && argv[argc-1][0] != TEXT('-')) {
            bool fSpec = m_fIgnoreExt;
            if (!m_fIgnoreExt) {
                LPCTSTR ext = ::PathFindExtension(argv[argc-1]);
                if (ext && (!::lstrcmpi(ext, TEXT(".ts")) ||
                            !::lstrcmpi(ext, TEXT(".m2t")) ||
                            !::lstrcmpi(ext, TEXT(".m2ts")) ||
                            !::lstrcmpi(ext, TEXT(".m3u")) ||
                            !::lstrcmpi(ext, TEXT(".tslist"))))
                {
                    m_fForceEnable = true;
                    fSpec = true;
                }
            }
            if (fSpec) {
                ::lstrcpyn(m_szSpecFileName, argv[argc-1], _countof(m_szSpecFileName));
            }
        }

        ::LocalFree(argv);
    }
}


// 初期化処理
bool CTvtPlay::Initialize()
{
    if (!::GetModuleFileName(g_hinstDLL, m_szIniFileName, _countof(m_szIniFileName)) ||
        !::PathRenameExtension(m_szIniFileName, TEXT(".ini"))) return false;

    TVTest::CommandInfo cmdList[_countof(COMMAND_LIST) + COMMAND_S_MAX * 2];
    memcpy(cmdList, COMMAND_LIST, sizeof(COMMAND_LIST));
    int count = _countof(COMMAND_LIST);

    // 設定に応じてコマンド数をふやす
    TCHAR seekKey[COMMAND_S_MAX][16];
    TCHAR seekName[COMMAND_S_MAX][16];
    for (int i = 0; i < COMMAND_S_MAX; ++i) {
        ::wsprintf(seekKey[i], TEXT("Seek%c"), TEXT('A') + i);
        ::wsprintf(seekName[i], TEXT("シーク:%c"), TEXT('A') + i);
        if (!DEFAULT_SEEK_LIST[i]) {
            if (!GetPrivateProfileSignedInt(SETTINGS, seekKey[i], 0, m_szIniFileName)) break;
        }
        cmdList[count].ID = ID_COMMAND_SEEK_A + i;
        cmdList[count].pszText = seekKey[i];
        cmdList[count++].pszName = seekName[i];
    }

    TCHAR stretchKey[COMMAND_S_MAX][16];
    TCHAR stretchName[COMMAND_S_MAX][16];
    for (int i = 0; i < COMMAND_S_MAX; ++i) {
        ::wsprintf(stretchKey[i], TEXT("Stretch%c"), TEXT('A') + i);
        ::wsprintf(stretchName[i], TEXT("倍速:%c"), TEXT('A') + i);
        if (!DEFAULT_STRETCH_LIST[i]) {
            if (!GetPrivateProfileSignedInt(SETTINGS, stretchKey[i], 0, m_szIniFileName)) break;
        }
        cmdList[count].ID = ID_COMMAND_STRETCH_A + i;
        cmdList[count].pszText = stretchKey[i];
        cmdList[count++].pszName = stretchName[i];
    }

    // コマンドを登録
    m_pApp->RegisterCommand(cmdList, count);

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);

    TCHAR cmdOption[128];
    ::GetPrivateProfileString(SETTINGS, TEXT("TvtpCmdOption"), TEXT(""), cmdOption, _countof(cmdOption), m_szIniFileName);
    AnalyzeCommandLine(cmdOption, false);

    int preSpecOffset = m_specOffset;
    AnalyzeCommandLine(::GetCommandLine(), true);
    if (m_specOffset < 0) m_specOffset = preSpecOffset;
    return true;
}


// 終了処理
bool CTvtPlay::Finalize()
{
    if (m_pApp->IsPluginEnabled()) EnablePlugin(false);
    return true;
}


// 設定の読み込み
void CTvtPlay::LoadSettings()
{
    if (m_fSettingsLoaded) return;

    int iniVer = 0;
    {
        TCHAR *pBuf = NULL;
        for (int bufSize = 4096; bufSize < 1024*1024; bufSize *= 2) {
            delete [] pBuf;
            pBuf = new TCHAR[bufSize];
            if ((int)::GetPrivateProfileSection(SETTINGS, pBuf, bufSize, m_szIniFileName) < bufSize - 2) break;
            pBuf[0] = 0;
        }
        iniVer              = GetBufferedProfileInt(pBuf, TEXT("Version"), 0);
        m_fAllRepeat        = GetBufferedProfileInt(pBuf, TEXT("TsRepeatAll"), 0) != 0;
        m_fSingleRepeat     = GetBufferedProfileInt(pBuf, TEXT("TsRepeatSingle"), 0) != 0;
        m_fRepeatChapter    = GetBufferedProfileInt(pBuf, TEXT("TsRepeatChapter"), 0) != 0;
        m_fSkipXChapter     = GetBufferedProfileInt(pBuf, TEXT("TsSkipXChapter"), 0) != 0;
        m_readBufSizeKB     = GetBufferedProfileInt(pBuf, TEXT("TsReadBufferSizeKB"), 2048);
        m_supposedDispDelay = GetBufferedProfileInt(pBuf, TEXT("TsSupposedDispDelay"), 500);
        m_supposedDispDelay = min(max(m_supposedDispDelay, 0), 5000);
        // m_resetMode == 0:ビューアRS, 1:全体RS, 2:空PAT+ビューアRS, 3:空PAT, 4:何もしない
        m_resetMode         = GetBufferedProfileInt(pBuf, TEXT("TsResetAllOnSeek"), 0);
        m_resetDropInterval = GetBufferedProfileInt(pBuf, TEXT("TsResetDropInterval"), 1000);
        m_threadPriority    = GetBufferedProfileInt(pBuf, TEXT("TsThreadPriority"), THREAD_PRIORITY_NORMAL);
        m_stretchMode       = GetBufferedProfileInt(pBuf, TEXT("TsStretchMode"), 3);
        m_noMuteMax         = GetBufferedProfileInt(pBuf, TEXT("TsStretchNoMuteMax"), 800);
        m_noMuteMin         = GetBufferedProfileInt(pBuf, TEXT("TsStretchNoMuteMin"), 50);
        m_fConvTo188        = GetBufferedProfileInt(pBuf, TEXT("TsConvTo188"), 1) != 0;
        m_fUnderrunCtrl     = GetBufferedProfileInt(pBuf, TEXT("TsEnableUnderrunCtrl"), 0) != 0;
        m_fUseQpc           = GetBufferedProfileInt(pBuf, TEXT("TsUsePerfCounter"), 1) != 0;
        m_fModTimestamp     = GetBufferedProfileInt(pBuf, TEXT("TsAvoidWraparound"), 0) != 0;
        m_pcrThresholdMsec  = GetBufferedProfileInt(pBuf, TEXT("TsPcrDiscontinuityThreshold"), 400);
        m_fShowOpenDialog   = GetBufferedProfileInt(pBuf, TEXT("ShowOpenDialog"), 0) != 0;
        m_fRaisePriority    = GetBufferedProfileInt(pBuf, TEXT("RaiseMainThreadPriority"), 0) != 0;
        m_fAutoClose        = GetBufferedProfileInt(pBuf, TEXT("AutoClose"), 0) != 0;
        m_fAutoHide         = GetBufferedProfileInt(pBuf, TEXT("AutoHide"), 0) != 0;
        m_statusRow         = GetBufferedProfileInt(pBuf, TEXT("RowPos"), 0);
        m_statusRowFull     = GetBufferedProfileInt(pBuf, TEXT("RowPosFull"), 0);
        m_fSeekDrawOfs      = GetBufferedProfileInt(pBuf, TEXT("DispOffset"), 0) != 0;
        m_fSeekDrawTot      = GetBufferedProfileInt(pBuf, TEXT("DispTot"), 0) != 0;
        m_fPosDrawTot       = GetBufferedProfileInt(pBuf, TEXT("DispTotOnStatus"), 0) != 0;
        m_seekItemMinWidth  = GetBufferedProfileInt(pBuf, TEXT("SeekItemMinWidth"), 128);
        m_posItemWidth      = GetBufferedProfileInt(pBuf, TEXT("StatusItemWidth"), -1);
        m_timeoutOnCmd      = GetBufferedProfileInt(pBuf, TEXT("TimeoutOnCommand"), 2000);
        m_timeoutOnMove     = GetBufferedProfileInt(pBuf, TEXT("TimeoutOnMouseMove"), 0);
        m_salt              = GetBufferedProfileInt(pBuf, TEXT("Salt"), (::GetTickCount()>>16)^(::GetTickCount()&0xffff));
        m_hashListMax       = GetBufferedProfileInt(pBuf, TEXT("FileInfoMax"), 0);
        m_fUpdateHashList   = GetBufferedProfileInt(pBuf, TEXT("FileInfoAutoUpdate"), 0) != 0;
        m_popupMax          = GetBufferedProfileInt(pBuf, TEXT("PopupMax"), 30);
        m_fPopupDesc        = GetBufferedProfileInt(pBuf, TEXT("PopupDesc"), 0) != 0;
        GetBufferedProfileString(pBuf, TEXT("PopupPattern"), TEXT("%RecordFolder%*.ts"), m_szPopupPattern, _countof(m_szPopupPattern));
        GetBufferedProfileString(pBuf, TEXT("ChaptersFolderName"), TEXT("chapters"), m_szChaptersDirName, _countof(m_szChaptersDirName));
#ifdef EN_SWC
        GetBufferedProfileString(pBuf, TEXT("CaptionDll"), TEXT("Plugins\\TvtPlay_Caption.dll"), m_szCaptionDllPath, _countof(m_szCaptionDllPath));
        GetBufferedProfileString(pBuf, TEXT("BregonigDll"), TEXT(""), m_szBregonigDllPath, _countof(m_szBregonigDllPath));
        m_slowerWithCaption = GetBufferedProfileInt(pBuf, TEXT("SlowerWithCaption"), 0);
        m_swcShowLate       = GetBufferedProfileInt(pBuf, TEXT("SlowerWithCaptionShowLate"), 0);
        m_swcShowLate       = min(max(m_swcShowLate, 0), 5000);
        m_swcClearEarly     = GetBufferedProfileInt(pBuf, TEXT("SlowerWithCaptionClearEarly"), 0);
        m_swcClearEarly     = min(max(m_swcClearEarly, 0), 5000);
#endif
        m_seekItemOrder     = GetBufferedProfileInt(pBuf, TEXT("SeekItemOrder"), 99);
        m_posItemOrder      = GetBufferedProfileInt(pBuf, TEXT("StatusItemOrder"), 99);
        GetBufferedProfileString(pBuf, TEXT("IconImage"), TEXT(""), m_szIconFileName, _countof(m_szIconFileName));

        // シークコマンドのシーク量設定を取得
        m_seekListNum = 0;
        while (m_seekListNum < COMMAND_S_MAX) {
            TCHAR key[16];
            ::wsprintf(key, TEXT("Seek%c"), TEXT('A') + m_seekListNum);
            int val = GetBufferedProfileInt(pBuf, key, DEFAULT_SEEK_LIST[m_seekListNum]);
            if (!val) break;
            m_seekList[m_seekListNum++] = val;
        }
        // 倍速コマンドの倍率(25%～800%)設定を取得
        m_stretchListNum = 0;
        while (m_stretchListNum < COMMAND_S_MAX) {
            TCHAR key[16];
            ::wsprintf(key, TEXT("Stretch%c"), TEXT('A') + m_stretchListNum);
            int val = GetBufferedProfileInt(pBuf, key, DEFAULT_STRETCH_LIST[m_stretchListNum]);
            if (!val) break;
            m_stretchList[m_stretchListNum++] = min(max(val, 25), 800);
        }
        // ボタンアイテムの配置設定を取得
        int j = 0;
        for (int i = 0; i < BUTTON_MAX; ++i) {
            TCHAR key[16];
            ::wsprintf(key, TEXT("Button%02d"), i);
            GetBufferedProfileString(pBuf, key, DEFAULT_BUTTON_LIST[j] ? DEFAULT_BUTTON_LIST[j++] : TEXT(""),
                                     m_buttonList[i], _countof(m_buttonList[0]));
        }
        delete [] pBuf;
    }

    m_fSettingsLoaded = true;

    if (iniVer < 10) {
        // Button02が変更されていない場合はデフォルトに置換する
        if (!::lstrcmpi(m_buttonList[2], TEXT(";4,Loop")) ||
            !::lstrcmpi(m_buttonList[2], TEXT("4,Loop")))
        {
            ::lstrcpy(m_buttonList[2], DEFAULT_BUTTON_LIST[2]);
        }
    }
    if (iniVer < 12) {
        // StatusItemWidthが変更されていない場合はデフォルトに置換する
        if (m_posItemWidth == 112) m_posItemWidth = -1;
    }
    if (iniVer < INFO_VERSION) {
        // デフォルトの設定キーを出力するため
        SaveSettings(true);
    }
    // もしなければFileInfoの有効/無効キーをつくる
    if (GetPrivateProfileSignedInt(TEXT("FileInfo"), TEXT("Enabled"), -1, m_szIniFileName) < 0) {
        WritePrivateProfileInt(TEXT("FileInfo"), TEXT("Enabled"), 1, m_szIniFileName);
    }

    LoadTVTestSettings();
}


static void LoadFontSetting(LOGFONT *pFont, LPCTSTR iniFileName)
{
    TCHAR szFont[LF_FACESIZE];
    ::GetPrivateProfileString(TEXT("Status"), TEXT("FontName"), TEXT(""), szFont, _countof(szFont), iniFileName);
    if (szFont[0]) {
        ::lstrcpy(pFont->lfFaceName, szFont);
        pFont->lfEscapement     = 0;
        pFont->lfOrientation    = 0;
        pFont->lfUnderline      = 0;
        pFont->lfStrikeOut      = 0;
        pFont->lfCharSet        = DEFAULT_CHARSET;
        pFont->lfOutPrecision   = OUT_DEFAULT_PRECIS;
        pFont->lfClipPrecision  = CLIP_DEFAULT_PRECIS;
        pFont->lfQuality        = DRAFT_QUALITY;
        pFont->lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    }

    int val = GetPrivateProfileSignedInt(TEXT("Status"), TEXT("FontSize"), INT_MAX, iniFileName);
    if (val != INT_MAX) {
        pFont->lfHeight = val;
        pFont->lfWidth  = 0;
    }
    val = GetPrivateProfileSignedInt(TEXT("Status"), TEXT("FontWeight"), INT_MAX, iniFileName);
    if (val != INT_MAX) pFont->lfWeight = val;
    val = GetPrivateProfileSignedInt(TEXT("Status"), TEXT("FontItalic"), INT_MAX, iniFileName);
    if (val != INT_MAX) pFont->lfItalic = static_cast<BYTE>(val);
}


// TVTest本体から設定を読み込む
void CTvtPlay::LoadTVTestSettings()
{
    LOGFONT logFont;
    CColorScheme scheme;
    bool fFontLoaded = false;
    bool fColorLoaded = false;
    TCHAR val[2];

    // 各セクションがプラグインiniにあれば、そちらを読む(本体仕様変更への保険)
    m_statusView.GetFont(&logFont);
    ::GetPrivateProfileString(TEXT("Status"), TEXT("FontName"), TEXT("!"), val, _countof(val), m_szIniFileName);
    if (val[0] != TEXT('!')) {
        LoadFontSetting(&logFont, m_szIniFileName);
        fFontLoaded = true;
    }
    ::GetPrivateProfileString(TEXT("ColorScheme"), TEXT("Name"), TEXT("!"), val, _countof(val), m_szIniFileName);
    if (val[0] != TEXT('!')) {
        scheme.Load(m_szIniFileName);
        fColorLoaded = true;
    }

    // TVTest本体のiniを読む
    if (!fFontLoaded || !fColorLoaded) {
        TVTest::HostInfo hostInfo;
        WCHAR szAppIniPath[MAX_PATH];
        if (m_pApp->GetHostInfo(&hostInfo) &&
            m_pApp->GetSetting(L"IniFilePath", szAppIniPath, MAX_PATH) > 0)
        {
            // Mutex名を生成
            WCHAR szMutexName[64+MAX_PATH];
            ::lstrcpyn(szMutexName, hostInfo.pszAppName, 48);
            ::lstrcat(szMutexName, L"_Ini_Mutex_");
            ::lstrcat(szMutexName, szAppIniPath);
            for (WCHAR *p = szMutexName; *p; ++p)
                if (*p == L'\\') *p = L':';

            // iniをロックする(できなければ中途半端に読み込みにいかない)
            CGlobalLock fileLock;
            if (fileLock.Create(szMutexName)) {
                if (fileLock.Wait(5000)) {
                    if (!fFontLoaded) LoadFontSetting(&logFont, szAppIniPath);
                    if (!fColorLoaded) scheme.Load(szAppIniPath);
                    fileLock.Release();
                }
                fileLock.Close();
            }
        }
    }

    // フォントを適用
    m_statusView.SetFont(&logFont);

    // 配色
    CStatusView::ThemeInfo theme;
    scheme.GetStyle(CColorScheme::STYLE_STATUSITEM, &theme.ItemStyle);
    scheme.GetStyle(CColorScheme::STYLE_STATUSBOTTOMITEM, &theme.BottomItemStyle);
    scheme.GetStyle(CColorScheme::STYLE_STATUSHIGHLIGHTITEM, &theme.HighlightItemStyle);
    scheme.GetBorderInfo(CColorScheme::BORDER_STATUS, &theme.Border);
    m_statusView.SetTheme(&theme);

    // コントロールの高さを算出
    m_statusHeight = m_statusView.CalcHeight(0);
}


// 設定の保存
void CTvtPlay::SaveSettings(bool fWriteDefault) const
{
    if (!m_fSettingsLoaded) return;

    // 起動中に値を変えない設定値はfWriteDefaultのときだけ書く
    if (fWriteDefault) {
        WritePrivateProfileInt(SETTINGS, TEXT("Version"), INFO_VERSION, m_szIniFileName);
        TCHAR val[2];
        ::GetPrivateProfileString(SETTINGS, TEXT("TvtpCmdOption"), TEXT("!"), val, _countof(val), m_szIniFileName);
        if (val[0] == TEXT('!')) {
            ::WritePrivateProfileString(SETTINGS, TEXT("TvtpCmdOption"), TEXT(";/tvtpudp ;/tvtpipe"), m_szIniFileName);
        }
    }
    WritePrivateProfileInt(SETTINGS, TEXT("TsRepeatAll"), m_fAllRepeat, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsRepeatSingle"), m_fSingleRepeat, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsRepeatChapter"), m_fRepeatChapter, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsSkipXChapter"), m_fSkipXChapter, m_szIniFileName);
    if (fWriteDefault) {
        WritePrivateProfileInt(SETTINGS, TEXT("TsReadBufferSizeKB"), m_readBufSizeKB, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsSupposedDispDelay"), m_supposedDispDelay, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsResetAllOnSeek"), m_resetMode, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsResetDropInterval"), m_resetDropInterval, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsThreadPriority"), m_threadPriority, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsStretchMode"), m_stretchMode, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsStretchNoMuteMax"), m_noMuteMax, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsStretchNoMuteMin"), m_noMuteMin, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsConvTo188"), m_fConvTo188, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsEnableUnderrunCtrl"), m_fUnderrunCtrl, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsUsePerfCounter"), m_fUseQpc, m_szIniFileName);
    }
    WritePrivateProfileInt(SETTINGS, TEXT("TsAvoidWraparound"), m_fModTimestamp, m_szIniFileName);
    if (fWriteDefault) {
        WritePrivateProfileInt(SETTINGS, TEXT("TsPcrDiscontinuityThreshold"), m_pcrThresholdMsec, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("ShowOpenDialog"), m_fShowOpenDialog, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("RaiseMainThreadPriority"), m_fRaisePriority, m_szIniFileName);
    }
    WritePrivateProfileInt(SETTINGS, TEXT("AutoClose"), m_fAutoClose, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("AutoHide"), m_fAutoHide, m_szIniFileName);
    if (fWriteDefault) {
        WritePrivateProfileInt(SETTINGS, TEXT("RowPos"), m_statusRow, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("RowPosFull"), m_statusRowFull, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("DispOffset"), m_fSeekDrawOfs, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("DispTot"), m_fSeekDrawTot, m_szIniFileName);
    }
    WritePrivateProfileInt(SETTINGS, TEXT("DispTotOnStatus"), m_fPosDrawTot, m_szIniFileName);
    if (fWriteDefault) {
        WritePrivateProfileInt(SETTINGS, TEXT("SeekItemMinWidth"), m_seekItemMinWidth, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("StatusItemWidth"), m_posItemWidth, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TimeoutOnCommand"), m_timeoutOnCmd, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TimeoutOnMouseMove"), m_timeoutOnMove, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("Salt"), m_salt, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("FileInfoMax"), m_hashListMax, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("FileInfoAutoUpdate"), m_fUpdateHashList, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("PopupMax"), m_popupMax, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("PopupDesc"), m_fPopupDesc, m_szIniFileName);
        ::WritePrivateProfileString(SETTINGS, TEXT("PopupPattern"), m_szPopupPattern, m_szIniFileName);
        ::WritePrivateProfileString(SETTINGS, TEXT("ChaptersFolderName"), m_szChaptersDirName, m_szIniFileName);
#ifdef EN_SWC
        ::WritePrivateProfileString(SETTINGS, TEXT("CaptionDll"), m_szCaptionDllPath, m_szIniFileName);
        ::WritePrivateProfileString(SETTINGS, TEXT("BregonigDll"), m_szBregonigDllPath, m_szIniFileName);
    }
    WritePrivateProfileInt(SETTINGS, TEXT("SlowerWithCaption"), m_slowerWithCaption, m_szIniFileName);
    if (fWriteDefault) {
        WritePrivateProfileInt(SETTINGS, TEXT("SlowerWithCaptionShowLate"), m_swcShowLate, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("SlowerWithCaptionClearEarly"), m_swcClearEarly, m_szIniFileName);
#endif
        WritePrivateProfileInt(SETTINGS, TEXT("SeekItemOrder"), m_seekItemOrder, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("StatusItemOrder"), m_posItemOrder, m_szIniFileName);
        ::WritePrivateProfileString(SETTINGS, TEXT("IconImage"), m_szIconFileName, m_szIniFileName);

        for (int i = 0; i < m_seekListNum; ++i) {
            TCHAR key[16];
            ::wsprintf(key, TEXT("Seek%c"), TEXT('A') + i);
            WritePrivateProfileInt(SETTINGS, key, m_seekList[i], m_szIniFileName);
        }
        for (int i = 0; i < m_stretchListNum; ++i) {
            TCHAR key[16];
            ::wsprintf(key, TEXT("Stretch%c"), TEXT('A') + i);
            WritePrivateProfileInt(SETTINGS, key, m_stretchList[i], m_szIniFileName);
        }
        for (int i = 0; i < BUTTON_MAX; ++i) {
            TCHAR key[16];
            ::wsprintf(key, TEXT("Button%02d"), i);
            ::WritePrivateProfileString(SETTINGS, key, m_buttonList[i], m_szIniFileName);
        }
    }
}


// ファイル固有情報のリストを読み込む
// 読み込みに失敗した場合はリストをクリアした上でfalseを返す
bool CTvtPlay::LoadFileInfoSetting(std::list<HASH_INFO> &hashList) const
{
    hashList.clear();
    if (!m_fSettingsLoaded || m_hashListMax <= 0) return false;

    TCHAR *pBuf = NULL;
    for (int bufSize = 4096; bufSize < 1024*1024; bufSize *= 2) {
        delete [] pBuf;
        pBuf = new TCHAR[bufSize];
        if ((int)::GetPrivateProfileSection(TEXT("FileInfo"), pBuf, bufSize, m_szIniFileName) < bufSize - 2) break;
        pBuf[0] = 0;
    }
    if (!GetBufferedProfileInt(pBuf, TEXT("Enabled"), 0)) {
        delete [] pBuf;
        return false;
    }
    for (int i = 0; i < m_hashListMax; ++i) {
        // キー番号が小さいものほど新しい
        HASH_INFO hashInfo;
        TCHAR key[32], val[64];
        ::wsprintf(key, TEXT("Hash%d"), i);
        GetBufferedProfileString(pBuf, key, TEXT(""), val, _countof(val));
        if (!val[0]) break;
        if (!::StrToInt64Ex(val, STIF_SUPPORT_HEX, &hashInfo.hash)) continue;

        ::wsprintf(key, TEXT("Resume%d"), i);
        hashInfo.resumePos = GetBufferedProfileInt(pBuf, key, 0);
        hashList.push_back(hashInfo);
    }
    delete [] pBuf;
    return true;
}

// ファイル固有情報のリストを保存する
void CTvtPlay::SaveFileInfoSetting(const std::list<HASH_INFO> &hashList) const
{
    if (!m_fSettingsLoaded || m_hashListMax <= 0) return;

    // 不整合を防ぐため一度に書き込む
    TCHAR *pBuf = new TCHAR[32 + hashList.size() * 96];
    TCHAR *p = pBuf;
    p += ::wsprintf(p, TEXT("Enabled=1")) + 1;
    std::list<HASH_INFO>::const_iterator it = hashList.begin();
    for (int i = 0; i < m_hashListMax && it != hashList.end(); ++i, ++it) {
        p += ::wsprintf(p, TEXT("Hash%d=0x%06x%08x"), i, (DWORD)(it->hash>>32), (DWORD)(it->hash)) + 1;
        p += ::wsprintf(p, TEXT("Resume%d=%d"), i, it->resumePos) + 1;
    }
    p[0] = 0;
    ::WritePrivateProfileSection(TEXT("FileInfo"), pBuf, m_szIniFileName);
    delete [] pBuf;
}

// ファイル固有情報を更新する
void CTvtPlay::UpdateFileInfoSetting(const HASH_INFO &hashInfo) const
{
    std::list<HASH_INFO> hashList;
    if (LoadFileInfoSetting(hashList)) {
        // リストから削除
        std::list<HASH_INFO>::iterator it = hashList.begin();
        for (; it != hashList.end(); ++it) {
            if (it->hash == hashInfo.hash) {
                hashList.erase(it);
                break;
            }
        }
        // レジューム情報が有効なときだけ追加(TODO: HASH_INFOを拡張するときはこの部分の再考が必要)
        if (hashInfo.resumePos >= 0) {
            // リストの最も新しい位置に追加する
            hashList.push_front(hashInfo);
        }
        SaveFileInfoSetting(hashList);
    }
}


// プラグインが有効にされた時の初期化処理
bool CTvtPlay::InitializePlugin()
{
    if (m_fInitialized) return true;

    // ウィンドウクラスの登録
    WNDCLASS wc;
    wc.style            = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc      = FrameWindowProc;
    wc.cbClsExtra       = 0;
    wc.cbWndExtra       = 0;
    wc.hInstance        = g_hinstDLL;
    wc.hIcon            = NULL;
    wc.hCursor          = NULL;
    wc.hbrBackground    = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName     = NULL;
    wc.lpszClassName    = TVTPLAY_FRAME_WINDOW_CLASS;
    if (!::RegisterClass(&wc)) return false;

    LoadSettings();

    // アイコン画像読み込み
    DrawUtil::CBitmap iconMap;
    if (m_szIconFileName[0])
        iconMap.Load(NULL, m_szIconFileName, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    if (!iconMap.IsCreated())
        if (!iconMap.Load(g_hinstDLL, IDB_BUTTONS)) return false;

    DrawUtil::CBitmap iconS, iconL;
    if (!iconS.Create(ICON_SIZE * 3, ICON_SIZE, 1) ||
        !iconL.Create(ICON_SIZE * COMMAND_S_MAX, ICON_SIZE, 1)) return false;

    HDC hdcMem = ::CreateCompatibleDC(NULL);
    if (!hdcMem) return false;

    int seekItemOrder = min(max(m_seekItemOrder, 0), BUTTON_MAX);
    int posItemOrder = min(max(m_posItemOrder, 0), BUTTON_MAX);
    if (m_posItemOrder < m_seekItemOrder) {
        if (seekItemOrder==0) posItemOrder = -1;
        if (posItemOrder==BUTTON_MAX) seekItemOrder = BUTTON_MAX + 1;
    }

    // 文字列解析してボタンアイテムを生成
    for (int i = -1; i < BUTTON_MAX + 2; i++) {
        if (i == seekItemOrder) {
            m_statusView.AddItem(new CSeekStatusItem(this, m_fSeekDrawOfs, m_fSeekDrawTot, m_seekItemMinWidth));
        }
        if (i == posItemOrder) {
            m_statusView.AddItem(new CPositionStatusItem(this));
        }
        if (i < 0 || BUTTON_MAX <= i || m_buttonList[i][0] == TEXT(';')) continue;

        // コマンドはカンマ区切りで2つまで指定できる
        int cmdID[2] = {-1, -1};
        int cmdCount = 0, width = ICON_SIZE;
        TCHAR text[BUTTON_TEXT_MAX];
        ::lstrcpy(text, m_buttonList[i]);
        ::CharUpper(text);
        for (;;) {
            LPTSTR cmd = ::StrRChr(text, NULL, TEXT(','));
            if (!cmd) break;

            if (!::StrCmpNI(cmd+1, TEXT("Width="), 6)) {
                width = ::StrToInt(cmd+7);
            }
            else if (cmdCount < 2) {
                if (::lstrcmpi(cmd+1, TEXT("SeekA")) >= 0 &&
                    ::lstrcmpi(cmd+1, TEXT("SeekZ")) <= 0 &&
                    ::lstrlen(cmd) == 6)
                {
                    cmdID[cmdCount++] = ID_COMMAND_SEEK_A + cmd[5] - TEXT('A');
                }
                else if (::lstrcmpi(cmd+1, TEXT("StretchA")) >= 0 &&
                         ::lstrcmpi(cmd+1, TEXT("StretchZ")) <= 0 &&
                         ::lstrlen(cmd) == 9)
                {
                    cmdID[cmdCount++] = ID_COMMAND_STRETCH_A + cmd[8] - TEXT('A');
                }
                else {
                    for (int k = 0; k < _countof(COMMAND_LIST); ++k) {
                        if (!::lstrcmpi(cmd+1, COMMAND_LIST[k].pszText)) {
                            cmdID[cmdCount++] = COMMAND_LIST[k].ID;
                            break;
                        }
                    }
                }
            }
            cmd[0] = 0;
        }
        // 同一コマンドのボタンは複数生成できない
        if (cmdID[0] < 0 || m_statusView.GetItemByID(STATUS_ITEM_BUTTON + cmdID[0])) continue;
        if (cmdID[1] < 0) {
            // サブコマンドの省略時解釈
            cmdID[1] = (cmdID[0]==ID_COMMAND_PREV || cmdID[0]==ID_COMMAND_SEEK_END) ? ID_COMMAND_LIST_POPUP :
                       (cmdID[0]==ID_COMMAND_OPEN) ? ID_COMMAND_OPEN_POPUP : ID_COMMAND_NOP;
        }

        // StretchとStretchReのみ大きいビットマップを使う
        DrawUtil::CBitmap *pIcon = (cmdID[0] == ID_COMMAND_STRETCH ||
                                    cmdID[0] == ID_COMMAND_STRETCH_RE) ? &iconL : &iconS;
        HBITMAP hbmOld = SelectBitmap(hdcMem, pIcon->GetHandle());

        RGBQUAD rgbq[2] = {0};
        rgbq[1].rgbBlue = rgbq[1].rgbGreen = rgbq[1].rgbRed = 255;
        ::SetDIBColorTable(hdcMem, 0, 2, rgbq);

        ComposeMonoColorIcon(hdcMem, 0, 0, iconMap.GetHandle(), m_buttonList[i]);

        m_statusView.AddItem(new CButtonStatusItem(this, STATUS_ITEM_BUTTON + cmdID[0],
                                                   STATUS_ITEM_BUTTON + cmdID[1], width, *pIcon));
        SelectBitmap(hdcMem, hbmOld);
    }
    ::DeleteDC(hdcMem);

    m_fInitialized = true;
    return true;
}


// プラグインの有効状態が変化した
bool CTvtPlay::EnablePlugin(bool fEnable) {
    if (fEnable) {
        if (!InitializePlugin()) return false;

        if (!m_hwndFrame) {
            m_hwndFrame = ::CreateWindow(TVTPLAY_FRAME_WINDOW_CLASS, NULL, WS_POPUP, 
                                         CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                                         m_pApp->GetAppWindow(), NULL, g_hinstDLL, this);
            if (!m_hwndFrame) return false;
        }

        if (!m_statusView.Create(m_hwndFrame, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS)) {
            ::DestroyWindow(m_hwndFrame);
            m_hwndFrame = NULL;
            return false;
        }
        m_statusView.SetEventHandler(&m_eventHandler);

        CStatusItem *pItem = m_statusView.GetItemByID(STATUS_ITEM_POSITION);
        if (pItem) {
            if (m_posItemWidth < 0) {
                CPositionStatusItem *pPosItem = dynamic_cast<CPositionStatusItem*>(pItem);
                pItem->SetWidth(pPosItem->CalcSuitableWidth());
            }
            else {
                pItem->SetWidth(m_posItemWidth);
            }
        }
        OnDispModeChange(m_pApp->GetStandby(), true);
#ifdef EN_SWC
        TCHAR blacklistPath[MAX_PATH + 32];
        if (::GetModuleFileName(g_hinstDLL, blacklistPath, MAX_PATH)) {
            ::PathRemoveExtension(blacklistPath);
            ::lstrcat(blacklistPath, TEXT("_Blacklist.txt"));
            if (m_captionAnalyzer.Initialize(m_szCaptionDllPath, m_szBregonigDllPath, blacklistPath, m_swcShowLate, m_swcClearEarly)) {
                // ストリームコールバックの登録
                m_pApp->SetStreamCallback(0, StreamCallback, this);
            }
        }
#endif
        if (m_fShowOpenDialog && !m_szSpecFileName[0] && !m_pApp->GetStandby()) OpenWithDialog();

        m_pApp->SetWindowMessageCallback(WindowMsgCallback, this);
        ::DragAcceptFiles(m_pApp->GetAppWindow(), TRUE);
    }
    else {
        ::DragAcceptFiles(m_pApp->GetAppWindow(), FALSE);
        m_pApp->SetWindowMessageCallback(NULL, NULL);
        Close();
#ifdef EN_SWC
        if (m_captionAnalyzer.IsInitialized()) {
            // ストリームコールバックの登録解除
            m_pApp->SetStreamCallback(TVTest::STREAM_CALLBACK_REMOVE, StreamCallback);
            m_captionAnalyzer.UnInitialize();
        }
#endif
        if (m_hwndFrame) {
            m_statusView.SetEventHandler(NULL);
            m_statusView.Destroy();
            ::DestroyWindow(m_hwndFrame);
            m_hwndFrame = NULL;
        }
    }
    return true;
}


// ポップアップメニュー選択でプラグイン設定する
void CTvtPlay::SetupWithPopup(const POINT &pt, UINT flags)
{
    // メニュー生成
    int selID = 0;
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        if ((m_statusRow != 0 || IsAppMaximized()) && !m_pApp->GetFullscreen()) {
            ::AppendMenu(hmenu, MF_STRING|(m_fAutoHide?MF_UNCHECKED:MF_CHECKED), 1, TEXT("常に表示する"));
        }
        ::AppendMenu(hmenu, MF_STRING|(m_fPosDrawTot?MF_CHECKED:MF_UNCHECKED), 2, TEXT("放送時刻を表示する"));
        ::AppendMenu(hmenu, MF_STRING|(m_fModTimestamp?MF_CHECKED:MF_UNCHECKED), 3, TEXT("ラップアラウンドを回避する"));
        ::AppendMenu(hmenu, MF_STRING|(m_fAutoClose?MF_CHECKED:MF_UNCHECKED), 4, TEXT("再生終了時に自動終了する"));
#ifdef EN_SWC
        HMENU hSubMenu = ::CreatePopupMenu();
        if (hSubMenu) {
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==0   ?MF_CHECKED:MF_UNCHECKED), 101, TEXT("しない"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==75  ?MF_CHECKED:MF_UNCHECKED), 102, TEXT("0.75倍"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==50  ?MF_CHECKED:MF_UNCHECKED), 103, TEXT("0.50倍"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==25  ?MF_CHECKED:MF_UNCHECKED), 104, TEXT("0.25倍"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==-100?MF_CHECKED:MF_UNCHECKED), 105, TEXT("-100%"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==-200?MF_CHECKED:MF_UNCHECKED), 106, TEXT("-200%"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==-300?MF_CHECKED:MF_UNCHECKED), 107, TEXT("-300%"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==-400?MF_CHECKED:MF_UNCHECKED), 108, TEXT("-400%"));
            ::AppendMenu(hmenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), TEXT("字幕でゆっくり"));
        }
#endif
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hmenu);
    }
    if (selID == 1) {
        m_fAutoHide = !m_fAutoHide;
        OnDispModeChange(m_pApp->GetStandby());
        SaveSettings();
    }
    else if (selID == 2) {
        m_fPosDrawTot = !m_fPosDrawTot;
        CStatusItem *pItem = m_statusView.GetItemByID(STATUS_ITEM_POSITION);
        if (pItem && m_posItemWidth < 0) {
            CPositionStatusItem *pPosItem = dynamic_cast<CPositionStatusItem*>(pItem);
            pItem->SetWidth(pPosItem->CalcSuitableWidth());
            OnFrameResize();
            m_statusView.Invalidate();
        }
        SaveSettings();
    }
    else if (selID == 3) {
        m_fModTimestamp = !m_fModTimestamp;
        SetModTimestamp(m_fModTimestamp);
        SaveSettings();
    }
    else if (selID == 4) {
        m_fAutoClose = !m_fAutoClose;
        SaveSettings();
    }
#ifdef EN_SWC
    else if (101 <= selID && selID <= 108) {
        switch (selID) {
        case 101: m_slowerWithCaption = 0; break;
        case 102: m_slowerWithCaption = 75; break;
        case 103: m_slowerWithCaption = 50; break;
        case 104: m_slowerWithCaption = 25; break;
        case 105: m_slowerWithCaption = -100; break;
        case 106: m_slowerWithCaption = -200; break;
        case 107: m_slowerWithCaption = -300; break;
        case 108: m_slowerWithCaption = -400; break;
        }
        Stretch(GetStretchID());
        SaveSettings();
    }
#endif
}


// TVTestのフルスクリーンHWNDを取得する
// 必ず取得できると仮定してはいけない
HWND CTvtPlay::GetFullscreenWindow()
{
    TVTest::HostInfo hostInfo;
    if (m_pApp->GetFullscreen() && m_pApp->GetHostInfo(&hostInfo)) {
        TCHAR className[64];
        ::lstrcpyn(className, hostInfo.pszAppName, 48);
        ::lstrcat(className, L" Fullscreen");

        HWND hwnd = NULL;
        while ((hwnd = ::FindWindowEx(NULL, hwnd, className, NULL)) != NULL) {
            DWORD pid;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid == ::GetCurrentProcessId()) return hwnd;
        }
    }
    return NULL;
}


// ダイアログ選択でファイルを開く
bool CTvtPlay::OpenWithDialog()
{
    if (m_fDialogOpen) return false;

    HWND hwndOwner = GetFullscreenWindow();
    if (!hwndOwner) hwndOwner = m_pApp->GetAppWindow();
    LPTSTR lpstrFile;

    OPENFILENAME ofn = {0};
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner   = hwndOwner;
    ofn.lpstrFilter = TEXT("再生可能なメディア(*.ts;*.m2t;*.m2ts;*.m3u;*.tslist)\0*.ts;*.m2t;*.m2ts;*.m3u;*.tslist\0すべてのファイル\0*.*\0");
    ofn.lpstrTitle  = TEXT("ファイルを開く");
    // MSDN記述と違いOFN_NOCHANGEDIRはGetOpenFileName()にも効果がある模様
    ofn.Flags       = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR |
                      OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    // OFN_ENABLEHOOKではVistaスタイルが適用されないため
    ofn.nMaxFile    = 32768;
    ofn.lpstrFile   = lpstrFile = new TCHAR[ofn.nMaxFile];
    lpstrFile[0] = 0;

    bool fAdded = false;
    m_fDialogOpen = true;
    if (::GetOpenFileName(&ofn)) {
        LPCTSTR spec = lpstrFile + ::lstrlen(lpstrFile) + 1;
        if (*spec) {
            // 複数のファイルが選択された
            for (; *spec; spec += ::lstrlen(spec) + 1) {
                TCHAR path[MAX_PATH];
                if (::PathCombine(path, lpstrFile, spec)) {
                    if (m_playlist.PushBackListOrFile(path, !fAdded) >= 0) fAdded = true;
                }
            }
        }
        else {
            fAdded = m_playlist.PushBackListOrFile(lpstrFile, true) >= 0;
        }
    }
    else if (::CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
        ::MessageBox(hwndOwner, TEXT("選択されたファイルが多すぎます。"),
                     TEXT("ファイルを開く"), MB_OK | MB_ICONERROR);
    }
    m_fDialogOpen = false;

    delete [] lpstrFile;
    return fAdded ? OpenCurrent() : false;
}


static inline bool CompareAsc(LPCTSTR& l, LPCTSTR& r) { return ::lstrcmpi(l, r) < 0; }

static inline bool CompareDesc(LPCTSTR& l, LPCTSTR& r) { return ::lstrcmpi(l, r) > 0; }

// ポップアップメニュー選択でファイルを開く
bool CTvtPlay::OpenWithPopup(const POINT &pt, UINT flags)
{
    if (m_popupMax <= 0) return false;

    // 特定指示子をTVTestの保存先フォルダに置換する(手抜き)
    TCHAR pattern[MAX_PATH];
    if (!::StrCmpNI(m_szPopupPattern, TEXT("%RecordFolder%"), 14)) {
        if (m_pApp->GetSetting(L"RecordFolder", pattern, _countof(pattern)) <= 0) pattern[0] = 0;
        ::PathAppend(pattern, m_szPopupPattern + 14);
    }
    else {
        ::lstrcpy(pattern, m_szPopupPattern);
    }

    // ファイルリスト取得
    std::vector<WIN32_FIND_DATA> findList;
    WIN32_FIND_DATA findData;
    HANDLE hFind = ::FindFirstFile(pattern, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        findList.push_back(findData);
        for (int i = 0; i < POPUP_MAX_MAX; ++i) {
            if (!::FindNextFile(hFind, &findData)) break;
             findList.push_back(findData);
        }
        ::FindClose(hFind);
    }
    int listSize = static_cast<int>(findList.size());

    // ファイル名を昇順or降順ソート
    std::vector<LPCTSTR> nameList;
    nameList.resize(listSize);
    for (int i = 0; i < listSize; ++i) nameList[i] = findList[i].cFileName;
    std::sort(nameList.begin(), nameList.end(), m_fPopupDesc ? CompareDesc : CompareAsc);

    // ポップアップしない部分をとばす
    int skipSize = max(listSize - m_popupMax, 0);

    // メニュー生成
    int selID = 0;
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        if (listSize <= 0) {
            ::AppendMenu(hmenu, MF_STRING | MF_GRAYED, 0, TEXT("(なし)"));
        }
        else {
            for (int i = 0; skipSize+i < listSize; ++i) {
                TCHAR str[64];
                ::lstrcpyn(str, nameList[skipSize+i], 64);
                if (::lstrlen(str) == 63) ::lstrcpy(&str[60], TEXT("..."));
                // プレフィクス対策
                for (LPTSTR p = str; *p; p++)
                    if (*p == TEXT('&')) *p = TEXT('_');
                ::AppendMenu(hmenu, MF_STRING, i + 1, str);
            }
        }
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hmenu);
    }

    TCHAR fileName[MAX_PATH];
    if (selID > 0 && skipSize+selID-1 < listSize &&
        ::PathRemoveFileSpec(pattern) &&
        ::PathCombine(fileName, pattern, nameList[skipSize+selID-1]))
    {
        return m_playlist.PushBackListOrFile(fileName, true) >= 0 ? OpenCurrent() : false;
    }
    return false;
}


// ポップアップメニュー選択で再生リストのファイルを開く
bool CTvtPlay::OpenWithPlayListPopup(const POINT &pt, UINT flags)
{
    // メニュー生成
    int selID = 0, cmdID = 1;
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        if (m_playlist.empty()) {
            ::AppendMenu(hmenu, MF_STRING | MF_GRAYED, 0, TEXT("(なし)"));
        }
        else {
            CPlaylist::const_iterator it = m_playlist.begin();
            for (; it != m_playlist.end(); ++cmdID, ++it) {
                TCHAR str[64];
                ::lstrcpyn(str, PathFindFileName((*it).path), 64);
                if (::lstrlen(str) == 63) ::lstrcpy(&str[60], TEXT("..."));
                // プレフィクス対策
                for (LPTSTR p = str; *p; ++p)
                    if (*p == TEXT('&')) *p = TEXT('_');

                MENUITEMINFO mi;
                mi.cbSize = sizeof(MENUITEMINFO);
                mi.fMask = MIIM_ID | MIIM_STATE | MIIM_TYPE;
                mi.wID = cmdID;
                mi.fState = cmdID-1==(int)m_playlist.GetPosition() ? MFS_DEFAULT | MFS_CHECKED : 0;
                mi.fType = MFT_STRING | MFT_RADIOCHECK;
                mi.dwTypeData = str;
                ::InsertMenuItem(hmenu, cmdID - 1, TRUE, &mi);
            }
            ::AppendMenu(hmenu, MF_SEPARATOR, 0, NULL);
            ::AppendMenu(hmenu, MF_STRING, cmdID + 0, TEXT("現在のファイルを前へ移動(&B)"));
            ::AppendMenu(hmenu, MF_STRING, cmdID + 1, TEXT("現在のファイルを次へ移動(&N)"));
            ::AppendMenu(hmenu, MF_STRING, cmdID + 2, TEXT("現在のファイルをリストから削除(&X)"));
            HMENU hSubMenu = ::CreatePopupMenu();
            if (hSubMenu) {
                ::AppendMenu(hSubMenu, MF_STRING, cmdID + 3, TEXT("リストをクリア"));
                ::AppendMenu(hSubMenu, MF_STRING, cmdID + 4, TEXT("昇順にソート"));
                ::AppendMenu(hSubMenu, MF_STRING, cmdID + 5, TEXT("降順にソート"));
                ::AppendMenu(hSubMenu, MF_SEPARATOR, 0, NULL);
                ::AppendMenu(hSubMenu, MF_STRING, cmdID + 6, TEXT("コピー"));
                ::AppendMenu(hSubMenu, MF_STRING, cmdID + 7, TEXT("コピー(ファイル名のみ)"));
                ::AppendMenu(hmenu, MF_SEPARATOR, 0, NULL);
                ::AppendMenu(hmenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), TEXT("その他の操作(&O)"));
            }
        }
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hmenu);
    }

    if (selID >= cmdID || selID - 1 < 0 || (int)m_playlist.size() <= selID - 1) {
        if (selID == cmdID + 0) {
            m_playlist.MoveCurrentToPrev();
        }
        else if (selID == cmdID + 1) {
            m_playlist.MoveCurrentToNext();
        }
        else if (selID == cmdID + 2) {
            // ファイルが開かれていれば閉じる
            Close();
            m_playlist.EraseCurrent();
        }
        else if (selID == cmdID + 3) {
            m_playlist.ClearWithoutCurrent();
        }
        else if (selID == cmdID + 4 || selID == cmdID + 5) {
            m_playlist.Sort(selID==cmdID+5);
        }
        else if (selID == cmdID + 6 || selID == cmdID + 7) {
            // 出力文字数を算出
            int size = m_playlist.ToString(NULL, 0, selID==cmdID+7);
            // クリップボードにコピー
            if (size > 1 && ::OpenClipboard(m_hwndFrame)) {
                if (::EmptyClipboard()) {
                    HGLOBAL hg = ::GlobalAlloc(GMEM_MOVEABLE, size * sizeof(TCHAR));
                    if (hg) {
                        LPTSTR clip = reinterpret_cast<LPTSTR>(::GlobalLock(hg));
                        if (clip) {
                            m_playlist.ToString(clip, size, selID==cmdID+7);
                            ::GlobalUnlock(hg);
                            if (!::SetClipboardData(CF_UNICODETEXT, hg)) ::GlobalFree(hg);
                        }
                        else ::GlobalFree(hg);
                    }
                }
                ::CloseClipboard();
            }
        }
        return false;
    }

    m_playlist.SetPosition(selID - 1);
    return OpenCurrent();
}


static INT_PTR CALLBACK EditChapterDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // WM_INITDIALOGのとき不定
    CHAPTER *pch = reinterpret_cast<CHAPTER*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
    switch (uMsg) {
    case WM_INITDIALOG:
        ::SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        pch = reinterpret_cast<CHAPTER*>(lParam);
        ::SetDlgItemText(hDlg, IDC_EDIT_NAME, pch->second.val);
        ::SetDlgItemInt(hDlg, IDC_EDIT_HOUR, pch->first/3600000%100, FALSE);
        ::SetDlgItemInt(hDlg, IDC_EDIT_MIN, pch->first/60000%60, FALSE);
        ::SetDlgItemInt(hDlg, IDC_EDIT_SEC, pch->first/1000%60, FALSE);
        ::SetDlgItemInt(hDlg, IDC_EDIT_MSEC, pch->first%1000, FALSE);
        ::SendDlgItemMessage(hDlg, IDC_EDIT_NAME, EM_LIMITTEXT, _countof(pch->second.val) - 1, 0);
        ::SendDlgItemMessage(hDlg, IDC_EDIT_HOUR, EM_LIMITTEXT, 2, 0);
        ::SendDlgItemMessage(hDlg, IDC_EDIT_MIN, EM_LIMITTEXT, 2, 0);
        ::SendDlgItemMessage(hDlg, IDC_EDIT_SEC, EM_LIMITTEXT, 2, 0);
        ::SendDlgItemMessage(hDlg, IDC_EDIT_MSEC, EM_LIMITTEXT, 3, 0);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BUTTON_MAX:
            ::SetDlgItemInt(hDlg, IDC_EDIT_HOUR, 99, FALSE);
            ::SetDlgItemInt(hDlg, IDC_EDIT_MIN, 59, FALSE);
            ::SetDlgItemInt(hDlg, IDC_EDIT_SEC, 59, FALSE);
            ::SetDlgItemInt(hDlg, IDC_EDIT_MSEC, 999, FALSE);
            break;
        case IDOK:
            if (!::GetDlgItemText(hDlg, IDC_EDIT_NAME, pch->second.val, _countof(pch->second.val))) {
                pch->second.val[0] = 0;
            }
            pch->first = ::GetDlgItemInt(hDlg, IDC_EDIT_HOUR, NULL, FALSE)%100*3600000 +
                         ::GetDlgItemInt(hDlg, IDC_EDIT_MIN, NULL, FALSE)%60*60000 +
                         ::GetDlgItemInt(hDlg, IDC_EDIT_SEC, NULL, FALSE)%60*1000 +
                         ::GetDlgItemInt(hDlg, IDC_EDIT_MSEC, NULL, FALSE)%1000;
            if (pch->first < 0) pch->first = 0;
            // FALL THROUGH!
        case IDCANCEL:
            ::EndDialog(hDlg, LOWORD(wParam));
            break;
        }
        break;
    }
    return FALSE;
}


// ポップアップメニュー選択でチャプターを編集する
void CTvtPlay::EditChapterWithPopup(int pos, const POINT &pt, UINT flags)
{
    CChapterMap::const_iterator ci = m_chapter.find(pos);
    if (!m_chapter.IsOpen() || ci == m_chapter.end()) return;
    CHAPTER ch = *ci;

    // メニュー生成
    int selID = 0;
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        TCHAR str[128];
        ::wsprintf(str, TEXT("%d:%02d:%02d.%03d %s"),
                   ch.first/3600000, ch.first/60000%60, ch.first/1000%60, ch.first%1000,
                   ch.second.val);
        ::AppendMenu(hmenu, MF_STRING, 8, str);
        ::AppendMenu(hmenu, MF_SEPARATOR, 0, NULL);
        ::AppendMenu(hmenu, MF_STRING, 1, TEXT("0.2秒前へ"));
        ::AppendMenu(hmenu, MF_STRING, 2, TEXT("0.2秒後ろへ"));
        ::AppendMenu(hmenu, MF_STRING, 3, TEXT("チャプターを削除"));
        ::AppendMenu(hmenu, MF_SEPARATOR, 0, NULL);
        ::AppendMenu(hmenu, MF_STRING | (ch.second.IsIn() ? MFS_CHECKED : 0), 4, TEXT("開始チャプター"));
        ::AppendMenu(hmenu, MF_STRING | (ch.second.IsOut() ? MFS_CHECKED : 0), 5, TEXT("終了チャプター"));
        ::AppendMenu(hmenu, MF_STRING | (ch.second.IsIn() && ch.second.IsX() ? MFS_CHECKED : 0), 6, TEXT("スキップ開始"));
        ::AppendMenu(hmenu, MF_STRING | (ch.second.IsOut() && ch.second.IsX() ? MFS_CHECKED : 0), 7, TEXT("スキップ終了"));
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hmenu);
    }
    if (selID == 8) {
        // ダイアログで編集
        selID = 0;
        if (!m_fPopuping) {
            HWND hwndOwner = GetFullscreenWindow();
            if (!hwndOwner) hwndOwner = m_pApp->GetAppWindow();
            m_fPopuping = true;
            if (::DialogBoxParam(g_hinstDLL, MAKEINTRESOURCE(IDD_EDIT_CHAPTER), hwndOwner,
                                 EditChapterDlgProc, reinterpret_cast<LPARAM>(&ch)) == IDOK)
            {
                selID = 8;
            }
            m_fPopuping = false;
        }
    }
    if (!selID || !m_chapter.IsOpen() || m_chapter.find(pos) == m_chapter.end()) return;

    // マップのチャプターをchの値に更新する
    m_chapter.erase(pos);
    switch (selID) {
    case 1:
        if (ch.first < CHAPTER_POS_MAX)
            ch.first = max(ch.first-200, 0);
        break;
    case 2:
        ch.first = min(ch.first+200, CHAPTER_POS_MAX);
        break;
    case 4:
        if (ch.second.IsOut()) ch.second.val[0] = TEXT('i');
        else ch.second.InvertPrefix(TEXT('i'));
        break;
    case 5:
        if (ch.second.IsIn()) ch.second.val[0] = TEXT('o');
        else ch.second.InvertPrefix(TEXT('o'));
        break;
    case 6:
        {
            CHAPTER_NAME chName;
            ::lstrcpyn(chName.val, ch.second.val + (ch.second.IsIn()||ch.second.IsOut() ? 1 : 0) +
                       (ch.second.IsX() ? 1 : 0), _countof(chName.val) - 2);
            ::lstrcpy(ch.second.val, ch.second.IsIn() && ch.second.IsX() ? TEXT("") : TEXT("ix"));
            ::lstrcat(ch.second.val, chName.val);
        }
        break;
    case 7:
        {
            CHAPTER_NAME chName;
            ::lstrcpyn(chName.val, ch.second.val + (ch.second.IsIn()||ch.second.IsOut() ? 1 : 0) +
                       (ch.second.IsX() ? 1 : 0), _countof(chName.val) - 2);
            ::lstrcpy(ch.second.val, ch.second.IsOut() && ch.second.IsX() ? TEXT("") : TEXT("ox"));
            ::lstrcat(ch.second.val, chName.val);
        }
        break;
    }
    if (selID != 3) m_chapter.insert(ch);
    m_chapter.Save();
    BeginWatchingNextChapter(false);
}


// ポップアップメニュー選択でチャプター全体を編集する
void CTvtPlay::EditAllChaptersWithPopup(const POINT &pt, UINT flags)
{
    // メニュー生成
    int selID = 0;
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        ::AppendMenu(hmenu, MF_STRING, 1, TEXT("全チャプターを0.2秒前へ"));
        ::AppendMenu(hmenu, MF_STRING, 2, TEXT("全チャプターを0.2秒後ろへ"));
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hmenu);
    }
    if (!selID || !m_chapter.IsOpen()) return;

    if (selID == 1) {
        CChapterMap::iterator it = m_chapter.begin();
        while (it != m_chapter.end()) {
            CHAPTER ch = *it;
            m_chapter.erase(it);
            if (ch.first < CHAPTER_POS_MAX) ch.first = max(ch.first - 200, 0);
            it = m_chapter.insert(ch).first;
            ++it;
        }
    }
    else {
        CChapterMap::iterator it = m_chapter.end();
        while (it != m_chapter.begin()) {
            CHAPTER ch = *(--it);
            m_chapter.erase(it);
            if (ch.first > 0) ch.first = min(ch.first + 200, CHAPTER_POS_MAX);
            it = m_chapter.insert(ch).first;
        }
    }
    m_chapter.Save();
    BeginWatchingNextChapter(false);
}


int CTvtPlay::TrackPopup(HMENU hmenu, const POINT &pt, UINT flags)
{
    int selID = 0;
    if (!m_fPopuping) {
        m_fPopuping = true;
        // まずコントロールを表示させておく
        if (m_fAutoHideActive) ::SendMessage(m_hwndFrame, WM_TIMER, TIMER_ID_AUTO_HIDE, 1);
        selID = static_cast<int>(::TrackPopupMenu(hmenu, flags | TPM_NONOTIFY | TPM_RETURNCMD,
                                                  pt.x, pt.y, 0, m_hwndFrame, NULL));
        m_fPopuping = false;
    }
    return selID;
}


// プレイリストの現在位置のファイルを開く
bool CTvtPlay::OpenCurrent(int offset)
{
    size_t pos = m_playlist.GetPosition();
    return pos < m_playlist.size() ? Open(m_playlist[pos].path, offset) : false;
}


bool CTvtPlay::Open(LPCTSTR fileName, int offset)
{
    Close();

    if (!m_tsSender.Open(fileName, m_salt, m_readBufSizeKB*1024, m_fConvTo188, m_fUnderrunCtrl, m_fUseQpc, m_pcrThresholdMsec)) return false;

    bool fSeeked = false;
    if (offset >= 0) {
        // オフセットが指定されているのでシーク
        if (0 < offset && offset < m_tsSender.GetDuration() - 5000) {
            m_tsSender.Seek(offset);
            fSeeked = true;
        }
    }
    else {
        // レジューム情報があればその地点までシーク
        std::list<HASH_INFO> hashList;
        LoadFileInfoSetting(hashList);
        LONGLONG hash = m_tsSender.GetFileHash();
        std::list<HASH_INFO>::const_iterator it = hashList.begin();
        for (; it != hashList.end(); ++it) {
            if ((*it).hash == hash) {
                // 先頭or終端から5秒の範囲はレジュームしない
                if (5000 < (*it).resumePos && (*it).resumePos < m_tsSender.GetDuration() - 5000) {
                    m_tsSender.Seek((*it).resumePos - 3000);
                    fSeeked = true;
                }
                break;
            }
        }
    }

    // チャプターを読み込む
    m_chapter.Open(fileName, m_szChaptersDirName);
    if (!fSeeked && m_fSkipXChapter && m_chapter.IsOpen()) {
        // 先頭から1秒未満のスキップ開始チャプターを解釈
        CChapterMap::const_iterator it = m_chapter.begin();
        for (; it != m_chapter.end() && it->first < 1000; ++it) {
            if (it->second.IsIn() && it->second.IsX()) {
                for (++it; it != m_chapter.end(); ++it) {
                    // スキップ終了チャプターまでシーク
                    if (it->second.IsOut() && it->second.IsX()) {
                        m_tsSender.Seek(it->first);
                        break;
                    }
                }
                break;
            }
        }
    }

    // 再生情報を初期化する
    UpdateInfos();

    m_hThreadEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_hThreadEvent) {
        m_tsSender.Close();
        return false;
    }
    m_hThread = ::CreateThread(NULL, 0, TsSenderThread, this, 0, &m_threadID);
    if (!m_hThread) {
        ::CloseHandle(m_hThreadEvent);
        m_hThreadEvent = NULL;
        m_tsSender.Close();
        return false;
    }
    if (m_threadPriority >= THREAD_PRIORITY_LOWEST &&
        m_threadPriority <= THREAD_PRIORITY_HIGHEST) {
        ::SetThreadPriority(m_hThread, m_threadPriority);
    }

    // メセージキューができるまで待つ
    ::WaitForSingleObject(m_hThreadEvent, INFINITE);

    // TSデータの送り先を設定
    SetupDestination();

    // PCR/PTS/DTSを変更するかどうか設定
    SetModTimestamp(m_fModTimestamp);

    if (m_hashListMax > 0 && m_fUpdateHashList) {
        ::SetTimer(m_hwndFrame, TIMER_ID_UPDATE_HASH_LIST, TIMER_UPDATE_HASH_LIST_INTERVAL, NULL);
    }
    if (m_chapter.NeedToSync()) {
        ::SetTimer(m_hwndFrame, TIMER_ID_SYNC_CHAPTER, TIMER_SYNC_CHAPTER_INTERVAL, NULL);
    }
    BeginWatchingNextChapter(true);

    m_statusView.Invalidate();
    return true;
}


void CTvtPlay::Close()
{
    if (m_hThread) {
        ::PostThreadMessage(m_threadID, WM_QUIT, 0, 0);
        ::WaitForSingleObject(m_hThread, INFINITE);
        ::CloseHandle(m_hThread);
        m_hThread = NULL;
        ::CloseHandle(m_hThreadEvent);
        m_hThreadEvent = NULL;

        HASH_INFO hashInfo = {0};
        hashInfo.hash = m_tsSender.GetFileHash();
        int pos = m_tsSender.GetPosition();
        // 先頭or終端から5秒の範囲はレジューム情報を記録しない
        hashInfo.resumePos = pos <= 5000 || m_tsSender.IsFixed() && m_tsSender.GetDuration()-5000 <= pos ? -1 : pos;
        UpdateFileInfoSetting(hashInfo);

        ::KillTimer(m_hwndFrame, TIMER_ID_UPDATE_HASH_LIST);
        ::KillTimer(m_hwndFrame, TIMER_ID_SYNC_CHAPTER);
        ::KillTimer(m_hwndFrame, TIMER_ID_WATCH_POS_GT);
        m_chapter.Close();
        m_tsSender.Close();

        // 再生情報を初期化する
        UpdateInfos();
        m_statusView.Invalidate();
    }
}


// ドライバの状態に応じてTSデータの転送先を設定する
void CTvtPlay::SetupDestination()
{
    TCHAR path[MAX_PATH];
    TVTest::ChannelInfo chInfo;

    m_pApp->GetDriverName(path, _countof(path));
    LPCTSTR name = ::PathFindFileName(path);

    if (!::lstrcmpi(name, TEXT("BonDriver_UDP.dll"))) {
        if (m_pApp->GetCurrentChannelInfo(&chInfo) && m_hThread) {
            ::PostThreadMessage(m_threadID, WM_TS_SET_UDP, 0, 1234 + chInfo.Channel);
        }
    }
    else if (!::lstrcmpi(name, TEXT("BonDriver_Pipe.dll"))) {
        if (m_pApp->GetCurrentChannelInfo(&chInfo) && m_hThread) {
            ::PostThreadMessage(m_threadID, WM_TS_SET_PIPE, 0, chInfo.Channel);
        }
    }
    else {
        if (m_hThread) ::PostThreadMessage(m_threadID, WM_TS_SET_UDP, 0, -1);
    }
}

void CTvtPlay::WaitAndPostToSender(UINT Msg, WPARAM wParam, LPARAM lParam, bool fResetAll)
{
    if (m_hThread) {
        // 転送停止するまで待つ
        m_fHalt = true;
        ::ResetEvent(m_hThreadEvent);
        ::WaitForSingleObject(m_hThreadEvent, 1000);
        ::PostThreadMessage(m_threadID, Msg, wParam, lParam);
        m_fHalt = false;
    }
}

void CTvtPlay::Pause(bool fPause)
{
    WaitAndPostToSender(WM_TS_PAUSE, fPause, 0, false);
}

void CTvtPlay::SeekToBegin()
{
    WaitAndPostToSender(WM_TS_SEEK_BGN, 0, 0, m_resetMode != 0);
    BeginWatchingNextChapter(true);
}

void CTvtPlay::SeekToEnd()
{
    WaitAndPostToSender(WM_TS_SEEK_END, 0, 0, m_resetMode != 0);
    BeginWatchingNextChapter(true);
}

void CTvtPlay::Seek(int msec)
{
    WaitAndPostToSender(WM_TS_SEEK, 0, msec, m_resetMode != 0);
    BeginWatchingNextChapter(true);
}

void CTvtPlay::SeekAbsolute(int msec)
{
    WaitAndPostToSender(WM_TS_SEEK_ABSOLUTE, 0, msec, m_resetMode != 0);
    BeginWatchingNextChapter(true);
}

void CTvtPlay::SetModTimestamp(bool fModTimestamp)
{
    if (m_hThread) ::PostThreadMessage(m_threadID, WM_TS_SET_MOD_TS, fModTimestamp, 0);
}

void CTvtPlay::SetRepeatFlags(bool fAllRepeat, bool fSingleRepeat)
{
    m_fAllRepeat = fAllRepeat;
    m_fSingleRepeat = fSingleRepeat;
    m_statusView.UpdateItem(STATUS_ITEM_BUTTON + ID_COMMAND_LOOP);
    SaveSettings();
}

int CTvtPlay::GetStretchID()
{
    CBlockLock lock(&m_tsInfoLock);
    if (m_infoSpeed == 100) return -1;
    for (int i = 0; i < m_stretchListNum; ++i) {
        if (m_stretchList[i] == m_infoSpeed) return i;
    }
    return -1;
}

void CTvtPlay::Stretch(int stretchID)
{
    int speed = 0 <= stretchID && stretchID < m_stretchListNum ?
                m_stretchList[stretchID] : 100;
    int lowSpeed = speed;
#ifdef EN_SWC
    // 字幕表示中の速度を計算
    lowSpeed = m_slowerWithCaption > 0 ? speed * m_slowerWithCaption / 100 : speed + m_slowerWithCaption;
    // 切り替え時のプチノイズ防止のため101
    lowSpeed = min(max(lowSpeed, 101), speed);
#endif

    // 速度が設定値より大or小のときはミュートフラグをつける
    bool fMute = speed < m_noMuteMin || m_noMuteMax < speed;
    if (m_hThread) ::PostThreadMessage(m_threadID, WM_TS_SET_SPEED, (fMute?4:0)|m_stretchMode, MAKELPARAM(speed, lowSpeed));
}

// 再生位置から直近のチャプターの監視を開始する
// 再生位置がチャプター位置より大きくなればWM_SATISFIED_POS_GTが呼ばれ、監視は終了する
// シークが発生すると監視は終了するので注意
void CTvtPlay::BeginWatchingNextChapter(bool fDoDelay)
{
    if (m_hThread && m_chapter.IsOpen() && !m_chapter.empty()) {
        if (fDoDelay) {
            // シーク時などはGetPosition()が更新されるまで遅延させる
            ::SetTimer(m_hwndFrame, TIMER_ID_WATCH_POS_GT, TIMER_WATCH_POS_GT_INTERVAL, NULL);
        }
        else {
            // 直近の終了チャプターかスキップ開始チャプターを監視する
            // 監視位置は必ずposより大きくする(でないと即座にWM_SATISFIED_POS_GTが呼ばれてしまう)
            int pos = GetPosition();
            if (pos >= 0) {
                CChapterMap::const_iterator it = m_chapter.upper_bound(pos);
                for (; it != m_chapter.end(); ++it) {
                    if (m_fSkipXChapter && it->second.IsIn() && it->second.IsX() ||
                        m_fRepeatChapter && it->second.IsOut())
                    {
                        ::PostThreadMessage(m_threadID, WM_TS_WATCH_POS_GT, 0, it->first + m_supposedDispDelay);
                        break;
                    }
                }
            }
        }
    }
}

bool CTvtPlay::CalcStatusRect(RECT *pRect, bool fInit)
{
    if (m_pApp->GetFullscreen()) {
        // 切り替え時にバーが画面外にいることもある
        HMONITOR hMon = ::MonitorFromWindow(fInit?m_pApp->GetAppWindow():m_hwndFrame, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi;
        mi.cbSize = sizeof(MONITORINFO);
        if (::GetMonitorInfo(hMon, &mi)) {
            pRect->left = mi.rcMonitor.left;
            pRect->right = mi.rcMonitor.right;
            pRect->top = m_statusRowFull < 0 ? mi.rcMonitor.top + m_statusHeight * (-m_statusRowFull-1) :
                         m_statusRowFull > 0 ? mi.rcMonitor.bottom - m_statusHeight * m_statusRowFull :
                         mi.rcMonitor.bottom - m_statusHeight;
            pRect->bottom = pRect->top + m_statusHeight;
            return true;
        }
    }
    else {
        HWND hwndApp = m_pApp->GetAppWindow();
        RECT rcw, rcc;
        if (::GetWindowRect(hwndApp, &rcw) && ::GetClientRect(hwndApp, &rcc)) {
            int margin = ((rcw.right-rcw.left)-rcc.right) / 2;
            pRect->left = rcw.left + (m_statusRow==0 && margin<=2 ? 0 : margin);
            pRect->right = rcw.right - (m_statusRow==0 && margin<=2 ? 0 : margin);
            pRect->top = m_statusRow < 0 ? rcw.top + margin + m_statusHeight * (-m_statusRow-1) :
                         m_statusRow > 0 ? rcw.bottom - margin - m_statusHeight * m_statusRow :
                         m_statusRow == 0 && IsAppMaximized() ? rcw.bottom - margin - m_statusHeight : rcw.bottom;
            pRect->bottom = pRect->top + m_statusHeight + (m_statusRow==0 && margin<=2 ? margin : 0);
            return true;
        }
    }
    return false;
}

// TVTest本体リサイズ時の処理
void CTvtPlay::OnResize(bool fInit)
{
    RECT rect;
    if (CalcStatusRect(&rect, fInit)) {
        ::SetWindowPos(m_hwndFrame, NULL, rect.left, rect.top,
                       rect.right-rect.left, rect.bottom-rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void CTvtPlay::OnDispModeChange(bool fStandy, bool fInit)
{
    if (!fInit && m_fAutoHideActive) {
        ::KillTimer(m_hwndFrame, TIMER_ID_AUTO_HIDE);
    }
    m_fAutoHideActive = false;
    m_fHoveredFromOutside = false;
    m_dispCount = 0;

    OnResize(fInit);
    // 常にサイズが変化するとは限らない
    OnFrameResize();

    if (fStandy) {
        // 待機状態
        ::ShowWindow(m_hwndFrame, SW_HIDE);
        // ファイルが開かれていれば閉じる
        Close();
    }
    else if (m_pApp->GetFullscreen()) {
        // フルスクリーン表示状態
        ::GetCursorPos(&m_lastCurPos);
        m_idleCurPos = m_lastCurPos;
        if (!m_fAutoHideActive) {
            ::SetTimer(m_hwndFrame, TIMER_ID_AUTO_HIDE, TIMER_AUTO_HIDE_INTERVAL, NULL);
            m_fAutoHideActive = true;
        }
    }
    else {
        // 通常表示状態
        ::SetWindowPos(m_hwndFrame, m_pApp->GetAlwaysOnTop() ? HWND_TOPMOST : HWND_NOTOPMOST,
                       0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        ::GetCursorPos(&m_lastCurPos);
        m_idleCurPos.x = m_idleCurPos.y = -10;
        if (m_fAutoHide && !m_fAutoHideActive) {
            ::SetTimer(m_hwndFrame, TIMER_ID_AUTO_HIDE, TIMER_AUTO_HIDE_INTERVAL, NULL);
            m_fAutoHideActive = true;
        }
    }
}

// コントロールのリサイズ時の処理
void CTvtPlay::OnFrameResize()
{
    bool fFull = m_pApp->GetFullscreen();
    int mgnx, mgny;
    RECT rcw, rcc;
    if (!fFull &&
        ::GetWindowRect(m_pApp->GetAppWindow(), &rcw) &&
        ::GetClientRect(m_pApp->GetAppWindow(), &rcc))
    {
        int margin = ((rcw.right-rcw.left)-rcc.right) / 2;
        mgnx = mgny = m_statusRow==0 && margin<=2 ? margin : 0;
    }
    else {
        mgnx = 0;
        mgny = -1<=m_statusRowFull && m_statusRowFull<=1 ? -1 : 0;
    }

    if (::GetClientRect(m_hwndFrame, &rcc)) {
        // シークバーをリサイズする
        CStatusItem *pItemSeek = m_statusView.GetItemByID(STATUS_ITEM_SEEK);
        if (pItemSeek) {
            // シークバーアイテム以外の幅を算出
            int cmpl = 0;
            int num = m_statusView.NumItems();
            int idSeek = pItemSeek->GetIndex();
            RECT rcMgn;
            m_statusView.GetItemMargin(&rcMgn);
            for (int i = 0; i < num; ++i) {
                if (i != idSeek) {
                    cmpl += m_statusView.GetItem(i)->GetWidth() + rcMgn.left + rcMgn.right;
                }
            }
            pItemSeek->SetWidth(rcc.right - rcMgn.left - rcMgn.right - 2 - mgnx*2 - cmpl);
        }
        m_statusView.SetPosition(mgnx, fFull && m_statusRowFull==-1 ? -1 : 0,
                                 rcc.right - mgnx*2, rcc.bottom - mgny);
    }
}

// コントロールを状況に応じて表示・非表示する
void CTvtPlay::ProcessAutoHide(bool fNoDecDispCount)
{
    POINT curPos;
    RECT rect;
    if (::GetCursorPos(&curPos) && CalcStatusRect(&rect)) {
        // curPosとrectはともに仮想スクリーン座標系
        bool fHovered = rect.left <= curPos.x && curPos.x < rect.right &&
                        rect.top <= curPos.y && curPos.y < rect.bottom;
        bool fFull = m_pApp->GetFullscreen();
        // 全画面表示状態で最小化されることもある(キー割り当てからの最小化など)
        bool fFullAndIconic = fFull && ::IsIconic(m_pApp->GetAppWindow());
        // 最下部配置のときは最大化表示状態かどうかで挙動を変える
        bool fRow0AndMaximized = !fFull && m_statusRow==0 && IsAppMaximized();

        // カーソルがどの方向からホバーされたか
        POINT lastPos = m_lastCurPos;
        if (fFull || !fHovered) {
            m_fHoveredFromOutside = false;
        }
        else if ((m_statusRow==1 || fRow0AndMaximized) && (rect.left>lastPos.x || rect.right<=lastPos.x || rect.bottom<=lastPos.y) ||
                 m_statusRow==-1 && (rect.left>lastPos.x || rect.right<=lastPos.x || rect.top>lastPos.y))
        {
            m_fHoveredFromOutside = true;
        }
        m_lastCurPos = curPos;

        if (fFull) {
            // カーソルが移動したときに表示する(全画面表示)
            if (m_timeoutOnMove > 0 &&
                (curPos.x < m_idleCurPos.x-2 || m_idleCurPos.x+2 < curPos.x ||
                    curPos.y < m_idleCurPos.y-2 || m_idleCurPos.y+2 < curPos.y))
            {
                HMONITOR hMonApp = ::MonitorFromWindow(m_hwndFrame, MONITOR_DEFAULTTONEAREST);
                HMONITOR hMonCur = ::MonitorFromPoint(curPos, MONITOR_DEFAULTTONULL);
                if (hMonApp == hMonCur) {
                    m_dispCount = m_timeoutOnMove / TIMER_AUTO_HIDE_INTERVAL;
                    m_idleCurPos = curPos;
                }
            }
        }

        if (!fFullAndIconic && (m_dispCount > 0 || (fHovered && !m_fHoveredFromOutside) ||
            m_fPopuping || (!fFull && m_statusRow==0 && !fRow0AndMaximized))) {
            if (!::IsWindowVisible(m_hwndFrame)) {
                if (fFull) {
                    ::SetWindowPos(m_hwndFrame, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                }
                else {
                    ::ShowWindow(m_hwndFrame, SW_SHOWNOACTIVATE);
                }
            }
            if (fHovered) m_dispCount = 0;
            else if (!fNoDecDispCount) m_dispCount--;
        }
        else {
            if (::IsWindowVisible(m_hwndFrame)) {
                ::ShowWindow(m_hwndFrame, SW_HIDE);
            }
        }
    }
}


void CTvtPlay::EnablePluginByDriverName()
{
    if (m_fAutoEnUdp || m_fAutoEnPipe) {
        TCHAR path[MAX_PATH];
        m_pApp->GetDriverName(path, _countof(path));
        LPCTSTR name = ::PathFindFileName(path);

        // ドライバ名に応じて有効無効を切り替える
        bool fEnable = false;
        if (m_fAutoEnUdp && !::lstrcmpi(name, TEXT("BonDriver_UDP.dll"))) fEnable = true;
        if (m_fAutoEnPipe && !::lstrcmpi(name, TEXT("BonDriver_Pipe.dll"))) fEnable = true;

        if (m_pApp->IsPluginEnabled() != fEnable) m_pApp->EnablePlugin(fEnable);
    }
}


void CTvtPlay::OnPreviewChange(bool fPreview)
{
    if (!fPreview) {
        // TVTest再生オフで一時停止する
        m_fPausedOnPreviewChange = false;
        if (IsOpen() && !IsPaused()) {
            Pause(true);
            m_fPausedOnPreviewChange = true;
        }
    }
    else {
        if (m_fPausedOnPreviewChange && IsOpen() && IsPaused()) {
            Pause(false);
        }
        m_fPausedOnPreviewChange = false;
    }
}


void CTvtPlay::OnCommand(int id, const POINT *pPt, UINT flags)
{
    switch (id) {
    case ID_COMMAND_OPEN:
        OpenWithDialog();
        break;
    case ID_COMMAND_OPEN_POPUP:
    case ID_COMMAND_LIST_POPUP:
        if (m_fPopuping) {
            ::PostMessage(m_hwndFrame, WM_KEYDOWN, VK_ESCAPE, 0);
            ::PostMessage(m_hwndFrame, WM_KEYUP, VK_ESCAPE, 0);
        }
        else if (!pPt) {
            // 適当な位置に表示するためボタンを擬似的に押しに行く
            CStatusItem *pItem = m_statusView.GetItemByID(STATUS_ITEM_BUTTON + id);
            if (pItem) {
                pItem->OnLButtonDown(0, 0);
            }
            else {
                int num = m_statusView.NumItems();
                for (int i = 0; i < num; ++i) {
                    CButtonStatusItem *pButton = dynamic_cast<CButtonStatusItem*>(m_statusView.GetItem(i));
                    if (pButton && pButton->GetSubID() == STATUS_ITEM_BUTTON + id) {
                        pButton->OnRButtonDown(0, 0);
                        break;
                    }
                }
            }
        }
        else if (id == ID_COMMAND_OPEN_POPUP) {
            OpenWithPopup(*pPt, flags);
        }
        else {
            OpenWithPlayListPopup(*pPt, flags);
        }
        break;
    case ID_COMMAND_CLOSE:
        Close();
        break;
    case ID_COMMAND_PREV:
        if (m_playlist.Prev(IsAllRepeat())) OpenCurrent();
        else SeekToBegin();
        break;
    case ID_COMMAND_SEEK_BGN:
        SeekToBegin();
        break;
    case ID_COMMAND_SEEK_END:
        if (m_playlist.size() >= 2 && m_playlist.Next(IsAllRepeat())) OpenCurrent();
        else SeekToEnd();
        break;
    case ID_COMMAND_SEEK_PREV:
        if (m_chapter.IsOpen() && !m_chapter.empty()) {
            int pos = GetPosition();
            if (pos >= 3000) {
                // 再生中の連続ジャンプを考慮して多めに戻す
                CChapterMap::const_iterator it = m_chapter.lower_bound(pos - 3000);
                if (it != m_chapter.begin()) SeekAbsolute((--it)->first);
            }
        }
        break;
    case ID_COMMAND_SEEK_NEXT:
        if (m_chapter.IsOpen() && !m_chapter.empty()) {
            int pos = GetPosition();
            if (pos >= 0) {
                CChapterMap::const_iterator it = m_chapter.lower_bound(pos + 1000);
                if (it != m_chapter.end()) SeekAbsolute(it->first);
            }
        }
        break;
    case ID_COMMAND_ADD_CHAPTER:
        if (m_chapter.IsOpen()) {
            int pos = GetPosition();
            if (0 <= pos) {
                // ユーザは映像のタイミングでチャプターを付けるので、少し戻す
                pos = max(pos - m_supposedDispDelay, 0);
                // 追加位置から±3秒以内に既存のチャプターがないか
                CChapterMap::const_iterator it = m_chapter.lower_bound(pos - 3000);
                if (it == m_chapter.end() || it->first > pos + 3000) {
                    CHAPTER ch;
                    // どうせPCR挿入間隔の精度しか出ないので100msec単位に落とす
                    ch.first = pos / 100 * 100;
                    // 無名チャプター
                    ch.second.val[0] = 0;
                    m_chapter.insert(ch);
                    m_chapter.Save();
                    m_statusView.UpdateItem(STATUS_ITEM_SEEK);
                }
            }
        }
        break;
    case ID_COMMAND_REPEAT_CHAPTER:
        m_fRepeatChapter = !m_fRepeatChapter;
        BeginWatchingNextChapter(false);
        m_statusView.UpdateItem(STATUS_ITEM_BUTTON + id);
        SaveSettings();
        break;
    case ID_COMMAND_SKIP_X_CHAPTER:
        m_fSkipXChapter = !m_fSkipXChapter;
        BeginWatchingNextChapter(false);
        m_statusView.UpdateItem(STATUS_ITEM_BUTTON + id);
        SaveSettings();
        break;
    case ID_COMMAND_LOOP:
        if (!IsAllRepeat() && !IsSingleRepeat())
            SetRepeatFlags(true, false);
        else if (IsAllRepeat() && !IsSingleRepeat())
            SetRepeatFlags(true, true);
        else
            SetRepeatFlags(false, false);
        break;
    case ID_COMMAND_PAUSE:
        // 閉じたファイルを開きなおす
        if (!IsOpen()) OpenCurrent();
        else Pause(!IsPaused());
        break;
    case ID_COMMAND_NOP:
        break;
    case ID_COMMAND_STRETCH:
        {
            int stid = GetStretchID();
            Stretch(stid < 0 ? 0 : stid + 1);
        }
        break;
    case ID_COMMAND_STRETCH_RE:
        {
            int stid = GetStretchID();
            if (stid < 0) {
                int rear = 1;
                for (; rear < m_stretchListNum && m_stretchList[rear] != 100; ++rear);
                Stretch(rear - 1);
            }
            else {
                Stretch(stid - 1);
            }
        }
        break;
    default:
        if (ID_COMMAND_SEEK_A <= id && id < ID_COMMAND_SEEK_A + m_seekListNum) {
            Seek(m_seekList[id-ID_COMMAND_SEEK_A]);
        }
        else if (ID_COMMAND_STRETCH_A <= id && id < ID_COMMAND_STRETCH_A + COMMAND_S_MAX) {
            Stretch(GetStretchID() == id-ID_COMMAND_STRETCH_A ? -1 : id-ID_COMMAND_STRETCH_A);
        }
        break;
    }
    m_dispCount = m_timeoutOnCmd / TIMER_AUTO_HIDE_INTERVAL;
}


// イベントコールバック関数
// 何かイベントが起きると呼ばれる
LRESULT CALLBACK CTvtPlay::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData)
{
    CTvtPlay *pThis = static_cast<CTvtPlay*>(pClientData);

    switch (Event) {
    case TVTest::EVENT_PLUGINENABLE:
        // プラグインの有効状態が変化した
        return pThis->EnablePlugin(lParam1 != 0);
    case TVTest::EVENT_FULLSCREENCHANGE:
        // 全画面表示状態が変化した
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->OnDispModeChange(false);
        break;
    case TVTest::EVENT_STANDBY:
        // 待機状態が変化した
        // 全画面表示時はEVENT_FULLSCREENCHANGEの後に呼ばれる
        // GetStandby()の変化に先行して呼ばれるので注意
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->OnDispModeChange(lParam1 != 0);
        break;
    case TVTest::EVENT_DRIVERCHANGE:
        // ドライバが変更された
        // 起動が完了するまでは不安定なので有効にしない
        if (pThis->m_fEventStartupDone) {
            pThis->EnablePluginByDriverName();
        }
        // 複数禁止起動時にチャンネル設定されない場合の対策(ver.0.7.22未満)
        if (pThis->m_fEventExecute &&
            pThis->m_pApp->GetVersion() < TVTest::MakeVersion(0,7,22))
        {
            TCHAR path[MAX_PATH];
            pThis->m_pApp->GetDriverName(path, _countof(path));
            LPCTSTR name = ::PathFindFileName(path);

            if (pThis->m_pApp->IsPluginEnabled() &&
                (!::lstrcmpi(name, TEXT("BonDriver_UDP.dll")) ||
                !::lstrcmpi(name, TEXT("BonDriver_Pipe.dll"))))
            {
                TVTest::ChannelInfo chInfo;
                if (!pThis->m_pApp->GetCurrentChannelInfo(&chInfo)) {
                    pThis->m_pApp->AddLog(L"SetChannel");
                    for (int ch=0; ch < 10; ch++) {
                        if (pThis->m_pApp->SetChannel(0, ch)) break;
                    }
                }
            }
        }
        pThis->m_fEventExecute = false;

        // コマンドラインにパスが指定されていれば開く
        if (pThis->m_pApp->IsPluginEnabled() && pThis->m_szSpecFileName[0]) {
            if (pThis->m_playlist.PushBackListOrFile(pThis->m_szSpecFileName, true) >= 0) {
                pThis->OpenCurrent(pThis->m_specOffset);
            }
            pThis->m_szSpecFileName[0] = 0;
        }
        break;
    case TVTest::EVENT_CHANNELCHANGE:
        // チャンネルが変更された
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->SetupDestination();
        break;
#ifdef EN_SWC
    case TVTest::EVENT_SERVICEUPDATE:
        // サービスの構成が変化した
        if (pThis->m_pApp->IsPluginEnabled()) {
            // 字幕PIDをセットする
            int index = pThis->m_pApp->GetService();
            TVTest::ServiceInfo si;
            if (index >= 0 && pThis->m_pApp->GetServiceInfo(index, &si) && si.SubtitlePID != 0) {
                pThis->m_captionAnalyzer.SetPid(si.SubtitlePID);
            }
            else {
                pThis->m_captionAnalyzer.SetPid(-1);
            }
        }
        break;
#endif
    case TVTest::EVENT_PREVIEWCHANGE:
        // プレビュー表示状態が変化した
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->OnPreviewChange(lParam1 != 0);
        break;
    case TVTest::EVENT_COMMAND:
        // コマンドが選択された
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->OnCommand(static_cast<int>(lParam1));
        return TRUE;
    case TVTest::EVENT_EXECUTE:
        // 複数起動禁止時に複数起動された
        // ドライバ変更前に呼ばれるので、このあとEVENT_DRIVERCHANGEが呼ばれるかもしれない
        pThis->AnalyzeCommandLine(reinterpret_cast<LPCWSTR>(lParam1), false);
        pThis->m_fEventExecute = true;
        // FALL THROUGH!
    case TVTest::EVENT_STARTUPDONE:
        // 起動時の処理が終わった
        if (Event == TVTest::EVENT_STARTUPDONE) {
            if (pThis->m_fForceEnable) pThis->m_pApp->EnablePlugin(true);
            pThis->m_fEventStartupDone = true;
        }
        pThis->EnablePluginByDriverName();
        if (pThis->m_pApp->IsPluginEnabled()) {
            // コマンドラインにパスが指定されていれば開く
            if (pThis->m_szSpecFileName[0]) {
                if (pThis->m_playlist.PushBackListOrFile(pThis->m_szSpecFileName, true) >= 0) {
                    pThis->OpenCurrent(pThis->m_specOffset);
                }
                pThis->m_szSpecFileName[0] = 0;
            }
            // 起動時フリーズ対策(仮)
            if (pThis->m_fRaisePriority) {
                ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            }
        }
        break;
    }
    return 0;
}


// ウィンドウメッセージコールバック関数
// TRUEを返すとTVTest側でメッセージを処理しなくなる
// WM_CREATEは呼ばれない
BOOL CALLBACK CTvtPlay::WindowMsgCallback(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pResult, void *pUserData)
{
    CTvtPlay *pThis = static_cast<CTvtPlay*>(pUserData);

    switch (uMsg) {
    case WM_SIZE:
        pThis->OnResize();
        break;
    case WM_MOVE:
        if (!pThis->m_pApp->GetFullscreen()) {
            RECT rect;
            if (pThis->CalcStatusRect(&rect)) {
                ::SetWindowPos(pThis->m_hwndFrame, NULL, rect.left, rect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
        break;
    case WM_DROPFILES:
        if (!pThis->m_fDialogOpen) {
            bool fAdded = false;
            TCHAR fileName[MAX_PATH];
            int num = ::DragQueryFile((HDROP)wParam, 0xFFFFFFFF, fileName, _countof(fileName));
            for (int i = 0; i < num; ++i) {
                if (::DragQueryFile((HDROP)wParam, i, fileName, _countof(fileName)) != 0) {
                    if (pThis->m_playlist.PushBackListOrFile(fileName, !fAdded) >= 0) fAdded = true;
                }
            }
            // 少なくとも1ファイルが再生リストに追加されればそのファイルを開く
            if (fAdded) pThis->OpenCurrent();
            ::DragFinish((HDROP)wParam);
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (pThis->m_fAutoHideActive && !pThis->m_pApp->GetFullscreen()) {
            // m_fHoveredFromOutsideの更新のため
            ::SendMessage(pThis->m_hwndFrame, WM_TIMER, TIMER_ID_AUTO_HIDE, 1);
            // カーソルが移動したときに表示する(通常表示)
            int cx = GET_X_LPARAM(lParam);
            int cy = GET_Y_LPARAM(lParam);
            if (pThis->m_timeoutOnMove > 0 &&
                (cx < pThis->m_idleCurPos.x-2 || pThis->m_idleCurPos.x+2 < cx ||
                 cy < pThis->m_idleCurPos.y-2 || pThis->m_idleCurPos.y+2 < cy))
            {
                if (!pThis->m_fHoveredFromOutside)
                    pThis->m_dispCount = pThis->m_timeoutOnMove / TIMER_AUTO_HIDE_INTERVAL;
                pThis->m_idleCurPos.x = cx;
                pThis->m_idleCurPos.y = cy;
            }
        }
        break;
    }
    return FALSE;
}


// コントロールのウインドウプロシージャ
LRESULT CALLBACK CTvtPlay::FrameWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // WM_CREATEのとき不定
    CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_CREATE:
        {
            LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            pThis = reinterpret_cast<CTvtPlay*>(pcs->lpCreateParams);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        break;
    case WM_SIZE:
        pThis->OnFrameResize();
        break;
    case WM_TIMER:
        switch (wParam) {
        case TIMER_ID_AUTO_HIDE:
            // lParam!=0のときは表示タイムアウト値を減じない
            pThis->ProcessAutoHide(lParam != 0);
            break;
        case TIMER_ID_RESET_DROP:
            {
                TVTest::StatusInfo si;
                // ドロップカウントが安定すればRESETSTATUSする
                if (!pThis->m_pApp->GetStatus(&si) || si.DropPacketCount == pThis->m_lastDropCount) {
                    pThis->m_pApp->ResetStatus();
                    ::KillTimer(hwnd, TIMER_ID_RESET_DROP);
                }
                pThis->m_lastDropCount = si.DropPacketCount;
            }
            break;
        case TIMER_ID_UPDATE_HASH_LIST:
            if (pThis->IsOpen()) {
                HASH_INFO hashInfo = {0};
                hashInfo.hash = pThis->m_tsSender.GetFileHash();
                int pos = pThis->GetPosition();
                // 先頭or終端から5秒の範囲はレジューム情報を記録しない
                hashInfo.resumePos = pos <= 5000 || !pThis->IsExtending() && pThis->GetDuration()-5000 <= pos ? -1 : pos;
                pThis->UpdateFileInfoSetting(hashInfo);
            }
            break;
        case TIMER_ID_SYNC_CHAPTER:
            if (pThis->m_chapter.Sync()) {
                pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
                pThis->BeginWatchingNextChapter(false);
            }
            break;
        case TIMER_ID_WATCH_POS_GT:
            ::KillTimer(hwnd, TIMER_ID_WATCH_POS_GT);
            pThis->BeginWatchingNextChapter(false);
            break;
        }
        break;
    case WM_UPDATE_STATUS:
        //DEBUG_OUT(TEXT("CTvtPlay::FrameWindowProc(): WM_UPDATE_STATUS\n"));
        if (wParam) {
            pThis->m_statusView.Invalidate();
        }
        else {
            pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
            pThis->m_statusView.UpdateItem(STATUS_ITEM_POSITION);
        }
        break;
    case WM_QUERY_CLOSE_NEXT:
        pThis->Close();
        if (pThis->m_playlist.Next(pThis->IsAllRepeat()))
            pThis->OpenCurrent();
        else if (pThis->m_fAutoClose)
            pThis->m_pApp->Close();
        break;
    case WM_QUERY_SEEK_BGN:
        pThis->SeekToBegin();
        break;
    case WM_QUERY_RESET:
        DEBUG_OUT(TEXT("CTvtPlay::FrameWindowProc(): WM_QUERY_RESET\n"));
        if (pThis->m_resetMode <= 2) {
            pThis->m_pApp->Reset(wParam && pThis->m_resetMode==1 ? TVTest::RESET_ALL : TVTest::RESET_VIEWER);
        }
        if (wParam && pThis->m_resetDropInterval > 0) {
            pThis->m_lastDropCount = 0;
            ::SetTimer(hwnd, TIMER_ID_RESET_DROP, pThis->m_resetDropInterval, NULL);
        }
        break;
    case WM_SATISFIED_POS_GT:
        DEBUG_OUT(TEXT("CTvtPlay::FrameWindowProc(): WM_SATISFIED_POS_GT\n"));
        if (pThis->m_chapter.IsOpen()) {
            CChapterMap::const_iterator it = pThis->m_chapter.find(static_cast<int>(lParam) -
                                                                   pThis->m_supposedDispDelay);
            if (pThis->m_fSkipXChapter && it != pThis->m_chapter.end() &&
                it->second.IsIn() && it->second.IsX())
            {
                for (++it; it != pThis->m_chapter.end(); ++it) {
                    // スキップ終了チャプターまでシーク
                    if (it->second.IsOut() && it->second.IsX()) {
                        pThis->SeekAbsolute(it->first);
                        break;
                    }
                }
            }
            else if (pThis->m_fRepeatChapter && it != pThis->m_chapter.end() &&
                     it->second.IsOut() && it != pThis->m_chapter.begin())
            {
                bool isX = it->second.IsX();
                do {
                    // 対応する開始チャプターまでシーク
                    if ((--it)->second.IsIn() && (isX && it->second.IsX() || !isX && !it->second.IsX())) {
                        pThis->SeekAbsolute(it->first);
                        break;
                    }
                } while (it != pThis->m_chapter.begin());
            }
            pThis->BeginWatchingNextChapter(true);
        }
        break;
    case WM_APPCOMMAND:
        // メディアキー対策(オーナーウィンドウには自分で送る必要がある)
        ::SendMessage(::GetParent(hwnd), uMsg, wParam, lParam);
        return 0;
    }
    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}


void CTvtPlay::UpdateInfos()
{
    if (m_tsSender.IsOpen()) {
        bool fSpecialExt;
        m_infoPos = m_tsSender.GetPosition();
        m_infoDur = m_tsSender.GetDuration();
        m_infoTot = m_tsSender.GetBroadcastTime();
        m_infoExtMode = m_tsSender.IsFixed(&fSpecialExt) ? 0 : fSpecialExt ? 2 : 1;
        m_fInfoPaused = m_tsSender.IsPaused();
        // m_infoSpeedはm_tsSenderのもつ値と必ずしも一致しない
    }
    else {
        m_infoPos = m_infoDur = m_infoExtMode = 0;
        m_infoTot = -1;
        m_infoSpeed = 100;
        m_fInfoPaused = false;
    }
}


// TSデータの送信制御スレッド
DWORD WINAPI CTvtPlay::TsSenderThread(LPVOID pParam)
{
    MSG msg;
    DWORD_PTR dwRes;
    CTvtPlay *pThis = static_cast<CTvtPlay*>(pParam);
    int posSec, durSec, lastTot, lastExtMode, lastSpeed;
    bool fLastPaused;
    int speed = 100, posWatch = -1;
    int lowSpeed = 100;
    int stretchMode = 0;
    int fLowSpeed = false;
    static const int RESET_WAIT = 10;
    int resetCount = -RESET_WAIT;
    {
        CBlockLock lock(&pThis->m_tsInfoLock);
        pThis->m_infoSpeed = speed;
        posSec = pThis->m_infoPos / 1000;
        durSec = pThis->m_infoDur / 1000;
        lastTot = pThis->m_infoTot;
        lastExtMode = pThis->m_infoExtMode;
        lastSpeed = pThis->m_infoSpeed;
        fLastPaused = pThis->m_fInfoPaused;
    }
    pThis->m_tsSender.SetupQpc();

    for (;;) {
        BOOL rv = ::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
        ::SetEvent(pThis->m_hThreadEvent);
        if (rv) {
            if (msg.message == WM_QUIT) break;
            switch (msg.message) {
            case WM_TS_SET_UDP:
                if (1234 <= msg.lParam && msg.lParam <= 1243)
                    pThis->m_tsSender.SetUdpAddress(UDP_ADDR, static_cast<unsigned short>(msg.lParam));
                else
                    pThis->m_tsSender.SetUdpAddress("", 0);
                break;
            case WM_TS_SET_PIPE:
                if (0 <= msg.lParam && msg.lParam <= 9) {
                    TCHAR name[MAX_PATH];
                    ::wsprintf(name, PIPE_NAME, static_cast<int>(msg.lParam));
                    pThis->m_tsSender.SetPipeName(name);
                }
                else {
                    pThis->m_tsSender.SetPipeName(TEXT(""));
                }
                break;
            case WM_TS_PAUSE:
                pThis->m_tsSender.Pause(msg.wParam != 0);
                ::SendMessageTimeout(pThis->m_hwndFrame, WM_QUERY_RESET, 0, 0, SMTO_NORMAL, 1000, &dwRes);
                break;
            case WM_TS_SEEK_BGN:
                posWatch = -1;
                if (pThis->m_tsSender.SeekToBegin())
                    resetCount = !resetCount ? -RESET_WAIT : RESET_WAIT;
                break;
            case WM_TS_SEEK_END:
                posWatch = -1;
                if (pThis->m_tsSender.SeekToEnd())
                    resetCount = !resetCount ? -RESET_WAIT : RESET_WAIT;
                break;
            case WM_TS_SEEK:
                posWatch = -1;
                if (pThis->m_tsSender.Seek(static_cast<int>(msg.lParam)))
                    resetCount = !resetCount ? -RESET_WAIT : RESET_WAIT;
                break;
            case WM_TS_SEEK_ABSOLUTE:
                posWatch = -1;
                if (pThis->m_tsSender.GetPosition() >= 0 &&
                    pThis->m_tsSender.Seek(static_cast<int>(msg.lParam) - pThis->m_tsSender.GetPosition()))
                {
                    resetCount = !resetCount ? -RESET_WAIT : RESET_WAIT;
                }
                break;
            case WM_TS_SET_SPEED:
                speed = LOWORD(msg.lParam);
                lowSpeed = HIWORD(msg.lParam);
                stretchMode = static_cast<int>(msg.wParam);
                fLowSpeed = false;
#ifdef EN_SWC
                {
                    CBlockLock lock(&pThis->m_streamLock);
                    fLowSpeed = pThis->m_captionAnalyzer.CheckShowState();
                }
#endif
                if (!ASFilterSendMessageTimeout(WM_ASFLT_STRETCH, stretchMode, MAKELPARAM(fLowSpeed?lowSpeed:speed, 100), 1000)) {
                    // コマンド失敗の場合は等速にする
                    speed = lowSpeed = 100;
                }
                // テンポが変化する場合は転送速度を変更する
                pThis->m_tsSender.SetSpeed(stretchMode & 1 ? (fLowSpeed?lowSpeed:speed) : 100, 100);
                break;
            case WM_TS_SET_MOD_TS:
                pThis->m_tsSender.SetModTimestamp(msg.wParam != 0);
                break;
            case WM_TS_WATCH_POS_GT:
                posWatch = static_cast<int>(msg.lParam);
                break;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        else if (pThis->m_fHalt) {
            ::Sleep(20);
        }
        else {
            if (resetCount != 0) {
                // 最初のリセット要求はすみやかに送るが、連続して大量には送らない
                if (resetCount == -RESET_WAIT || resetCount == 1) {
                    ::SendMessageTimeout(pThis->m_hwndFrame, WM_QUERY_RESET, 1, 0, SMTO_NORMAL, 1000, &dwRes);
                    if (pThis->m_resetMode==2 || pThis->m_resetMode==3) {
                        pThis->m_tsSender.SendEmptyPat();
                    }
#ifdef EN_SWC
                    // PCRが不連続になるので一旦無効にする
                    CBlockLock lock(&pThis->m_streamLock);
                    pThis->m_captionAnalyzer.ClearShowState();
#endif
                }
                resetCount += resetCount < 0 ? 1 : -1;
                if (resetCount > 0) {
                    ::Sleep(20);
                    // コントロールの表示を更新する必要はないのでcontinueできる
                    continue;
                }
            }

            int rvSend = pThis->m_tsSender.Send();
            if (speed != 100) {
                // ファイル末尾に達したか、追っかけ時に(再生時間-3)秒以上の位置に達した場合
                if (!rvSend || (!pThis->m_tsSender.IsFixed() &&
                    pThis->m_tsSender.GetPosition() > pThis->m_tsSender.GetDuration() - 3000))
                {
                    // 等速に戻しておく
                    ASFilterSendNotifyMessage(WM_ASFLT_STRETCH, 0, MAKELPARAM(100, 100));
                    pThis->m_tsSender.SetSpeed(100, 100);
                    speed = lowSpeed = 100;
                }
            }
#ifdef EN_SWC
            if (speed != 100 && speed != lowSpeed) {
                int fSetSpeed = false;
                {
                    CBlockLock lock(&pThis->m_streamLock);
                    if (pThis->m_captionAnalyzer.CheckShowState()) {
                        if (!fLowSpeed) {
                            // 低速にする
                            ::OutputDebugString(TEXT("CTvtPlay::TsSenderThread(): *** LOW SPEED ***\n"));
                            fLowSpeed = true;
                            fSetSpeed = true;
                        }
                    }
                    else {
                        if (fLowSpeed) {
                            // 高速にする
                            ::OutputDebugString(TEXT("CTvtPlay::TsSenderThread(): *** NORMAL SPEED ***\n"));
                            fLowSpeed = false;
                            fSetSpeed = true;
                        }
                    }
                }
                if (fSetSpeed) {
                    if (!ASFilterSendMessageTimeout(WM_ASFLT_STRETCH, stretchMode, MAKELPARAM(fLowSpeed?lowSpeed:speed, 100), 1000)) {
                        // コマンド失敗の場合は等速にする
                        speed = lowSpeed = 100;
                    }
                    // テンポが変化する場合は転送速度を変更する
                    pThis->m_tsSender.SetSpeed(stretchMode & 1 ? (fLowSpeed?lowSpeed:speed) : 100, 100);
                }
            }
#endif
            if (!rvSend) {
                if (pThis->m_tsSender.IsFixed()) {
                    ::Sleep(pThis->m_supposedDispDelay + 300);
                    ::PostMessage(pThis->m_hwndFrame,
                                  pThis->m_fSingleRepeat ? WM_QUERY_SEEK_BGN : WM_QUERY_CLOSE_NEXT,
                                  0, 0);
                }
                ::Sleep(100);
            }
            else if (rvSend == 2) {
                // カット編集やドロップの可能性が高いのでビューアリセットする
                ::SendMessageTimeout(pThis->m_hwndFrame, WM_QUERY_RESET, 0, 0, SMTO_NORMAL, 1000, &dwRes);
                DEBUG_OUT(TEXT("CTvtPlay::TsSenderThread(): ResetRateControl\n"));
#ifdef EN_SWC
                // PCRが不連続になるので一旦無効にする
                CBlockLock lock(&pThis->m_streamLock);
                pThis->m_captionAnalyzer.ClearShowState();
#endif
            }
        }

        {
            CBlockLock lock(&pThis->m_tsInfoLock);
            pThis->UpdateInfos();
            pThis->m_infoSpeed = speed;
            if (posSec != pThis->m_infoPos / 1000 || durSec != pThis->m_infoDur / 1000 ||
                lastTot != pThis->m_infoTot || lastExtMode != pThis->m_infoExtMode ||
                lastSpeed != pThis->m_infoSpeed || fLastPaused != pThis->m_fInfoPaused)
            {
                // コントロールの表示を更新する
                ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_STATUS,
                              lastSpeed != pThis->m_infoSpeed || fLastPaused != pThis->m_fInfoPaused, 0);
                posSec = pThis->m_infoPos / 1000;
                durSec = pThis->m_infoDur / 1000;
                lastTot = pThis->m_infoTot;
                lastExtMode = pThis->m_infoExtMode;
                lastSpeed = pThis->m_infoSpeed;
                fLastPaused = pThis->m_fInfoPaused;
            }
            if (posWatch >= 0 && pThis->m_infoPos > posWatch) {
                // 再生位置が監視位置を越えたことを伝える
                ::SendNotifyMessage(pThis->m_hwndFrame, WM_SATISFIED_POS_GT, 0, posWatch);
                posWatch = -1;
            }
        }
    }

    // 等速に戻しておく
    ASFilterSendNotifyMessage(WM_ASFLT_STRETCH, 0, MAKELPARAM(100, 100));
    return 0;
}


#ifdef EN_SWC
// ストリームコールバック(別スレッド)
BOOL CALLBACK CTvtPlay::StreamCallback(BYTE *pData, void *pClientData)
{
    CTvtPlay *pThis = static_cast<CTvtPlay*>(pClientData);
    TS_HEADER header;
    extract_ts_header(&header, pData);

    // Early reject
    if ((header.adaptation_field_control&2)/*2,3*/ ||
        header.pid == pThis->m_captionAnalyzer.GetPid())
    {
        CBlockLock lock(&pThis->m_streamLock);
        pThis->m_captionAnalyzer.AddPacket(pData);
    }
    return TRUE;
}
#endif


TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTvtPlay;
}
