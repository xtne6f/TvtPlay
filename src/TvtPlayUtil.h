#ifndef INCLUDE_TVT_PLAY_UTIL_H
#define INCLUDE_TVT_PLAY_UTIL_H

#define COMMAND_S_MAX   26

enum {
    ID_COMMAND_OPEN,
    ID_COMMAND_OPEN_POPUP,
    ID_COMMAND_LIST_POPUP,
    ID_COMMAND_CLOSE,
    ID_COMMAND_PREV,
    ID_COMMAND_SEEK_BGN,
    ID_COMMAND_SEEK_END,
    ID_COMMAND_SEEK_PREV,
    ID_COMMAND_SEEK_NEXT,
    ID_COMMAND_ADD_CHAPTER,
    ID_COMMAND_REPEAT_CHAPTER,
    ID_COMMAND_SKIP_X_CHAPTER,
    ID_COMMAND_CHAPTER_POPUP,
    ID_COMMAND_LOOP,
    ID_COMMAND_PAUSE,
    ID_COMMAND_NOP,
    ID_COMMAND_STRETCH,
    ID_COMMAND_STRETCH_RE,
    ID_COMMAND_STRETCH_POPUP,
    ID_COMMAND_SEEK_A,
    ID_COMMAND_STRETCH_A = ID_COMMAND_SEEK_A + COMMAND_S_MAX,
};

enum {
    STATUS_ITEM_SEEK,
    STATUS_ITEM_POSITION,
    STATUS_ITEM_BUTTON,
};

class ITvtPlayController
{
public:
    virtual ~ITvtPlayController() {}
    virtual bool IsOpen() const=0;
    virtual int GetPosition()=0;
    virtual int GetApparentPosition()=0;
    virtual int GetDuration()=0;
    virtual int GetTotTime()=0;
    virtual int IsExtending()=0;
    virtual bool IsPaused()=0;
    virtual CChapterMap& GetChapter()=0;
    virtual bool IsAllRepeat() const=0;
    virtual bool IsSingleRepeat() const=0;
    virtual bool IsRepeatChapterEnabled() const=0;
    virtual bool IsSkipXChapterEnabled() const=0;
    virtual bool IsPosDrawTotEnabled() const=0;
    virtual int GetStretchID()=0;
    virtual void SetupWithPopup(const POINT &pt, UINT flags)=0;
    virtual void EditChapterWithPopup(int pos, const POINT &pt, UINT flags)=0;
    virtual void EditAllChaptersWithPopup(const POINT &pt, UINT flags)=0;
    virtual void Pause(bool fPause)=0;
    virtual void SeekToBegin()=0;
    virtual void SeekToEnd()=0;
    virtual void Seek(int msec)=0;
    virtual void SeekAbsolute(int msec)=0;
    virtual void SeekAbsoluteApparently(int msec)=0;
    virtual void OnCommand(int id, const POINT *pPt = nullptr, UINT flags = 0)=0;
};

class CStatusViewEventHandler : public CStatusView::CEventHandler
{
public:
    void OnMouseLeave();
};

class CSeekStatusItem : public CStatusItem
{
public:
    CSeekStatusItem(ITvtPlayController *pPlugin, bool fDrawOfs, bool fDrawTot, int width, int seekMode);
    LPCTSTR GetName() const { return TEXT("シークバー"); }
    void Draw(HDC hdc, const RECT *pRect);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnRButtonDown(int x, int y);
    void OnMouseMove(int x, int y);
    void SetMousePos(int x, int y) { m_mousePos.x = x; m_mousePos.y = y; }
private:
    void ProcessSeek(int x);
    static int ConvUnit(int x, int a, int b) { return x<0||a<0||b<=0 ? 0 : x>=b ? a : (int)((long long)x*a/b); }
    ITvtPlayController *m_pPlugin;
    bool m_fDrawOfs, m_fDrawTot;
    POINT m_mousePos;
    int m_seekMode;
};

class CPositionStatusItem : public CStatusItem
{
public:
    CPositionStatusItem(ITvtPlayController *pPlugin);
    LPCTSTR GetName() const { return TEXT("再生位置"); }
    void Draw(HDC hdc, const RECT *pRect);
    int CalcSuitableWidth();
    void OnRButtonDown(int x, int y);
private:
    ITvtPlayController *m_pPlugin;
};

class CButtonStatusItem : public CStatusItem
{
public:
    CButtonStatusItem(ITvtPlayController *pPlugin, int id, int subID, int width, const DrawUtil::CBitmap &icon);
    LPCTSTR GetName() const { return TEXT("ボタン"); }
    void Draw(HDC hdc, const RECT *pRect);
    void OnLButtonDown(int x, int y);
    void OnRButtonDown(int x, int y);
    int GetSubID() const { return m_subID; }
private:
    ITvtPlayController *m_pPlugin;
    int m_subID;
    DrawUtil::CBitmap m_icon;
};

#endif // INCLUDE_TVT_PLAY_UTIL_H
