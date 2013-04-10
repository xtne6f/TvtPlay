// TVTestにtsファイル再生機能を追加するプラグイン
// 最終更新: 2011-08-27
// 署名: 849fa586809b0d16276cd644c6749503
#include <Windows.h>
#include <WindowsX.h>
#include <Shlwapi.h>
#include "Util.h"
#include "ColorScheme.h"
#include "TvtPlay.h"
#include "resource.h"


#define WM_UPDATE_POSITION  (WM_APP + 1)
#define WM_UPDATE_TOT_TIME  (WM_APP + 2)
#define WM_UPDATE_F_PAUSED  (WM_APP + 3)
#define WM_QUERY_CLOSE      (WM_APP + 4)
#define WM_QUERY_SEEK_BGN   (WM_APP + 5)
#define WM_QUERY_RESET_DROP (WM_APP + 6)

#define WM_TS_SET_UDP       (WM_APP + 1)
#define WM_TS_SET_PIPE      (WM_APP + 2)
#define WM_TS_PAUSE         (WM_APP + 3)
#define WM_TS_SEEK_BGN      (WM_APP + 4)
#define WM_TS_SEEK_END      (WM_APP + 5)
#define WM_TS_SEEK          (WM_APP + 6)

#define TVTPLAY_FRAME_WINDOW_CLASS  TEXT("TvtPlay Frame")
#define UDP_ADDR                    "127.0.0.1"
#define PIPE_NAME                   TEXT("\\\\.\\pipe\\BonDriver_Pipe%02d")

enum {
    ID_COMMAND_OPEN,
    ID_COMMAND_CLOSE,
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
};

static const TVTest::CommandInfo COMMAND_LIST[] = {
    {ID_COMMAND_OPEN, L"Open", L"ファイルを開く"},
    {ID_COMMAND_CLOSE, L"Close", L"ファイルを閉じる"},
    {ID_COMMAND_SEEK_BGN, L"SeekToBgn", L"シーク:先頭"},
    {ID_COMMAND_SEEK_END, L"SeekToEnd", L"シーク:末尾"},
    {ID_COMMAND_LOOP, L"Loop", L"ループする/しない"},
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
};

static const int DEFAULT_SEEK_LIST[COMMAND_SEEK_MAX] = {
    -60000, -30000, -15000, -5000, 4000, 14000, 29000, 59000
};

// NULL禁止
static LPCTSTR DEFAULT_BUTTON_LIST[BUTTON_MAX] = {
    TEXT("0,Open"), TEXT(";1,Close"), TEXT(";4,Loop"),
    TEXT(";2,SeekToBgn"),
    TEXT("8,SeekA"), TEXT(";9,SeekB"), TEXT("10,SeekC"), TEXT("11,SeekD"),
    TEXT("6,Pause"),
    TEXT("12,SeekE"), TEXT("13,SeekF"), TEXT(";14,SeekG"), TEXT("15,SeekH"),
    TEXT("3,SeekToEnd")
};


CTvtPlay::CTvtPlay()
    : m_fInitialized(false)
    , m_fSettingsLoaded(false)
    , m_fForceEnable(false)
    , m_hwndFrame(NULL)
    , m_fFullScreen(false)
    , m_fHide(false)
    , m_fToBottom(false)
    , m_fSeekDrawTot(false)
    , m_fPosDrawTot(false)
    , m_posItemWidth(0)
    , m_timeoutOnCmd(0)
    , m_timeoutOnMove(0)
    , m_dispCount(0)
    , m_lastDropCount(0)
    , m_resetDropInterval(0)
    , m_buttonNum(0)
    , m_hThread(NULL)
    , m_hThreadEvent(NULL)
    , m_threadID(0)
    , m_position(0)
    , m_duration(0)
    , m_totTime(-1)
    , m_fFixed(false)
    , m_fPaused(false)
    , m_fHalt(false)
    , m_fAutoClose(false)
    , m_fAutoLoop(false)
    , m_fResetAllOnSeek(false)
{
    m_szIniFileName[0] = 0;
    m_szSpecFileName[0] = 0;
    m_szIconFileName[0] = 0;
    m_szPrevFileName[0] = 0;
    CStatusView::Initialize(g_hinstDLL);
}


