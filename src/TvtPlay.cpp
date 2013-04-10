// TVTestにtsファイル再生機能を追加するプラグイン
// 最終更新: 2011-11-21
// 署名: 849fa586809b0d16276cd644c6749503
#include <Windows.h>
#include <WindowsX.h>
#include <Shlwapi.h>
#include <list>
#include <vector>
#include "Util.h"
#include "Settings.h"
#include "ColorScheme.h"
#include "StatusView.h"
#include "TsSender.h"
#include "Playlist.h"
#include "resource.h"
#include "TvtPlay.h"

static LPCWSTR INFO_PLUGIN_NAME = L"TvtPlay";
static LPCWSTR INFO_DESCRIPTION = L"ファイル再生機能を追加 (ver.1.2)";
static const int INFO_VERSION = 12;

#define WM_UPDATE_POSITION  (WM_APP + 1)
#define WM_UPDATE_TOT_TIME  (WM_APP + 2)
#define WM_UPDATE_F_PAUSED  (WM_APP + 3)
#define WM_UPDATE_SPEED     (WM_APP + 4)
#define WM_QUERY_CLOSE_NEXT (WM_APP + 5)
#define WM_QUERY_SEEK_BGN   (WM_APP + 6)
#define WM_QUERY_RESET      (WM_APP + 7)

#define WM_TS_SET_UDP       (WM_APP + 1)
#define WM_TS_SET_PIPE      (WM_APP + 2)
#define WM_TS_PAUSE         (WM_APP + 3)
#define WM_TS_SEEK_BGN      (WM_APP + 4)
#define WM_TS_SEEK_END      (WM_APP + 5)
#define WM_TS_SEEK          (WM_APP + 6)
#define WM_TS_SET_SPEED     (WM_APP + 7)
#define WM_TS_SET_MOD_TS    (WM_APP + 8)

static LPCTSTR SETTINGS = TEXT("Settings");
static LPCTSTR TVTPLAY_FRAME_WINDOW_CLASS = TEXT("TvtPlay Frame");
static LPCSTR UDP_ADDR = "127.0.0.1";
static LPCTSTR PIPE_NAME = TEXT("\\\\.\\pipe\\BonDriver_Pipe%02d");

enum {
    ID_COMMAND_OPEN,
    ID_COMMAND_OPEN_POPUP,
    ID_COMMAND_LIST_POPUP,
    ID_COMMAND_CLOSE,
    ID_COMMAND_PREV,
    ID_COMMAND_SEEK_BGN,
    ID_COMMAND_SEEK_END,
    ID_COMMAND_LOOP,
    ID_COMMAND_PAUSE,
    ID_COMMAND_NOP,
    ID_COMMAND_SEEK_A,
    ID_COMMAND_SEEK_B,
    ID_COMMAND_SEEK_C,
    ID_COMMAND_SEEK_D,
    ID_COMMAND_SEEK_E,
    ID_COMMAND_SEEK_F,
    ID_COMMAND_SEEK_G,
    ID_COMMAND_SEEK_H,
    ID_COMMAND_STRETCH_A,
    ID_COMMAND_STRETCH_B,
    ID_COMMAND_STRETCH_C,
    ID_COMMAND_STRETCH_D,
    ID_COMMAND_STRETCH_E,
    ID_COMMAND_STRETCH_F,
    ID_COMMAND_STRETCH,
};

static const TVTest::CommandInfo COMMAND_LIST[] = {
    {ID_COMMAND_OPEN, L"Open", L"ファイルを開く"},
    {ID_COMMAND_OPEN_POPUP, L"OpenPopup", L"ファイルを開く(ポップアップ)"},
    {ID_COMMAND_LIST_POPUP, L"ListPopup", L"再生リストを開く(ポップアップ)"},
    {ID_COMMAND_CLOSE, L"Close", L"ファイルを閉じる"},
    {ID_COMMAND_PREV, L"Prev", L"前のファイル/先頭にシーク"},
    {ID_COMMAND_SEEK_BGN, L"SeekToBgn", L"先頭にシーク"},
    {ID_COMMAND_SEEK_END, L"SeekToEnd", L"次のファイル/末尾にシーク"},
    {ID_COMMAND_LOOP, L"Loop", L"全体/シングル/リピートしない"},
    {ID_COMMAND_PAUSE, L"Pause", L"一時停止/再生"},
    {ID_COMMAND_NOP, L"Nop", L"何もしない"},
    {ID_COMMAND_SEEK_A, L"SeekA", L"シーク:A"},
    {ID_COMMAND_SEEK_B, L"SeekB", L"シーク:B"},
    {ID_COMMAND_SEEK_C, L"SeekC", L"シーク:C"},
    {ID_COMMAND_SEEK_D, L"SeekD", L"シーク:D"},
    {ID_COMMAND_SEEK_E, L"SeekE", L"シーク:E"},
    {ID_COMMAND_SEEK_F, L"SeekF", L"シーク:F"},
    {ID_COMMAND_SEEK_G, L"SeekG", L"シーク:G"},
    {ID_COMMAND_SEEK_H, L"SeekH", L"シーク:H"},
    {ID_COMMAND_STRETCH_A, L"StretchA", L"早送り:A"},
    {ID_COMMAND_STRETCH_B, L"StretchB", L"早送り:B"},
    {ID_COMMAND_STRETCH_C, L"StretchC", L"早送り:C"},
    {ID_COMMAND_STRETCH_D, L"StretchD", L"早送り:D"},
    {ID_COMMAND_STRETCH_E, L"StretchE", L"早送り:E"},
    {ID_COMMAND_STRETCH_F, L"StretchF", L"早送り:F"},
    {ID_COMMAND_STRETCH, L"Stretch", L"早送り:切り替え"},
};

static const int DEFAULT_SEEK_LIST[COMMAND_SEEK_MAX] = {
    -60000, -30000, -15000, -5000, 4000, 14000, 29000, 59000
};

static const int DEFAULT_STRETCH_LIST[COMMAND_STRETCH_MAX] = {
    400, 200, 50, 25, 100, 100
};

// NULL禁止
static LPCTSTR DEFAULT_BUTTON_LIST[BUTTON_MAX] = {
    TEXT("0,Open"), TEXT(";1,Close"), TEXT("4:5:14,Loop"),
    TEXT(";9,Prev  2,SeekToBgn"),
    TEXT("'-'6'0,SeekA"), TEXT(";'-'3'0,SeekB"),
    TEXT("'-'1'5,SeekC"), TEXT("'-' '5,SeekD"),
    TEXT("6,Pause"),
    TEXT("'+' '5,SeekE"), TEXT("'+'1'5,SeekF"),
    TEXT(";'+'3'0,SeekG"), TEXT("'+'6'0,SeekH"),
    TEXT("3,SeekToEnd"),
    TEXT("'*'2'.'0:30~'*'2'.'0,StretchB"),
    TEXT("'*'0'.'5:30~'*'0'.'5,StretchC")
};


