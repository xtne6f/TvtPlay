﻿// TVTestにtsファイル再生機能を追加するプラグイン
// 最終更新: 2023-09-30
// 署名: 849fa586809b0d16276cd644c6749503
#include <Windows.h>
#include <WindowsX.h>
#include <Shlwapi.h>
#include <algorithm>
#include <list>
#include <vector>
#include <map>
#include <process.h>
#include "Util.h"
#include "StatusView.h"
#include "TsSender.h"
#include "Playlist.h"
#include "ChapterMap.h"

#ifdef EN_SWC
#include "Caption.h"
#include <regex>
#include "CaptionAnalyzer.h"
#endif

#include "resource.h"
#include "TvtPlayUtil.h"
#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0,0,14)
#include "TVTestPlugin.h"
#include "TvtPlay.h"

#define INFO_DESCRIPTION_SUFFIX L"+)"

static const WCHAR INFO_PLUGIN_NAME[] = L"TvtPlay";
static const WCHAR INFO_DESCRIPTION[] = L"ファイル再生機能を追加 (ver.3.1" INFO_DESCRIPTION_SUFFIX;
static const int INFO_VERSION = 25;

#define WM_UPDATE_STATUS    (WM_APP + 1)
#define WM_QUERY_CLOSE_NEXT (WM_APP + 2)
#define WM_QUERY_SEEK_BGN   (WM_APP + 3)
#define WM_QUERY_RESET      (WM_APP + 4)
#define WM_SATISFIED_POS_GT (WM_APP + 5)

// TvtPlayから他プラグインに情報提供するメッセージ
#define TVTP_CURRENT_MSGVER 1
#define WM_TVTP_GET_MSGVER      (WM_APP + 50)
#define WM_TVTP_IS_OPEN         (WM_APP + 51)
#define WM_TVTP_GET_POSITION    (WM_APP + 52)
#define WM_TVTP_GET_DURATION    (WM_APP + 53)
#define WM_TVTP_GET_TOT_TIME    (WM_APP + 54)
#define WM_TVTP_IS_EXTENDING    (WM_APP + 55)
#define WM_TVTP_IS_PAUSED       (WM_APP + 56)
#define WM_TVTP_GET_PLAY_FLAGS  (WM_APP + 57)
#define WM_TVTP_GET_STRETCH     (WM_APP + 58)
#define WM_TVTP_GET_PATH        (WM_APP + 59)
#define WM_TVTP_SEEK            (WM_APP + 60)
#define WM_TVTP_SEEK_ABSOLUTE   (WM_APP + 61)

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
#define WM_TS_INIT_DONE     (WM_APP + 11)

static const TCHAR SETTINGS[] = TEXT("Settings");
static const TCHAR TVTPLAY_FRAME_WINDOW_CLASS[] = TEXT("TvtPlay Frame");

enum {
    TIMER_ID_RESET_DROP = 2,
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
    {ID_COMMAND_CHAPTER_POPUP, L"ChapterPopup", L"チャプターを選択:ポップアップ"},
    {ID_COMMAND_LOOP, L"Loop", L"全体/シングル/リピートしない"},
    {ID_COMMAND_PAUSE, L"Pause", L"一時停止/再生"},
    {ID_COMMAND_NOP, L"Nop", L"何もしない"},
    {ID_COMMAND_STRETCH, L"Stretch", L"倍速:切り替え"},
    {ID_COMMAND_STRETCH_RE, L"StretchRe", L"倍速:逆順切り替え"},
    {ID_COMMAND_STRETCH_POPUP, L"StretchPopup", L"倍速:ポップアップ"},
};

static const int DEFAULT_SEEK_LIST[COMMAND_S_MAX] = {
    -60000, -30000, -15000, -5000, 4000, 14000, 29000, 59000
};

static const int DEFAULT_STRETCH_LIST[COMMAND_S_MAX] = {
    400, 200, 50, 25
};

static const LPCTSTR DEFAULT_BUTTON_LIST[] = {
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
    TEXT(";'*'1' '.'0:30~'*'4'.'0:30~'*'2'.'0:30~'*'0'.'5:30~'*'0'.'2,Width=32,StretchPopup,Stretch"),
    nullptr
};


CTvtPlay::CTvtPlay()
    : m_fInitialized(false)
    , m_fSettingsLoaded(false)
    , m_fForceEnable(false)
    , m_fIgnoreExt(false)
    , m_fAutoEnUdp(false)
    , m_fAutoEnPipe(false)
    , m_fEventStartupDone(false)
    , m_fPausedOnPreviewChange(false)
    , m_specOffset(-1)
    , m_specStretchID(-1)
    , m_fShowOpenDialog(false)
    , m_fRaisePriority(false)
    , m_hwndFrame(nullptr)
    , m_fSeekDrawOfs(false)
    , m_fSeekDrawTot(false)
    , m_fPosDrawTot(false)
    , m_fAutoClose(false)
    , m_seekItemMinWidth(0)
    , m_posItemWidth(0)
    , m_seekItemOrder(0)
    , m_posItemOrder(0)
    , m_lastDropCount(0)
    , m_resetDropInterval(0)
    , m_seekListNum(0)
    , m_stretchListNum(0)
    , m_popupMax(0)
    , m_playlistPopupMax(0)
    , m_fPopupDesc(false)
    , m_fPopuping(false)
    , m_fDialogOpen(false)
    , m_seekMode(0)
    , m_apparentPos(-1)
    , m_hThread(nullptr)
    , m_hThreadEvent(nullptr)
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
    , m_modTimestampMode(0)
    , m_initialStretchID(-1)
    , m_pcrThresholdMsec(0)
    , m_fTryGaplessPause(false)
    , m_salt(0)
    , m_hashListMax(0)
    , m_fUpdateHashList(false)
    , m_streamCallbackRefCount(0)
    , m_fResetPat(false)
#ifdef EN_SWC
    , m_slowerWithCaption(0)
    , m_swcShowLate(0)
    , m_swcClearEarly(0)
    , m_pcr(0)
    , m_captionPid(-1)
#endif
{
    m_szIniFileName[0] = 0;
    m_szSpecFileName[0] = 0;
    m_szIconFileName[0] = 0;
    m_szPopupPattern[0] = 0;
    m_szChaptersDirName[0] = 0;
#ifdef EN_SWC
    m_szCaptionDllPath[0] = 0;
    PAT zeroPat = {};
    m_pat = zeroPat;
#endif
}

