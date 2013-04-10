#ifndef INCLUDE_TVT_PLAY_H
#define INCLUDE_TVT_PLAY_H

// プラグインクラス
class CTvtPlay : public TVTest::CTVTestPlugin, public ITvtPlayController
{
    static const int BUTTON_MAX = 18;
    static const int BUTTON_TEXT_MAX = 192;
    static const int TIMER_AUTO_HIDE_INTERVAL = 100;
    static const int TIMER_UPDATE_HASH_LIST_INTERVAL = 5000;
    static const int TIMER_SYNC_CHAPTER_INTERVAL = 1000;
    static const int TIMER_WATCH_POS_GT_INTERVAL = 1000;
    static const int POPUP_MAX_MAX = 10000;
public:
    // CTVTestPlugin
    CTvtPlay();
    ~CTvtPlay();
    bool GetPluginInfo(TVTest::PluginInfo *pInfo);
    bool Initialize();
    bool Finalize();
    // ITvtPlayController
    bool IsOpen() const { return m_hThread ? true : false; }
    int GetPosition() { CBlockLock lock(&m_tsInfoLock); return m_infoPos; }
    int GetApparentPosition() { return m_apparentPos>=0 ? m_apparentPos : GetPosition(); }
    int GetDuration() { CBlockLock lock(&m_tsInfoLock); return m_infoDur; }
    int GetTotTime() { CBlockLock lock(&m_tsInfoLock); return m_infoTot; }
    int IsExtending() { CBlockLock lock(&m_tsInfoLock); return m_infoExtMode; }
    bool IsPaused() { CBlockLock lock(&m_tsInfoLock); return m_fInfoPaused; }
    CChapterMap& GetChapter() { return m_chapter; }
    bool IsAllRepeat() const { return m_fAllRepeat; }
    bool IsSingleRepeat() const { return m_fSingleRepeat; }
    bool IsRepeatChapterEnabled() const { return m_fRepeatChapter; }
    bool IsSkipXChapterEnabled() const { return m_fSkipXChapter; }
    bool IsPosDrawTotEnabled() const { return m_fPosDrawTot; }
    int GetStretchID();
    void SetupWithPopup(const POINT &pt, UINT flags);
    void EditChapterWithPopup(int pos, const POINT &pt, UINT flags);
    void EditAllChaptersWithPopup(const POINT &pt, UINT flags);
    void Pause(bool fPause);
    void SeekToBegin();
    void SeekToEnd();
    void Seek(int msec);
    void SeekAbsolute(int msec);
    void SeekAbsoluteApparently(int msec) { m_apparentPos = msec; }
    void OnCommand(int id, const POINT *pPt = NULL, UINT flags = 0);
private:
    // ファイルごとの固有情報
    struct HASH_INFO {
        LONGLONG hash; // 56bitハッシュ値
        int resumePos; // レジューム位置(msec)
    };
    void AnalyzeCommandLine(LPCWSTR cmdLine, bool fIgnoreFirst);
    void LoadSettings();
    void LoadTVTestSettings();
    void SaveSettings(bool fWriteDefault = false) const;
    bool LoadFileInfoSetting(std::list<HASH_INFO> &hashList) const;
    void SaveFileInfoSetting(const std::list<HASH_INFO> &hashList) const;
    void UpdateFileInfoSetting(const HASH_INFO &hashInfo) const;
    bool InitializePlugin();
    int GetCaptionPid();
    bool EnablePlugin(bool fEnable);
    bool IsAppMaximized() { return (::GetWindowLong(m_pApp->GetAppWindow(), GWL_STYLE) & WS_MAXIMIZE) != 0; }
    HWND GetFullscreenWindow();
    bool OpenWithDialog();
    bool OpenWithPopup(const POINT &pt, UINT flags);
    bool OpenWithPlayListPopup(const POINT &pt, UINT flags);
    void StretchWithPopup(const POINT &pt, UINT flags);
    int TrackPopup(HMENU hmenu, const POINT &pt, UINT flags);
    bool OpenCurrent(int offset = -1, int stretchID = -1);
    bool Open(LPCTSTR fileName, int offset, int stretchID);
    void Close();
    void SetupDestination();
    void WaitAndPostToSender(UINT Msg, WPARAM wParam, LPARAM lParam, bool fResetAll);
    void SetModTimestamp(bool fModTimestamp);
    void SetRepeatFlags(bool fAllRepeat, bool fSingleRepeat);
    void Stretch(int stretchID);
    void BeginWatchingNextChapter(bool fDoDelay);
    bool CalcStatusRect(RECT *pRect, bool fInit = false);
    void OnResize(bool fInit = false);
    void OnDispModeChange(bool fStandby, bool fInit = false);
    void OnFrameResize();
    void ProcessAutoHide(bool fNoDecDispCount);
    void EnablePluginByDriverName();
    void OnPreviewChange(bool fPreview);
    static LRESULT CALLBACK EventCallback(UINT Event, LPARAM lParam1, LPARAM lParam2, void *pClientData);
    static BOOL CALLBACK WindowMsgCallback(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT *pResult, void *pUserData);
    static LRESULT CALLBACK FrameWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    void UpdateInfos();
    static unsigned int __stdcall TsSenderThread(LPVOID pParam);
    void AddStreamCallback();
    void RemoveStreamCallback();
    static BOOL CALLBACK StreamCallback(BYTE *pData, void *pClientData);