CTvtPlay::CTvtPlay()
    : m_fInitialized(false)
    , m_fSettingsLoaded(false)
    , m_fForceEnable(false)
    , m_fIgnoreExt(false)
    , m_fAutoEnUdp(false)
    , m_fAutoEnPipe(false)
    , m_fEventExecute(false)
    , m_fPausedOnPreviewChange(false)
    , m_specOffset(-1)
    , m_fShowOpenDialog(false)
    , m_hwndFrame(NULL)
    , m_fFullScreen(false)
    , m_fHide(false)
    , m_fAutoHide(false)
    , m_fAutoHideActive(false)
    , m_fHoveredFromOutside(false)
    , m_statusRow(0)
    , m_statusRowFull(0)
    , m_statusHeight(0)
    , m_fSeekDrawOfs(false)
    , m_fSeekDrawTot(false)
    , m_fPosDrawTot(false)
    , m_posItemWidth(0)
    , m_timeoutOnCmd(0)
    , m_timeoutOnMove(0)
    , m_dispCount(0)
    , m_lastDropCount(0)
    , m_resetDropInterval(0)
    , m_popupMax(0)
    , m_fPopupDesc(false)
    , m_fPopuping(false)
    , m_fDialogOpen(false)
    , m_hThread(NULL)
    , m_hThreadEvent(NULL)
    , m_threadID(0)
    , m_position(0)
    , m_duration(0)
    , m_totTime(-1)
    , m_speed(100)
    , m_fFixed(false)
    , m_fSpecialExt(false)
    , m_fPaused(false)
    , m_fHalt(false)
    , m_fAllRepeat(false)
    , m_fSingleRepeat(false)
    , m_waitOnStop(0)
    , m_fResetAllOnSeek(false)
    , m_stretchMode(0)
    , m_noMuteMax(0)
    , m_noMuteMin(0)
    , m_fConvTo188(false)
    , m_fModTimestamp(false)
    , m_salt(0)
    , m_hashListMax(0)
{
    m_szIniFileName[0] = 0;
    m_szSpecFileName[0] = 0;
    m_szIconFileName[0] = 0;
    m_szPopupPattern[0] = 0;
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
        int specOffset = -1;
        for (int i = fIgnoreFirst; i < argc; ++i) {
            // オプションは複数起動禁止時に無効->有効にすることができる
            if (argv[i][0] == TEXT('/') || argv[i][0] == TEXT('-')) {
                if (!::lstrcmpi(argv[i]+1, TEXT("tvtplay"))) m_fForceEnable = m_fIgnoreExt = true;
                else if (!::lstrcmpi(argv[i]+1, TEXT("tvtpudp"))) m_fAutoEnUdp = true;
                else if (!::lstrcmpi(argv[i]+1, TEXT("tvtpipe"))) m_fAutoEnPipe = true;
                else if (!::lstrcmpi(argv[i]+1, TEXT("tvtpofs")) && i+1 < argc) {
                    ++i;
                    if (TEXT('0') <= argv[i][0] && argv[i][0] <= TEXT('9')) {
                        specOffset = ::StrToInt(argv[i]);
                        int n = ::StrCSpn(argv[i], TEXT("+-"));
                        if (argv[i][n] == TEXT('+')) ++n;
                        specOffset += ::StrToInt(argv[i] + n);
                        if (specOffset < 0) specOffset = 0;
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
                m_specOffset = specOffset;
            }
        }

        ::LocalFree(argv);
    }
}


bool CTvtPlay::Initialize()
{
    // 初期化処理

    // コマンドを登録
    m_pApp->RegisterCommand(COMMAND_LIST, _countof(COMMAND_LIST));

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);

    AnalyzeCommandLine(::GetCommandLine(), true);
    if (m_fForceEnable) m_pApp->EnablePlugin(true);

    return true;
}


bool CTvtPlay::Finalize()
{
    // 終了処理
    if (m_pApp->IsPluginEnabled()) EnablePlugin(false);
    return true;
}