CTvtPlay::~CTvtPlay()
{
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
    m_specStretchID = -1;

    int argc;
    LPTSTR *argv = ::CommandLineToArgvW(cmdLine, &argc);
    if (argv) {
        for (int i = fIgnoreFirst; i < argc; ++i) {
            // オプションは複数起動禁止時に無効->有効にすることができる
            if (argv[i][0] == TEXT('/') || argv[i][0] == TEXT('-')) {
                if (!_tcsicmp(argv[i] + 1, TEXT("tvtplay"))) m_fForceEnable = m_fIgnoreExt = true;
                else if (!_tcsicmp(argv[i] + 1, TEXT("tvtpudp"))) m_fAutoEnUdp = true;
                else if (!_tcsicmp(argv[i] + 1, TEXT("tvtpipe"))) m_fAutoEnPipe = true;
                else if (!_tcsicmp(argv[i] + 1, TEXT("tvtpofs")) && i+1 < argc) {
                    ++i;
                    if (TEXT('0') <= argv[i][0] && argv[i][0] <= TEXT('9')) {
                        m_specOffset = _tcstol(argv[i], nullptr, 10);
                        size_t n = _tcscspn(argv[i], TEXT("+-"));
                        if (argv[i][n] == TEXT('+')) ++n;
                        m_specOffset += _tcstol(argv[i] + n, nullptr, 10);
                        if (m_specOffset < 0) m_specOffset = 0;
                    }
                }
                else if (!_tcsicmp(argv[i] + 1, TEXT("tvtpstr")) && i+1 < argc) {
                    ++i;
                    int stid = argv[i][0] - (argv[i][0] < TEXT('a') ? TEXT('A') : TEXT('a'));
                    if (0 <= stid && stid < COMMAND_S_MAX) {
                        m_specStretchID = stid;
                    }
                }
            }
        }

        if (argc >= 1 + fIgnoreFirst && argv[argc-1][0] != TEXT('/') && argv[argc-1][0] != TEXT('-')) {
            bool fSpec = m_fIgnoreExt;
            if (!m_fIgnoreExt) {
                if (CPlaylist::IsPlayListFile(argv[argc-1]) || CPlaylist::IsMediaFile(argv[argc-1])) {
                    m_fForceEnable = true;
                    fSpec = true;
                }
            }
            if (fSpec) {
                _tcsncpy_s(m_szSpecFileName, argv[argc-1], _TRUNCATE);
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

    // コマンドを登録
    m_pApp->RegisterCommand(COMMAND_LIST, _countof(COMMAND_LIST));

    // 設定に応じてコマンド数をふやす
    std::vector<TCHAR> buf = GetPrivateProfileSectionBuffer(SETTINGS, m_szIniFileName);
    for (int i = 0; i < COMMAND_S_MAX; ++i) {
        TCHAR key[16], name[16];
        _stprintf_s(key, TEXT("Seek%c"), TEXT('A') + i);
        _stprintf_s(name, TEXT("シーク:%c"), TEXT('A') + i);
        if (!DEFAULT_SEEK_LIST[i]) {
            if (!GetBufferedProfileInt(buf.data(), key, 0)) break;
        }
        m_pApp->RegisterCommand(ID_COMMAND_SEEK_A + i, key, name);
    }
    for (int i = 0; i < COMMAND_S_MAX; ++i) {
        TCHAR key[16], name[16];
        _stprintf_s(key, TEXT("Stretch%c"), TEXT('A') + i);
        _stprintf_s(name, TEXT("倍速:%c"), TEXT('A') + i);
        if (!DEFAULT_STRETCH_LIST[i]) {
            if (!GetBufferedProfileInt(buf.data(), key, 0)) break;
        }
        m_pApp->RegisterCommand(ID_COMMAND_STRETCH_A + i, key, name);
    }

    // TVTestの変数登録を初期化
    TVTest::RegisterVariableInfo rvi;
    rvi.Flags = 0;
    rvi.pszKeyword = L"playback-filename";
    rvi.pszDescription = L"再生ファイル名";
    rvi.pszValue = L"";
    m_pApp->RegisterVariable(&rvi);
    rvi.Flags = 0;
    rvi.pszKeyword = L"playback-nts-filename";
    rvi.pszDescription = L"再生ファイル名(非TS時のみ)";
    rvi.pszValue = L"";
    m_pApp->RegisterVariable(&rvi);

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);

    TCHAR cmdOption[128];
    GetBufferedProfileString(buf.data(), TEXT("TvtpCmdOption"), TEXT(""), cmdOption, _countof(cmdOption));

    // コマンドラインで指定されていなければ設定ファイルの値を適用する
    AnalyzeCommandLine(cmdOption, false);
    int preSpecOffset = m_specOffset;
    int preSpecStretchID = m_specStretchID;
    AnalyzeCommandLine(::GetCommandLine(), true);
    if (m_specOffset < 0) m_specOffset = preSpecOffset;
    if (m_specStretchID < 0) m_specStretchID = preSpecStretchID;

    // ステータス項目を登録
    TVTest::StatusItemInfo item;
    item.Size = sizeof(item);
    item.Flags = 0;
    item.Style = TVTest::STATUS_ITEM_STYLE_FORCEFULLROW;
    item.ID = 1;
    item.pszIDText = L"TvtPlayStatus";
    item.pszName = L"TvtPlayステータスバー";
    item.MinWidth = 0;
    item.MaxWidth = -1;
    item.DefaultWidth = TVTest::StatusItemWidthByFontSize(15);
    item.MinHeight = 0;
    return m_pApp->RegisterStatusItem(&item);
}


// 終了処理
bool CTvtPlay::Finalize()
{
    if (m_pApp->IsPluginEnabled()) EnablePlugin(false);
    // 本体や他プラグインとの干渉を防ぐため、一旦有効にしたD&Dは最後まで維持する
    if (m_fInitialized) {
        ::DragAcceptFiles(m_pApp->GetAppWindow(), FALSE);
    }
    return true;
}


// 設定の読み込み
void CTvtPlay::LoadSettings()
{
    if (m_fSettingsLoaded) return;

    int iniVer = 0;
    {
        std::vector<TCHAR> buf = GetPrivateProfileSectionBuffer(SETTINGS, m_szIniFileName);
        LPCTSTR pBuf = buf.data();
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
        m_modTimestampMode  = GetBufferedProfileInt(pBuf, TEXT("TsAvoidWraparound"), 0);
        m_pcrThresholdMsec  = GetBufferedProfileInt(pBuf, TEXT("TsPcrDiscontinuityThreshold"), 400);
        m_fTryGaplessPause  = GetBufferedProfileInt(pBuf, TEXT("TsTryGaplessPause"), 0) != 0;
        m_fShowOpenDialog   = GetBufferedProfileInt(pBuf, TEXT("ShowOpenDialog"), 0) != 0;
        m_fRaisePriority    = GetBufferedProfileInt(pBuf, TEXT("RaiseMainThreadPriority"), 0) != 0;
        m_seekMode          = GetBufferedProfileInt(pBuf, TEXT("SeekMode"), 1);
        m_fSeekDrawOfs      = GetBufferedProfileInt(pBuf, TEXT("DispOffset"), 0) != 0;
        m_fSeekDrawTot      = GetBufferedProfileInt(pBuf, TEXT("DispTot"), 0) != 0;
        m_fPosDrawTot       = GetBufferedProfileInt(pBuf, TEXT("DispTotOnStatus"), 0) != 0;
        m_fAutoClose        = GetBufferedProfileInt(pBuf, TEXT("AutoClose"), 0) != 0;
        m_seekItemMinWidth  = GetBufferedProfileInt(pBuf, TEXT("SeekItemMinWidth"), 128);
        m_posItemWidth      = GetBufferedProfileInt(pBuf, TEXT("StatusItemWidth"), -1);
        m_salt              = GetBufferedProfileInt(pBuf, TEXT("Salt"), (::GetTickCount()>>16)^(::GetTickCount()&0xffff));
        m_hashListMax       = GetBufferedProfileInt(pBuf, TEXT("FileInfoMax"), 0);
        m_fUpdateHashList   = GetBufferedProfileInt(pBuf, TEXT("FileInfoAutoUpdate"), 0) != 0;
        m_popupMax          = GetBufferedProfileInt(pBuf, TEXT("PopupMax"), 30);
        m_fPopupDesc        = GetBufferedProfileInt(pBuf, TEXT("PopupDesc"), 0) != 0;
        GetBufferedProfileString(pBuf, TEXT("PopupPattern"), TEXT("%RecordFolder%*.ts"), m_szPopupPattern, _countof(m_szPopupPattern));
        m_playlistPopupMax  = GetBufferedProfileInt(pBuf, TEXT("PlaylistPopupMax"), 30);
        GetBufferedProfileString(pBuf, TEXT("ChaptersFolderName"), TEXT("chapters"), m_szChaptersDirName, _countof(m_szChaptersDirName));
#ifdef EN_SWC
        GetBufferedProfileString(pBuf, TEXT("CaptionDll"), TEXT("Plugins\\TvtPlay_Caption.dll"), m_szCaptionDllPath, _countof(m_szCaptionDllPath));
        m_slowerWithCaption = GetBufferedProfileInt(pBuf, TEXT("SlowerWithCaption"), 0);
        m_swcShowLate       = GetBufferedProfileInt(pBuf, TEXT("SlowerWithCaptionShowLate"), 450);
        m_swcShowLate       = min(max(m_swcShowLate, -5000), 5000);
        m_swcClearEarly     = GetBufferedProfileInt(pBuf, TEXT("SlowerWithCaptionClearEarly"), -450);
        m_swcClearEarly     = min(max(m_swcClearEarly, -5000), 5000);
#endif
        m_seekItemOrder     = GetBufferedProfileInt(pBuf, TEXT("SeekItemOrder"), 99);
        m_posItemOrder      = GetBufferedProfileInt(pBuf, TEXT("StatusItemOrder"), 99);
        GetBufferedProfileString(pBuf, TEXT("IconImage"), TEXT(""), m_szIconFileName, _countof(m_szIconFileName));

        // シークコマンドのシーク量設定を取得
        m_seekListNum = 0;
        while (m_seekListNum < COMMAND_S_MAX) {
            TCHAR key[16];
            _stprintf_s(key, TEXT("Seek%c"), TEXT('A') + m_seekListNum);
            int val = GetBufferedProfileInt(pBuf, key, DEFAULT_SEEK_LIST[m_seekListNum]);
            if (!val) break;
            m_seekList[m_seekListNum++] = val;
        }
        // 倍速コマンドの倍率(25%～800%)設定を取得
        m_stretchListNum = 0;
        while (m_stretchListNum < COMMAND_S_MAX) {
            TCHAR key[16];
            _stprintf_s(key, TEXT("Stretch%c"), TEXT('A') + m_stretchListNum);
            int val = GetBufferedProfileInt(pBuf, key, DEFAULT_STRETCH_LIST[m_stretchListNum]);
            if (!val) break;
            m_stretchList[m_stretchListNum++] = min(max(val, 25), 800);
        }
        // ボタンアイテムの配置設定を取得
        int j = 0;
        for (int i = 0; i < BUTTON_MAX; ++i) {
            TCHAR key[16];
            _stprintf_s(key, TEXT("Button%02d"), i);
            GetBufferedProfileString(pBuf, key, DEFAULT_BUTTON_LIST[j] ? DEFAULT_BUTTON_LIST[j++] : TEXT(""),
                                     m_buttonList[i], _countof(m_buttonList[0]));
        }
    }

    m_fSettingsLoaded = true;

    if (iniVer < INFO_VERSION) {
        // デフォルトの設定キーを出力するため
        SaveSettings(true);
    }
    // もしなければFileInfoの有効/無効キーをつくる
    if (::GetPrivateProfileInt(TEXT("FileInfo"), TEXT("Enabled"), 2, m_szIniFileName) > 1) {
        WritePrivateProfileInt(TEXT("FileInfo"), TEXT("Enabled"), 1, m_szIniFileName);
    }
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
    WritePrivateProfileInt(SETTINGS, TEXT("TsAvoidWraparound"), m_modTimestampMode, m_szIniFileName);
    if (fWriteDefault) {
        WritePrivateProfileInt(SETTINGS, TEXT("TsPcrDiscontinuityThreshold"), m_pcrThresholdMsec, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("TsTryGaplessPause"), m_fTryGaplessPause, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("ShowOpenDialog"), m_fShowOpenDialog, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("SeekMode"), m_seekMode, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("DispOffset"), m_fSeekDrawOfs, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("DispTot"), m_fSeekDrawTot, m_szIniFileName);
    }
    WritePrivateProfileInt(SETTINGS, TEXT("DispTotOnStatus"), m_fPosDrawTot, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("AutoClose"), m_fAutoClose, m_szIniFileName);
    if (fWriteDefault) {
        WritePrivateProfileInt(SETTINGS, TEXT("SeekItemMinWidth"), m_seekItemMinWidth, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("StatusItemWidth"), m_posItemWidth, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("Salt"), m_salt, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("FileInfoMax"), m_hashListMax, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("FileInfoAutoUpdate"), m_fUpdateHashList, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("PopupMax"), m_popupMax, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("PopupDesc"), m_fPopupDesc, m_szIniFileName);
        ::WritePrivateProfileString(SETTINGS, TEXT("PopupPattern"), m_szPopupPattern, m_szIniFileName);
        WritePrivateProfileInt(SETTINGS, TEXT("PlaylistPopupMax"), m_playlistPopupMax, m_szIniFileName);
        ::WritePrivateProfileString(SETTINGS, TEXT("ChaptersFolderName"), m_szChaptersDirName, m_szIniFileName);
#ifdef EN_SWC
        ::WritePrivateProfileString(SETTINGS, TEXT("CaptionDll"), m_szCaptionDllPath, m_szIniFileName);
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
            _stprintf_s(key, TEXT("Seek%c"), TEXT('A') + i);
            WritePrivateProfileInt(SETTINGS, key, m_seekList[i], m_szIniFileName);
        }
        for (int i = 0; i < m_stretchListNum; ++i) {
            TCHAR key[16];
            _stprintf_s(key, TEXT("Stretch%c"), TEXT('A') + i);
            WritePrivateProfileInt(SETTINGS, key, m_stretchList[i], m_szIniFileName);
        }
        for (int i = 0; i < BUTTON_MAX; ++i) {
            TCHAR key[16];
            _stprintf_s(key, TEXT("Button%02d"), i);
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

    std::vector<TCHAR> buf = GetPrivateProfileSectionBuffer(TEXT("FileInfo"), m_szIniFileName);
    if (!GetBufferedProfileInt(buf.data(), TEXT("Enabled"), 0)) {
        return false;
    }
    for (int i = 0; i < m_hashListMax; ++i) {
        // キー番号が小さいものほど新しい
        HASH_INFO hashInfo;
        TCHAR key[32], val[64];
        _stprintf_s(key, TEXT("Hash%d"), i);
        GetBufferedProfileString(buf.data(), key, TEXT(""), val, _countof(val));
        if (!val[0]) break;
        if (!::StrToInt64Ex(val, STIF_SUPPORT_HEX, &hashInfo.hash)) continue;

        _stprintf_s(key, TEXT("Resume%d"), i);
        hashInfo.resumePos = GetBufferedProfileInt(buf.data(), key, 0);
        hashList.push_back(hashInfo);
    }
    return true;
}

// ファイル固有情報のリストを保存する
void CTvtPlay::SaveFileInfoSetting(const std::list<HASH_INFO> &hashList) const
{
    if (!m_fSettingsLoaded || m_hashListMax <= 0) return;

    // 不整合を防ぐため一度に書き込む
    std::vector<TCHAR> buf(32 + hashList.size() * 96);
    size_t pos = _stprintf_s(buf.data(), buf.size(), TEXT("Enabled=1")) + 1;
    std::list<HASH_INFO>::const_iterator it = hashList.begin();
    for (int i = 0; i < m_hashListMax && it != hashList.end(); ++i, ++it) {
        pos += _stprintf_s(buf.data() + pos, buf.size() - pos, TEXT("Hash%d=0x%06x%08x"), i, (DWORD)(it->hash>>32), (DWORD)(it->hash)) + 1;
        pos += _stprintf_s(buf.data() + pos, buf.size() - pos, TEXT("Resume%d=%d"), i, it->resumePos) + 1;
    }
    buf[pos] = 0;
    ::WritePrivateProfileSection(TEXT("FileInfo"), buf.data(), m_szIniFileName);
}

// ファイル固有情報を更新する
void CTvtPlay::UpdateFileInfoSetting(const HASH_INFO &hashInfo, LONGLONG oldHash) const
{
    std::list<HASH_INFO> hashList;
    if (LoadFileInfoSetting(hashList)) {
        // リストから削除
        std::list<HASH_INFO>::iterator it = hashList.begin();
        while (it != hashList.end()) {
            if (it->hash == hashInfo.hash || it->hash == oldHash) {
                it = hashList.erase(it);
            }
            else {
                ++it;
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
    WNDCLASS wc = {};
    wc.lpfnWndProc      = FrameWindowProc;
    wc.hInstance        = g_hinstDLL;
    wc.lpszClassName    = TVTPLAY_FRAME_WINDOW_CLASS;
    if (!::RegisterClass(&wc)) return false;

    LoadSettings();

    // アイコン画像読み込み
    DrawUtil::CBitmap iconMap;
    int iconSize = 0;
    if (m_szIconFileName[0]) {
        if (iconMap.Load(nullptr, m_szIconFileName, LR_CREATEDIBSECTION | LR_LOADFROMFILE)) {
            iconSize = iconMap.GetHeight();
            if (iconSize < ICON_SIZE_MIN || ICON_SIZE_MAX < iconSize) {
                iconMap.Destroy();
            }
        }
    }
    if (!iconMap.IsCreated()) {
        if (!iconMap.Load(g_hinstDLL, IDB_BUTTONS)) return false;
        iconSize = iconMap.GetHeight();
    }

    DrawUtil::CBitmap iconS, iconL;
    if (iconSize < ICON_SIZE_MIN || ICON_SIZE_MAX < iconSize ||
        !iconS.Create(iconSize * 3, iconSize, 1) ||
        !iconL.Create(iconSize * COMMAND_S_MAX, iconSize, 1)) return false;

    HDC hdcMem = ::CreateCompatibleDC(nullptr);
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
            new CSeekStatusItem(&m_statusView, this, m_fSeekDrawOfs, m_fSeekDrawTot, m_seekItemMinWidth, m_seekMode);
        }
        if (i == posItemOrder) {
            new CPositionStatusItem(&m_statusView, this);
        }
        if (i < 0 || BUTTON_MAX <= i || m_buttonList[i][0] == TEXT(';')) continue;

        // コマンドはカンマ区切りで2つまで指定できる
        int cmdID[2] = {-1, -1};
        int cmdCount = 0, width = iconSize;
        TCHAR text[BUTTON_TEXT_MAX];
        _tcscpy_s(text, m_buttonList[i]);
        for (;;) {
            LPTSTR cmd = _tcsrchr(text, TEXT(','));
            if (!cmd) break;

            if (!_tcsnicmp(cmd + 1, TEXT("Width="), 6)) {
                width = _tcstol(cmd + 7, nullptr, 10);
            }
            else if (cmdCount < 2) {
                if (_tcsicmp(cmd + 1, TEXT("SeekA")) >= 0 &&
                    _tcsicmp(cmd + 1, TEXT("SeekZ")) <= 0 &&
                    _tcslen(cmd) == 6)
                {
                    cmdID[cmdCount++] = ID_COMMAND_SEEK_A + cmd[5] - (cmd[5] < TEXT('a') ? TEXT('A') : TEXT('a'));
                }
                else if (_tcsicmp(cmd + 1, TEXT("StretchA")) >= 0 &&
                         _tcsicmp(cmd + 1, TEXT("StretchZ")) <= 0 &&
                         _tcslen(cmd) == 9)
                {
                    cmdID[cmdCount++] = ID_COMMAND_STRETCH_A + cmd[8] - (cmd[8] < TEXT('a') ? TEXT('A') : TEXT('a'));
                }
                else {
                    for (int k = 0; k < _countof(COMMAND_LIST); ++k) {
                        if (!_tcsicmp(cmd + 1, COMMAND_LIST[k].pszText)) {
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

        // StretchとStretchReとStretchPopupのみ大きいビットマップを使う
        DrawUtil::CBitmap *pIcon = (cmdID[0] == ID_COMMAND_STRETCH ||
                                    cmdID[0] == ID_COMMAND_STRETCH_RE ||
                                    cmdID[0] == ID_COMMAND_STRETCH_POPUP) ? &iconL : &iconS;
        HBITMAP hbmOld = SelectBitmap(hdcMem, pIcon->GetHandle());

        RGBQUAD rgbq[2] = {};
        rgbq[1].rgbBlue = rgbq[1].rgbGreen = rgbq[1].rgbRed = 255;
        ::SetDIBColorTable(hdcMem, 0, 2, rgbq);

        ComposeMonoColorIcon(hdcMem, 0, 0, iconMap.GetHandle(), m_buttonList[i], iconSize);

        new CButtonStatusItem(&m_statusView, this, STATUS_ITEM_BUTTON + cmdID[0],
                              STATUS_ITEM_BUTTON + cmdID[1], width, *pIcon);
        SelectBitmap(hdcMem, hbmOld);
    }
    ::DeleteDC(hdcMem);

    ::DragAcceptFiles(m_pApp->GetAppWindow(), TRUE);

    m_fInitialized = true;
    return true;
}


// 字幕PIDを取得する(無い場合は-1)
// プラグインAPIが内部でストリームをロックするので、デッドロックを完成させないように注意
int CTvtPlay::GetCaptionPid()
{
    int index = m_pApp->GetService();
    TVTest::ServiceInfo si;
    if (index >= 0 && m_pApp->GetServiceInfo(index, &si) && si.SubtitlePID != 0) {
        return si.SubtitlePID;
    }
    return -1;
}


// プラグインの有効状態が変化した
bool CTvtPlay::EnablePlugin(bool fEnable) {
    if (fEnable) {
        if (!InitializePlugin()) return false;

        if (!m_hwndFrame) {
            m_hwndFrame = ::CreateWindow(TVTPLAY_FRAME_WINDOW_CLASS, nullptr, WS_POPUP, 0, 0, 0, 0,
                                         m_pApp->GetAppWindow(), nullptr, g_hinstDLL, this);
            if (!m_hwndFrame) return false;
        }

        // ステータス項目の表示
        TVTest::StatusItemSetInfo info;
        info.Size = sizeof(info);
        info.Mask = TVTest::STATUS_ITEM_SET_INFO_MASK_STATE;
        info.ID = 1;
        info.State = info.StateMask = TVTest::STATUS_ITEM_STATE_VISIBLE;
        m_pApp->SetStatusItem(&info);
#ifdef EN_SWC
        TCHAR blacklistPath[MAX_PATH + 32];
        if (::GetModuleFileName(g_hinstDLL, blacklistPath, MAX_PATH)) {
            ::PathRemoveExtension(blacklistPath);
            _tcscat_s(blacklistPath, TEXT("_Blacklist.txt"));
            if (m_captionAnalyzer.Initialize(m_szCaptionDllPath, blacklistPath, m_swcShowLate, m_swcClearEarly)) {
                m_captionPid = GetCaptionPid();
                m_fResetPat = true;
                // ストリームコールバックの登録
                AddStreamCallback();
            }
        }
#endif
        if (m_fShowOpenDialog && !m_szSpecFileName[0] && !m_pApp->GetStandby()) OpenWithDialog();

        m_pApp->SetWindowMessageCallback(WindowMsgCallback, this);
    }
    else {
        m_pApp->SetWindowMessageCallback(nullptr, nullptr);
        Close();
#ifdef EN_SWC
        if (m_captionAnalyzer.IsInitialized()) {
            // ストリームコールバックの登録解除
            RemoveStreamCallback();
            // ここでは必ず登録解除されていることを前提にしている
            m_captionAnalyzer.UnInitialize();
        }
#endif
        // ステータス項目の非表示
        TVTest::StatusItemSetInfo info;
        info.Size = sizeof(info);
        info.Mask = TVTest::STATUS_ITEM_SET_INFO_MASK_STATE;
        info.ID = 1;
        info.StateMask = TVTest::STATUS_ITEM_STATE_VISIBLE;
        info.State = 0;
        m_pApp->SetStatusItem(&info);

        if (m_hwndFrame) {
            ::DestroyWindow(m_hwndFrame);
            m_hwndFrame = nullptr;
        }
    }
    return true;
}


// ポップアップメニュー選択でプラグイン設定する
void CTvtPlay::SetupWithPopup(const POINT &pt, UINT flags)
{
    // メニュー生成
    int selID = 0;
    HMENU hTopMenu = ::LoadMenu(g_hinstDLL, MAKEINTRESOURCE(IDR_MENU_SETUP));
    if (hTopMenu) {
        HMENU hmenu = ::GetSubMenu(hTopMenu, 0);
        ::CheckMenuItem(hmenu, IDM_POS_DRAW_TOT, m_fPosDrawTot ? MF_CHECKED : MF_UNCHECKED);
        ::CheckMenuItem(hmenu, IDM_MOD_TIMESTAMP, m_modTimestampMode==1 ? MF_CHECKED : MF_UNCHECKED);
        ::CheckMenuItem(hmenu, IDM_MOD_TIMESTAMP2, m_modTimestampMode==2 ? MF_CHECKED : MF_UNCHECKED);
        ::CheckMenuItem(hmenu, IDM_AUTO_CLOSE, m_fAutoClose ? MF_CHECKED : MF_UNCHECKED);
        if (m_pApp->GetVersion() < TVTest::MakeVersion(0,8,0)) {
            ::DeleteMenu(hmenu, IDM_MOD_TIMESTAMP2, 0);
        }
#ifdef EN_SWC
        HMENU hSubMenu = ::CreatePopupMenu();
        if (hSubMenu) {
            TCHAR str[32];
            for (int i = 0; i < m_stretchListNum; ++i) {
                if (m_stretchList[i] > 100) {
                    _stprintf_s(str, TEXT("=%d%%"), m_stretchList[i]);
                    ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==-m_stretchList[i]?MF_CHECKED:MF_UNCHECKED), 105 + i, str);
                }
            }
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==-100?MF_CHECKED:MF_UNCHECKED), 101, TEXT("=100%"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==75  ?MF_CHECKED:MF_UNCHECKED), 102, TEXT("0.75倍"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==50  ?MF_CHECKED:MF_UNCHECKED), 103, TEXT("0.50倍"));
            ::AppendMenu(hSubMenu, MF_STRING|(m_slowerWithCaption==25  ?MF_CHECKED:MF_UNCHECKED), 104, TEXT("0.25倍"));
            _tcscpy_s(str, TEXT("字幕でゆっくり"));
            if (m_slowerWithCaption > 0) {
                _stprintf_s(str, TEXT("字幕でゆっくり [0.%02d倍]"), m_slowerWithCaption);
            }
            else if (m_slowerWithCaption < 0) {
                _stprintf_s(str, TEXT("字幕でゆっくり [=%d%%]"), -m_slowerWithCaption);
            }
            ::AppendMenu(hmenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSubMenu), str);
        }
#endif
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hTopMenu);
    }

    switch (selID) {
    case IDM_POS_DRAW_TOT:
        {
        m_fPosDrawTot = !m_fPosDrawTot;
        SetWidthPositionItem();
        m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
        SaveSettings();
        }
        break;
    case IDM_MOD_TIMESTAMP:
        m_modTimestampMode = m_modTimestampMode != 1 ? 1 : 0;
        SetModTimestamp(m_modTimestampMode == 1);
        SaveSettings();
        break;
    case IDM_MOD_TIMESTAMP2:
        m_modTimestampMode = m_modTimestampMode != 2 ? 2 : 0;
        SetModTimestamp(false);
        SaveSettings();
        break;
    case IDM_AUTO_CLOSE:
        m_fAutoClose = !m_fAutoClose;
        SaveSettings();
        break;
    default:
#ifdef EN_SWC
        if (101 <= selID && selID-105 < m_stretchListNum) {
            if (selID == 101) m_slowerWithCaption = m_slowerWithCaption==-100 ? 0 : -100;
            else if (selID == 102) m_slowerWithCaption = m_slowerWithCaption==75 ? 0 : 75;
            else if (selID == 103) m_slowerWithCaption = m_slowerWithCaption==50 ? 0 : 50;
            else if (selID == 104) m_slowerWithCaption = m_slowerWithCaption==25 ? 0 : 25;
            else m_slowerWithCaption = m_slowerWithCaption==-m_stretchList[selID-105] ? 0 : -m_stretchList[selID-105];
            Stretch(GetStretchID());
            SaveSettings();
        }
#endif
        break;
    }
}


// TVTestのフルスクリーンHWNDを取得する
// 必ず取得できると仮定してはいけない
HWND CTvtPlay::GetFullscreenWindow()
{
    TVTest::HostInfo hostInfo;
    if (m_pApp->GetFullscreen() && m_pApp->GetHostInfo(&hostInfo)) {
        TCHAR className[64];
        _tcsncpy_s(className, hostInfo.pszAppName, 47);
        _tcscat_s(className, L" Fullscreen");

        HWND hwnd = nullptr;
        while ((hwnd = ::FindWindowEx(nullptr, hwnd, className, nullptr)) != nullptr) {
            DWORD pid;
            ::GetWindowThreadProcessId(hwnd, &pid);
            if (pid == ::GetCurrentProcessId()) return hwnd;
        }
    }
    return nullptr;
}


// ダイアログ選択でファイルを開く
bool CTvtPlay::OpenWithDialog()
{
    if (m_fDialogOpen) return false;

    HWND hwndOwner = GetFullscreenWindow();
    if (!hwndOwner) hwndOwner = m_pApp->GetAppWindow();
    std::vector<TCHAR> bufFile(32768, TEXT('\0'));

    OPENFILENAME ofn = {};
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner   = hwndOwner;
    ofn.lpstrFilter = TEXT("再生可能なメディア(*.ts;*.m2t;*.m2ts;*.mp4;*.m3u;*.tslist)\0*.ts;*.m2t;*.m2ts;*.mp4;*.m3u;*.tslist\0すべてのファイル\0*.*\0");
    ofn.lpstrTitle  = TEXT("ファイルを開く");
    // MSDN記述と違いOFN_NOCHANGEDIRはGetOpenFileName()にも効果がある模様
    ofn.Flags       = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR |
                      OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    // OFN_ENABLEHOOKではVistaスタイルが適用されないため
    ofn.nMaxFile    = static_cast<DWORD>(bufFile.size());
    ofn.lpstrFile   = bufFile.data();

    bool fAdded = false;
    m_fDialogOpen = true;
    if (::GetOpenFileName(&ofn)) {
        LPCTSTR spec = bufFile.data() + _tcslen(bufFile.data()) + 1;
        if (*spec) {
            // 複数のファイルが選択された
            for (; *spec; spec += _tcslen(spec) + 1) {
                TCHAR path[MAX_PATH];
                if (::PathCombine(path, bufFile.data(), spec)) {
                    if (m_playlist.PushBackListOrFile(path, !fAdded) >= 0) fAdded = true;
                }
            }
        }
        else {
            fAdded = m_playlist.PushBackListOrFile(bufFile.data(), true) >= 0;
        }
    }
    else if (::CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
        ::MessageBox(hwndOwner, TEXT("選択されたファイルが多すぎます。"),
                     TEXT("ファイルを開く"), MB_OK | MB_ICONERROR);
    }
    m_fDialogOpen = false;

    return fAdded ? OpenCurrent() : false;
}


// ポップアップメニュー選択でファイルを開く
bool CTvtPlay::OpenWithPopup(const POINT &pt, UINT flags)
{
    if (m_popupMax <= 0) return false;

    // 特定指示子をTVTestの保存先フォルダに置換する(手抜き)
    TCHAR pattern[MAX_PATH];
    if (!_tcsnicmp(m_szPopupPattern, TEXT("%RecordFolder%"), 14)) {
        if (m_pApp->GetSetting(L"RecordFolder", pattern, _countof(pattern)) <= 0) pattern[0] = 0;
        ::PathAppend(pattern, m_szPopupPattern + 14);
    }
    else {
        _tcscpy_s(pattern, m_szPopupPattern);
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
    std::vector<LPCTSTR> nameList(listSize);
    for (int i = 0; i < listSize; ++i) nameList[i] = findList[i].cFileName;
    std::sort(nameList.begin(), nameList.end(), [](LPCTSTR a, LPCTSTR b) { return _tcsicmp(a, b) < 0; });
    if (m_fPopupDesc) std::reverse(nameList.begin(), nameList.end());

    // メニュー生成
    int selID = 0;
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        if (listSize <= 0) {
            ::AppendMenu(hmenu, MF_STRING | MF_GRAYED, 0, TEXT("(なし)"));
        }
        else {
            HMENU hSubMenu = hmenu;
            for (int i = 0; i < listSize; ++i) {
                TCHAR str[64];
                if (i > 0 && i % m_popupMax == 0) {
                    HMENU hNewMenu = ::CreateMenu();
                    if (hNewMenu) {
                        // 手前のアイテムをサブメニュー化
                        _stprintf_s(str, TEXT("[%d - %d] (&P)"), max(listSize - i - m_popupMax, 0) + 1, listSize - i);
                        if (::InsertMenu(hSubMenu, 0, MF_STRING | MF_POPUP | MF_BYPOSITION, (UINT_PTR)hNewMenu, str)) {
                            ::InsertMenu(hSubMenu, 1, MF_SEPARATOR | MF_BYPOSITION, 0, nullptr);
                            hSubMenu = hNewMenu;
                        }
                        else {
                            ::DestroyMenu(hNewMenu);
                        }
                    }
                }
                _tcsncpy_s(str, nameList[listSize - i - 1], _TRUNCATE);
                if (_tcslen(str) == 63) _tcscpy_s(&str[60], 4, TEXT("..."));
                // プレフィクス対策
                for (LPTSTR p = str; *p; p++)
                    if (*p == TEXT('&')) *p = TEXT('_');
                ::InsertMenu(hSubMenu, 0, MF_STRING | MF_BYPOSITION, listSize - i, str);
            }
        }
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hmenu);
    }

    TCHAR fileName[MAX_PATH];
    if (selID > 0 && selID - 1 < listSize &&
        ::PathRemoveFileSpec(pattern) &&
        ::PathCombine(fileName, pattern, nameList[selID - 1]))
    {
        return m_playlist.PushBackListOrFile(fileName, true) >= 0 ? OpenCurrent() : false;
    }
    return false;
}


// ポップアップメニュー選択で再生リストのファイルを開く
bool CTvtPlay::OpenWithPlayListPopup(const POINT &pt, UINT flags)
{
    // メニュー生成
    int selID = 0;
    HMENU hTopMenu = ::LoadMenu(g_hinstDLL, MAKEINTRESOURCE(IDR_MENU_PLAYLIST));
    if (hTopMenu) {
        HMENU hmenu;
        if (m_playlist.Get().empty()) {
            hmenu = ::GetSubMenu(hTopMenu, 1);
        }
        else {
            hmenu = ::GetSubMenu(hTopMenu, 0);
            HMENU hSubMenu = hmenu;
            int listSize = (int)m_playlist.Get().size();
            auto it = m_playlist.Get().rbegin();
            for (int i = 0; i < listSize && i < POPUP_MAX_MAX; ++i, ++it) {
                TCHAR str[64];
                if (i > 0 && i % max(m_playlistPopupMax, 1) == 0) {
                    HMENU hNewMenu = ::CreateMenu();
                    if (hNewMenu) {
                        // 手前のアイテムをサブメニュー化
                        _stprintf_s(str, TEXT("[%d - %d] (&P)"), max(listSize - i - max(m_playlistPopupMax, 1), 0) + 1, listSize - i);
                        if (::InsertMenu(hSubMenu, 0, MF_STRING | MF_POPUP | MF_BYPOSITION, (UINT_PTR)hNewMenu, str)) {
                            ::InsertMenu(hSubMenu, 1, MF_SEPARATOR | MF_BYPOSITION, 0, nullptr);
                            hSubMenu = hNewMenu;
                        }
                        else {
                            ::DestroyMenu(hNewMenu);
                        }
                    }
                }
                _tcsncpy_s(str, PathFindFileName((*it).path), _TRUNCATE);
                if (_tcslen(str) == 63) _tcscpy_s(&str[60], 4, TEXT("..."));
                // プレフィクス対策
                for (LPTSTR p = str; *p; ++p)
                    if (*p == TEXT('&')) *p = TEXT('_');

                MENUITEMINFO mi;
                mi.cbSize = sizeof(MENUITEMINFO);
                mi.fMask = MIIM_ID | MIIM_STATE | MIIM_TYPE;
                mi.wID = listSize - i;
                mi.fState = mi.wID - 1 == m_playlist.GetPosition() ? MFS_DEFAULT | MFS_CHECKED : 0;
                mi.fType = MFT_STRING | MFT_RADIOCHECK;
                mi.dwTypeData = str;
                ::InsertMenuItem(hSubMenu, 0, TRUE, &mi);
            }
        }
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hTopMenu);
    }

    switch (selID) {
    case IDM_PLAYLIST_PREV:
        m_playlist.MoveCurrentToPrev();
        break;
    case IDM_PLAYLIST_NEXT:
        m_playlist.MoveCurrentToNext();
        break;
    case IDM_PLAYLIST_ERASE:
        // ファイルが開かれていれば閉じる
        Close();
        m_playlist.EraseCurrent();
        break;
    case IDM_PLAYLIST_CLEAR:
        m_playlist.ClearWithoutCurrent();
        break;
    case IDM_PLAYLIST_SORT_ASC:
        m_playlist.Sort(CPlaylist::SORT_ASC);
        break;
    case IDM_PLAYLIST_SORT_DESC:
        m_playlist.Sort(CPlaylist::SORT_DESC);
        break;
    case IDM_PLAYLIST_SHUFFLE:
        m_playlist.Sort(CPlaylist::SORT_SHUFFLE);
        break;
    case IDM_PLAYLIST_CLIP_PATH:
    case IDM_PLAYLIST_CLIP_NAME:
        {
            // 出力文字数を算出
            int size = m_playlist.ToString(nullptr, 0, selID==IDM_PLAYLIST_CLIP_NAME);
            // クリップボードにコピー
            if (size > 1 && ::OpenClipboard(m_hwndFrame)) {
                if (::EmptyClipboard()) {
                    HGLOBAL hg = ::GlobalAlloc(GMEM_MOVEABLE, size * sizeof(TCHAR));
                    if (hg) {
                        LPTSTR clip = reinterpret_cast<LPTSTR>(::GlobalLock(hg));
                        if (clip) {
                            m_playlist.ToString(clip, size, selID==IDM_PLAYLIST_CLIP_NAME);
                            ::GlobalUnlock(hg);
                            if (!::SetClipboardData(CF_UNICODETEXT, hg)) ::GlobalFree(hg);
                        }
                        else ::GlobalFree(hg);
                    }
                }
                ::CloseClipboard();
            }
        }
        break;
    default:
        if (1 <= selID && selID - 1 < (int)m_playlist.Get().size()) {
            m_playlist.SetPosition(selID - 1);
            return OpenCurrent();
        }
        break;
    }
    return false;
}


// ポップアップメニュー選択で再生速度を設定
void CTvtPlay::StretchWithPopup(const POINT &pt, UINT flags)
{
    // メニュー生成
    int selID = 0;
    int stid = GetStretchID();
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        for (int i = 0; i < m_stretchListNum; ++i) {
            if (m_stretchList[i] != 100) {
                TCHAR str[32];
                _stprintf_s(str, TEXT("%d%%"), m_stretchList[i]);
                ::AppendMenu(hmenu, MF_STRING|(i==stid?MF_CHECKED:MF_UNCHECKED), 1 + i, str);
            }
        }
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hmenu);
    }
    if (0 <= selID-1 && selID-1 < m_stretchListNum) {
        Stretch(selID-1==stid ? -1 : selID-1);
    }
}


