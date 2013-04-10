// TVTestにtsファイル再生機能を追加するプラグイン
// 最終更新: 2011-08-16
// 署名: 849fa586809b0d16276cd644c6749503
#include <Windows.h>
#include <WindowsX.h>
#include <Shlwapi.h>
#include "Util.h"
#include "ColorScheme.h"
#include "TvtPlay.h"
#include "resource.h"


#define WM_UPDATE_POSITION  (WM_APP + 1)
#define WM_UPDATE_F_PAUSED  (WM_APP + 2)

#define WM_TS_CHPORT        (WM_APP + 1)
#define WM_TS_PAUSE         (WM_APP + 2)
#define WM_TS_SEEK_BGN      (WM_APP + 3)
#define WM_TS_SEEK_END      (WM_APP + 4)
#define WM_TS_SEEK          (WM_APP + 5)

enum {
    ID_COMMAND_OPEN,
    ID_COMMAND_PAUSE,
    ID_COMMAND_SEEK_BGN,
    ID_COMMAND_SEEK_END,
    ID_COMMAND_SEEK_M05,
    ID_COMMAND_SEEK_P05,
    ID_COMMAND_SEEK_M15,
    ID_COMMAND_SEEK_P15,
    ID_COMMAND_SEEK_M30,
    ID_COMMAND_SEEK_P30,
    ID_COMMAND_SEEK_M60,
    ID_COMMAND_SEEK_P60,
    ID_COMMAND_SEEK_M5M,
    ID_COMMAND_SEEK_P5M,
    ID_COMMAND_NOP,
    NUM_ID_COMMAND
};


static const LPCTSTR TVTPLAY_FRAME_WINDOW_CLASS = TEXT("TvtPlay Frame");