// 設定の読み込み
void CTvtPlay::LoadSettings()
{
    if (m_fSettingsLoaded) return;

    if (!::GetModuleFileName(g_hinstDLL, m_szIniFileName, _countof(m_szIniFileName)) ||
        !::PathRenameExtension(m_szIniFileName, TEXT(".ini"))) m_szIniFileName[0] = 0;

    int defSalt = (::GetTickCount()>>16)^(::GetTickCount()&0xffff);

    m_fAllRepeat = ::GetPrivateProfileInt(SETTINGS, TEXT("TsRepeatAll"), 0, m_szIniFileName) != 0;
    m_fSingleRepeat = ::GetPrivateProfileInt(SETTINGS, TEXT("TsRepeatSingle"), 0, m_szIniFileName) != 0;
    m_waitOnStop = ::GetPrivateProfileInt(SETTINGS, TEXT("TsWaitOnStop"), 800, m_szIniFileName);
    m_waitOnStop = max(m_waitOnStop, 0);
    m_fResetAllOnSeek = ::GetPrivateProfileInt(SETTINGS, TEXT("TsResetAllOnSeek"), 0, m_szIniFileName) != 0;
    m_resetDropInterval = ::GetPrivateProfileInt(SETTINGS, TEXT("TsResetDropInterval"), 1000, m_szIniFileName);
    m_threadPriority = ::GetPrivateProfileInt(SETTINGS, TEXT("TsThreadPriority"), THREAD_PRIORITY_NORMAL, m_szIniFileName);
    m_stretchMode = ::GetPrivateProfileInt(SETTINGS, TEXT("TsStretchMode"), 3, m_szIniFileName);
    m_noMuteMax = ::GetPrivateProfileInt(SETTINGS, TEXT("TsStretchNoMuteMax"), 800, m_szIniFileName);
    m_noMuteMin = ::GetPrivateProfileInt(SETTINGS, TEXT("TsStretchNoMuteMin"), 50, m_szIniFileName);
    m_fConvTo188 = ::GetPrivateProfileInt(SETTINGS, TEXT("TsConvTo188"), 1, m_szIniFileName) != 0;
    m_fModTimestamp = ::GetPrivateProfileInt(SETTINGS, TEXT("TsAvoidWraparound"), 0, m_szIniFileName) != 0;
    m_fShowOpenDialog = ::GetPrivateProfileInt(SETTINGS, TEXT("ShowOpenDialog"), 0, m_szIniFileName) != 0;
    m_fAutoHide = ::GetPrivateProfileInt(SETTINGS, TEXT("AutoHide"), 1, m_szIniFileName) != 0;
    m_statusRow = ::GetPrivateProfileInt(SETTINGS, TEXT("RowPos"), 0, m_szIniFileName);
    m_statusRowFull = ::GetPrivateProfileInt(SETTINGS, TEXT("RowPosFull"), 0, m_szIniFileName);
    m_fSeekDrawOfs = ::GetPrivateProfileInt(SETTINGS, TEXT("DispOffset"), 0, m_szIniFileName) != 0;
    m_fSeekDrawTot = ::GetPrivateProfileInt(SETTINGS, TEXT("DispTot"), 0, m_szIniFileName) != 0;
    m_fPosDrawTot = ::GetPrivateProfileInt(SETTINGS, TEXT("DispTotOnStatus"), 0, m_szIniFileName) != 0;
    m_posItemWidth = ::GetPrivateProfileInt(SETTINGS, TEXT("StatusItemWidth"), -1, m_szIniFileName);
    m_timeoutOnCmd = ::GetPrivateProfileInt(SETTINGS, TEXT("TimeoutOnCommand"), 2000, m_szIniFileName);
    m_timeoutOnMove = ::GetPrivateProfileInt(SETTINGS, TEXT("TimeoutOnMouseMove"), 0, m_szIniFileName);
    m_salt = ::GetPrivateProfileInt(SETTINGS, TEXT("Salt"), defSalt, m_szIniFileName);
    m_hashListMax = ::GetPrivateProfileInt(SETTINGS, TEXT("FileInfoMax"), 0, m_szIniFileName);
    m_hashListMax = min(m_hashListMax, HASH_LIST_MAX_MAX);
    m_popupMax = ::GetPrivateProfileInt(SETTINGS, TEXT("PopupMax"), 30, m_szIniFileName);
    m_popupMax = min(m_popupMax, POPUP_MAX_MAX);
    m_fPopupDesc = ::GetPrivateProfileInt(SETTINGS, TEXT("PopupDesc"), 0, m_szIniFileName) != 0;

    ::GetPrivateProfileString(SETTINGS, TEXT("PopupPattern"), TEXT("%RecordFolder%*.ts"),
                              m_szPopupPattern, _countof(m_szPopupPattern), m_szIniFileName);

    ::GetPrivateProfileString(SETTINGS, TEXT("IconImage"), TEXT(""),
                              m_szIconFileName, _countof(m_szIconFileName), m_szIniFileName);

    // シークコマンドのシーク量設定を取得
    for (int i = 0; i < COMMAND_SEEK_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Seek%c"), (TCHAR)i + TEXT('A'));
        m_seekList[i] = ::GetPrivateProfileInt(SETTINGS, key, DEFAULT_SEEK_LIST[i], m_szIniFileName);
    }

    // 早送りコマンドの倍率(25%～800%)設定を取得
    for (int i = 0; i < COMMAND_STRETCH_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Stretch%c"), (TCHAR)i + TEXT('A'));
        int r = ::GetPrivateProfileInt(SETTINGS, key, DEFAULT_STRETCH_LIST[i], m_szIniFileName);
        m_stretchList[i] = min(max(r, 25), 800);
    }

    // ボタンアイテムの配置設定を取得
    for (int i = 0; i < BUTTON_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Button%02d"), i);
        ::GetPrivateProfileString(SETTINGS, key, DEFAULT_BUTTON_LIST[i],
                                  m_buttonList[i], _countof(m_buttonList[0]), m_szIniFileName);
    }

    // ファイル固有のレジューム情報などを取得
    // キー番号が小さいものほど新しい
    m_hashList.clear();
    if (m_hashListMax > 0) {
        // FileInfoセクションは一気に読み込む
        // strにはm_hashListMaxだけのキーが格納できればよい
        LPTSTR str = new TCHAR[m_hashListMax * 96];
        DWORD read = ::GetPrivateProfileSection(TEXT("FileInfo"), str, m_hashListMax * 96, m_szIniFileName);
        if (read > 0) {
            LPCTSTR p = str;
            for (int i = 0; i < m_hashListMax; i++) {
                TCHAR key[32];
                int keyLen;
                HASH_INFO hashInfo;

                keyLen = ::wsprintf(key, TEXT("Hash%d="), i);
                while (*p && ::StrCmpNI(p, key, keyLen)) p += ::lstrlen(p) + 1;
                if (!*p) break;
                if (!::StrToInt64Ex(p + keyLen, STIF_SUPPORT_HEX, &hashInfo.hash)) continue;

                keyLen = ::wsprintf(key, TEXT("Resume%d="), i);
                while (*p && ::StrCmpNI(p, key, keyLen)) p += ::lstrlen(p) + 1;
                if (!*p) break;
                hashInfo.resumePos = ::StrToInt(p + keyLen);

                m_hashList.push_back(hashInfo);
            }
        }
        delete [] str;
    }

    m_fSettingsLoaded = true;

    int iniVer = ::GetPrivateProfileInt(SETTINGS, TEXT("Version"), 0, m_szIniFileName);
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
        WritePrivateProfileInt(SETTINGS, TEXT("Version"), INFO_VERSION, m_szIniFileName);
        SaveSettings();
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

    int val = ::GetPrivateProfileInt(TEXT("Status"), TEXT("FontSize"), INT_MAX, iniFileName);
    if (val != INT_MAX) {
        pFont->lfHeight = val;
        pFont->lfWidth  = 0;
    }
    val = ::GetPrivateProfileInt(TEXT("Status"), TEXT("FontWeight"), INT_MAX, iniFileName);
    if (val != INT_MAX) pFont->lfWeight = val;
    val = ::GetPrivateProfileInt(TEXT("Status"), TEXT("FontItalic"), INT_MAX, iniFileName);
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
void CTvtPlay::SaveSettings() const
{
    if (!m_fSettingsLoaded) return;

    WritePrivateProfileInt(SETTINGS, TEXT("TsRepeatAll"), m_fAllRepeat, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsRepeatSingle"), m_fSingleRepeat, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsWaitOnStop"), m_waitOnStop, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsResetAllOnSeek"), m_fResetAllOnSeek, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsResetDropInterval"), m_resetDropInterval, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsThreadPriority"), m_threadPriority, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsStretchMode"), m_stretchMode, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsStretchNoMuteMax"), m_noMuteMax, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsStretchNoMuteMin"), m_noMuteMin, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsConvTo188"), m_fConvTo188, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TsAvoidWraparound"), m_fModTimestamp, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("ShowOpenDialog"), m_fShowOpenDialog, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("AutoHide"), m_fAutoHide, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("RowPos"), m_statusRow, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("RowPosFull"), m_statusRowFull, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("DispOffset"), m_fSeekDrawOfs, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("DispTot"), m_fSeekDrawTot, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("DispTotOnStatus"), m_fPosDrawTot, m_szIniFileName);
    if (m_posItemWidth < 0) ::WritePrivateProfileString(SETTINGS, TEXT("StatusItemWidth"), NULL, m_szIniFileName);
    else WritePrivateProfileInt(SETTINGS, TEXT("StatusItemWidth"), m_posItemWidth, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TimeoutOnCommand"), m_timeoutOnCmd, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("TimeoutOnMouseMove"), m_timeoutOnMove, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("Salt"), m_salt, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("FileInfoMax"), m_hashListMax, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("PopupMax"), m_popupMax, m_szIniFileName);
    WritePrivateProfileInt(SETTINGS, TEXT("PopupDesc"), m_fPopupDesc, m_szIniFileName);
    ::WritePrivateProfileString(SETTINGS, TEXT("PopupPattern"), m_szPopupPattern, m_szIniFileName);
    ::WritePrivateProfileString(SETTINGS, TEXT("IconImage"), m_szIconFileName, m_szIniFileName);

    for (int i = 0; i < COMMAND_SEEK_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Seek%c"), (TCHAR)i + TEXT('A'));
        WritePrivateProfileInt(SETTINGS, key, m_seekList[i], m_szIniFileName);
    }

    for (int i = 0; i < COMMAND_STRETCH_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Stretch%c"), (TCHAR)i + TEXT('A'));
        WritePrivateProfileInt(SETTINGS, key, m_stretchList[i], m_szIniFileName);
    }

    for (int i = 0; i < BUTTON_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Button%02d"), i);
        ::WritePrivateProfileString(SETTINGS, key, m_buttonList[i], m_szIniFileName);
    }

    // FileInfoセクションは一気に書き込む
    LPTSTR str = new TCHAR[2 + m_hashList.size() * 96];
    LPTSTR tail = str;
    tail[0] = tail[1] = 0;
    std::list<HASH_INFO>::const_iterator it = m_hashList.begin();
    for (int i = 0; it != m_hashList.end(); ++i, ++it) {
        tail += ::wsprintf(tail, TEXT("Hash%d=0x%06x%08x"), i, (DWORD)((*it).hash>>32), (DWORD)((*it).hash)) + 1;
        tail += ::wsprintf(tail, TEXT("Resume%d=%d"), i, (*it).resumePos) + 1;
    }
    tail[0] = 0;
    ::WritePrivateProfileSection(TEXT("FileInfo"), str, m_szIniFileName);
    delete [] str;
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

    DrawUtil::CBitmap icon;
    if (!icon.Create(ICON_SIZE * 7, ICON_SIZE, 1)) return false;

    HDC hdcMem = ::CreateCompatibleDC(NULL);
    if (!hdcMem) return false;
    HGDIOBJ hgdiOld = ::SelectObject(hdcMem, icon.GetHandle());

    RGBQUAD rgbq[2] = {0};
    rgbq[0].rgbBlue = rgbq[0].rgbGreen = rgbq[0].rgbRed = 0;
    rgbq[1].rgbBlue = rgbq[1].rgbGreen = rgbq[1].rgbRed = 255;
    ::SetDIBColorTable(hdcMem, 0, 2, rgbq);

    // 文字列解析してボタンアイテムを生成
    for (int i = 0; i < BUTTON_MAX; i++) {
        if (m_buttonList[i][0] == TEXT(';')) continue;

        int commandID = -1;
        LPCTSTR cmd = ::StrRChr(m_buttonList[i], NULL, TEXT(','));
        for (int j = 0; cmd && j < _countof(COMMAND_LIST); j++) {
            if (!::lstrcmpi(&cmd[1], COMMAND_LIST[j].pszText)) {
                commandID = COMMAND_LIST[j].ID;
                break;
            }
        }
        if (commandID == -1) continue;

        ComposeMonoColorIcon(hdcMem, 0, 0, iconMap.GetHandle(), m_buttonList[i]);

        m_statusView.AddItem(new CButtonStatusItem(this, STATUS_ITEM_BUTTON + commandID, icon));
    }
    ::SelectObject(hdcMem, hgdiOld);
    ::DeleteDC(hdcMem);

    m_statusView.AddItem(new CSeekStatusItem(this, m_fSeekDrawOfs, m_fSeekDrawTot));
    m_statusView.AddItem(new CPositionStatusItem(this));

    // CTsSenderを初期設定
    m_tsSender.SetConvTo188(m_fConvTo188);

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

        if (m_fShowOpenDialog && !m_szSpecFileName[0]) OpenWithDialog();

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
        OnResize(true);

        m_pApp->SetWindowMessageCallback(WindowMsgCallback, this);
        ::DragAcceptFiles(m_pApp->GetAppWindow(), TRUE);
    }
    else {
        ::DragAcceptFiles(m_pApp->GetAppWindow(), FALSE);
        m_pApp->SetWindowMessageCallback(NULL, NULL);
        Close();

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
    if (m_fPopuping) return;

    // メニュー生成
    int selID = 0;
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        if (m_statusRow != 0 && !m_pApp->GetFullscreen())
            ::AppendMenu(hmenu, MF_STRING|(m_fAutoHide?MF_UNCHECKED:MF_CHECKED), 1, TEXT("常に表示する"));
        ::AppendMenu(hmenu, MF_STRING|(m_fPosDrawTot?MF_CHECKED:MF_UNCHECKED), 2, TEXT("放送時刻を表示する"));
        ::AppendMenu(hmenu, MF_STRING|(m_fModTimestamp?MF_CHECKED:MF_UNCHECKED), 3, TEXT("ラップアラウンドを回避する"));
        m_fPopuping = true;
        // まずコントロールを表示させておく
        if (m_fAutoHideActive) ::SendMessage(m_hwndFrame, WM_TIMER, TIMER_ID_AUTO_HIDE, 1);
        selID = (int)::TrackPopupMenu(hmenu, flags | TPM_NONOTIFY | TPM_RETURNCMD,
                                      pt.x, pt.y, 0, m_hwndFrame, NULL);
        ::DestroyMenu(hmenu);
        m_fPopuping = false;
    }
    if (selID == 1) {
        m_fAutoHide = !m_fAutoHide;
        m_fFullScreen = !m_pApp->GetFullscreen();
        OnResize();
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
}