void CTvtPlay::SeekChapterWithPopup(const POINT &pt, UINT flags)
{
    if (m_chapter.Get().empty()) return;

    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        int i = 0;
        int selID = 0;
        std::map<int, CChapterMap::CHAPTER>::const_iterator it;

        for (i = 0, it = m_chapter.Get().begin(); it != m_chapter.Get().end(); i++, it++)
        {
            TCHAR str[128];
            _stprintf_s(str, TEXT("%d:%02d:%02d.%03d %.63s"),
                        it->first/3600000, it->first/60000%60, it->first/1000%60, it->first%1000,
                        it->second.name.data());
            ::AppendMenu(hmenu, MF_STRING, 1 + i, str);
        }
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hmenu);

        if (0 <= selID - 1 && selID - 1 < (int)m_chapter.Get().size())
        {
            it = m_chapter.Get().begin();
            std::advance(it, selID - 1);
            SeekAbsolute(it->first);
        }
    }
}


static INT_PTR CALLBACK EditChapterDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    std::pair<int, CChapterMap::CHAPTER> *pch;
    switch (uMsg) {
    case WM_INITDIALOG:
        ::SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
        pch = reinterpret_cast<std::pair<int, CChapterMap::CHAPTER>*>(lParam);
        ::SetDlgItemText(hDlg, IDC_EDIT_NAME, pch->second.name.data());
        ::SetDlgItemInt(hDlg, IDC_EDIT_HOUR, pch->first/3600000%100, FALSE);
        ::SetDlgItemInt(hDlg, IDC_EDIT_MIN, pch->first/60000%60, FALSE);
        ::SetDlgItemInt(hDlg, IDC_EDIT_SEC, pch->first/1000%60, FALSE);
        ::SetDlgItemInt(hDlg, IDC_EDIT_MSEC, pch->first%1000, FALSE);
        ::SendDlgItemMessage(hDlg, IDC_EDIT_NAME, EM_LIMITTEXT, 4095, 0);
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
            pch = reinterpret_cast<std::pair<int, CChapterMap::CHAPTER>*>(::GetWindowLongPtr(hDlg, GWLP_USERDATA));
            pch->second.name.resize(4096);
            if (!::GetDlgItemText(hDlg, IDC_EDIT_NAME, pch->second.name.data(), 4096)) {
                pch->second.name[0] = 0;
            }
            pch->second.name.resize(_tcslen(pch->second.name.data()) + 1);
            pch->first = ::GetDlgItemInt(hDlg, IDC_EDIT_HOUR, nullptr, FALSE)%100*3600000 +
                         ::GetDlgItemInt(hDlg, IDC_EDIT_MIN, nullptr, FALSE)%60*60000 +
                         ::GetDlgItemInt(hDlg, IDC_EDIT_SEC, nullptr, FALSE)%60*1000 +
                         ::GetDlgItemInt(hDlg, IDC_EDIT_MSEC, nullptr, FALSE)%1000;
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
    std::map<int, CChapterMap::CHAPTER>::const_iterator ci = m_chapter.Get().find(pos);
    if (ci == m_chapter.Get().end()) return;
    std::pair<int, CChapterMap::CHAPTER> ch = *ci;

    // メニュー生成
    int selID = 0;
    HMENU hTopMenu = ::LoadMenu(g_hinstDLL, MAKEINTRESOURCE(IDR_MENU_CHAPTER));
    if (hTopMenu) {
        HMENU hmenu = ::GetSubMenu(hTopMenu, 0);
        TCHAR str[128];
        _stprintf_s(str, TEXT("%d:%02d:%02d.%03d %.63s"),
                    ch.first/3600000, ch.first/60000%60, ch.first/1000%60, ch.first%1000,
                    ch.second.name.data());
        ::ModifyMenu(hmenu, IDM_CHAPTER_EDIT, MF_STRING, IDM_CHAPTER_EDIT, str);
        ::CheckMenuItem(hmenu, IDM_CHAPTER_IN, ch.second.IsIn() ? MF_CHECKED : MF_UNCHECKED);
        ::CheckMenuItem(hmenu, IDM_CHAPTER_OUT, ch.second.IsOut() ? MF_CHECKED : MF_UNCHECKED);
        ::CheckMenuItem(hmenu, IDM_CHAPTER_X_IN, ch.second.IsIn() && ch.second.IsX() ? MF_CHECKED : MF_UNCHECKED);
        ::CheckMenuItem(hmenu, IDM_CHAPTER_X_OUT, ch.second.IsOut() && ch.second.IsX() ? MF_CHECKED : MF_UNCHECKED);
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hTopMenu);
    }
    if (selID == IDM_CHAPTER_EDIT) {
        // ダイアログで編集
        selID = 0;
        if (!m_fPopuping) {
            HWND hwndOwner = GetFullscreenWindow();
            if (!hwndOwner) hwndOwner = m_pApp->GetAppWindow();
            m_fPopuping = true;
            if (::DialogBoxParam(g_hinstDLL, MAKEINTRESOURCE(IDD_EDIT_CHAPTER), hwndOwner,
                                 EditChapterDlgProc, reinterpret_cast<LPARAM>(&ch)) == IDOK)
            {
                selID = IDM_CHAPTER_EDIT;
            }
            m_fPopuping = false;
        }
    }
    if (!selID || m_chapter.Get().count(pos) == 0) return;

    // マップのチャプターをchの値に更新する
    switch (selID) {
    case IDM_CHAPTER_EDIT:
        m_chapter.Insert(ch, pos);
        break;
    case IDM_CHAPTER_FORWARD:
        if (ch.first < CChapterMap::CHAPTER_POS_MAX)
            ch.first = max(ch.first-200, 0);
        m_chapter.Insert(ch, pos);
        break;
    case IDM_CHAPTER_BACKWARD:
        ch.first = min(ch.first+200, CChapterMap::CHAPTER_POS_MAX);
        m_chapter.Insert(ch, pos);
        break;
    case IDM_CHAPTER_ERASE:
        m_chapter.Erase(pos);
        break;
    case IDM_CHAPTER_IN:
        ch.second.SetOut(false);
        ch.second.SetIn(!ch.second.IsIn());
        m_chapter.Insert(ch, pos);
        break;
    case IDM_CHAPTER_OUT:
        ch.second.SetIn(false);
        ch.second.SetOut(!ch.second.IsOut());
        m_chapter.Insert(ch, pos);
        break;
    case IDM_CHAPTER_X_IN:
        {
            bool fSet = ch.second.IsIn() && ch.second.IsX();
            ch.second.SetOut(false);
            ch.second.SetIn(!fSet);
            ch.second.SetX(!fSet);
            m_chapter.Insert(ch, pos);
        }
        break;
    case IDM_CHAPTER_X_OUT:
        {
            bool fSet = ch.second.IsOut() && ch.second.IsX();
            ch.second.SetIn(false);
            ch.second.SetOut(!fSet);
            ch.second.SetX(!fSet);
            m_chapter.Insert(ch, pos);
        }
        break;
    }
    BeginWatchingNextChapter(false);
}


