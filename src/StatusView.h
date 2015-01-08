#ifndef STATUS_VIEW_H
#define STATUS_VIEW_H


#include <vector>
#include "DrawUtil.h"


class CStatusView;

class CStatusItem
{
public:
    CStatusItem(CStatusView *pStatus, int id, int width);
    virtual ~CStatusItem() {}
    bool GetRect(RECT *pRect) const;
    bool GetClientRect(RECT *pRect) const;
    int GetID() const { return m_ID; }
    int GetWidth() const { return m_Width; }
    void SetWidth(int width) { m_Width = max(width, m_MinWidth); }
    int GetMinWidth() const { return m_MinWidth; }
    bool Update();
    virtual void Draw(HDC hdc, const RECT *pRect) = 0;
    virtual void OnLButtonDown(int x, int y) {}
    virtual void OnLButtonUp(int x, int y) {}
    virtual void OnLButtonDoubleClick(int x, int y) { OnLButtonDown(x, y); }
    virtual void OnRButtonDown(int x, int y) { OnLButtonDown(x, y); }
    virtual void OnMouseMove(int x, int y) {}
protected:
    CStatusView *m_pStatus;
    int m_ID;
    int m_Width;
    int m_MinWidth;
    bool GetMenuPos(POINT *pPos, UINT *pFlags) const;
    void DrawIcon(HDC hdc, const RECT *pRect, HBITMAP hbm, int srcX = 0, int srcY = 0, int iconWidth = 16, int iconHeight = 16) const;
};

class CStatusView
{
public:
    enum VIEW_EVENT {
        VIEW_EVENT_NONE,
        VIEW_EVENT_ENTER,
        VIEW_EVENT_LEAVE,
    };
    enum MOUSE_ACTION {
        MOUSE_ACTION_NONE,
        MOUSE_ACTION_LDOWN,
        MOUSE_ACTION_LUP,
        MOUSE_ACTION_LDOUBLECLICK,
        MOUSE_ACTION_RDOWN,
        MOUSE_ACTION_MOVE,
    };
    CStatusView();
    ~CStatusView();
    int NumItems() const { return static_cast<int>(m_itemList.size()); }
    CStatusItem *GetItem(int index) const { return 0 <= index && index < NumItems() ? m_itemList[index] : NULL; }
    CStatusItem *GetItemByID(int id) const { return GetItem(IDToIndex(id)); }
    void AddItem(CStatusItem *pItem) { m_itemList.push_back(pItem); }
    int IDToIndex(int id) const;
    bool SetItemMargin(const RECT &margin);
    void GetItemMargin(RECT *pMargin) const { *pMargin = m_itemMargin; }
    bool OnViewEvent(VIEW_EVENT ev);
    bool OnMouseAction(MOUSE_ACTION action, HWND hwnd, const POINT &cursorPos, const RECT &statusRect);
    bool GetHotRect(const RECT &statusRect, RECT *pRect) const;
    void Draw(HDC hdc, const RECT &statusRect, const LOGFONT &logFont, COLORREF crText, COLORREF crBk, COLORREF crHText, COLORREF crHBk);
    HWND GetHandle() const { return m_hwnd; }
    bool GetClientRect(RECT *pRect) const;
    bool GetItemRect(int id, RECT *pRect) const;
    bool GetItemClientRect(int id, RECT *pRect) const;
    bool GetItemPartition(int index, int *left, int *right) const;
    bool Invalidate() { return m_fInvalidate = m_hwnd != NULL; }
    int GetCurItem() const { return m_hotItem < 0 ? -1 : m_itemList[m_hotItem]->GetID(); }
private:
    RECT m_itemMargin;
    std::vector<CStatusItem*> m_itemList;
    int m_hotItem;
    RECT m_statusRect;
    HWND m_hwnd;
    bool m_fInvalidate;
};

inline bool CStatusItem::GetRect(RECT *pRect) const { return m_pStatus->GetItemRect(m_ID, pRect); }
inline bool CStatusItem::GetClientRect(RECT *pRect) const { return m_pStatus->GetItemClientRect(m_ID, pRect); }
inline bool CStatusItem::Update() { return m_pStatus->Invalidate(); }

#endif