// TVTestのフルスクリーンHWNDを取得する
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


// ポップアップメニュー選択でファイルを開く
bool CTvtPlay::OpenWithPopup(const POINT &pt, UINT flags)
{
    if (m_popupMax <= 0 || m_fPopuping) return false;

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
    WIN32_FIND_DATA *findList = new WIN32_FIND_DATA[POPUP_MAX_MAX];
    int count = 0;
    HANDLE hFind = ::FindFirstFile(pattern, &findList[count]);
    if (hFind != INVALID_HANDLE_VALUE) {
        for (count++; count < POPUP_MAX_MAX; count++) {
            if (!::FindNextFile(hFind, &findList[count])) break;
        }
        ::FindClose(hFind);
    }

    // ファイル名を昇順or降順ソート
    LPCTSTR nameList[POPUP_MAX_MAX];
    for (int i = 0; i < count; i++) nameList[i] = findList[i].cFileName;
    qsort(nameList, count, sizeof(nameList[0]), m_fPopupDesc ? CompareTStrIX : CompareTStrI);

    // メニュー生成
    int selID = 0;
    HMENU hmenu = ::CreatePopupMenu();
    if (hmenu) {
        if (count == 0) {
            ::AppendMenu(hmenu, MF_STRING | MF_GRAYED, 0, TEXT("(なし)"));
        }
        else {
            for (int i = max(count - m_popupMax, 0); i < count; i++) {
                TCHAR str[64];
                ::lstrcpyn(str, nameList[i], 64);
                if (::lstrlen(str) == 63) ::lstrcpy(&str[60], TEXT("..."));
                // プレフィクス対策
                for (LPTSTR p = str; *p; p++)
                    if (*p == TEXT('&')) *p = TEXT('_');
                ::AppendMenu(hmenu, MF_STRING, i + 1, str);
            }
        }

        m_fPopuping = true;
        // まずコントロールを表示させておく
        if (m_fAutoHideActive) ::SendMessage(m_hwndFrame, WM_TIMER, TIMER_ID_AUTO_HIDE, 1);
        selID = (int)::TrackPopupMenu(hmenu, flags | TPM_NONOTIFY | TPM_RETURNCMD,
                                      pt.x, pt.y, 0, m_hwndFrame, NULL);
        ::DestroyMenu(hmenu);
        m_fPopuping = false;
    }

    TCHAR fileName[MAX_PATH];
    if (selID == 0 || selID - 1 >= count ||
        !::PathRemoveFileSpec(pattern) ||
        !::PathCombine(fileName, pattern, nameList[selID - 1]))
    {
        fileName[0] = 0;
    }

    delete [] findList;
    if (!fileName[0]) return false;

    return m_playlist.PushBackListOrFile(fileName, true) >= 0 ? OpenCurrent() : false;
}


