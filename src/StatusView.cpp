#include <Windows.h>
#include <WindowsX.h>
#include "StatusView.h"

CStatusItem::CStatusItem(CStatusView *pStatus, int id, int width)
    : m_pStatus(pStatus)
    , m_ID(id)
    , m_Width(width)
    , m_MinWidth(0)
{
    pStatus->AddItem(this);
}

bool CStatusItem::GetMenuPos(POINT *pPos, UINT *pFlags) const
{
    HWND hwnd = m_pStatus->GetHandle();
    RECT rc;
    if (hwnd && GetRect(&rc)) {
        pPos->x = rc.left;
        pPos->y = rc.bottom;
        ::ClientToScreen(hwnd, pPos);
        if (pFlags) *pFlags = 0;

        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        HMONITOR hMonitor = ::MonitorFromPoint(*pPos, MONITOR_DEFAULTTONULL);
        if (hMonitor && ::GetMonitorInfo(hMonitor, &mi) && pPos->y >= mi.rcMonitor.bottom - 32) {
            pPos->y += rc.top - rc.bottom;
            if (pFlags) *pFlags = TPM_BOTTOMALIGN;
        }
        return true;
    }
    return false;
}

void CStatusItem::DrawIcon(HDC hdc, const RECT *pRect, HBITMAP hbm, int srcX, int srcY, int iconWidth, int iconHeight) const
{
    DrawUtil::DrawMonoColorDIB(hdc,
        pRect->left + ((pRect->right - pRect->left) - iconWidth) / 2,
        pRect->top + ((pRect->bottom - pRect->top) - iconHeight) / 2,
        hbm, srcX, srcY, iconWidth, iconHeight, ::GetTextColor(hdc));
}

CStatusView::CStatusView()
    : m_hotItem(-1)
    , m_hwnd(NULL)
    , m_fInvalidate(false)
{
    ::SetRect(&m_itemMargin, 4, 4, 4, 4);
    m_statusRect.left = LONG_MAX;
}

CStatusView::~CStatusView()
{
    for (size_t i = 0; i < m_itemList.size(); ++i) {
        delete m_itemList[i];
    }
}

int CStatusView::IDToIndex(int id) const
{
    for (int i = 0; i < NumItems(); ++i) {
        if (m_itemList[i]->GetID() == id) {
            return i;
        }
    }
    return -1;
}

bool CStatusView::SetItemMargin(const RECT &margin)
{
    if (margin.left < 0 || margin.top < 0 || margin.right < 0 || margin.bottom < 0) {
        return false;
    }
    m_itemMargin = margin;
    return true;
}

bool CStatusView::OnViewEvent(VIEW_EVENT ev)
{
    if (ev == VIEW_EVENT_LEAVE) {
        m_hotItem = -1;
        return true;
    }
    return false;
}

bool CStatusView::OnMouseAction(MOUSE_ACTION action, HWND hwnd, const POINT &cursorPos, const RECT &statusRect)
{
    m_fInvalidate = false;
    m_hwnd = hwnd;
    m_statusRect = statusRect;
    bool fCaptured = ::GetCapture() == hwnd;
    int i = 0;
    for (; i < NumItems(); ++i) {
        int left, right;
        GetItemPartition(i, &left, &right);
        if (fCaptured && i == m_hotItem || !fCaptured && statusRect.left + left <= cursorPos.x && cursorPos.x < statusRect.left + right) {
            int x = cursorPos.x - statusRect.left - left;
            int y = cursorPos.y - statusRect.top;
            switch (action) {
            case MOUSE_ACTION_LDOWN:
                m_itemList[i]->OnLButtonDown(x, y);
                break;
            case MOUSE_ACTION_LUP:
                m_itemList[i]->OnLButtonUp(x, y);
                break;
            case MOUSE_ACTION_RDOWN:
                m_itemList[i]->OnRButtonDown(x, y);
                break;
            case MOUSE_ACTION_MOVE:
                m_itemList[i]->OnMouseMove(x, y);
                break;
            }
            break;
        }
    }
    if ((i < NumItems() ? i : -1) != m_hotItem) {
        m_hotItem = i < NumItems() ? i : -1;
        Invalidate();
    }
    m_statusRect.left = LONG_MAX;
    m_hwnd = NULL;
    return m_fInvalidate;
}

bool CStatusView::GetHotRect(const RECT &statusRect, RECT *pRect) const
{
    int left, right;
    if (!GetItemPartition(m_hotItem, &left, &right)) return false;
    ::SetRect(pRect, statusRect.left + left, statusRect.top, statusRect.left + right, statusRect.bottom);
    return true;
}

void CStatusView::Draw(HDC hdc, const RECT &statusRect, const LOGFONT &logFont, COLORREF crText, COLORREF crBk, COLORREF crHText, COLORREF crHBk)
{
    HFONT hfont = ::CreateFontIndirect(&logFont);
    if (hfont) {
        // OnMouseAction()などの表示上の位置とは一致しない
        m_statusRect = statusRect;
        for (int i = 0; i < NumItems(); ++i) {
            RECT rc;
            GetItemClientRect(m_itemList[i]->GetID(), &rc);
            HFONT hfontOld = SelectFont(hdc, hfont);
            int oldBkMode = ::SetBkMode(hdc, TRANSPARENT);
            COLORREF crOldText = ::SetTextColor(hdc, i == m_hotItem ? crHText : crText);
            COLORREF crOldBk = ::SetBkColor(hdc, i == m_hotItem ? crHBk : crBk);
            m_itemList[i]->Draw(hdc, &rc);
            ::SetBkColor(hdc, crOldBk);
            ::SetTextColor(hdc, crOldText);
            ::SetBkMode(hdc, oldBkMode);
            SelectFont(hdc, hfontOld);
        }
        m_statusRect.left = LONG_MAX;
        ::DeleteObject(hfont);
    }
}

bool CStatusView::GetClientRect(RECT *pRect) const
{
    if (m_statusRect.left == LONG_MAX) return false;
    *pRect = m_statusRect;
    return true;
}

bool CStatusView::GetItemRect(int id, RECT *pRect) const
{
    int left, right;
    if (m_statusRect.left == LONG_MAX || !GetItemPartition(IDToIndex(id), &left, &right)) {
        return false;
    }
    ::SetRect(pRect, m_statusRect.left + left, m_statusRect.top, m_statusRect.left + right, m_statusRect.bottom);
    return true;
}

bool CStatusView::GetItemClientRect(int id, RECT *pRect) const
{
    if (!GetItemRect(id, pRect)) return false;
    pRect->left += m_itemMargin.left;
    pRect->top += m_itemMargin.top;
    pRect->right -= m_itemMargin.right;
    pRect->bottom -= m_itemMargin.bottom;
    return true;
}

bool CStatusView::GetItemPartition(int index, int *left, int *right) const
{
    if (index < 0 || NumItems() <= index) {
        return false;
    }
    *right = 0;
    for (int i = 0; i <= index; ++i) {
        *left = *right;
        *right += m_itemList[i]->GetWidth() + m_itemMargin.left + m_itemMargin.right;
    }
    return true;
}