bool CTvtPlay::GetPluginInfo(TVTest::PluginInfo *pInfo)
{
    // プラグインの情報を返す
    pInfo->Type           = TVTest::PLUGIN_TYPE_NORMAL;
    pInfo->Flags          = TVTest::PLUGIN_FLAG_DISABLEONSTART;
    pInfo->pszPluginName  = L"TvtPlay";
    pInfo->pszCopyright   = L"Public Domain";
    pInfo->pszDescription = L"ファイル再生機能を追加";
    return true;
}


void CTvtPlay::AnalyzeCommandLine(LPCWSTR cmdLine)
{
    m_fForceEnable = false;
    m_szSpecFileName[0] = 0;

    int argc;
    LPTSTR *argv = ::CommandLineToArgvW(cmdLine, &argc);
    if (argv) {
        for (int i = 0; i < argc; i++) {
            if (!::lstrcmpi(argv[i], TEXT("/tvtplay")) ||
                !::lstrcmpi(argv[i], TEXT("-tvtplay")))
            {
                m_fForceEnable = true;
                break;
            }
        }

        if (argc >= 1 && argv[argc-1][0] != TEXT('/') && argv[argc-1][0] != TEXT('-')) {
            if (!m_fForceEnable) {
                LPCTSTR ext = ::PathFindExtension(argv[argc-1]);
                if (ext && (!::lstrcmpi(ext, TEXT(".ts")) ||
                            !::lstrcmpi(ext, TEXT(".m2t")) ||
                            !::lstrcmpi(ext, TEXT(".m2ts"))))
                {
                    m_fForceEnable = true;
                }
            }
            if (m_fForceEnable) ::lstrcpyn(m_szSpecFileName, argv[argc-1], ARRAY_SIZE(m_szSpecFileName));
        }

        ::LocalFree(argv);
    }
}


bool CTvtPlay::Initialize()
{
    // 初期化処理

    // コマンドを登録
    m_pApp->RegisterCommand(COMMAND_LIST, ARRAY_SIZE(COMMAND_LIST));

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);
    
    AnalyzeCommandLine(::GetCommandLine());
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
    
    if (!::GetModuleFileName(g_hinstDLL, m_szIniFileName, ARRAY_SIZE(m_szIniFileName)) ||
        !::PathRenameExtension(m_szIniFileName, TEXT(".ini"))) m_szIniFileName[0] = 0;

    m_fAutoClose = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("TsAutoClose"), 0, m_szIniFileName) != 0;
    m_fAutoLoop = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("TsAutoLoop"), 0, m_szIniFileName) != 0;
    m_fResetAllOnSeek = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("TsResetAllOnSeek"), 0, m_szIniFileName) != 0;
    m_resetDropInterval = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("TsResetDropInterval"), 1000, m_szIniFileName);
    m_fToBottom = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("ToBottom"), 1, m_szIniFileName) != 0;
    m_fSeekDrawTot = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("DispTot"), 0, m_szIniFileName) != 0;
    m_fPosDrawTot = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("DispTotOnStatus"), 0, m_szIniFileName) != 0;
    m_posItemWidth = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("StatusItemWidth"), 112, m_szIniFileName);
    m_timeoutOnCmd = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("TimeoutOnCommand"), 2000, m_szIniFileName);
    m_timeoutOnMove = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("TimeoutOnMouseMove"), 0, m_szIniFileName);
    ::GetPrivateProfileString(TEXT("Settings"), TEXT("IconImage"), TEXT(""),
                              m_szIconFileName, ARRAY_SIZE(m_szIconFileName), m_szIniFileName);

    // シークコマンドのシーク量設定を取得
    for (int i = 0; i < COMMAND_SEEK_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Seek%c"), (TCHAR)i + TEXT('A'));
        m_seekList[i] = ::GetPrivateProfileInt(TEXT("Settings"), key, DEFAULT_SEEK_LIST[i], m_szIniFileName);
    }

    // ボタンアイテムの配置設定を取得
    for (int i = 0; i < BUTTON_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Button%02d"), i);
        ::GetPrivateProfileString(TEXT("Settings"), key, DEFAULT_BUTTON_LIST[i],
                                  m_buttonList[i], ARRAY_SIZE(m_buttonList[0]), m_szIniFileName);
    }

    CColorScheme scheme;
    WCHAR szAppIniPath[MAX_PATH];
    if (m_pApp->GetSetting(L"IniFilePath", szAppIniPath, MAX_PATH) > 0)
        scheme.Load(szAppIniPath);
    
    // 配色
    CStatusView::ThemeInfo theme;
    scheme.GetStyle(CColorScheme::STYLE_STATUSITEM, &theme.ItemStyle);
    scheme.GetStyle(CColorScheme::STYLE_STATUSBOTTOMITEM, &theme.BottomItemStyle);
    scheme.GetStyle(CColorScheme::STYLE_STATUSHIGHLIGHTITEM, &theme.HighlightItemStyle);
    scheme.GetBorderInfo(CColorScheme::BORDER_STATUS, &theme.Border);
    m_statusView.SetTheme(&theme);

    m_fSettingsLoaded = true;

    // デフォルトの設定キーを出力するため
    if (::GetPrivateProfileInt(TEXT("Settings"), TEXT("DispTot"), -1, m_szIniFileName) == -1)
        SaveSettings();
}


