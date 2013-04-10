#ifndef INCLUDE_TVT_PLAY_H
#define INCLUDE_TVT_PLAY_H

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#include "TVTestPlugin.h"
#include "StatusView.h"
#include "TsSender.h"

class CStatusViewEventHandler : public CStatusView::CEventHandler
{
public:
    void OnMouseLeave();
};

// プラグインクラス
class CTvtPlay : public TVTest::CTVTestPlugin
{
    static const int STATUS_HEIGHT = 22;
    static const int STATUS_MARGIN = 2;
    static const int STATUS_TIMER_INTERVAL = 100;
    static const int STATUS_HIDE_TIMEOUT = 30;
    bool m_fInitialized;
    bool m_fSettingsLoaded;
    bool m_fForceEnable;
    TCHAR m_szSpecFileName[MAX_PATH];
    HWND m_hwndFrame;
    CStatusView m_statusView;
    CStatusViewEventHandler m_eventHandler;
    HANDLE m_threadHandle;
    DWORD m_threadID;
    CTsSender m_tsSender;
    int m_position;
    int m_duration;
    bool m_fPaused;
    bool m_fFullScreen, m_fHide;
    int m_hideCount;
    bool m_fHalt;
    
    void LoadSettings();
    bool InitializePlugin();
    bool EnablePlugin(bool fEnable);
    unsigned short GetCurrentPort();
    void ResetAndPostToSender(UINT Msg, WPARAM wParam, LPARAM lParam);
    void Resize();
    void OnCommand(int id);
    void OnStartUpDone();
    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);
    static BOOL CALLBACK WindowMsgCallback(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pResult, void *pUserData);
    static LRESULT CALLBACK FrameWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI TsSenderThread(LPVOID pParam);
public:
    CTvtPlay();
    virtual bool GetPluginInfo(TVTest::PluginInfo *pInfo);
    virtual bool Initialize();
    virtual bool Finalize();
    bool Open(HWND hwndOwner);
    bool Open(LPCTSTR fileName);
    void Close();
    int GetPosition() const;
    int GetDuration() const;
    bool IsPaused() const;
    void ChangePort(unsigned short port);
    void Pause(bool fPause);
    void SeekToBegin();
    void SeekToEnd();
    void Seek(int msec);
};

enum {
    STATUS_ITEM_BUTTON_OPEN,
    STATUS_ITEM_BUTTON_M60,
    STATUS_ITEM_BUTTON_M15,
    STATUS_ITEM_BUTTON_M05,
    STATUS_ITEM_BUTTON_P05,
    STATUS_ITEM_BUTTON_P15,
    STATUS_ITEM_BUTTON_P60,
    STATUS_ITEM_BUTTON_FIRST = STATUS_ITEM_BUTTON_OPEN,
    STATUS_ITEM_BUTTON_LAST = STATUS_ITEM_BUTTON_P60,
    STATUS_ITEM_SEEK,
    STATUS_ITEM_POSITION,
    STATUS_ITEM_FIRST = STATUS_ITEM_BUTTON_OPEN,
    STATUS_ITEM_LAST = STATUS_ITEM_POSITION
};

class CSeekStatusItem : public CStatusItem
{
public:
    CSeekStatusItem(CTvtPlay *pPlugin);
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
};

class CPositionStatusItem : public CStatusItem
{
public:
    CPositionStatusItem(CTvtPlay *pPlugin);
    LPCTSTR GetName() const { return TEXT("再生位置"); };
    void Draw(HDC hdc, const RECT *pRect);
private:
    CTvtPlay *m_pPlugin;
};

class CButtonStatusItem : public CStatusItem
{
public:
    CButtonStatusItem(CTvtPlay *pPlugin, int id);
    LPCTSTR GetName() const { return TEXT("ボタン"); }
    void Draw(HDC hdc, const RECT *pRect);
    void OnLButtonDown(int x, int y);
private:
    CTvtPlay *m_pPlugin;
    DrawUtil::CBitmap m_icons;
};

#endif // INCLUDE_TVT_PLAY_H