// ポップアップメニュー選択でチャプター全体を編集する
void CTvtPlay::EditAllChaptersWithPopup(const POINT &pt, UINT flags)
{
    // メニュー生成
    int selID = 0;
    HMENU hTopMenu = ::LoadMenu(g_hinstDLL, MAKEINTRESOURCE(IDR_MENU_CHAPTER_ALL));
    if (hTopMenu) {
        HMENU hmenu = ::GetSubMenu(hTopMenu, 0);
        selID = TrackPopup(hmenu, pt, flags);
        ::DestroyMenu(hTopMenu);
    }
    if (!selID || !m_chapter.IsOpen()) return;

    if (selID == IDM_CHAPTER_FORWARD) {
        m_chapter.ShiftAll(-200);
    }
    else if (selID == IDM_CHAPTER_BACKWARD) {
        m_chapter.ShiftAll(200);
    }
    BeginWatchingNextChapter(false);
}


int CTvtPlay::TrackPopup(HMENU hmenu, const POINT &pt, UINT flags)
{
    int selID = 0;
    TVTest::StatusItemGetInfo info;
    info.Mask = TVTest::STATUS_ITEM_GET_INFO_MASK_HWND;
    info.ID = 1;
    if (!m_fPopuping && m_pApp->GetStatusItemInfo(&info)) {
        m_fPopuping = true;
        selID = static_cast<int>(::TrackPopupMenu(hmenu, flags | TPM_NONOTIFY | TPM_RETURNCMD,
                                                  pt.x, pt.y, 0, info.hwnd, nullptr));
        m_fPopuping = false;
    }
    return selID;
}