// ポップアップメニュー選択で再生リストのファイルを開く
bool CTvtPlay::OpenWithPlayListPopup(const POINT &pt, UINT flags)
{
    if (m_fPopuping) return false;

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

        m_fPopuping = true;
        // まずコントロールを表示させておく
        if (m_fAutoHideActive) ::SendMessage(m_hwndFrame, WM_TIMER, TIMER_ID_AUTO_HIDE, 1);
        selID = (int)::TrackPopupMenu(hmenu, flags | TPM_NONOTIFY | TPM_RETURNCMD,
                                      pt.x, pt.y, 0, m_hwndFrame, NULL);
        ::DestroyMenu(hmenu);
        m_fPopuping = false;
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


// プレイリストの現在位置のファイルを開く
bool CTvtPlay::OpenCurrent(int offset)
{
    size_t pos = m_playlist.GetPosition();
    return pos < m_playlist.size() ? Open(m_playlist[pos].path, offset) : false;
}


bool CTvtPlay::Open(LPCTSTR fileName, int offset)
{
    Close();

    if (!m_tsSender.Open(fileName, m_salt)) return false;

    if (offset >= 0) {
        // オフセットが指定されているのでシーク
        if (0 < offset && offset < m_tsSender.GetDuration() - 5000) {
            m_tsSender.Seek(offset);
        }
    }
    else {
        // レジューム情報があればその地点までシーク
        LONGLONG hash = m_tsSender.GetFileHash();
        std::list<HASH_INFO>::const_iterator it = m_hashList.begin();
        for (; it != m_hashList.end(); ++it) {
            if ((*it).hash == hash) {
                // 先頭or終端から5秒の範囲はレジュームしない
                if (5000 < (*it).resumePos && (*it).resumePos < m_tsSender.GetDuration() - 5000) {
                    m_tsSender.Seek((*it).resumePos - 3000);
                }
                break;
            }
        }
    }

    if (m_pApp->GetVersion() < TVTest::MakeVersion(0,7,21))
        m_pApp->Reset(TVTest::RESET_VIEWER);

    m_hThreadEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
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

        // 固有情報リストを検索
        HASH_INFO hashInfo = {0};
        hashInfo.hash = m_tsSender.GetFileHash();
        std::list<HASH_INFO>::iterator it = m_hashList.begin();
        for (; it != m_hashList.end(); ++it) {
            if ((*it).hash == hashInfo.hash) {
                hashInfo = *it;
                m_hashList.erase(it);
                break;
            }
        }
        hashInfo.resumePos = m_tsSender.GetPosition();

        // 固有情報リストの最も新しい位置に追加する
        if (!m_hashList.empty() && (int)m_hashList.size() >= m_hashListMax) {
            m_hashList.pop_back();
        }
        if (m_hashListMax > 0) {
            m_hashList.push_front(hashInfo);
            SaveSettings();
        }

        m_tsSender.Close();
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

void CTvtPlay::ResetAndPostToSender(UINT Msg, WPARAM wParam, LPARAM lParam, bool fResetAll)
{
    if (m_hThread) {
        // 転送停止するまで待つ
        m_fHalt = true;
        ::WaitForSingleObject(m_hThreadEvent, 1000);

        if (m_pApp->GetVersion() < TVTest::MakeVersion(0,7,21)) {
            // RESET_VIEWERのデッドロック対策のため先にリセットする
            m_pApp->Reset(fResetAll ? TVTest::RESET_ALL : TVTest::RESET_VIEWER);
        }
        ::PostThreadMessage(m_threadID, Msg, wParam, lParam);
        m_fHalt = false;
    }
}

void CTvtPlay::Pause(bool fPause)
{
    ResetAndPostToSender(WM_TS_PAUSE, fPause, 0, false);
}

void CTvtPlay::SeekToBegin()
{
    ResetAndPostToSender(WM_TS_SEEK_BGN, 0, 0, m_fResetAllOnSeek);
}

void CTvtPlay::SeekToEnd()
{
    ResetAndPostToSender(WM_TS_SEEK_END, 0, 0, m_fResetAllOnSeek);
}

void CTvtPlay::Seek(int msec)
{
    ResetAndPostToSender(WM_TS_SEEK, 0, msec, m_fResetAllOnSeek);
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

int CTvtPlay::GetStretchID() const
{
    if (m_speed == 100) return -1;
    for (int i = 0; i < COMMAND_STRETCH_MAX; i++) {
        if (m_stretchList[i] == m_speed) return i;
    }
    return -1;
}

void CTvtPlay::Stretch(int stretchID)
{
    int speed = 0 <= stretchID && stretchID < COMMAND_STRETCH_MAX ?
                m_stretchList[stretchID] : 100;

    // 速度が設定値より大or小のときはミュートフラグをつける
    bool fMute = speed < m_noMuteMin || m_noMuteMax < speed;
    if (m_hThread) ::PostThreadMessage(m_threadID, WM_TS_SET_SPEED, (fMute?4:0)|m_stretchMode, speed);
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
            bool fMaximize = (::GetWindowLong(hwndApp, GWL_STYLE) & WS_MAXIMIZE) != 0;
            pRect->left = rcw.left + (m_statusRow==0 && margin<=2 ? 0 : margin);
            pRect->right = rcw.right - (m_statusRow==0 && margin<=2 ? 0 : margin);
            pRect->top = m_statusRow < 0 ? rcw.top + margin + m_statusHeight * (-m_statusRow-1) :
                         m_statusRow > 0 ? rcw.bottom - margin - m_statusHeight * m_statusRow :
                         m_statusRow == 0 && fMaximize ? rcw.bottom - margin - m_statusHeight : rcw.bottom;
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
    if (fInit) {
        m_fFullScreen = !m_pApp->GetFullscreen();
        m_fAutoHideActive = false;
        ::ShowWindow(m_hwndFrame, SW_SHOWNOACTIVATE);
    }
    if (m_pApp->GetFullscreen()) {
        if (!m_fFullScreen) {
            // フルスクリーン表示への遷移
            m_fHide = m_fHoveredFromOutside = false;
            m_dispCount = 0;
            ::GetCursorPos(&m_lastCurPos);
            m_idleCurPos = m_lastCurPos;
            if (!m_fAutoHideActive) {
                ::SetTimer(m_hwndFrame, TIMER_ID_AUTO_HIDE, TIMER_AUTO_HIDE_INTERVAL, NULL);
                m_fAutoHideActive = true;
            }
            m_fFullScreen = true;
        }
    }
    else {
        if (m_fFullScreen) {
            // 通常表示への遷移
            if ((m_statusRow == 0 || !m_fAutoHide) && m_fAutoHideActive) {
                ::KillTimer(m_hwndFrame, TIMER_ID_AUTO_HIDE);
                m_fAutoHideActive = false;
            }
            ::SetWindowPos(m_hwndFrame, m_pApp->GetAlwaysOnTop() ? HWND_TOPMOST : HWND_NOTOPMOST,
                           0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
            m_fHide = m_fHoveredFromOutside = false;
            m_dispCount = 0;
            ::GetCursorPos(&m_lastCurPos);
            m_idleCurPos.x = m_idleCurPos.y = -10;
            if (m_statusRow != 0 && m_fAutoHide && !m_fAutoHideActive) {
                ::SetTimer(m_hwndFrame, TIMER_ID_AUTO_HIDE, TIMER_AUTO_HIDE_INTERVAL, NULL);
                m_fAutoHideActive = true;
            }
            m_fFullScreen = false;
        }
    }
}

// CStatusViewの外枠リサイズ時の処理
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


void CTvtPlay::OnCommand(int id)
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
        else {
            CStatusItem *pItem;
            if (id == ID_COMMAND_OPEN_POPUP) {
                pItem = m_statusView.GetItemByID(STATUS_ITEM_BUTTON + ID_COMMAND_OPEN);
            }
            else {
                pItem = m_statusView.GetItemByID(STATUS_ITEM_BUTTON + ID_COMMAND_PREV);
                if (!pItem) pItem = m_statusView.GetItemByID(STATUS_ITEM_BUTTON + ID_COMMAND_SEEK_END);
            }
            if (pItem) pItem->OnRButtonDown(0, 0);
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
    case ID_COMMAND_SEEK_A:
    case ID_COMMAND_SEEK_B:
    case ID_COMMAND_SEEK_C:
    case ID_COMMAND_SEEK_D:
    case ID_COMMAND_SEEK_E:
    case ID_COMMAND_SEEK_F:
    case ID_COMMAND_SEEK_G:
    case ID_COMMAND_SEEK_H:
        Seek(m_seekList[id - ID_COMMAND_SEEK_A]);
        break;
    case ID_COMMAND_STRETCH_A:
    case ID_COMMAND_STRETCH_B:
    case ID_COMMAND_STRETCH_C:
    case ID_COMMAND_STRETCH_D:
    case ID_COMMAND_STRETCH_E:
    case ID_COMMAND_STRETCH_F:
        Stretch(GetStretchID() == id - ID_COMMAND_STRETCH_A ?
                -1 : id - ID_COMMAND_STRETCH_A);
        break;
    case ID_COMMAND_STRETCH:
            Stretch(GetStretchID() < 0 ? 0 : GetStretchID() + 1);
        break;
    }
    m_dispCount = m_timeoutOnCmd / TIMER_AUTO_HIDE_INTERVAL;
}


// イベントコールバック関数
// 何かイベントが起きると呼ばれる
LRESULT CALLBACK CTvtPlay::EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData)
{
    CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(pClientData);

    switch (Event) {
    case TVTest::EVENT_PLUGINENABLE:
        // プラグインの有効状態が変化した
        return pThis->EnablePlugin(lParam1 != 0);
    case TVTest::EVENT_FULLSCREENCHANGE:
        // 全画面表示状態が変化した
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->OnResize();
        break;
    case TVTest::EVENT_DRIVERCHANGE:
        // ドライバが変更された
        pThis->EnablePluginByDriverName();

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
            pThis->m_specOffset = -1;
        }
        break;
    case TVTest::EVENT_CHANNELCHANGE:
        // チャンネルが変更された
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->SetupDestination();
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
        pThis->m_fEventExecute = true;
        // FALL THROUGH!
    case TVTest::EVENT_STARTUPDONE:
        // 起動時の処理が終わった
        pThis->EnablePluginByDriverName();
        // コマンドラインにパスが指定されていれば開く
        if (pThis->m_pApp->IsPluginEnabled() && pThis->m_szSpecFileName[0]) {
            if (pThis->m_playlist.PushBackListOrFile(pThis->m_szSpecFileName, true) >= 0) {
                pThis->OpenCurrent(pThis->m_specOffset);
            }
            pThis->m_szSpecFileName[0] = 0;
            pThis->m_specOffset = -1;
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
    CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(pUserData);

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
        if (!pThis->m_pApp->GetFullscreen()) {
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
        if (pThis->m_fAutoHideActive && !pThis->m_fFullScreen) {
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


// CStatusViewの外枠ウインドウプロシージャ
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
        // SendMessage()で呼ぶときはlParam==1とすること
        if (wParam == TIMER_ID_AUTO_HIDE) {
            POINT curPos;
            RECT rect;
            if (::GetCursorPos(&curPos) && pThis->CalcStatusRect(&rect)) {
                // curPosとrectはともに仮想スクリーン座標系
                bool fHovered = rect.left <= curPos.x && curPos.x < rect.right &&
                                rect.top <= curPos.y && curPos.y < rect.bottom;

                // カーソルがどの方向からホバーされたか
                POINT lastPos = pThis->m_lastCurPos;
                if (pThis->m_fFullScreen || !fHovered) {
                    pThis->m_fHoveredFromOutside = false;
                }
                else if (pThis->m_statusRow== 1 && (rect.left>lastPos.x || rect.right<=lastPos.x || rect.bottom<=lastPos.y) ||
                         pThis->m_statusRow==-1 && (rect.left>lastPos.x || rect.right<=lastPos.x || rect.top>lastPos.y))
                {
                    pThis->m_fHoveredFromOutside = true;
                }
                pThis->m_lastCurPos = curPos;

                if (pThis->m_fFullScreen) {
                    // カーソルが移動したときに表示する(フルスクリーン)
                    if (pThis->m_timeoutOnMove > 0 &&
                        (curPos.x < pThis->m_idleCurPos.x-2 || pThis->m_idleCurPos.x+2 < curPos.x ||
                         curPos.y < pThis->m_idleCurPos.y-2 || pThis->m_idleCurPos.y+2 < curPos.y))
                    {
                        HMONITOR hMonApp = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                        HMONITOR hMonCur = ::MonitorFromPoint(curPos, MONITOR_DEFAULTTONULL);
                        if (hMonApp == hMonCur) {
                            pThis->m_dispCount = pThis->m_timeoutOnMove / TIMER_AUTO_HIDE_INTERVAL;
                            pThis->m_idleCurPos = curPos;
                        }
                    }
                }

                if (pThis->m_dispCount > 0 || (fHovered && !pThis->m_fHoveredFromOutside) || pThis->m_fPopuping) {
                    if (pThis->m_fHide) {
                        if (pThis->m_fFullScreen) {
                            ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                        }
                        else {
                            ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                        }
                        pThis->m_fHide = false;
                    }
                    if (fHovered) pThis->m_dispCount = 0;
                    else if (!lParam) pThis->m_dispCount--;
                }
                else {
                    if (!pThis->m_fHide) {
                        ::ShowWindow(hwnd, SW_HIDE);
                        pThis->m_fHide = true;
                    }
                }
            }
        }
        else if (wParam == TIMER_ID_RESET_DROP) {
            TVTest::StatusInfo si;
            // ドロップカウントが安定すればRESETSTATUSする
            if (!pThis->m_pApp->GetStatus(&si) || si.DropPacketCount == pThis->m_lastDropCount) {
                pThis->m_pApp->ResetStatus();
                ::KillTimer(hwnd, TIMER_ID_RESET_DROP);
            }
            pThis->m_lastDropCount = si.DropPacketCount;
        }
        break;
    case WM_UPDATE_POSITION:
        {
            DWORD pos = static_cast<DWORD>(wParam);
            DWORD dur = static_cast<DWORD>(lParam);
            pThis->m_position = pos & 0x7fffffff;
            pThis->m_duration = dur & 0x7fffffff;
            pThis->m_fSpecialExt = (pos&0x80000000) != 0;
            pThis->m_fFixed = (dur&0x80000000) != 0;
            pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
            pThis->m_statusView.UpdateItem(STATUS_ITEM_POSITION);
        }
        break;
    case WM_UPDATE_TOT_TIME:
        pThis->m_totTime = static_cast<int>(lParam);
        pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
        break;
    case WM_UPDATE_F_PAUSED:
        pThis->m_fPaused = wParam != 0;
        pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
        pThis->m_statusView.UpdateItem(STATUS_ITEM_BUTTON + ID_COMMAND_PAUSE);
        break;
    case WM_UPDATE_SPEED:
        pThis->m_speed = static_cast<int>(lParam);
        for (int i = 0; i < COMMAND_STRETCH_MAX; i++) 
            pThis->m_statusView.UpdateItem(STATUS_ITEM_BUTTON + ID_COMMAND_STRETCH_A + i);
        pThis->m_statusView.UpdateItem(STATUS_ITEM_BUTTON + ID_COMMAND_STRETCH);
        break;
    case WM_QUERY_CLOSE_NEXT:
        pThis->Close();
        if (pThis->m_playlist.Next(pThis->IsAllRepeat())) pThis->OpenCurrent();
        break;
    case WM_QUERY_SEEK_BGN:
        pThis->SeekToBegin();
        break;
    case WM_QUERY_RESET:
        if (pThis->m_pApp->GetVersion() >= TVTest::MakeVersion(0,7,21)) {
            pThis->m_pApp->Reset((wParam && pThis->m_fResetAllOnSeek) ? TVTest::RESET_ALL : TVTest::RESET_VIEWER);
        }
        if (wParam && pThis->m_resetDropInterval > 0) {
            pThis->m_lastDropCount = 0;
            ::SetTimer(hwnd, TIMER_ID_RESET_DROP, pThis->m_resetDropInterval, NULL);
        }
        break;
    case WM_APPCOMMAND:
        // メディアキー対策(オーナーウィンドウには自分で送る必要がある)
        ::SendMessage(::GetParent(hwnd), uMsg, wParam, lParam);
        return 0;
    }
    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}


// TSデータの送信制御スレッド
DWORD WINAPI CTvtPlay::TsSenderThread(LPVOID pParam)
{
    MSG msg;
    CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(pParam);
    int posSec = -1, durSec = -1, totSec = -1;
    bool fPrevFixed = false;
    int resetCount = 5;

    // コントロールの表示をリセット
    pThis->m_tsSender.Pause(false);
    ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_F_PAUSED, 0, 0);

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
                ::PostMessage(pThis->m_hwndFrame, WM_QUERY_RESET, 0, 0);
                ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_F_PAUSED, msg.wParam, 0);
                break;
            case WM_TS_SEEK_BGN:
                if (pThis->m_tsSender.SeekToBegin())
                    resetCount = 5;
                break;
            case WM_TS_SEEK_END:
                if (pThis->m_tsSender.SeekToEnd())
                    resetCount = 5;
                break;
            case WM_TS_SEEK:
                if (pThis->m_tsSender.Seek(static_cast<int>(msg.lParam)))
                    resetCount = 5;
                break;
            case WM_TS_SET_SPEED:
                {
                    int speed = static_cast<int>(msg.lParam);
                    if (!ASFilterSendMessage(WM_ASFLT_STRETCH, msg.wParam, MAKELPARAM(speed, 100))) {
                        // コマンド失敗の場合は等速にする
                        pThis->m_tsSender.SetSpeed(100, 100);
                        ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_SPEED, 0, 100);
                    }
                    else {
                        // テンポが変化する場合は転送速度を変更する
                        if (msg.wParam&1) pThis->m_tsSender.SetSpeed(speed, 100);
                        ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_SPEED, 0, speed);
                    }
                }
                break;
            case WM_TS_SET_MOD_TS:
                pThis->m_tsSender.SetModTimestamp(msg.wParam != 0);
                break;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        else if (pThis->m_fHalt) {
            ::Sleep(10);
        }
        else {
            bool fRead = pThis->m_tsSender.Send();

            int num, den;
            pThis->m_tsSender.GetSpeed(&num, &den);
            if (num != 100) {
                bool fSpecialExt;
                // ファイル末尾に達したか、容量確保録画の追っかけ時に(再生時間-3)秒以上の位置に達した場合
                if (!fRead || (!pThis->m_tsSender.IsFixed(&fSpecialExt) && fSpecialExt &&
                    pThis->m_tsSender.GetPosition() > pThis->m_tsSender.GetDuration() - 3000))
                {
                    // 等速に戻しておく
                    ASFilterPostMessage(WM_ASFLT_STRETCH, 0, MAKELPARAM(100, 100));
                    pThis->m_tsSender.SetSpeed(100, 100);
                    ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_SPEED, 0, 100);
                }
            }
            if (!fRead) {
                if (pThis->m_tsSender.IsFixed()) {
                    ::Sleep(pThis->m_waitOnStop);
                    ::PostMessage(pThis->m_hwndFrame,
                                  pThis->m_fSingleRepeat ? WM_QUERY_SEEK_BGN : WM_QUERY_CLOSE_NEXT,
                                  0, 0);
                }
            }
            if (!fRead || pThis->m_tsSender.IsPaused()) {
                resetCount = 0;
                ::Sleep(100);
            }
            else if (resetCount > 0 && --resetCount == 0) {
                // シーク後のリセットはある程度転送してから行う
                ::PostMessage(pThis->m_hwndFrame, WM_QUERY_RESET, 1, 0);
            }
        }

        int pos = pThis->m_tsSender.GetPosition();
        int dur = pThis->m_tsSender.GetDuration();
        int tot = pThis->m_tsSender.GetBroadcastTime();
        bool fSpecialExt;
        bool fFixed = pThis->m_tsSender.IsFixed(&fSpecialExt);

        // 再生位置の更新を伝える
        if (posSec != pos / 1000 || durSec != dur / 1000 || fPrevFixed != fFixed) {
            ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_POSITION,
                          fSpecialExt ? 0x80000000|(DWORD)pos : pos,
                          fFixed ? 0x80000000|(DWORD)dur : dur);
            posSec = pos / 1000;
            durSec = dur / 1000;
            fPrevFixed = fFixed;
        }
        // 放送時刻情報の更新を伝える
        if (totSec != tot) {
            ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_TOT_TIME, 0, tot);
            totSec = tot / 1000;
        }
    }

    // 等速に戻しておく(主スレッドが待っているのでSendMessageしてはいけない)
    ASFilterPostMessage(WM_ASFLT_STRETCH, 0, MAKELPARAM(100, 100));
    pThis->m_tsSender.SetSpeed(100, 100);
    ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_SPEED, 0, 100);

    // コントロールの表示をリセット
    pThis->m_tsSender.Pause(false);
    ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_F_PAUSED, 0, 0);
    ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_POSITION, 0, 0);
    ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_TOT_TIME, 0, -1);
    return 0;
}