    // 初期パラメータ
    bool m_fInitialized;
    bool m_fSettingsLoaded;
    bool m_fForceEnable, m_fIgnoreExt;
    bool m_fAutoEnUdp, m_fAutoEnPipe;
    bool m_fEventExecute;
    bool m_fEventStartupDone;
    bool m_fPausedOnPreviewChange;
    TCHAR m_szIniFileName[MAX_PATH];
    TCHAR m_szSpecFileName[MAX_PATH];
    int m_specOffset;
    int m_specStretchID;
    bool m_fShowOpenDialog;
    bool m_fRaisePriority;

    // コントロール
    HWND m_hwndFrame;
    bool m_fAutoHide, m_fAutoHideActive;
    bool m_fHoveredFromOutside;
    int m_statusRow, m_statusRowFull;
    int m_statusHeight;
    bool m_fSeekDrawOfs, m_fSeekDrawTot, m_fPosDrawTot;
    int m_seekItemMinWidth, m_posItemWidth;
    int m_timeoutOnCmd, m_timeoutOnMove;
    int m_seekItemOrder, m_posItemOrder;
    int m_dispCount;
    DWORD m_lastDropCount;
    int m_resetDropInterval;
    POINT m_lastCurPos, m_idleCurPos;
    CStatusView m_statusView;
    CStatusViewEventHandler m_eventHandler;
    TCHAR m_szIconFileName[MAX_PATH];
    int m_seekList[COMMAND_S_MAX];
    int m_stretchList[COMMAND_S_MAX];
    int m_seekListNum, m_stretchListNum;
    TCHAR m_buttonList[BUTTON_MAX][BUTTON_TEXT_MAX];
    int m_popupMax;
    TCHAR m_szPopupPattern[MAX_PATH];
    bool m_fPopupDesc, m_fPopuping;
    bool m_fDialogOpen;
    int m_seekMode;
    int m_apparentPos;

    // TS送信
    HANDLE m_hThread, m_hThreadEvent;
    DWORD m_threadID;
    int m_threadPriority;
    CTsSender m_tsSender;
    CCriticalLock m_tsInfoLock;
    int m_infoPos, m_infoDur, m_infoTot, m_infoExtMode, m_infoSpeed;
    bool m_fInfoPaused;
    bool m_fHalt, m_fAllRepeat, m_fSingleRepeat;
    bool m_fRepeatChapter, m_fSkipXChapter;
    int m_readBufSizeKB;
    int m_supposedDispDelay;
    int m_resetMode;
    int m_stretchMode, m_noMuteMax, m_noMuteMin;
    bool m_fConvTo188, m_fUnderrunCtrl, m_fUseQpc;
    int m_modTimestampMode;
    int m_initialStretchID;
    int m_pcrThresholdMsec;
    int m_salt, m_hashListMax;
    bool m_fUpdateHashList;

    // 再生リスト
    CPlaylist m_playlist;
    // 現在再生中のチャプター
    CChapterMap m_chapter;
    TCHAR m_szChaptersDirName[MAX_PATH];

    CTsTimestampShifter m_tsShifter;
    CCriticalLock m_streamLock;
    int m_streamCallbackRefCount;
    bool m_fResetPat;

#ifdef EN_SWC
    // 字幕でゆっくり
    CCaptionAnalyzer m_captionAnalyzer;
    TCHAR m_szCaptionDllPath[MAX_PATH];
    TCHAR m_szBregonigDllPath[MAX_PATH];
    int m_slowerWithCaption;
    int m_swcShowLate;
    int m_swcClearEarly;
    // ストリーム解析
    DWORD m_pcr;
    PAT m_pat;
    int m_captionPid;
#endif
};

#endif // INCLUDE_TVT_PLAY_H