// プレイリストの現在位置のファイルを開く
bool CTvtPlay::OpenCurrent(int offset, int stretchID)
{
    return !m_playlist.Get().empty() ? Open(m_playlist.Get()[m_playlist.GetPosition()].path, offset, stretchID) : false;
}


bool CTvtPlay::Open(LPCTSTR fileName, int offset, int stretchID)
{
    Close();

    LPCTSTR errorMessage = nullptr;
    if (!m_tsSender.Open(fileName, m_salt, m_readBufSizeKB*1024, m_fConvTo188, m_fUnderrunCtrl, m_fUseQpc,
                         m_pcrThresholdMsec, errorMessage))
    {
        if (errorMessage) {
            m_pApp->AddLog(errorMessage, TVTest::LOG_TYPE_ERROR);
        }
        return false;
    }

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
        std::list<HASH_INFO>::const_iterator it = hashList.begin();
        for (; it != hashList.end(); ++it) {
            if (it->hash == m_tsSender.GetFileHash() || it->hash == m_tsSender.GetOldFileHash()) {
                // 先頭or終端から5秒の範囲はレジュームしない
                if (5000 < it->resumePos && it->resumePos < m_tsSender.GetDuration() - 5000) {
                    m_tsSender.Seek(it->resumePos - 3000);
                    fSeeked = true;
                }
                break;
            }
        }
    }

    // チャプターを読み込む
    m_chapter.Open(fileName, m_szChaptersDirName);
    if (!fSeeked && m_fSkipXChapter) {
        // 先頭から1秒未満のスキップ開始チャプターを解釈
        std::map<int, CChapterMap::CHAPTER>::const_iterator it = m_chapter.Get().begin();
        for (; it != m_chapter.Get().end() && it->first < 1000; ++it) {
            if (it->second.IsIn() && it->second.IsX()) {
                for (++it; it != m_chapter.Get().end(); ++it) {
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

    m_hThreadEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!m_hThreadEvent) {
        m_tsSender.Close();
        return false;
    }
    m_hThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, TsSenderThread, this, 0, nullptr));
    if (!m_hThread) {
        ::CloseHandle(m_hThreadEvent);
        m_hThreadEvent = nullptr;
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
    SetModTimestamp(m_modTimestampMode == 1);

    // ストリームコールバックを利用してPCR/PTS/DTSを変更するかどうか設定
    if (!m_tsShifter.IsEnabled() && m_modTimestampMode == 2) {
        if (m_pApp->GetVersion() >= TVTest::MakeVersion(0,8,0)) {
            m_tsShifter.SetInitialPcr(m_tsSender.GetInitialPcr());
            m_tsShifter.Enable(true);
            m_fResetPat = true;
            AddStreamCallback();
        }
    }

    if (m_hashListMax > 0 && m_fUpdateHashList) {
        ::SetTimer(m_hwndFrame, TIMER_ID_UPDATE_HASH_LIST, TIMER_UPDATE_HASH_LIST_INTERVAL, nullptr);
    }
    if (m_chapter.NeedToSync()) {
        ::SetTimer(m_hwndFrame, TIMER_ID_SYNC_CHAPTER, TIMER_SYNC_CHAPTER_INTERVAL, nullptr);
    }
    BeginWatchingNextChapter(true);

    // 初期再生速度を設定
    if (stretchID >= 0) m_initialStretchID = stretchID;
    if (m_initialStretchID >= 0) Stretch(m_initialStretchID);

    // TVTestに変数登録
    TVTest::RegisterVariableInfo rvi;
    rvi.Flags = 0;
    rvi.pszKeyword = L"playback-filename";
    rvi.pszDescription = L"再生ファイル名";
    rvi.pszValue = ::PathFindFileName(fileName);
    m_pApp->RegisterVariable(&rvi);
    if (!_tcsicmp(::PathFindExtension(fileName), TEXT(".mp4"))) {
        rvi.Flags = 0;
        rvi.pszKeyword = L"playback-nts-filename";
        rvi.pszDescription = L"再生ファイル名(非TS時のみ)";
        rvi.pszValue = ::PathFindFileName(fileName);
        m_pApp->RegisterVariable(&rvi);
    }

    // 再生初期化が完了したことを知らせる
    ::PostThreadMessage(m_threadID, WM_TS_INIT_DONE, 0, 0);

    m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
    return true;
}


void CTvtPlay::Close()
{
    if (m_hThread) {
        ::PostThreadMessage(m_threadID, WM_QUIT, 0, 0);
        ::WaitForSingleObject(m_hThread, INFINITE);
        ::CloseHandle(m_hThread);
        m_hThread = nullptr;
        ::CloseHandle(m_hThreadEvent);
        m_hThreadEvent = nullptr;

        HASH_INFO hashInfo = {};
        hashInfo.hash = m_tsSender.GetFileHash();
        if (hashInfo.hash >= 0) {
            int pos = m_tsSender.GetPosition();
            // 先頭or終端から5秒の範囲はレジューム情報を記録しない
            hashInfo.resumePos = pos <= 5000 || (m_tsSender.IsFixed() && m_tsSender.GetDuration() - 5000 <= pos) ? -1 : pos;
            UpdateFileInfoSetting(hashInfo, m_tsSender.GetOldFileHash());
        }

        ::KillTimer(m_hwndFrame, TIMER_ID_UPDATE_HASH_LIST);
        ::KillTimer(m_hwndFrame, TIMER_ID_SYNC_CHAPTER);
        ::KillTimer(m_hwndFrame, TIMER_ID_WATCH_POS_GT);
        m_chapter.Close();
        m_tsSender.Close();

        // ストリームコールバックを利用したPCR/PTS/DTS変更を無効化
        if (m_tsShifter.IsEnabled()) {
            RemoveStreamCallback();
            m_tsShifter.Enable(false);
        }

        // 閉じる直前の再生速度を保存
        m_initialStretchID = GetStretchID();

        // TVTestの変数登録を初期化
        TVTest::RegisterVariableInfo rvi;
        rvi.Flags = 0;
        rvi.pszKeyword = L"playback-filename";
        rvi.pszDescription = L"再生ファイル名";
        rvi.pszValue = L"";
        m_pApp->RegisterVariable(&rvi);
        rvi.Flags = 0;
        rvi.pszKeyword = L"playback-nts-filename";
        rvi.pszDescription = L"再生ファイル名(非TS時のみ)";
        rvi.pszValue = L"";
        m_pApp->RegisterVariable(&rvi);

        // 再生情報を初期化する
        UpdateInfos();
        m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
    }
}


// ドライバの状態に応じてTSデータの転送先を設定する
void CTvtPlay::SetupDestination()
{
    TCHAR path[MAX_PATH];
    TVTest::ChannelInfo chInfo;

    m_pApp->GetDriverName(path, _countof(path));
    LPCTSTR name = ::PathFindFileName(path);

    if (!_tcsicmp(name, TEXT("BonDriver_UDP.dll"))) {
        if (m_pApp->GetCurrentChannelInfo(&chInfo) && m_hThread) {
            ::PostThreadMessage(m_threadID, WM_TS_SET_UDP, 0, 1234 + chInfo.Channel);
        }
    }
    else {
        TCHAR ordinal[20] = TEXT("BonDriver_Pipe0.dll");
        if (_tcsicmp(name, TEXT("BonDriver_Pipe.dll"))) {
            for (; ordinal[14] <= TEXT('9') && _tcsicmp(name, ordinal); ++ordinal[14]);
        }
        if (ordinal[14] <= TEXT('9')) {
            if (m_pApp->GetCurrentChannelInfo(&chInfo) && m_hThread) {
                ::PostThreadMessage(m_threadID, WM_TS_SET_PIPE, 0, chInfo.Channel);
            }
        }
        else {
            if (m_hThread) ::PostThreadMessage(m_threadID, WM_TS_SET_UDP, 0, -1);
        }
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
    m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
    SaveSettings();
}

int CTvtPlay::GetStretchID()
{
    lock_recursive_mutex lock(m_tsInfoLock);
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
    lowSpeed = m_slowerWithCaption > 0 ? speed * m_slowerWithCaption / 100 :
               m_slowerWithCaption < 0 ? -m_slowerWithCaption : speed;
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
    if (m_hThread && !m_chapter.Get().empty()) {
        if (fDoDelay) {
            // シーク時などはGetPosition()が更新されるまで遅延させる
            ::SetTimer(m_hwndFrame, TIMER_ID_WATCH_POS_GT, TIMER_WATCH_POS_GT_INTERVAL, nullptr);
        }
        else {
            // 直近の終了チャプターかスキップ開始チャプターを監視する
            // 監視位置は必ずposより大きくする(でないと即座にWM_SATISFIED_POS_GTが呼ばれてしまう)
            int pos = GetPosition();
            if (pos >= 0) {
                std::map<int, CChapterMap::CHAPTER>::const_iterator it = m_chapter.Get().upper_bound(pos);
                for (; it != m_chapter.Get().end(); ++it) {
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


void CTvtPlay::EnablePluginByDriverName()
{
    if (m_fAutoEnUdp || m_fAutoEnPipe) {
        TCHAR path[MAX_PATH];
        m_pApp->GetDriverName(path, _countof(path));
        LPCTSTR name = ::PathFindFileName(path);

        // ドライバ名に応じて有効無効を切り替える
        bool fEnable = false;
        if (m_fAutoEnUdp && !_tcsicmp(name, TEXT("BonDriver_UDP.dll"))) fEnable = true;
        if (m_fAutoEnPipe) {
            if (!_tcsicmp(name, TEXT("BonDriver_Pipe.dll"))) fEnable = true;
            else {
                TCHAR ordinal[20] = TEXT("BonDriver_Pipe0.dll");
                for (; ordinal[14] <= TEXT('9'); ++ordinal[14]) {
                    if (!_tcsicmp(name, ordinal)) {
                        fEnable = true;
                        break;
                    }
                }
            }
        }

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
    case ID_COMMAND_STRETCH_POPUP:
    case ID_COMMAND_CHAPTER_POPUP:
        if (m_fPopuping) {
            TVTest::StatusItemGetInfo info;
            info.Mask = TVTest::STATUS_ITEM_GET_INFO_MASK_HWND;
            info.ID = 1;
            if (m_pApp->GetStatusItemInfo(&info)) {
                ::PostMessage(info.hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
                ::PostMessage(info.hwnd, WM_KEYUP, VK_ESCAPE, 0);
            }
        }
        else if (!pPt) {
            // 適当な位置に表示するためボタンを擬似的に押しに行く
            TVTest::StatusItemGetInfo info;
            info.Mask = TVTest::STATUS_ITEM_GET_INFO_MASK_HWND | TVTest::STATUS_ITEM_GET_INFO_MASK_ITEMRECT;
            info.ID = 1;
            if (m_pApp->GetStatusItemInfo(&info)) {
                CStatusView::MOUSE_ACTION action = CStatusView::MOUSE_ACTION_LDOWN;
                CStatusItem *pItem = m_statusView.GetItemByID(STATUS_ITEM_BUTTON + id);
                if (!pItem) {
                    action = CStatusView::MOUSE_ACTION_RDOWN;
                    for (int i = 0; i < m_statusView.NumItems(); ++i) {
                        CButtonStatusItem *pButton = dynamic_cast<CButtonStatusItem*>(m_statusView.GetItem(i));
                        if (pButton && pButton->GetSubID() == STATUS_ITEM_BUTTON + id) {
                            pItem = pButton;
                            break;
                        }
                    }
                }
                if (pItem) {
                    int left, right;
                    m_statusView.GetItemPartition(m_statusView.IDToIndex(pItem->GetID()), &left, &right);
                    POINT cursorPos;
                    cursorPos.x = info.ItemRect.left + left;
                    cursorPos.y = info.ItemRect.top;
                    m_statusView.OnMouseAction(action, info.hwnd, cursorPos, info.ItemRect);
                }
            }
        }
        else if (id == ID_COMMAND_OPEN_POPUP) {
            OpenWithPopup(*pPt, flags);
        }
        else if (id == ID_COMMAND_LIST_POPUP) {
            OpenWithPlayListPopup(*pPt, flags);
        }
        else if (id == ID_COMMAND_STRETCH_POPUP) {
            StretchWithPopup(*pPt, flags);
        }
        else if (id == ID_COMMAND_CHAPTER_POPUP) {
            SeekChapterWithPopup(*pPt, flags);
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
        if (m_playlist.Get().size() >= 2 && m_playlist.Next(IsAllRepeat())) OpenCurrent();
        else SeekToEnd();
        break;
    case ID_COMMAND_SEEK_PREV:
        if (!m_chapter.Get().empty()) {
            int pos = GetPosition();
            if (pos >= 3000) {
                // 再生中の連続ジャンプを考慮して多めに戻す
                std::map<int, CChapterMap::CHAPTER>::const_iterator it = m_chapter.Get().lower_bound(pos - 3000);
                if (it != m_chapter.Get().begin()) SeekAbsolute((--it)->first);
            }
        }
        break;
    case ID_COMMAND_SEEK_NEXT:
        if (!m_chapter.Get().empty()) {
            int pos = GetPosition();
            if (pos >= 0) {
                std::map<int, CChapterMap::CHAPTER>::const_iterator it = m_chapter.Get().lower_bound(pos + 1000);
                if (it != m_chapter.Get().end()) SeekAbsolute(it->first);
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
                std::map<int, CChapterMap::CHAPTER>::const_iterator it = m_chapter.Get().lower_bound(pos - 3000);
                if (it == m_chapter.Get().end() || it->first > pos + 3000) {
                    // どうせPCR挿入間隔の精度しか出ないので100msec単位に落とす
                    m_chapter.Insert(std::make_pair(pos / 100 * 100, CChapterMap::CHAPTER()));
                    m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
                }
            }
        }
        break;
    case ID_COMMAND_REPEAT_CHAPTER:
        m_fRepeatChapter = !m_fRepeatChapter;
        BeginWatchingNextChapter(false);
        m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
        SaveSettings();
        break;
    case ID_COMMAND_SKIP_X_CHAPTER:
        m_fSkipXChapter = !m_fSkipXChapter;
        BeginWatchingNextChapter(false);
        m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
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
}


// シークバーの幅を調整する
void CTvtPlay::AdjustSeekItem(int statusWidth)
{
    CStatusItem *pItem = m_statusView.GetItemByID(STATUS_ITEM_SEEK);
    if (pItem) {
        // シークバーアイテム以外の幅を算出
        int cmpl = 0;
        RECT rcMgn;
        m_statusView.GetItemMargin(&rcMgn);
        for (int i = 0; i < m_statusView.NumItems(); ++i) {
            if (m_statusView.GetItem(i) != pItem) {
                cmpl += m_statusView.GetItem(i)->GetWidth() + rcMgn.left + rcMgn.right;
            }
        }
        pItem->SetWidth(statusWidth - rcMgn.left - rcMgn.right - cmpl);
    }
}


// 再生位置アイテムの幅を設定する
void CTvtPlay::SetWidthPositionItem()
{
    CStatusItem *pItem = m_statusView.GetItemByID(STATUS_ITEM_POSITION);
    if (pItem) {
        if (m_posItemWidth < 0) {
            CPositionStatusItem *pPosItem = dynamic_cast<CPositionStatusItem*>(pItem);
            LOGFONT font;
            TVTest::StatusItemGetInfo info;
            info.Mask = TVTest::STATUS_ITEM_GET_INFO_MASK_HWND;
            info.ID = 1;
            if (m_pApp->GetFont(L"StatusBarFont", &font) && m_pApp->GetStatusItemInfo(&info)) {
                pItem->SetWidth(pPosItem->CalcSuitableWidth(info.hwnd, font));
            }
        }
        else {
            pItem->SetWidth(m_posItemWidth);
        }
    }
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
    case TVTest::EVENT_DRIVERCHANGE:
        // ドライバが変更された
        // 起動が完了するまでは不安定なので有効にしない
        if (pThis->m_fEventStartupDone) {
            pThis->EnablePluginByDriverName();
        }
        // コマンドラインにパスが指定されていれば開く
        if (pThis->m_pApp->IsPluginEnabled() && pThis->m_szSpecFileName[0]) {
            if (pThis->m_playlist.PushBackListOrFile(pThis->m_szSpecFileName, true) >= 0) {
                pThis->OpenCurrent(pThis->m_specOffset, pThis->m_specStretchID);
            }
            pThis->m_szSpecFileName[0] = 0;
        }
        break;
    case TVTest::EVENT_CHANNELCHANGE:
        // チャンネルが変更された
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->SetupDestination();
        break;
    case TVTest::EVENT_SERVICEUPDATE:
        // サービスの構成が変化した
        if (pThis->m_pApp->IsPluginEnabled()) {
#ifdef EN_SWC
            // 字幕PIDをセットする
            pThis->m_captionPid = pThis->GetCaptionPid();
#endif
            pThis->m_fResetPat = true;
        }
        break;
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
                    pThis->OpenCurrent(pThis->m_specOffset, pThis->m_specStretchID);
                }
                pThis->m_szSpecFileName[0] = 0;
            }
            // 起動時フリーズ対策(仮)
            if (pThis->m_fRaisePriority && pThis->m_pApp->GetVersion() < TVTest::MakeVersion(0,8,1)) {
                ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            }
        }
        break;
    case TVTest::EVENT_FILTERGRAPH_FINALIZED:
        // フィルタグラフの終了処理終了
        if (pThis->m_pApp->IsPluginEnabled()) {
            // つぎの初期化後にフィルタの再生速度を再設定するため
            pThis->Stretch(pThis->GetStretchID());
        }
        break;
    case TVTest::EVENT_STATUSITEM_DRAW:
        // ステータス項目の描画
        {
            const TVTest::StatusItemDrawInfo *pInfo = reinterpret_cast<const TVTest::StatusItemDrawInfo*>(lParam1);
            if (pInfo->Flags & TVTest::STATUS_ITEM_DRAW_FLAG_PREVIEW) {
                pThis->m_pApp->ThemeDrawText(pInfo->pszStyle, pInfo->hdc, L"TvtPlayステータスバー", pInfo->DrawRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            else if (pThis->m_pApp->IsPluginEnabled()) {
                pThis->AdjustSeekItem(pInfo->ItemRect.right - pInfo->ItemRect.left);
                pThis->m_pApp->ThemeDrawBackground(L"status-bar.item", pInfo->hdc, pInfo->ItemRect);
                RECT rc;
                if (pThis->m_statusView.GetHotRect(pInfo->ItemRect, &rc)) {
                    pThis->m_pApp->ThemeDrawBackground(L"status-bar.item.hot", pInfo->hdc, rc);
                }
                LOGFONT font;
                if (pThis->m_pApp->GetFont(L"StatusBarFont", &font)) {
                    pThis->m_statusView.Draw(pInfo->hdc, pInfo->ItemRect, font,
                                             pThis->m_pApp->GetColor(L"StatusText"), pThis->m_pApp->GetColor(L"StatusBack"),
                                             pThis->m_pApp->GetColor(L"StatusHighlightText"), pThis->m_pApp->GetColor(L"StatusHighlightBack"));
                }
            }
        }
        return TRUE;
    case TVTest::EVENT_STATUSITEM_NOTIFY:
        // ステータス項目の通知
        {
            const TVTest::StatusItemEventInfo *pInfo = reinterpret_cast<const TVTest::StatusItemEventInfo*>(lParam1);
            switch (pInfo->Event) {
            case TVTest::STATUS_ITEM_EVENT_CREATED:
                // 項目が作成された
                {
                    // ここでは常に非表示にしておく
                    TVTest::StatusItemSetInfo info;
                    info.Size = sizeof(info);
                    info.Mask = TVTest::STATUS_ITEM_SET_INFO_MASK_STATE;
                    info.ID = 1;
                    info.StateMask = TVTest::STATUS_ITEM_STATE_VISIBLE;
                    info.State = 0;
                    pThis->m_pApp->SetStatusItem(&info);
                }
                return TRUE;
            case TVTest::STATUS_ITEM_EVENT_VISIBILITYCHANGED:
                // 項目の表示状態が変わった
                if (pInfo->Param != 0) {
                    pThis->SetWidthPositionItem();
                }
                return TRUE;
            case TVTest::STATUS_ITEM_EVENT_ENTER:
            case TVTest::STATUS_ITEM_EVENT_LEAVE:
                // フォーカスが当たった/離れた
                if (pThis->m_statusView.OnViewEvent(
                        pInfo->Event == TVTest::STATUS_ITEM_EVENT_ENTER ? CStatusView::VIEW_EVENT_ENTER : CStatusView::VIEW_EVENT_LEAVE)) {
                    pThis->m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
                }
                return TRUE;
            }
        }
        break;
    case TVTest::EVENT_STATUSITEM_MOUSE:
        // ステータス項目のマウス操作
        {
            const TVTest::StatusItemMouseEventInfo *pInfo = reinterpret_cast<const TVTest::StatusItemMouseEventInfo*>(lParam1);
            pThis->AdjustSeekItem(pInfo->ItemRect.right - pInfo->ItemRect.left);
            CStatusView::MOUSE_ACTION action =
                pInfo->Action == TVTest::STATUS_ITEM_MOUSE_ACTION_LDOWN ? CStatusView::MOUSE_ACTION_LDOWN :
                pInfo->Action == TVTest::STATUS_ITEM_MOUSE_ACTION_LUP ? CStatusView::MOUSE_ACTION_LUP :
                pInfo->Action == TVTest::STATUS_ITEM_MOUSE_ACTION_LDOUBLECLICK ? CStatusView::MOUSE_ACTION_LDOUBLECLICK :
                pInfo->Action == TVTest::STATUS_ITEM_MOUSE_ACTION_RDOWN ? CStatusView::MOUSE_ACTION_RDOWN :
                pInfo->Action == TVTest::STATUS_ITEM_MOUSE_ACTION_RDOUBLECLICK ? CStatusView::MOUSE_ACTION_RDOWN :
                pInfo->Action == TVTest::STATUS_ITEM_MOUSE_ACTION_MOVE ? CStatusView::MOUSE_ACTION_MOVE : CStatusView::MOUSE_ACTION_NONE;
            if (action != CStatusView::MOUSE_ACTION_NONE) {
                if (pThis->m_statusView.OnMouseAction(action, pInfo->hwnd, pInfo->CursorPos, pInfo->ItemRect)) {
                    pThis->m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
                }
                return TRUE;
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
    case WM_DROPFILES:
        {
            if (pThis->m_pApp->GetFullscreen()) {
                // ファイルダイアログ等でのD&Dを無視するため(確実ではない)
                HWND hwndActive = ::GetActiveWindow();
                if (hwndActive && (::GetWindowLong(::GetAncestor(hwndActive, GA_ROOT), GWL_EXSTYLE) & WS_EX_DLGMODALFRAME) != 0) {
                    break;
                }
            }
            bool fAdded = false;
            TCHAR fileName[MAX_PATH];
            int num = ::DragQueryFile((HDROP)wParam, 0xFFFFFFFF, fileName, _countof(fileName));
            for (int i = 0; i < num; ++i) {
                if (::DragQueryFile((HDROP)wParam, i, fileName, _countof(fileName)) != 0 &&
                    (CPlaylist::IsPlayListFile(fileName) || CPlaylist::IsMediaFile(fileName)))
                {
                    if (pThis->m_playlist.PushBackListOrFile(fileName, !fAdded) >= 0) fAdded = true;
                }
            }
            // 少なくとも1ファイルが再生リストに追加されればそのファイルを開く
            if (fAdded) pThis->OpenCurrent();
            // DragFinish()せずに本体のデフォルトプロシージャに任せる
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
            pThis = static_cast<CTvtPlay*>(pcs->lpCreateParams);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        return 0;
    case WM_TIMER:
        switch (wParam) {
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
            return 0;
        case TIMER_ID_UPDATE_HASH_LIST:
            if (pThis->IsOpen()) {
                HASH_INFO hashInfo = {};
                hashInfo.hash = pThis->m_tsSender.GetFileHash();
                if (hashInfo.hash >= 0) {
                    int pos = pThis->GetPosition();
                    // 先頭or終端から5秒の範囲はレジューム情報を記録しない
                    hashInfo.resumePos = pos <= 5000 || (!pThis->IsExtending() && pThis->GetDuration() - 5000 <= pos) ? -1 : pos;
                    pThis->UpdateFileInfoSetting(hashInfo, pThis->m_tsSender.GetOldFileHash());
                }
            }
            return 0;
        case TIMER_ID_SYNC_CHAPTER:
            if (pThis->m_chapter.Sync()) {
                pThis->m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
                pThis->BeginWatchingNextChapter(false);
            }
            return 0;
        case TIMER_ID_WATCH_POS_GT:
            ::KillTimer(hwnd, TIMER_ID_WATCH_POS_GT);
            pThis->BeginWatchingNextChapter(false);
            return 0;
        }
        break;
    case WM_UPDATE_STATUS:
        //DEBUG_OUT(TEXT("CTvtPlay::FrameWindowProc(): WM_UPDATE_STATUS\n"));
        pThis->m_pApp->StatusItemNotify(1, TVTest::STATUS_ITEM_NOTIFY_REDRAW);
        return 0;
    case WM_QUERY_CLOSE_NEXT:
        pThis->Close();
        if (pThis->m_playlist.Next(pThis->IsAllRepeat()))
            pThis->OpenCurrent();
        else if (pThis->m_fAutoClose)
            pThis->m_pApp->Close();
        return 0;
    case WM_QUERY_SEEK_BGN:
        pThis->SeekToBegin();
        return 0;
    case WM_QUERY_RESET:
        DEBUG_OUT(TEXT("CTvtPlay::FrameWindowProc(): WM_QUERY_RESET\n"));
        if (pThis->m_resetMode <= 2) {
            pThis->m_pApp->Reset(wParam && pThis->m_resetMode==1 ? TVTest::RESET_ALL : TVTest::RESET_VIEWER);
        }
        if (wParam && pThis->m_resetDropInterval > 0) {
            pThis->m_lastDropCount = 0;
            ::SetTimer(hwnd, TIMER_ID_RESET_DROP, pThis->m_resetDropInterval, nullptr);
        }
        return 0;
    case WM_SATISFIED_POS_GT:
        DEBUG_OUT(TEXT("CTvtPlay::FrameWindowProc(): WM_SATISFIED_POS_GT\n"));
        if (pThis->m_chapter.IsOpen()) {
            std::map<int, CChapterMap::CHAPTER>::const_iterator it =
                pThis->m_chapter.Get().find(static_cast<int>(lParam) - pThis->m_supposedDispDelay);
            if (pThis->m_fSkipXChapter && it != pThis->m_chapter.Get().end() &&
                it->second.IsIn() && it->second.IsX())
            {
                for (++it; it != pThis->m_chapter.Get().end(); ++it) {
                    // スキップ終了チャプターまでシーク
                    if (it->second.IsOut() && it->second.IsX()) {
                        pThis->SeekAbsolute(it->first);
                        break;
                    }
                }
            }
            else if (pThis->m_fRepeatChapter && it != pThis->m_chapter.Get().end() &&
                     it->second.IsOut() && it != pThis->m_chapter.Get().begin())
            {
                bool isX = it->second.IsX();
                do {
                    // 対応する開始チャプターまでシーク
                    if ((--it)->second.IsIn() && (isX && it->second.IsX() || !isX && !it->second.IsX())) {
                        pThis->SeekAbsolute(it->first);
                        break;
                    }
                } while (it != pThis->m_chapter.Get().begin());
            }
            pThis->BeginWatchingNextChapter(true);
        }
        return 0;
    case WM_TVTP_GET_MSGVER:
        return TVTP_CURRENT_MSGVER;
    case WM_TVTP_IS_OPEN:
        return pThis->IsOpen();
    case WM_TVTP_GET_POSITION:
        return pThis->GetPosition();
    case WM_TVTP_GET_DURATION:
        return pThis->GetDuration();
    case WM_TVTP_GET_TOT_TIME:
        return pThis->GetTotTime();
    case WM_TVTP_IS_EXTENDING:
        return pThis->IsExtending();
    case WM_TVTP_IS_PAUSED:
        return pThis->IsPaused();
    case WM_TVTP_GET_PLAY_FLAGS:
        return (int)pThis->IsAllRepeat() | (int)pThis->IsSingleRepeat()<<1 |
               (int)pThis->IsRepeatChapterEnabled()<<2 | (int)pThis->IsSkipXChapterEnabled()<<3;
    case WM_TVTP_GET_STRETCH:
        {
            int stid = pThis->GetStretchID();
            return stid < 0 ? MAKELRESULT(0xFFFF, 100) : MAKELRESULT(stid, pThis->m_stretchList[stid]);
        }
    case WM_TVTP_GET_PATH:
        if (pThis->IsOpen()) {
            if (!pThis->m_playlist.Get().empty()) {
                size_t pos = pThis->m_playlist.GetPosition();
                size_t len = _tcslen(pThis->m_playlist.Get()[pos].path);
                if (!lParam) {
                    return len;
                }
                if (len < static_cast<size_t>(wParam)) {
                    _tcscpy_s(reinterpret_cast<LPWSTR>(lParam), static_cast<size_t>(wParam), pThis->m_playlist.Get()[pos].path);
                    return len;
                }
            }
        }
        return 0;
    case WM_TVTP_SEEK:
        pThis->Seek(static_cast<int>(lParam));
        return TRUE;
    case WM_TVTP_SEEK_ABSOLUTE:
        pThis->SeekAbsolute(static_cast<int>(lParam));
        return TRUE;
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
unsigned int __stdcall CTvtPlay::TsSenderThread(LPVOID pParam)
{
    MSG msg;
    DWORD_PTR dwRes;
    CTvtPlay *pThis = static_cast<CTvtPlay*>(pParam);
    int posSec, durSec, lastTot, lastExtMode, lastSpeed;
    bool fLastPaused;
    int speed = 100, posWatch = -1;
    int lowSpeed = 100;
    int stretchMode = 0;
    bool fLowSpeed = false;
    MSG msgSetSpeed = {};
    static const int RESET_WAIT = 10;
    int resetCount = -RESET_WAIT;
    {
        lock_recursive_mutex lock(pThis->m_tsInfoLock);
        pThis->m_infoSpeed = speed;
        posSec = pThis->m_infoPos / 1000;
        durSec = pThis->m_infoDur / 1000;
        lastTot = pThis->m_infoTot;
        lastExtMode = pThis->m_infoExtMode;
        lastSpeed = pThis->m_infoSpeed;
        fLastPaused = pThis->m_fInfoPaused;
    }
    pThis->m_tsSender.SetupQpc();
    pThis->m_threadID = ::GetCurrentThreadId();

    for (;;) {
        BOOL rv = ::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
        ::SetEvent(pThis->m_hThreadEvent);
        if (rv && msg.message == WM_TS_SET_SPEED || !rv && msgSetSpeed.message == WM_TS_SET_SPEED) {
            if (rv) {
                // このメッセージはフィルタが生成されるかタイムアウトまで先送りする
                msgSetSpeed = msg;
                msgSetSpeed.time = ::GetTickCount();
                msg.message = WM_NULL;
            }
            if (ASFilterFindWindow()) {
                msg = msgSetSpeed;
                msgSetSpeed.message = WM_NULL;
                rv = TRUE;
            }
            else if (::GetTickCount() - msg.time > 10000) {
                msgSetSpeed.message = WM_NULL;
            }
        }
        if (rv) {
            if (msg.message == WM_QUIT) break;
            switch (msg.message) {
            case WM_TS_SET_UDP:
                pThis->m_tsSender.SetUdpPort(static_cast<unsigned short>(1234 <= msg.lParam && msg.lParam <= 1243 ? msg.lParam : 0));
                break;
            case WM_TS_SET_PIPE:
                pThis->m_tsSender.SetPipeNumber(0 <= msg.lParam && msg.lParam <= 9 ? static_cast<int>(msg.lParam) : -1);
                break;
            case WM_TS_PAUSE:
                if (msg.wParam) {
                    if (pThis->m_fTryGaplessPause) {
                        pThis->m_tsSender.Pause(true, false);
                    }
                    // フィルタグラフのポーズを試みる
                    if (!pThis->m_fTryGaplessPause || !ASFilterSendMessageTimeout(WM_ASFLT_PAUSE, TRUE, 0, 3000)) {
                        // 従来のポーズ
                        pThis->m_tsSender.Pause(true);
                        ::SendMessageTimeout(pThis->m_hwndFrame, WM_QUERY_RESET, 0, 0, SMTO_NORMAL, 1000, &dwRes);
                    }
                }
                else {
                    if (!pThis->m_fTryGaplessPause || !ASFilterSendMessageTimeout(WM_ASFLT_PAUSE, FALSE, 0, 3000)) {
                        ::SendMessageTimeout(pThis->m_hwndFrame, WM_QUERY_RESET, 0, 0, SMTO_NORMAL, 1000, &dwRes);
                    }
                    pThis->m_tsSender.Pause(false);
                }
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
                    lock_recursive_mutex lock(pThis->m_streamLock);
                    fLowSpeed = pThis->m_captionAnalyzer.CheckShowState(pThis->m_pcr);
                }
#endif
                if (!ASFilterSendMessageTimeout(WM_ASFLT_STRETCH, stretchMode, MAKELPARAM(fLowSpeed?lowSpeed:speed, 100), 3000)) {
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
            case WM_TS_INIT_DONE:
                pThis->m_tsSender.Pause(false);
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
                // 追っかけ時に(再生時間-3)秒以上の位置に達した場合
                if (!pThis->m_tsSender.IsFixed() &&
                    pThis->m_tsSender.GetPosition() > pThis->m_tsSender.GetDuration() - 3000)
                {
                    // 等速に戻しておく
                    ASFilterSendNotifyMessage(WM_ASFLT_STRETCH, 0, MAKELPARAM(100, 100));
                    pThis->m_tsSender.SetSpeed(100, 100);
                    speed = lowSpeed = 100;
                    msgSetSpeed.message = WM_NULL;
                }
            }
#ifdef EN_SWC
            if (speed != 100 && speed != lowSpeed) {
                int fSetSpeed = false;
                {
                    lock_recursive_mutex lock(pThis->m_streamLock);
                    if (pThis->m_captionAnalyzer.CheckShowState(pThis->m_pcr)) {
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
                    if (!ASFilterSendMessageTimeout(WM_ASFLT_STRETCH, stretchMode, MAKELPARAM(fLowSpeed?lowSpeed:speed, 100), 3000)) {
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
            }
        }

        {
            lock_recursive_mutex lock(pThis->m_tsInfoLock);
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
    // フィルタグラフのポーズを戻しておく
    if (pThis->m_fTryGaplessPause && pThis->m_tsSender.IsPaused()) {
        ASFilterSendNotifyMessage(WM_ASFLT_PAUSE, FALSE, 0);
    }
    return 0;
}


void CTvtPlay::AddStreamCallback()
{
    if (m_streamCallbackRefCount++ == 0) {
        m_pApp->SetStreamCallback(0, StreamCallback, this);
    }
}


void CTvtPlay::RemoveStreamCallback()
{
    if (--m_streamCallbackRefCount <= 0) {
        m_pApp->SetStreamCallback(TVTest::STREAM_CALLBACK_REMOVE, StreamCallback);
        m_streamCallbackRefCount = 0;
    }
}


// ストリームコールバック(別スレッド)
BOOL CALLBACK CTvtPlay::StreamCallback(BYTE *pData, void *pClientData)
{
    CTvtPlay &t = *static_cast<CTvtPlay*>(pClientData);

    if (t.m_fResetPat) {
        lock_recursive_mutex lock(t.m_streamLock);
        t.m_tsShifter.Reset();
#ifdef EN_SWC
        PAT zeroPat = {};
        t.m_pat = zeroPat;
        t.m_captionAnalyzer.ClearShowState();
#endif
        t.m_fResetPat = false;
    }

    // PCR/PTS/DTSを変更
    t.m_tsShifter.Transform(pData);

#ifdef EN_SWC
    TS_HEADER header;
    extract_ts_header(&header, pData);

    // PCRを取得
    if ((header.adaptation_field_control&2)/*2,3*/ &&
        !header.transport_error_indicator)
    {
        // アダプテーションフィールドがある
        ADAPTATION_FIELD adapt;
        extract_adaptation_field(&adapt, pData + 4);

        if (adapt.pcr_flag) {
            // 字幕のPCR参照PIDを取得する
            int pcrPid = -1;
            for (size_t i = 0; i < t.m_pat.pmt.size(); ++i) {
                const PMT *pPmt = &t.m_pat.pmt[i];
                for (int j = 0; j < pPmt->pid_count; ++j) {
                    if (pPmt->pid[j] == t.m_captionPid) {
                        pcrPid = pPmt->pcr_pid;
                        break;
                    }
                }
            }
            if (pcrPid < 0) {
                lock_recursive_mutex lock(t.m_streamLock);
                t.m_captionAnalyzer.ClearShowState();
            }
            // 参照PIDのときはPCRを取得する
            if (header.pid == pcrPid) {
                lock_recursive_mutex lock(t.m_streamLock);
                DWORD pcr = (DWORD)adapt.pcr_45khz;

                // PCRの連続性チェック
                if (CounterDiff(pcr, t.m_pcr) < 0 || pcr - t.m_pcr >= 1000 * PCR_PER_MSEC) {
                    DEBUG_OUT(TEXT(__FUNCTION__) TEXT("(): Discontinuous packet!\n"));
                    t.m_captionAnalyzer.Clear();
                }
                t.m_pcr = pcr;
            }
        }
    }

    // PAT/PMTを取得
    if ((header.adaptation_field_control&1)/*1,3*/ &&
        !header.transport_scrambling_control &&
        !header.transport_error_indicator)
    {
        LPCBYTE pPayload = pData + 4;
        if (header.adaptation_field_control == 3) {
            // アダプテーションに続けてペイロードがある
            ADAPTATION_FIELD adapt;
            extract_adaptation_field(&adapt, pPayload);
            if (adapt.adaptation_field_length < 0) return TRUE;
            pPayload += adapt.adaptation_field_length + 1;
        }
        int payloadSize = 188 - static_cast<int>(pPayload - pData);

        // PAT監視
        if (header.pid == 0) {
            extract_pat(&t.m_pat, pPayload, payloadSize,
                        header.payload_unit_start_indicator,
                        header.continuity_counter);
            return TRUE;
        }
        // PATリストにあるPMT監視
        for (size_t i = 0; i < t.m_pat.pmt.size(); ++i) {
            if (header.pid == t.m_pat.pmt[i].pmt_pid/* && header.pid != 0*/) {
                extract_pmt(&t.m_pat.pmt[i], pPayload, payloadSize,
                            header.payload_unit_start_indicator,
                            header.continuity_counter);
                return TRUE;
            }
        }
    }

    // 字幕ストリームを取得
    if (header.pid == t.m_captionPid &&
        !header.transport_scrambling_control &&
        !header.transport_error_indicator)
    {
        lock_recursive_mutex lock(t.m_streamLock);
        t.m_captionAnalyzer.AddPacket(pData);
    }
#endif
    return TRUE;
}


TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTvtPlay;
}
