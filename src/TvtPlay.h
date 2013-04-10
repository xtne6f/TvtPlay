#ifndef INCLUDE_TVT_PLAY_H
#define INCLUDE_TVT_PLAY_H

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0,0,13)
#include "TVTestPlugin.h"
#include "StatusView.h"
#include "TsSender.h"

#define COMMAND_SEEK_MAX    8
#define BUTTON_MAX          14

class CStatusViewEventHandler : public CStatusView::CEventHandler
{
public:
    void OnMouseLeave();
};

// プラグインクラス
class CTvtPlay : public TVTest::CTVTestPlugin
{
    static const int STATUS_HEIGHT = 22;
    static const int TIMER_ID_FULL_SCREEN = 1;
    static const int TIMER_ID_RESET_DROP = 2;
    static const int TIMER_FULL_SCREEN_INTERVAL = 100;
    bool m_fInitialized;
    bool m_fSettingsLoaded;
    bool m_fForceEnable;
    TCHAR m_szIniFileName[MAX_PATH];
    TCHAR m_szSpecFileName[MAX_PATH];

    // コントロール
    HWND m_hwndFrame;
    bool m_fFullScreen, m_fHide, m_fToBottom;
    int m_statusMargin;
    bool m_fSeekDrawTot, m_fPosDrawTot;
    int m_posItemWidth;
    int m_timeoutOnCmd, m_timeoutOnMove;
    int m_dispCount;
    DWORD m_lastDropCount;
    int m_resetDropInterval;
    POINT m_lastCurPos;
    CStatusView m_statusView;
    CStatusViewEventHandler m_eventHandler;
    TCHAR m_szIconFileName[MAX_PATH];
    int m_seekList[COMMAND_SEEK_MAX];
    TCHAR m_buttonList[BUTTON_MAX][16];
    int m_buttonNum;

    // TS送信
    HANDLE m_hThread, m_hThreadEvent;
    DWORD m_threadID;
    CTsSender m_tsSender;
    TCHAR m_szPrevFileName[MAX_PATH];
    int m_position;
    int m_duration;
    int m_totTime;
    bool m_fFixed, m_fPaused;
    bool m_fHalt, m_fAutoClose, m_fAutoLoop;
    bool m_fResetAllOnSeek;

    void AnalyzeCommandLine(LPCWSTR cmdLine);
    void LoadSettings();
    void SaveSettings() const;
    bool InitializePlugin();
    bool EnablePlugin(bool fEnable);
    bool Open(HWND hwndOwner);
    bool ReOpen();
    bool Open(LPCTSTR fileName);
    void Close();
    void SetUpDestination();
    void ResetAndPostToSender(UINT Msg, WPARAM wParam, LPARAM lParam, bool fResetAll);
    void Resize();
    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);
    static BOOL CALLBACK WindowMsgCallback(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pResult, void *pUserData);
    static LRESULT CALLBACK FrameWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI TsSenderThread(LPVOID pParam);
public:
    CTvtPlay();
    virtual bool GetPluginInfo(TVTest::PluginInfo *pInfo);
    virtual bool Initialize();
    virtual bool Finalize();
    bool IsOpen() const { return m_hThread ? true : false; }
    int GetPosition() const { return m_position; }
    int GetDuration() const { return m_duration; }
    int GetTotTime() const { return m_totTime; }
    bool IsFixed() const { return m_fFixed; }
    bool IsPaused() const { return m_fPaused; }
    bool IsAutoLoop() const { return m_fAutoLoop; }

    void Pause(bool fPause);
    void SeekToBegin();
    void SeekToEnd();
    void Seek(int msec);
    void SetAutoLoop(bool fAutoLoop);
    void OnCommand(int id);
};

enum {
    STATUS_ITEM_SEEK,
    STATUS_ITEM_POSITION,
    STATUS_ITEM_BUTTON,
};

class CSeekStatusItem : public CStatusItem
{
public:
    CSeekStatusItem(CTvtPlay *pPlugin, bool fDrawTot);
    LPCTSTR GetName() const { return TEXT("シークバー"); };
    void Draw(HDC hdc, const RECT *pRect);
    void OnLButtonDown(int x, int y);
    void OnRButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void SetDrawSeekPos(bool fDraw, int pos);
private:
    CTvtPlay *m_pPlugin;
    bool m_fDrawSeekPos, m_fDrawLeft;
    int m_seekPos;
    bool m_fDrawTot;
};

class CPositionStatusItem : public CStatusItem
{
public:
    CPositionStatusItem(CTvtPlay *pPlugin, bool fDrawTot, int width);
    LPCTSTR GetName() const { return TEXT("再生位置"); };
    void Draw(HDC hdc, const RECT *pRect);
private:
    CTvtPlay *m_pPlugin;
    bool m_fDrawTot;
};

class CButtonStatusItem : public CStatusItem
{
public:
    CButtonStatusItem(CTvtPlay *pPlugin, int id, LPCTSTR iconFileName, int iconIndex);
    LPCTSTR GetName() const { return TEXT("ボタン"); }
    void Draw(HDC hdc, const RECT *pRect);
    void OnLButtonDown(int x, int y);
private:
    CTvtPlay *m_pPlugin;
    DrawUtil::CBitmap m_icons;
    int m_iconIndex;
};

#endif // INCLUDE_TVT_PLAY_H