CTvtPlay::CTvtPlay()
    : m_fInitialized(false)
    , m_fSettingsLoaded(false)
    , m_fForceEnable(false)
    , m_hwndFrame(NULL)
    , m_threadHandle(NULL)
    , m_threadID(0)
    , m_position(0)
    , m_duration(0)
    , m_fPaused(false)
    , m_fFullScreen(false)
    , m_fHide(false)
    , m_hideCount(0)
    , m_fHalt(false)
{
    m_szSpecFileName[0] = 0;
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


bool CTvtPlay::Initialize()
{
    // 初期化処理
    int argc;
    LPTSTR *argv = ::CommandLineToArgvW(::GetCommandLine(), &argc);
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

    // コマンドを登録
    static const TVTest::CommandInfo commandList[] = {
        {ID_COMMAND_OPEN, L"Open", L"ファイルを開く"},
        {ID_COMMAND_PAUSE, L"Pause", L"一時停止/再生"},
        {ID_COMMAND_SEEK_BGN, L"SeekToBgn", L"シーク:先頭"},
        {ID_COMMAND_SEEK_END, L"SeekToEnd", L"シーク:末尾"},
        {ID_COMMAND_SEEK_M05, L"Seek-05", L"シーク:-5秒"},
        {ID_COMMAND_SEEK_P05, L"Seek+05", L"シーク:+5秒"},
        {ID_COMMAND_SEEK_M15, L"Seek-15", L"シーク:-15秒"},
        {ID_COMMAND_SEEK_P15, L"Seek+15", L"シーク:+15秒"},
        {ID_COMMAND_SEEK_M30, L"Seek-30", L"シーク:-30秒"},
        {ID_COMMAND_SEEK_P30, L"Seek+30", L"シーク:+30秒"},
        {ID_COMMAND_SEEK_M60, L"Seek-60", L"シーク:-60秒"},
        {ID_COMMAND_SEEK_P60, L"Seek+60", L"シーク:+60秒"},
        {ID_COMMAND_SEEK_M5M, L"Seek-5m", L"シーク:-5分"},
        {ID_COMMAND_SEEK_P5M, L"Seek+5m", L"シーク:+5分"},
        {ID_COMMAND_NOP, L"Nop", L"何もしない"},
    };
    m_pApp->RegisterCommand(commandList, ARRAY_SIZE(commandList));

    // イベントコールバック関数を登録
    m_pApp->SetEventCallback(EventCallback, this);

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
    
    for (int id = STATUS_ITEM_BUTTON_FIRST; id <= STATUS_ITEM_BUTTON_LAST; id++) {
        m_statusView.AddItem(new CButtonStatusItem(this, id));
    }
    m_statusView.AddItem(new CSeekStatusItem(this));
    m_statusView.AddItem(new CPositionStatusItem(this));
    
    LoadSettings();

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


unsigned short CTvtPlay::GetCurrentPort()
{
    TCHAR driverName[MAX_PATH];
    TVTest::ChannelInfo chInfo;

    m_pApp->GetDriverName(driverName, ARRAY_SIZE(driverName));

    if (!::lstrcmpi(::PathFindFileName(driverName), TEXT("BonDriver_UDP.dll")) &&
        m_pApp->GetCurrentChannelInfo(&chInfo))
    {
        return static_cast<unsigned short>(1234 + chInfo.Channel);
    }
    return 1234;
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


bool CTvtPlay::Open(LPCTSTR fileName)
{
    Close();
    m_statusView.SetSingleText(TEXT("ファイルを開いています"));
    
    if (!m_tsSender.Open(fileName, "127.0.0.1", GetCurrentPort())) {
        m_statusView.SetSingleText(NULL);
        return false;
    }

    m_pApp->Reset(TVTest::RESET_VIEWER);

    m_threadHandle = ::CreateThread(NULL, 0, TsSenderThread, this, 0, &m_threadID);
    if (!m_threadHandle) {
        m_tsSender.Close();
        m_statusView.SetSingleText(NULL);
        return false;
    }
    m_statusView.SetSingleText(NULL);
    return true;
}


void CTvtPlay::Close()
{
    m_statusView.SetSingleText(TEXT("ファイルを閉じています"));
    if (m_threadHandle) {
        ::PostThreadMessage(m_threadID, WM_QUIT, 0, 0);
        ::WaitForSingleObject(m_threadHandle, INFINITE);
        ::CloseHandle(m_threadHandle);
        m_threadHandle = NULL;
    }
    m_statusView.SetSingleText(NULL);
}


int CTvtPlay::GetPosition() const
{
    return m_position;
}

int CTvtPlay::GetDuration() const
{
    return m_duration;
}

bool CTvtPlay::IsPaused() const
{
    return m_fPaused;
}

void CTvtPlay::ChangePort(unsigned short port)
{
    if (m_threadHandle) ::PostThreadMessage(m_threadID, WM_TS_CHPORT, 0, port);
}

void CTvtPlay::Pause(bool fPause)
{
    if (m_threadHandle) ::PostThreadMessage(m_threadID, WM_TS_PAUSE, fPause ? 1 : 0, 0);
}

void CTvtPlay::ResetAndPostToSender(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (m_threadHandle) {
        m_fHalt = true;
        m_pApp->Reset(TVTest::RESET_VIEWER);
        if (!::PostThreadMessage(m_threadID, Msg, wParam, lParam)) m_fHalt = false;
    }
}

void CTvtPlay::SeekToBegin()
{
    ResetAndPostToSender(WM_TS_SEEK_BGN, 0, 0);
}

void CTvtPlay::SeekToEnd()
{
    ResetAndPostToSender(WM_TS_SEEK_END, 0, 0);
}

void CTvtPlay::Seek(int msec)
{
    ResetAndPostToSender(WM_TS_SEEK, 0, msec);
}


void CTvtPlay::Resize()
{
    if (m_pApp->GetFullscreen()) {
        RECT rect;
        if (::GetWindowRect(::GetDesktopWindow(), &rect)) {
            ::SetWindowPos(m_hwndFrame, NULL,
                           rect.left, rect.bottom - STATUS_HEIGHT * 2,
                           rect.right - rect.left, STATUS_HEIGHT, SWP_NOZORDER);
        }
        if (!m_fFullScreen) {
            m_fHide = false;
            m_hideCount = 0;
            ::SetTimer(m_hwndFrame, 0, STATUS_TIMER_INTERVAL, NULL);
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
            ::KillTimer(m_hwndFrame, 0);
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
        if (m_hwndFrame) Open(m_hwndFrame);
        break;
    case ID_COMMAND_PAUSE:
        Pause(!IsPaused());
        break;
    case ID_COMMAND_SEEK_BGN:
        SeekToBegin();
        break;
    case ID_COMMAND_SEEK_END:
        SeekToEnd();
        break;
    case ID_COMMAND_SEEK_M05: Seek(-5000); break;
    case ID_COMMAND_SEEK_P05: Seek( 4000); break;
    case ID_COMMAND_SEEK_M15: Seek(-15000); break;
    case ID_COMMAND_SEEK_P15: Seek( 14000); break;
    case ID_COMMAND_SEEK_M30: Seek(-30000); break;
    case ID_COMMAND_SEEK_P30: Seek( 29000); break;
    case ID_COMMAND_SEEK_M60: Seek(-60000); break;
    case ID_COMMAND_SEEK_P60: Seek( 59000); break;
    case ID_COMMAND_SEEK_M5M: Seek(-300000); break;
    case ID_COMMAND_SEEK_P5M: Seek( 299000); break;
    }
    m_hideCount = STATUS_HIDE_TIMEOUT;
}


void CTvtPlay::OnStartUpDone()
{
    if (m_szSpecFileName[0]) {
        Open(m_szSpecFileName);
        m_szSpecFileName[0] = 0;
    }
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
    case TVTest::EVENT_CHANNELCHANGE:
        // チャンネルが変更された
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->ChangePort(pThis->GetCurrentPort());
        break;
    case TVTest::EVENT_COMMAND:
        // コマンドが選択された
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->OnCommand(static_cast<int>(lParam1));
        return TRUE;
    case TVTest::EVENT_STARTUPDONE:
        // 起動時の処理が終わった
        if (pThis->m_pApp->IsPluginEnabled())
            pThis->OnStartUpDone();
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
    switch (uMsg) {
    case WM_CREATE:
        {
            LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(pcs->lpCreateParams);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        }
        break;
    case WM_TIMER:
        {
            CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
            RECT rect;
            POINT curPos;

            if (::GetWindowRect(::GetDesktopWindow(), &rect) && ::GetCursorPos(&curPos)) {
                HMONITOR hMonApp = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
                HMONITOR hMonCur = ::MonitorFromPoint(curPos, MONITOR_DEFAULTTOPRIMARY);
                bool isHovered = hMonApp == hMonCur && rect.bottom - STATUS_HEIGHT * 2 < curPos.y;
                
                if (pThis->m_hideCount > 0 || isHovered) {
                    if (pThis->m_fHide) {
                        ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                        pThis->m_fHide = false;
                    }
                    if (isHovered) pThis->m_hideCount = 0;
                    else pThis->m_hideCount--;
                }
                else {
                    if (!pThis->m_fHide) {
                        ::ShowWindow(hwnd, SW_HIDE);
                        pThis->m_fHide = true;
                    }
                }
            }
        }
        break;
    case WM_SIZE:
        {
            CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
            int margin = pThis->m_pApp->GetFullscreen() ? 0 : STATUS_MARGIN;
            
            CStatusItem *pItem = pThis->m_statusView.GetItemByID(STATUS_ITEM_SEEK);
            if (pItem) pItem->SetWidth(LOWORD(lParam) - 8 - 2 - margin*2 - 136 -
                                       24*(STATUS_ITEM_BUTTON_LAST-STATUS_ITEM_BUTTON_FIRST+1));
            pThis->m_statusView.SetPosition(margin, 0, LOWORD(lParam) - margin*2, HIWORD(lParam) - margin);
        }
        break;
    case WM_UPDATE_POSITION:
        {
            CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
            pThis->m_position = static_cast<int>(wParam);
            pThis->m_duration = static_cast<int>(lParam);
            pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
            pThis->m_statusView.UpdateItem(STATUS_ITEM_POSITION);
        }
        break;
    case WM_UPDATE_F_PAUSED:
        {
            CTvtPlay *pThis = reinterpret_cast<CTvtPlay*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
            pThis->m_fPaused = static_cast<int>(wParam) ? true : false;
            pThis->m_statusView.UpdateItem(STATUS_ITEM_SEEK);
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
    int posSec = -1, durSec = -1;

    pThis->m_fHalt = false;
    for (;;) {
        if (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;

            switch (msg.message) {
            case WM_TS_CHPORT:
                pThis->m_tsSender.SetAddress("127.0.0.1", static_cast<unsigned short>(msg.lParam));
                break;
            case WM_TS_PAUSE:
                pThis->m_tsSender.Pause(msg.wParam ? true : false);
                ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_F_PAUSED,
                              pThis->m_tsSender.IsPaused() ? 1 : 0, 0);
                break;
            case WM_TS_SEEK_BGN:
                pThis->m_tsSender.SeekToBegin();
                pThis->m_fHalt = false;
                break;
            case WM_TS_SEEK_END:
                pThis->m_tsSender.SeekToEnd();
                pThis->m_fHalt = false;
                break;
            case WM_TS_SEEK:
                pThis->m_tsSender.Seek(msg.lParam);
                pThis->m_fHalt = false;
                break;
            }
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
        }
        else if (pThis->m_fHalt) {
            ::Sleep(10);
        }
        else {
            int pos = pThis->m_tsSender.GetPosition();
            int dur = pThis->m_tsSender.GetDuration();

            if (posSec != pos / 1000 || durSec != dur / 1000) {
                ::PostMessage(pThis->m_hwndFrame, WM_UPDATE_POSITION, pos, dur);
                posSec = pos / 1000;
                durSec = dur / 1000;
            }

            if (!pThis->m_tsSender.Send() || pThis->m_tsSender.IsPaused()) ::Sleep(100);
            else for (int i = 0; i < 100; i++) pThis->m_tsSender.Send();
        }
    }

    pThis->m_tsSender.Close();
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


CSeekStatusItem::CSeekStatusItem(CTvtPlay *pPlugin)
    : CStatusItem(STATUS_ITEM_SEEK, 128)
    , m_pPlugin(pPlugin)
    , m_fDrawSeekPos(false)
{
    m_MinWidth = 128;
}

void CSeekStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    static const int DRAW_POS_WIDTH = 56;
    int dur = m_pPlugin->GetDuration();
    int pos = m_pPlugin->GetPosition();
    COLORREF crText = ::GetTextColor(hdc);
    COLORREF crBar = m_pPlugin->IsPaused() ? MixColor(crText, ::GetBkColor(hdc), 128) : crText;
    HPEN hpen, hpenOld;
    HBRUSH hbr, hbrOld;
    RECT rcBar, rc;

    rcBar.left   = pRect->left + 2;
    rcBar.top    = pRect->top + (pRect->bottom - pRect->top - 8) / 2 + 2;
    rcBar.right  = pRect->right - 2;
    rcBar.bottom = rcBar.top + 8 - 4;

    int barPos = min(rcBar.right, rcBar.left + (dur <= 0 ? 0 :
                 (int)((long long)(rcBar.right - rcBar.left) * pos / dur)));
    
    bool fDrawPos = m_fDrawSeekPos && rcBar.left <= m_seekPos && m_seekPos < rcBar.right;
    int drawPos = 0;
    if (fDrawPos) {
        m_fDrawLeft = m_seekPos >= rcBar.left + DRAW_POS_WIDTH + 4 &&
                      (m_seekPos >= rcBar.right - DRAW_POS_WIDTH - 4 ||
                      m_seekPos < barPos - 3 || m_seekPos < barPos + 3 && m_fDrawLeft);
        drawPos = m_fDrawLeft ? m_seekPos - DRAW_POS_WIDTH - 4 : m_seekPos + 5;
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
        ::Rectangle(hdc, drawPos + DRAW_POS_WIDTH, rc.top, rc.right, rc.bottom);
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

        if (rc.right >= drawPos + DRAW_POS_WIDTH + 2) {
            rc.left = drawPos + DRAW_POS_WIDTH + 2;
            ::FillRect(hdc, &rc, hbr);
        }
    }
    else {
        ::FillRect(hdc, &rc, hbr);
    }
    ::DeleteObject(hbr);

    if (fDrawPos) {
        // シーク位置を秒単位で描画
        int posSec = (int)((long long)(m_seekPos - rcBar.left) *
                     dur / (rcBar.right - rcBar.left) / 1000);
        TCHAR szText[128];
        ::wsprintf(szText, TEXT("%02d:%02d:%02d"),
                   posSec / 60 / 60, posSec / 60 % 60, posSec % 60);
        rc.left = drawPos + 6;
        rc.top = pRect->top;
        rc.right = drawPos + DRAW_POS_WIDTH;
        rc.bottom = pRect->bottom;
        DrawText(hdc, &rc, szText);
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
    if (fDraw && !m_fDrawSeekPos) m_fDrawLeft = false;
    m_fDrawSeekPos = fDraw;
    m_seekPos = pos;
}


CPositionStatusItem::CPositionStatusItem(CTvtPlay *pPlugin)
    : CStatusItem(STATUS_ITEM_POSITION, 128)
    , m_pPlugin(pPlugin)
{
    m_MinWidth = 128;
}

void CPositionStatusItem::Draw(HDC hdc,const RECT *pRect)
{
    TCHAR szText[128];
    int posSec = m_pPlugin->GetPosition() / 1000;
    int durSec = m_pPlugin->GetDuration() / 1000;
    ::wsprintf(szText, TEXT("%02d:%02d:%02d/%02d:%02d:%02d"),
               posSec / 60 / 60, posSec / 60 % 60, posSec % 60,
               durSec / 60 / 60, durSec / 60 % 60, durSec % 60);
    DrawText(hdc, pRect, szText);
}


CButtonStatusItem::CButtonStatusItem(CTvtPlay *pPlugin, int id)
    : CStatusItem(id, 16)
    , m_pPlugin(pPlugin)
{
    m_MinWidth = 16;
}

void CButtonStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    if (!m_icons.IsCreated())
        m_icons.Load(g_hinstDLL, IDB_BUTTONS);
    DrawIcon(hdc, pRect, m_icons.GetHandle(), (m_ID - STATUS_ITEM_BUTTON_FIRST) * 16);
}

void CButtonStatusItem::OnLButtonDown(int x, int y)
{
    switch (m_ID) {
    case STATUS_ITEM_BUTTON_OPEN:
        m_pPlugin->Open(m_pStatus->GetHandle());
        break;
    case STATUS_ITEM_BUTTON_M60:
        m_pPlugin->Seek(-60000);
        break;
    case STATUS_ITEM_BUTTON_M15:
        m_pPlugin->Seek(-15000);
        break;
    case STATUS_ITEM_BUTTON_M05:
        m_pPlugin->Seek(-5000);
        break;
    case STATUS_ITEM_BUTTON_P05:
        m_pPlugin->Seek(4000);
        break;
    case STATUS_ITEM_BUTTON_P15:
        m_pPlugin->Seek(14000);
        break;
    case STATUS_ITEM_BUTTON_P60:
        m_pPlugin->Seek(59000);
        break;
    }
}