TVTest::CTVTestPlugin *CreatePluginClass()
{
    return new CTvtPlay;
}




void CStatusViewEventHandler::OnMouseLeave()
{
    CStatusItem *pItem = m_pStatusView->GetItemByID(STATUS_ITEM_SEEK);
    if (pItem) {
        // ちょっと汚い…
        CSeekStatusItem *pSeekItem = dynamic_cast<CSeekStatusItem*>(pItem);
        pSeekItem->SetDrawSeekPos(false, 0);
        pSeekItem->Update();
    }
}


CSeekStatusItem::CSeekStatusItem(CTvtPlay *pPlugin, bool fDrawOfs, bool fDrawTot)
    : CStatusItem(STATUS_ITEM_SEEK, 128)
    , m_pPlugin(pPlugin)
    , m_fDrawSeekPos(false)
    , m_seekPos(0)
    , m_fDrawOfs(fDrawOfs)
    , m_fDrawTot(fDrawTot)
{
    m_MinWidth = 128;
}

void CSeekStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    int dur = m_pPlugin->GetDuration();
    int pos = m_pPlugin->GetPosition();
    COLORREF crText = ::GetTextColor(hdc);
    COLORREF crBk = ::GetBkColor(hdc);
    COLORREF crBar = m_pPlugin->IsPaused() ? MixColor(crText, crBk, 128) : crText;
    RECT rcBar, rc;

    // バーの最大矩形(外枠を含まない)
    rcBar.left   = pRect->left + 2;
    rcBar.top    = pRect->top + (pRect->bottom - pRect->top - 8) / 2 + 2;
    rcBar.right  = pRect->right - 2;
    rcBar.bottom = rcBar.top + 8 - 4;

    // 実際に描画するバーの右端位置
    int barPos = min(rcBar.right, rcBar.left + (dur <= 0 ? 0 :
                 (int)((long long)(rcBar.right - rcBar.left) * pos / dur)));

    // シーク位置を描画するかどうか
    bool fDrawPos = m_fDrawSeekPos && rcBar.left <= m_seekPos && m_seekPos < rcBar.right;

    // シーク位置を描画
    int drawPosWidth = 64;
    int drawPos = 0;
    if (fDrawPos) {
        TCHAR szText[128], szOfsText[64], szTotText[64];
        int posSec = (int)((long long)(m_seekPos-rcBar.left) * dur / (rcBar.right-rcBar.left) / 1000);

        szOfsText[0] = 0;
        if (m_fDrawOfs) {
            int ofsSec = posSec - pos / 1000;
            TCHAR sign = ofsSec < 0 ? TEXT('-') : TEXT('+');
            if (ofsSec < 0) ofsSec = -ofsSec;
            if (ofsSec < 60)
                ::wsprintf(szOfsText, TEXT("%c%d"), sign, ofsSec);
            else if (ofsSec < 3600)
                ::wsprintf(szOfsText, TEXT("%c%d:%02d"), sign, ofsSec/60, ofsSec%60);
            else
                ::wsprintf(szOfsText, TEXT("%c%d:%02d:%02d"), sign, ofsSec/60/60, ofsSec/60%60, ofsSec%60);
        }
        szTotText[0] = 0;
        if (m_fDrawTot) {
            int tot = m_pPlugin->GetTotTime();
            int totSec = tot / 1000 + posSec;
            if (tot < 0) ::lstrcpy(szTotText, TEXT(" (不明)"));
            else ::wsprintf(szTotText, TEXT(" (%d:%02d:%02d)"), totSec/60/60%24, totSec/60%60, totSec%60);
        }

        if (dur < 3600000) ::wsprintf(szText, TEXT("%02d:%02d%s%s"), posSec/60%60, posSec%60, szOfsText, szTotText);
        else ::wsprintf(szText, TEXT("%d:%02d:%02d%s%s"), posSec/60/60, posSec/60%60, posSec%60, szOfsText, szTotText);

        // シーク位置の描画に必要な幅を取得する
        ::SetRectEmpty(&rc);
        if (::DrawText(hdc, szText, -1, &rc,
                       DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT))
        {
            drawPosWidth = rc.right - rc.left + 10;
        }
        drawPos = min(m_seekPos + 5, rcBar.right - drawPosWidth - 1);

        // バーを描画
        ::SetRect(&rc, drawPos+1, rcBar.top,
                  min(max(barPos, drawPos+1), drawPos+drawPosWidth-1), rcBar.bottom);
        DrawUtil::Fill(hdc, &rc, MixColor(crText, crBk, 48));
        rc.left = min(max(barPos - 5, drawPos+1), drawPos+drawPosWidth-1);
        DrawUtil::FillGradient(hdc, &rc, MixColor(crText, crBk, 48), MixColor(crText, crBk, 192));

        // シーク位置を描画
        ::SetRect(&rc, drawPos + 5, pRect->top, drawPos + drawPosWidth, pRect->bottom);
        ::DrawText(hdc, szText, -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    // バーの外枠を描画
    HPEN hpen = ::CreatePen(PS_SOLID, 1, crText);
    HPEN hpenOld = SelectPen(hdc, hpen);
    HBRUSH hbrOld = SelectBrush(hdc, ::GetStockObject(NULL_BRUSH));
    ::SetRect(&rc, rcBar.left - 2, rcBar.top - 2, rcBar.right + 2, rcBar.bottom + 2);
    if (fDrawPos) {
        ::Rectangle(hdc, rc.left, rc.top, drawPos, rc.bottom);
        ::Rectangle(hdc, drawPos + drawPosWidth, rc.top, rc.right, rc.bottom);
    }
    else {
        ::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    }
    SelectBrush(hdc, hbrOld);
    SelectPen(hdc, hpenOld);
    ::DeleteObject(hpen);

    // バーを描画
    rc = rcBar;
    rc.right = fDrawPos ? min(barPos, drawPos - 2) : barPos;
    DrawUtil::Fill(hdc, &rc, crBar);
    if (fDrawPos && barPos >= drawPos + drawPosWidth + 2) {
        rc.left = drawPos + drawPosWidth + 2;
        rc.right = barPos;
        DrawUtil::Fill(hdc, &rc, crBar);
    }
}

void CSeekStatusItem::OnLButtonDown(int x, int y)
{
    RECT rc;
    GetClientRect(&rc);

    if (x <= 6) {
        m_pPlugin->SeekToBegin();
    }
    else if (x >= rc.right-rc.left+8-6) {
        m_pPlugin->SeekToEnd();
    }
    else {
        GetClientRect(&rc);
        int pos = m_pPlugin->GetPosition();
        int dur = m_pPlugin->GetDuration();
        int targetPos = (int)((long long)(x-4-2) * dur / ((rc.right-2) - (rc.left+2)));
        // ちょっと手前にシークされた方が使いやすいため1000ミリ秒引く
        m_pPlugin->Seek(targetPos - pos - 1000);
    }
}

void CSeekStatusItem::OnRButtonDown(int x, int y)
{
    m_pPlugin->Pause(!m_pPlugin->IsPaused());
}

void CSeekStatusItem::OnMouseMove(int x, int y)
{
    SetDrawSeekPos(true, x);
    Update();
}

void CSeekStatusItem::SetDrawSeekPos(bool fDraw, int pos)
{
    m_fDrawSeekPos = fDraw;
    m_seekPos = pos;
}


CPositionStatusItem::CPositionStatusItem(CTvtPlay *pPlugin)
    : CStatusItem(STATUS_ITEM_POSITION, 64)
    , m_pPlugin(pPlugin)
{
    m_MinWidth = 0;
}

void CPositionStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    TCHAR szText[128], szTotText[64];
    int posSec = m_pPlugin->GetPosition() / 1000;
    int durSec = m_pPlugin->GetDuration() / 1000;
    bool fSpecialExt;

    szTotText[0] = 0;
    if (m_pPlugin->IsPosDrawTotEnabled()) {
        int tot = m_pPlugin->GetTotTime();
        int totSec = tot / 1000 + posSec;
        if (tot < 0) ::lstrcpy(szTotText, TEXT(" (不明)"));
        else ::wsprintf(szTotText, TEXT(" (%d:%02d:%02d)"), totSec/60/60%24, totSec/60%60, totSec%60);
    }

    if (durSec < 60 * 60 && posSec < 60 * 60) {
        ::wsprintf(szText, TEXT("%02d:%02d/%02d:%02d%s%s"),
                   posSec / 60 % 60, posSec % 60,
                   durSec / 60 % 60, durSec % 60,
                   m_pPlugin->IsFixed(&fSpecialExt) ? TEXT("") : fSpecialExt ? TEXT("*") : TEXT("+"),
                   szTotText);
    }
    else {
        ::wsprintf(szText, TEXT("%d:%02d:%02d/%d:%02d:%02d%s%s"),
                   posSec / 60 / 60, posSec / 60 % 60, posSec % 60,
                   durSec / 60 / 60, durSec / 60 % 60, durSec % 60,
                   m_pPlugin->IsFixed(&fSpecialExt) ? TEXT("") : fSpecialExt ? TEXT("*") : TEXT("+"),
                   szTotText);
    }
    ::DrawText(hdc, szText, -1, const_cast<LPRECT>(pRect),
               DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

// 表示に適したアイテム幅を算出する
int CPositionStatusItem::CalcSuitableWidth()
{
    int rv = -1;
    LOGFONT logFont;
    if (m_pStatus->GetFont(&logFont)) {
        HFONT hfont = ::CreateFontIndirect(&logFont);
        if (hfont) {
            HDC hdc = ::GetDC(m_pStatus->GetHandle());
            if (hdc) {
                // 表示に適したアイテム幅を算出
                TCHAR szText[128];
                ::lstrcpy(szText, TEXT("00:00:00/00:00:00+"));
                if (m_pPlugin->IsPosDrawTotEnabled()) ::lstrcat(szText, TEXT(" (00:00:00)"));
                HFONT hfontOld = SelectFont(hdc, hfont);
                RECT rc;
                ::SetRectEmpty(&rc);
                if (::DrawText(hdc, szText, -1, &rc,
                               DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT))
                {
                    rv = rc.right - rc.left;
                }
                SelectFont(hdc, hfontOld);
                ::ReleaseDC(m_pStatus->GetHandle(), hdc);
            }
            ::DeleteObject(hfont);
        }
    }
    return rv;
}

void CPositionStatusItem::OnRButtonDown(int x, int y)
{
    POINT pt;
    UINT flags;
    if (GetMenuPos(&pt, &flags)) {
        m_pPlugin->SetupWithPopup(pt, flags);
    }
}

CButtonStatusItem::CButtonStatusItem(CTvtPlay *pPlugin, int id, const DrawUtil::CBitmap &icon)
    : CStatusItem(id, 16)
    , m_pPlugin(pPlugin)
    , m_icon(icon)
{
    m_MinWidth = 16;
}

void CButtonStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    // LoopとPauseとStretchは特別扱い
    int cmdID = m_ID - STATUS_ITEM_BUTTON;
    if (cmdID == ID_COMMAND_LOOP)
        DrawIcon(hdc, pRect, m_icon.GetHandle(), ICON_SIZE * (
                 m_pPlugin->IsSingleRepeat() ? 2 : m_pPlugin->IsAllRepeat() ? 1 : 0));
    else if (cmdID == ID_COMMAND_PAUSE)
        DrawIcon(hdc, pRect, m_icon.GetHandle(), ICON_SIZE * (
                 !m_pPlugin->IsOpen() || m_pPlugin->IsPaused() ? 1 : 0));
    else if (ID_COMMAND_STRETCH_A <= cmdID && cmdID <= ID_COMMAND_STRETCH_F)
        DrawIcon(hdc, pRect, m_icon.GetHandle(), ICON_SIZE * (
                 m_pPlugin->GetStretchID() == cmdID - ID_COMMAND_STRETCH_A ? 1 : 0));
    else if (cmdID == ID_COMMAND_STRETCH)
        DrawIcon(hdc, pRect, m_icon.GetHandle(), ICON_SIZE * (
                 m_pPlugin->GetStretchID() < 0 ? 0 : m_pPlugin->GetStretchID() + 1));
    else
        DrawIcon(hdc, pRect, m_icon.GetHandle(), 0);
}

void CButtonStatusItem::OnLButtonDown(int x, int y)
{
    m_pPlugin->OnCommand(m_ID - STATUS_ITEM_BUTTON);
}

void CButtonStatusItem::OnRButtonDown(int x, int y)
{
    // OpenとPrevとSeekToEndは特別扱い
    if (m_ID-STATUS_ITEM_BUTTON == ID_COMMAND_OPEN ||
        m_ID-STATUS_ITEM_BUTTON == ID_COMMAND_PREV ||
        m_ID-STATUS_ITEM_BUTTON == ID_COMMAND_SEEK_END)
    {
        POINT pt;
        UINT flags;
        if (GetMenuPos(&pt, &flags)) {
            if (m_ID-STATUS_ITEM_BUTTON == ID_COMMAND_OPEN) {
                m_pPlugin->OpenWithPopup(pt, flags);
            }
            else {
                m_pPlugin->OpenWithPlayListPopup(pt, flags);
            }
        }
    }
}
