#ifndef INCLUDE_TVT_PLAY_H
#define INCLUDE_TVT_PLAY_H

#define TVTEST_PLUGIN_CLASS_IMPLEMENT
#define TVTEST_PLUGIN_VERSION TVTEST_PLUGIN_VERSION_(0,0,13)
#include "TVTestPlugin.h"

#define COMMAND_SEEK_MAX        8
#define COMMAND_STRETCH_MAX     6
#define BUTTON_MAX              16

class CStatusViewEventHandler : public CStatusView::CEventHandler
{
public:
    void OnMouseLeave();
};

// プラグインクラス
class CTvtPlay : public TVTest::CTVTestPlugin
{
    static const int TIMER_ID_AUTO_HIDE = 1;
    static const int TIMER_ID_RESET_DROP = 2;
    static const int TIMER_AUTO_HIDE_INTERVAL = 100;
    static const int HASH_LIST_MAX_MAX = 10000;
    static const int POPUP_MAX_MAX = 100;
    bool m_fInitialized;
    bool m_fSettingsLoaded;
    bool m_fForceEnable, m_fIgnoreExt;
    bool m_fAutoEnUdp, m_fAutoEnPipe;
    bool m_fEventExecute;
    bool m_fPausedOnPreviewChange;
    TCHAR m_szIniFileName[MAX_PATH];
    TCHAR m_szSpecFileName[MAX_PATH];
    int m_specOffset;

    // コントロール
    HWND m_hwndFrame;
    bool m_fFullScreen, m_fHide;
    bool m_fAutoHide, m_fAutoHideActive;
    bool m_fHoveredFromOutside;
    int m_statusRow, m_statusRowFull;
    int m_statusHeight;
    bool m_fSeekDrawTot, m_fPosDrawTot;
    int m_posItemWidth;
    int m_timeoutOnCmd, m_timeoutOnMove;
    int m_dispCount;
    DWORD m_lastDropCount;
    int m_resetDropInterval;
    POINT m_lastCurPos, m_idleCurPos;
    CStatusView m_statusView;
    CStatusViewEventHandler m_eventHandler;
    TCHAR m_szIconFileName[MAX_PATH];
    int m_seekList[COMMAND_SEEK_MAX];
    int m_stretchList[COMMAND_STRETCH_MAX];
    TCHAR m_buttonList[BUTTON_MAX][128];
    int m_buttonNum;
    int m_popupMax;
    TCHAR m_szPopupPattern[MAX_PATH];
    bool m_fPopupDesc, m_fPopuping;

    // TS送信
    HANDLE m_hThread, m_hThreadEvent;
    DWORD m_threadID;
    int m_threadPriority;
    CTsSender m_tsSender;
    int m_position;
    int m_duration;
    int m_totTime;
    int m_speed;
    bool m_fFixed, m_fSpecialExt, m_fPaused;
    bool m_fHalt, m_fAllRepeat, m_fSingleRepeat;
    int m_waitOnStop;
    bool m_fResetAllOnSeek;
    int m_stretchMode, m_noMuteMax, m_noMuteMin;
    bool m_fConvTo188;
    bool m_fModTimestamp;

    // ファイルごとの固有情報
    int m_salt, m_hashListMax;
    struct HASH_INFO {
        LONGLONG hash; // 56bitハッシュ値
        int resumePos; // レジューム位置(msec)
    };
    std::list<HASH_INFO> m_hashList;

    // 再生リスト
    CPlaylist m_playlist;

    void AnalyzeCommandLine(LPCWSTR cmdLine, bool fIgnoreFirst);
    void LoadSettings();
    void LoadTVTestSettings();
    void SaveSettings() const;
    bool InitializePlugin();
    bool EnablePlugin(bool fEnable);
    bool OpenWithDialog(HWND hwndOwner);
    bool OpenCurrent(int offset = -1);
    bool Open(LPCTSTR fileName, int offset);
    void Close();
    void SetupDestination();
    void ResetAndPostToSender(UINT Msg, WPARAM wParam, LPARAM lParam, bool fResetAll);
    bool CalcStatusRect(RECT *pRect, bool fInit = false);
    void Resize(bool fInit = false);
    void EnablePluginByDriverName();
    void OnPreviewChange(bool fPreview);
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
    bool IsFixed(bool *pfSpecialExt = NULL) const { if (pfSpecialExt) *pfSpecialExt=m_fSpecialExt; return m_fFixed; }
    bool IsPaused() const { return m_fPaused; }
    bool IsAllRepeat() const { return m_fAllRepeat; }
    bool IsSingleRepeat() const { return m_fSingleRepeat; }

    void SetupWithPopup(const POINT &pt, UINT flags);
    bool OpenWithPopup(const POINT &pt, UINT flags);
    bool OpenWithPlayListPopup(const POINT &pt, UINT flags);
    void Pause(bool fPause);
    void SeekToBegin();
    void SeekToEnd();
    void Seek(int msec);
    void SetModTimestamp(bool fModTimestamp);
    void SetRepeatFlags(bool fAllRepeat, bool fSingleRepeat);
    int GetStretchID() const;
    void Stretch(int stretchID);
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
    void OnRButtonDown(int x, int y);
private:
    CTvtPlay *m_pPlugin;
    bool m_fDrawTot;
};

class CButtonStatusItem : public CStatusItem
{
public:
    CButtonStatusItem(CTvtPlay *pPlugin, int id, const DrawUtil::CBitmap &icon);
    LPCTSTR GetName() const { return TEXT("ボタン"); }
    void Draw(HDC hdc, const RECT *pRect);
    void OnLButtonDown(int x, int y);
    void OnRButtonDown(int x, int y);
private:
    CTvtPlay *m_pPlugin;
    DrawUtil::CBitmap m_icon;
};

#endif // INCLUDE_TVT_PLAY_H