// 設定の保存
void CTvtPlay::SaveSettings() const
{
    if (!m_fSettingsLoaded) return;

    WritePrivateProfileInt(TEXT("Settings"), TEXT("TsAutoClose"), m_fAutoClose, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("TsAutoLoop"), m_fAutoLoop, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("TsResetAllOnSeek"), m_fResetAllOnSeek, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("TsResetDropInterval"), m_resetDropInterval, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("ToBottom"), m_fToBottom, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("DispTot"), m_fSeekDrawTot, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("DispTotOnStatus"), m_fPosDrawTot, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("StatusItemWidth"), m_posItemWidth, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("TimeoutOnCommand"), m_timeoutOnCmd, m_szIniFileName);
    WritePrivateProfileInt(TEXT("Settings"), TEXT("TimeoutOnMouseMove"), m_timeoutOnMove, m_szIniFileName);
    ::WritePrivateProfileString(TEXT("Settings"), TEXT("IconImage"), m_szIconFileName, m_szIniFileName);
    
    for (int i = 0; i < COMMAND_SEEK_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Seek%c"), (TCHAR)i + TEXT('A'));
        WritePrivateProfileInt(TEXT("Settings"), key, m_seekList[i], m_szIniFileName);
    }

    for (int i = 0; i < BUTTON_MAX; i++) {
        TCHAR key[16];
        ::wsprintf(key, TEXT("Button%02d"), i);
        ::WritePrivateProfileString(TEXT("Settings"), key, m_buttonList[i], m_szIniFileName);
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
    
    // 文字列解析してボタンアイテムを生成
    m_buttonNum = 0;
    for (int i = 0; i < BUTTON_MAX; i++) {
        if (m_buttonList[i][0] == TEXT(';')) continue;

        int iconIndex = ::StrToInt(m_buttonList[i]);

        int commandID = -1;
        LPCTSTR cmd = ::StrChr(m_buttonList[i], TEXT(','));
        for (int j = 0; cmd && j < ARRAY_SIZE(COMMAND_LIST); j++) {
            if (!::lstrcmpi(&cmd[1], COMMAND_LIST[j].pszText)) {
                commandID = COMMAND_LIST[j].ID;
                break;
            }
        }
        if (commandID == -1) continue;

        m_statusView.AddItem(new CButtonStatusItem(this, STATUS_ITEM_BUTTON + commandID, m_szIconFileName, iconIndex));
        m_buttonNum++;
    }

    m_statusView.AddItem(new CSeekStatusItem(this, m_fSeekDrawTot));
    m_statusView.AddItem(new CPositionStatusItem(this, m_fPosDrawTot, m_posItemWidth));
    
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

        m_fFullScreen = false;
        Resize();
        ::ShowWindow(m_hwndFrame, SW_SHOW);

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


// ダイアログ選択でファイルを開く
bool CTvtPlay::Open(HWND hwndOwner)
{
    TCHAR fileName[MAX_PATH];
    fileName[0] = 0;

    OPENFILENAME ofn = {0};
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner   = hwndOwner;
    ofn.lpstrFilter = TEXT("MPEG-2 TS(*.ts;*.m2t;*.m2ts)\0*.ts;*.m2t;*.m2ts\0すべてのファイル\0*.*\0");
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = ARRAY_SIZE(fileName);
    ofn.lpstrTitle  = TEXT("TSファイルを開く");
    ofn.Flags       = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;

    if (!::GetOpenFileName(&ofn)) return false;
    return Open(fileName);
}


// 開いているor閉じたファイルを開きなおす
bool CTvtPlay::ReOpen()
{
    if (!m_szPrevFileName[0]) return false;
    return Open(m_szPrevFileName);
}


bool CTvtPlay::Open(LPCTSTR fileName)
{
    Close();
    
    if (!m_tsSender.Open(fileName)) return false;

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

    // メセージキューができるまで待つ
    ::WaitForSingleObject(m_hThreadEvent, INFINITE);

    // TSデータの送り先を設定
    SetUpDestination();
    
    ::lstrcpy(m_szPrevFileName, fileName);
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
    }
}


// ドライバの状態に応じてTSデータの転送先を設定する
void CTvtPlay::SetUpDestination()
{
    TCHAR path[MAX_PATH];
    TVTest::ChannelInfo chInfo;

    m_pApp->GetDriverName(path, ARRAY_SIZE(path));
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

        // RESET_VIEWERのデッドロック対策のため先にリセットする
        m_pApp->Reset(fResetAll ? TVTest::RESET_ALL : TVTest::RESET_VIEWER);
        ::PostThreadMessage(m_threadID, Msg, wParam, lParam);
        m_fHalt = false;
    }
}

void CTvtPlay::Pause(bool fPause)
{
    ResetAndPostToSender(WM_TS_PAUSE, fPause ? 1 : 0, 0, false);
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

void CTvtPlay::SetAutoLoop(bool fAutoLoop)
{
    m_fAutoLoop = fAutoLoop;
    m_statusView.UpdateItem(STATUS_ITEM_BUTTON + ID_COMMAND_LOOP);
    SaveSettings();
}


void CTvtPlay::Resize()
{
    if (m_pApp->GetFullscreen()) {
        HMONITOR hMon = ::MonitorFromWindow(m_hwndFrame, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi;
        mi.cbSize = sizeof(MONITORINFO);

        if (::GetMonitorInfo(hMon, &mi)) {
            ::SetWindowPos(m_hwndFrame, NULL,
                           mi.rcMonitor.left, mi.rcMonitor.bottom - (m_fToBottom ? STATUS_HEIGHT : STATUS_HEIGHT*2),
                           mi.rcMonitor.right - mi.rcMonitor.left, STATUS_HEIGHT, SWP_NOZORDER);
        }
        if (!m_fFullScreen) {
            m_fHide = false;
            m_dispCount = 0;
            ::GetCursorPos(&m_lastCurPos);
            ::SetTimer(m_hwndFrame, TIMER_ID_FULL_SCREEN, TIMER_FULL_SCREEN_INTERVAL, NULL);
            m_fFullScreen = true;
        }
    }
    else {
        RECT rect;
        if (::GetWindowRect(m_pApp->GetAppWindow(), &rect)) {
            ::SetWindowPos(m_hwndFrame, NULL, rect.left, rect.bottom,
                           rect.right - rect.left, STATUS_HEIGHT + STATUS_MARGIN, SWP_NOZORDER);
        }
        if (m_fFullScreen) {
            ::KillTimer(m_hwndFrame, TIMER_ID_FULL_SCREEN);
            ::SetWindowPos(m_hwndFrame, m_pApp->GetAlwaysOnTop() ? HWND_TOPMOST : HWND_NOTOPMOST,
                           0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            m_fFullScreen = false;
        }
    }
}


void CTvtPlay::OnCommand(int id)
{
    switch (id) {
    case ID_COMMAND_OPEN:
        Open(m_hwndFrame);
        break;
    case ID_COMMAND_CLOSE:
        Close();
        break;
    case ID_COMMAND_SEEK_BGN:
        SeekToBegin();
        break;
    case ID_COMMAND_SEEK_END:
        SeekToEnd();
        break;
    case ID_COMMAND_LOOP:
        SetAutoLoop(!IsAutoLoop());
        break;
    case ID_COMMAND_PAUSE:
        if (!IsOpen()) ReOpen();
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
    }
    m_dispCount = m_timeoutOnCmd / TIMER_FULL_SCREEN_INTERVAL;
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
            pThis->Resize();
        break;
    case TVTest::EVENT_DRIVERCHANGE:
    case TVTest::EVENT_CHANNELCHANGE:
        // ドライバorチャンネルが変更された
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->SetUpDestination();
        break;
    case TVTest::EVENT_COMMAND:
        // コマンドが選択された
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->OnCommand(static_cast<int>(lParam1));
        return TRUE;
    case TVTest::EVENT_EXECUTE:
        // 複数起動禁止時に複数起動された
        pThis->AnalyzeCommandLine(reinterpret_cast<LPCWSTR>(lParam1));
        // FALL THROUGH!
    case TVTest::EVENT_STARTUPDONE:
        // 起動時の処理が終わった
        // コマンドラインにパスが指定されていれば開く
        if (pThis->m_pApp->IsPluginEnabled() && pThis->m_szSpecFileName[0]) {
            pThis->Open(pThis->m_szSpecFileName);
            pThis->m_szSpecFileName[0] = 0;
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
        pThis->Resize();
        break;
    case WM_MOVE:
        if (!pThis->m_pApp->GetFullscreen()) {
            RECT rect;
            if (::GetWindowRect(hwnd, &rect)) {
                ::SetWindowPos(pThis->m_hwndFrame, NULL, rect.left, rect.bottom, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
        }
        break;
    case WM_DROPFILES:
        {
            TCHAR fileName[MAX_PATH];
            if (::DragQueryFile((HDROP)wParam, 0, fileName, ARRAY_SIZE(fileName)) != 0) {
                pThis->Open(fileName);
            }
            ::DragFinish((HDROP)wParam);
        }
        return TRUE;
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
    case WM_TIMER:
        if (wParam == TIMER_ID_FULL_SCREEN) {
            RECT rect;
            POINT curPos;

            if (::GetWindowRect(::GetDesktopWindow(), &rect) && ::GetCursorPos(&curPos)) {
                HMONITOR hMonApp = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                HMONITOR hMonCur = ::MonitorFromPoint(curPos, MONITOR_DEFAULTTOPRIMARY);
                bool isHoverd = hMonApp == hMonCur && rect.bottom -
                                (pThis->m_fToBottom ? STATUS_HEIGHT : STATUS_HEIGHT*2) < curPos.y;

                // カーソルが移動したときに表示する
                POINT lastPos = pThis->m_lastCurPos;
                if (pThis->m_timeoutOnMove > 0 && hMonApp == hMonCur &&
                    (curPos.x < lastPos.x-2 || lastPos.x+2 < curPos.x ||
                     curPos.y < lastPos.y-2 || lastPos.y+2 < curPos.y))
                {
                    pThis->m_dispCount = pThis->m_timeoutOnMove / TIMER_FULL_SCREEN_INTERVAL;
                    pThis->m_lastCurPos = curPos;
                }

                if (pThis->m_dispCount > 0 || isHoverd) {
                    if (pThis->m_fHide) {
                        ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                        pThis->m_fHide = false;
                    }
                    if (isHoverd) pThis->m_dispCount = 0;
                    else pThis->m_dispCount--;
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
    case WM_SIZE:
        {
            bool fFull = pThis->m_pApp->GetFullscreen();
            int margin = fFull ? 0 : STATUS_MARGIN;
            
            // シークバーをリサイズする
            CStatusItem *pItem = pThis->m_statusView.GetItemByID(STATUS_ITEM_SEEK);
            if (pItem) pItem->SetWidth(LOWORD(lParam) - 8 - 2 - margin*2 - pThis->m_posItemWidth - 8 - pThis->m_buttonNum * 24);
            pThis->m_statusView.SetPosition(margin, 0, LOWORD(lParam) - margin*2,
                                            HIWORD(lParam) - margin + (fFull && pThis->m_fToBottom ? 1 : 0));
        }
        break;
    case WM_UPDATE_POSITION:
        {
            pThis->m_position = static_cast<int>(wParam);
            int dur = static_cast<int>(lParam);
            pThis->m_duration = dur < 0 ? -dur : dur;
            pThis->m_fFixed = dur < 0;
            pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
            pThis->m_statusView.UpdateItem(STATUS_ITEM_POSITION);
        }
        break;
    case WM_UPDATE_TOT_TIME:
        pThis->m_totTime = static_cast<int>(lParam);
        pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
        break;
    case WM_UPDATE_F_PAUSED:
        pThis->m_fPaused = static_cast<int>(wParam) ? true : false;
        pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
        pThis->m_statusView.UpdateItem(STATUS_ITEM_BUTTON + ID_COMMAND_PAUSE);
        break;
    case WM_QUERY_CLOSE:
        pThis->Close();
        break;
    case WM_QUERY_SEEK_BGN:
        pThis->SeekToBegin();
        break;
    case WM_QUERY_RESET_DROP:
        if (pThis->m_resetDropInterval > 0) {
            pThis->m_lastDropCount = 0;
            ::SetTimer(hwnd, TIMER_ID_RESET_DROP, pThis->m_resetDropInterval, NULL);
        }
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


// TSデータの送信制御スレッド
DWORD WINAPI CTvtPlay::TsSenderThread(LPVOID pParam)
{
    MSG msg;
    CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(pParam);
    int posSec = -1, durSec = -1, totSec = -1;
    bool fPrevFixed = false;
    
    // コントロールの表示をリセット
    pThis->m_tsSender.Pause(false);
    ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_F_PAUSED, 0, 0);
    ::PostMessage(pThis->m_hwndFrame, WM_QUERY_RESET_DROP, 0, 0);

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
                pThis->m_tsSender.Pause(msg.wParam ? true : false);
                ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_F_PAUSED, msg.wParam, 0);
                break;
            case WM_TS_SEEK_BGN:
                pThis->m_tsSender.SeekToBegin();
                ::PostMessage(pThis->m_hwndFrame, WM_QUERY_RESET_DROP, 0, 0);
                break;
            case WM_TS_SEEK_END:
                pThis->m_tsSender.SeekToEnd();
                ::PostMessage(pThis->m_hwndFrame, WM_QUERY_RESET_DROP, 0, 0);
                break;
            case WM_TS_SEEK:
                pThis->m_tsSender.Seek(msg.lParam);
                ::PostMessage(pThis->m_hwndFrame, WM_QUERY_RESET_DROP, 0, 0);
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
            if (!fRead) {
                if (pThis->m_tsSender.IsFixed()) {
                    // ループ再生時に閉じてはいけない
                    if (pThis->m_fAutoLoop) ::PostMessage(pThis->m_hwndFrame, WM_QUERY_SEEK_BGN, 0, 0);
                    else if (pThis->m_fAutoClose) ::PostMessage(pThis->m_hwndFrame, WM_QUERY_CLOSE, 0, 0);
                }
            }
            if (!fRead || pThis->m_tsSender.IsPaused()) ::Sleep(100);
        }

        int pos = pThis->m_tsSender.GetPosition();
        int dur = pThis->m_tsSender.GetDuration();
        int tot = pThis->m_tsSender.GetBroadcastTime();
        bool fFixed = pThis->m_tsSender.IsFixed();

        // 再生位置の更新を伝える
        if (posSec != pos / 1000 || durSec != dur / 1000 || fPrevFixed != fFixed) {
            ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_POSITION, pos, fFixed ? -dur : dur);
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
    
    // ファイルを閉じてコントロールの表示をリセット
    pThis->m_tsSender.Pause(false);
    pThis->m_tsSender.Close();
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


CSeekStatusItem::CSeekStatusItem(CTvtPlay *pPlugin, bool fDrawTot)
    : CStatusItem(STATUS_ITEM_SEEK, 128)
    , m_pPlugin(pPlugin)
    , m_fDrawSeekPos(false)
{
    m_MinWidth = 128;
    m_fDrawTot = fDrawTot;
}

void CSeekStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    int dur = m_pPlugin->GetDuration();
    int pos = m_pPlugin->GetPosition();
    COLORREF crText = ::GetTextColor(hdc);
    COLORREF crBar = m_pPlugin->IsPaused() ? MixColor(crText, ::GetBkColor(hdc), 128) : crText;
    HPEN hpen, hpenOld;
    HBRUSH hbr, hbrOld;
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
        TCHAR szText[128];
        TCHAR szTotText[128];
        int posSec = (int)((long long)(m_seekPos-rcBar.left) * dur / (rcBar.right-rcBar.left) / 1000);
        
        szTotText[0] = 0;
        if (m_fDrawTot) {
            int tot = m_pPlugin->GetTotTime();
            int totSec = tot / 1000 + posSec;
            if (tot < 0) ::lstrcpy(szTotText, TEXT("#不明"));
            else ::wsprintf(szTotText, TEXT("#%02d:%02d:%02d"), totSec/60/60%24, totSec/60%60, totSec%60);
        }

        if (dur < 3600000) ::wsprintf(szText, TEXT("%02d:%02d%s"), posSec/60%60, posSec%60, szTotText);
        else ::wsprintf(szText, TEXT("%02d:%02d:%02d%s"), posSec/60/60, posSec/60%60, posSec%60, szTotText);

        // シーク位置の描画に必要な幅を取得する
        rc.left = rc.top = rc.right = rc.bottom = 0;
        if (::DrawText(hdc, szText, -1, &rc,
                       DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT))
            drawPosWidth = rc.right - rc.left + 10;
        
        m_fDrawLeft = m_seekPos >= rcBar.left + drawPosWidth + 4 &&
                      (m_seekPos >= rcBar.right - drawPosWidth - 4 ||
                      m_seekPos < barPos - 3 || m_seekPos < barPos + 3 && m_fDrawLeft);
        drawPos = m_fDrawLeft ? m_seekPos - drawPosWidth - 4 : m_seekPos + 5;
        
        // シーク位置を描画
        rc.left = drawPos + 5;
        rc.top = pRect->top;
        rc.right = drawPos + drawPosWidth;
        rc.bottom = pRect->bottom;
        ::DrawText(hdc, szText, -1, &rc,
                   DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    // バーの外枠を描画
    hpen = ::CreatePen(PS_SOLID, 1, crText);
    hpenOld = SelectPen(hdc, hpen);
    hbrOld = SelectBrush(hdc, ::GetStockObject(NULL_BRUSH));
    rc.left   = rcBar.left - 2;
    rc.top    = rcBar.top - 2;
    rc.right  = rcBar.right + 2;
    rc.bottom = rcBar.bottom + 2;
    if (fDrawPos) {
        ::Rectangle(hdc, rc.left, rc.top, drawPos, rc.bottom);
        ::Rectangle(hdc, drawPos + drawPosWidth, rc.top, rc.right, rc.bottom);
    }
    else {
        ::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    }
    ::SelectObject(hdc, hbrOld);
    ::SelectObject(hdc, hpenOld);
    ::DeleteObject(hpen);
    
    // バーを描画
    hbr = ::CreateSolidBrush(crBar);
    rc = rcBar;
    rc.right = barPos;
    if (fDrawPos) {
        int right = rc.right;
        if (rc.right > drawPos - 2) rc.right = drawPos - 2;
        ::FillRect(hdc, &rc, hbr);
        rc.right = right;

        if (rc.right >= drawPos + drawPosWidth + 2) {
            rc.left = drawPos + drawPosWidth + 2;
            ::FillRect(hdc, &rc, hbr);
        }
    }
    else {
        ::FillRect(hdc, &rc, hbr);
    }
    ::DeleteObject(hbr);
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
    if (fDraw && !m_fDrawSeekPos) m_fDrawLeft = false;
    m_fDrawSeekPos = fDraw;
    m_seekPos = pos;
}


CPositionStatusItem::CPositionStatusItem(CTvtPlay *pPlugin, bool fDrawTot, int width)
    : CStatusItem(STATUS_ITEM_POSITION, width)
    , m_pPlugin(pPlugin)
{
    m_MinWidth = width;
    m_fDrawTot = fDrawTot;
}

void CPositionStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    TCHAR szText[128];
    TCHAR szTotText[128];
    int posSec = m_pPlugin->GetPosition() / 1000;
    int durSec = m_pPlugin->GetDuration() / 1000;
    
    szTotText[0] = 0;
    if (m_fDrawTot) {
        int tot = m_pPlugin->GetTotTime();
        int totSec = tot / 1000 + posSec;
        if (tot < 0) ::lstrcpy(szTotText, TEXT("#不明"));
        else ::wsprintf(szTotText, TEXT("#%02d:%02d:%02d"), totSec/60/60%24, totSec/60%60, totSec%60);
    }

    if (durSec < 60 * 60 && posSec < 60 * 60) {
        ::wsprintf(szText, TEXT("%02d:%02d%s/%02d:%02d%c"),
                   posSec / 60 % 60, posSec % 60, szTotText,
                   durSec / 60 % 60, durSec % 60,
                   m_pPlugin->IsFixed() ? TEXT(' ') : TEXT('+'));
    }
    else {
        ::wsprintf(szText, TEXT("%02d:%02d:%02d%s/%02d:%02d:%02d%c"),
                   posSec / 60 / 60, posSec / 60 % 60, posSec % 60, szTotText,
                   durSec / 60 / 60, durSec / 60 % 60, durSec % 60,
                   m_pPlugin->IsFixed() ? TEXT(' ') : TEXT('+'));
    }
    DrawText(hdc, pRect, szText);
}


CButtonStatusItem::CButtonStatusItem(CTvtPlay *pPlugin, int id, LPCTSTR iconFileName, int iconIndex)
    : CStatusItem(id, 16)
    , m_pPlugin(pPlugin)
    , m_iconIndex(iconIndex)
{
    m_MinWidth = 16;

    if (iconFileName && iconFileName[0])
        m_icons.Load(NULL, iconFileName, LR_CREATEDIBSECTION | LR_LOADFROMFILE);
    
    if (!m_icons.IsCreated()) m_icons.Load(g_hinstDLL, IDB_BUTTONS);
}

void CButtonStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    // PauseとLoopは特別扱い
    if ((m_ID-STATUS_ITEM_BUTTON == ID_COMMAND_PAUSE && (!m_pPlugin->IsOpen() || m_pPlugin->IsPaused())) ||
        (m_ID-STATUS_ITEM_BUTTON == ID_COMMAND_LOOP && m_pPlugin->IsAutoLoop()))
    {
        DrawIcon(hdc, pRect, m_icons.GetHandle(), (m_iconIndex + 1) * 16);
    }
    else {
        DrawIcon(hdc, pRect, m_icons.GetHandle(), m_iconIndex * 16);
    }
}

void CButtonStatusItem::OnLButtonDown(int x, int y)
{
    m_pPlugin->OnCommand(m_ID - STATUS_ITEM_BUTTON);
}
