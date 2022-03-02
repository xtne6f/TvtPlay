#include <Windows.h>
#include <WindowsX.h>
#include <map>
#include "Util.h"
#include "StatusView.h"
#include "ChapterMap.h"
#include "TvtPlayUtil.h"

void CStatusViewEventHandler::OnMouseLeave()
{
    CStatusItem *pItem = m_pStatusView->GetItemByID(STATUS_ITEM_SEEK);
    if (pItem) {
        // ちょっと汚い…
        CSeekStatusItem *pSeekItem = dynamic_cast<CSeekStatusItem*>(pItem);
        pSeekItem->SetMousePos(-1, -1);
        pSeekItem->Update();
    }
}

CSeekStatusItem::CSeekStatusItem(ITvtPlayController *pPlugin, bool fDrawOfs, bool fDrawTot, int width, int seekMode)
    : CStatusItem(STATUS_ITEM_SEEK, max(width, 64))
    , m_pPlugin(pPlugin)
    , m_fDrawOfs(fDrawOfs)
    , m_fDrawTot(fDrawTot)
    , m_seekMode(seekMode==1 ? 1 : seekMode==2 ? 2 : 0)
{
    m_MinWidth = m_DefaultWidth;
    SetMousePos(-1, -1);
}

void CSeekStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    const std::map<int, CChapterMap::CHAPTER> &chMap = m_pPlugin->GetChapter().Get();
    int dur = m_pPlugin->GetDuration();
    int pos = m_pPlugin->GetApparentPosition();
    COLORREF crText = ::GetTextColor(hdc);
    COLORREF crBk = ::GetBkColor(hdc);
    COLORREF crBar = m_pPlugin->IsPaused() ? MixColor(crText, crBk, 128) : crText;
    RECT rcBar, rcr, rcc, rc;
    GetRect(&rcr);
    GetClientRect(&rcc);

    // バーの最大矩形(外枠を含まない)
    rcBar.left   = pRect->left + 2;
    rcBar.top    = pRect->top + (pRect->bottom - pRect->top) / 2 - 4 + 2;
    rcBar.right  = pRect->right - 2;
    rcBar.bottom = pRect->top + (pRect->bottom - pRect->top) / 2 + 4 - 2;

    // 実際に描画するバーの右端位置
    int barX = rcBar.left + ConvUnit(pos, rcBar.right - rcBar.left, dur);

    // マウスホバー中のチャプターを探す
    POINT mousePos = m_mousePos;
    mousePos.x += pRect->left - (rcc.left - rcr.left);
    mousePos.y += pRect->top - (rcc.top - rcr.top);
    std::map<int, CChapterMap::CHAPTER>::const_iterator itHover = chMap.end();
    bool fMouseOnBar = rcBar.left <= mousePos.x && mousePos.x < rcBar.right;
    if (fMouseOnBar && 0 <= mousePos.y && mousePos.y < rcBar.top - 1) {
        int chapPosL = ConvUnit(mousePos.x-rcBar.left-5, dur, rcBar.right - rcBar.left);
        int chapPosR = ConvUnit(mousePos.x-rcBar.left+5, dur, rcBar.right - rcBar.left);
        if (chapPosR >= dur) chapPosR = INT_MAX;
        itHover = chMap.lower_bound(chapPosL);
        if (itHover != chMap.end() && itHover->first >= chapPosR) itHover = chMap.end();
    }
    // シーク位置を描画するかどうか
    bool fDrawPos = itHover != chMap.end() || (m_seekMode==0 && fMouseOnBar);

    // シーク位置を描画
    int drawPosWidth = 64;
    int drawPosX = 0;
    if (fDrawPos) {
        TCHAR szText[256], szOfsText[64], szTotText[64], szChName[16];
        // マウスホバー中のチャプター位置もしくはマウス位置
        int posSec = itHover != chMap.end() ? itHover->first / 1000 :
                     ConvUnit(mousePos.x - rcBar.left, dur, rcBar.right - rcBar.left) / 1000;

        szOfsText[0] = 0;
        if (m_fDrawOfs) {
            int ofsSec = posSec - pos / 1000;
            TCHAR sign = ofsSec < 0 ? TEXT('-') : TEXT('+');
            if (ofsSec < 0) ofsSec = -ofsSec;
            if (ofsSec < 60)
                _stprintf_s(szOfsText, TEXT("%c%d"), sign, ofsSec);
            else if (ofsSec < 3600)
                _stprintf_s(szOfsText, TEXT("%c%d:%02d"), sign, ofsSec/60, ofsSec%60);
            else
                _stprintf_s(szOfsText, TEXT("%c%d:%02d:%02d"), sign, ofsSec/60/60, ofsSec/60%60, ofsSec%60);
        }
        szTotText[0] = 0;
        if (m_fDrawTot) {
            int tot = m_pPlugin->GetTotTime();
            int totSec = tot / 1000 + posSec;
            if (tot < 0) _tcscpy_s(szTotText, TEXT(" (不明)"));
            else _stprintf_s(szTotText, TEXT(" (%d:%02d:%02d)"), totSec/60/60%24, totSec/60%60, totSec%60);
        }
        szChName[0] = 0;
        if (itHover != chMap.end() && itHover->second.name[0]) {
            szChName[0] = TEXT(' ');
            _tcsncpy_s(szChName + 1, _countof(szChName) - 1, itHover->second.name.data(), _TRUNCATE);
        }
        if (posSec < 3600 && dur < 3600000) {
            _stprintf_s(szText, TEXT("%02d:%02d%s%s%s"), posSec/60%60, posSec%60, szOfsText, szTotText, szChName);
        }
        else {
            _stprintf_s(szText, TEXT("%d:%02d:%02d%s%s%s"), posSec/60/60, posSec/60%60, posSec%60, szOfsText, szTotText, szChName);
        }
        // シーク位置の描画に必要な幅を取得する
        ::SetRectEmpty(&rc);
        if (::DrawText(hdc, szText, -1, &rc,
                       DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT))
        {
            drawPosWidth = rc.right - rc.left + 10;
        }
        drawPosX = min(mousePos.x + 5, rcBar.right - drawPosWidth - 1);

        // バーを描画
        ::SetRect(&rc, drawPosX+1, rcBar.top, min(max(barX, drawPosX+1), drawPosX+drawPosWidth-1), rcBar.bottom);
        DrawUtil::Fill(hdc, &rc, MixColor(crText, crBk, 48));
        rc.left = min(max(barX - 5, drawPosX+1), drawPosX+drawPosWidth-1);
        DrawUtil::FillGradient(hdc, &rc, MixColor(crText, crBk, 48), MixColor(crText, crBk, 192));

        // シーク位置を描画
        ::SetRect(&rc, drawPosX + 5, pRect->top, drawPosX + drawPosWidth, pRect->bottom);
        ::DrawText(hdc, szText, -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    // バーを描画
    rc = rcBar;
    rc.right = fDrawPos ? min(barX, drawPosX - 2) : barX;
    DrawUtil::Fill(hdc, &rc, crBar);
    if (fDrawPos && barX >= drawPosX + drawPosWidth + 2) {
        rc.left = drawPosX + drawPosWidth + 2;
        rc.right = barX;
        DrawUtil::Fill(hdc, &rc, crBar);
    }
    if (m_seekMode==1) {
        int realPos = m_pPlugin->GetPosition();
        int realBarX = rcBar.left + ConvUnit(realPos, rcBar.right - rcBar.left, dur);
        if (realBarX != barX) {
            rc = rcBar;
            rc.left = realBarX - 1;
            rc.right = realBarX + 2;
            DrawUtil::Fill(hdc, &rc, MixColor(crBar, crBk, 128));
        }
    }

    // バーの外枠を描画
    HPEN hpen = ::CreatePen(PS_SOLID, 1, crText);
    if (hpen) {
        HPEN hpenOld = SelectPen(hdc, hpen);
        HBRUSH hbrOld = SelectBrush(hdc, ::GetStockObject(NULL_BRUSH));
        ::SetRect(&rc, rcBar.left - 2, rcBar.top - 2, rcBar.right + 2, rcBar.bottom + 2);
        if (fDrawPos) {
            ::Rectangle(hdc, rc.left, rc.top, drawPosX, rc.bottom);
            ::Rectangle(hdc, drawPosX + drawPosWidth, rc.top, rc.right, rc.bottom);
        }
        else {
            ::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        }
        SelectPen(hdc, hpenOld);
        SelectBrush(hdc, hbrOld);
    }

    // チャプターを描画
    HBRUSH hbr = ::CreateSolidBrush(crText);
    if (hpen && hbr) {
        HPEN hpenOld = SelectPen(hdc, hpen);
        bool isIn = false, isX = false;
        std::map<int, CChapterMap::CHAPTER>::const_iterator it = chMap.begin();
        for (; it != chMap.end(); ++it) {
            int chapX = rcBar.left + ConvUnit(it->first, rcBar.right - rcBar.left, dur);
            POINT apt[3] = { chapX, rcBar.top-3,
                             it->second.IsOut() ? chapX : chapX-4, rcBar.top-7,
                             it->second.IsIn() ? chapX : chapX+4, rcBar.top-7 };
            HBRUSH hbrOld = it==itHover ? SelectBrush(hdc, ::GetStockObject(NULL_BRUSH)) : SelectBrush(hdc, hbr);
            ::Polygon(hdc, apt, 3);
            SelectBrush(hdc, hbrOld);

            // チャプター区間を描画
            if (isIn) {
                if (it->second.IsOut() && (isX && it->second.IsX() || !isX && !it->second.IsX())) {
                    ::LineTo(hdc, chapX, rcBar.top-7);
                    isIn = false;
                }
            }
            else if (it->second.IsIn()) {
                ::MoveToEx(hdc, chapX, rcBar.top-7, nullptr);
                isX = it->second.IsX();
                isIn = true;
            }
        }
        SelectPen(hdc, hpenOld);
    }

    if (hpen) ::DeletePen(hpen);
    if (hbr) ::DeleteBrush(hbr);
}

void CSeekStatusItem::ProcessSeek(int x)
{
    int dur = m_pPlugin->GetDuration();
    RECT rc, rcc;
    GetRect(&rc);
    GetClientRect(&rcc);
    if (x < 7) {
        m_pPlugin->SeekToBegin();
    }
    else if (x >= rc.right-rc.left-7) {
        m_pPlugin->SeekToEnd();
    }
    else {
        int pos = ConvUnit(x-(rcc.left-rc.left)-2, dur, rcc.right-rcc.left-4);
        // ちょっと手前にシークされた方が使いやすいため1000ミリ秒引く
        m_pPlugin->SeekAbsolute(pos - 1000);
    }
}

void CSeekStatusItem::OnLButtonDown(int x, int y)
{
    const std::map<int, CChapterMap::CHAPTER> &chMap = m_pPlugin->GetChapter().Get();
    int dur = m_pPlugin->GetDuration();
    RECT rc, rcc;
    GetRect(&rc);
    GetClientRect(&rcc);

    if (y < (rc.bottom-rc.top)/2-3 && !chMap.empty()) {
        int chapPosL = ConvUnit(x-(rcc.left-rc.left)-2-5, dur, rcc.right-rcc.left-4);
        int chapPosR = ConvUnit(x-(rcc.left-rc.left)-2+5, dur, rcc.right-rcc.left-4);
        if (chapPosR >= dur) chapPosR = INT_MAX;
        std::map<int, CChapterMap::CHAPTER>::const_iterator it = chMap.lower_bound(chapPosL);
        if (it != chMap.end() && it->first < chapPosR) {
            m_pPlugin->SeekAbsolute(it->first);
        }
    }
    else if (m_seekMode==1) {
        ::SetCapture(m_pStatus->GetHandle());
        int pos = ConvUnit(x-(rcc.left-rc.left)-2, dur, rcc.right-rcc.left-4);
        m_pPlugin->SeekAbsoluteApparently(max(pos - 1000, 0));
        m_pStatus->Invalidate();
    }
    else if (m_seekMode==2) {
        ::SetCapture(m_pStatus->GetHandle());
        ProcessSeek(x);
    }
    else {
        ProcessSeek(x);
    }
}

void CSeekStatusItem::OnLButtonUp(int x, int y)
{
    if (m_seekMode==1 && ::GetCapture()==m_pStatus->GetHandle()) {
        m_pPlugin->SeekAbsoluteApparently(-1);
        ProcessSeek(x);
    }
}

void CSeekStatusItem::OnRButtonDown(int x, int y)
{
    const std::map<int, CChapterMap::CHAPTER> &chMap = m_pPlugin->GetChapter().Get();
    int dur = m_pPlugin->GetDuration();
    RECT rc, rcc;
    GetRect(&rc);
    GetClientRect(&rcc);

    if (y < (rc.bottom-rc.top)/2-3 && !chMap.empty()) {
        int chapPosL = ConvUnit(x-(rcc.left-rc.left)-2-5, dur, rcc.right-rcc.left-4);
        int chapPosR = ConvUnit(x-(rcc.left-rc.left)-2+5, dur, rcc.right-rcc.left-4);
        if (chapPosR >= dur) chapPosR = INT_MAX;
        std::map<int, CChapterMap::CHAPTER>::const_iterator it = chMap.lower_bound(chapPosL);
        POINT pt;
        UINT flags;
        if (GetMenuPos(&pt, &flags)) {
            pt.x += x;
            if (it != chMap.end() && it->first < chapPosR) {
                m_pPlugin->EditChapterWithPopup(it->first, pt, flags);
            }
            else {
                m_pPlugin->EditAllChaptersWithPopup(pt, flags);
            }
        }
    }
    else {
        m_pPlugin->Pause(!m_pPlugin->IsPaused());
    }
}

void CSeekStatusItem::OnMouseMove(int x, int y)
{
    if (m_mousePos.x != x || m_mousePos.y != y) {
        SetMousePos(x, y);
        if (m_seekMode==1 && ::GetCapture()==m_pStatus->GetHandle()) {
            int dur = m_pPlugin->GetDuration();
            RECT rc, rcc;
            GetRect(&rc);
            GetClientRect(&rcc);
            int pos = ConvUnit(x-(rcc.left-rc.left)-2, dur, rcc.right-rcc.left-4);
            m_pPlugin->SeekAbsoluteApparently(max(pos - 1000, 0));
            m_pStatus->Invalidate();
        }
        else if (m_seekMode==2 && ::GetCapture()==m_pStatus->GetHandle()) {
            ProcessSeek(x);
        }
        else {
            Update();
        }
    }
}


CPositionStatusItem::CPositionStatusItem(ITvtPlayController *pPlugin)
    : CStatusItem(STATUS_ITEM_POSITION, 64)
    , m_pPlugin(pPlugin)
{
    m_MinWidth = 0;
}

void CPositionStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    TCHAR szText[128], szTotText[64];
    int posSec = m_pPlugin->GetApparentPosition() / 1000;
    int durSec = m_pPlugin->GetDuration() / 1000;
    int extMode = m_pPlugin->IsExtending();

    szTotText[0] = 0;
    if (m_pPlugin->IsPosDrawTotEnabled()) {
        int tot = m_pPlugin->GetTotTime();
        int totSec = tot / 1000 + posSec;
        if (tot < 0) _tcscpy_s(szTotText, TEXT(" (不明)"));
        else _stprintf_s(szTotText, TEXT(" (%d:%02d:%02d)"), totSec/60/60%24, totSec/60%60, totSec%60);
    }

    if (durSec < 60 * 60 && posSec < 60 * 60) {
        _stprintf_s(szText, TEXT("%02d:%02d/%02d:%02d%s%s"),
                    posSec / 60 % 60, posSec % 60,
                    durSec / 60 % 60, durSec % 60,
                    extMode==2 ? TEXT("*") : extMode ? TEXT("+") : TEXT(""), szTotText);
    }
    else {
        _stprintf_s(szText, TEXT("%d:%02d:%02d/%d:%02d:%02d%s%s"),
                    posSec / 60 / 60, posSec / 60 % 60, posSec % 60,
                    durSec / 60 / 60, durSec / 60 % 60, durSec % 60,
                    extMode==2 ? TEXT("*") : extMode ? TEXT("+") : TEXT(""), szTotText);
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
                _tcscpy_s(szText, TEXT("00:00:00/00:00:00+"));
                if (m_pPlugin->IsPosDrawTotEnabled()) _tcscat_s(szText, TEXT(" (00:00:00)"));
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

CButtonStatusItem::CButtonStatusItem(ITvtPlayController *pPlugin, int id, int subID, int width, const DrawUtil::CBitmap &icon)
    : CStatusItem(id, max(width, 0))
    , m_pPlugin(pPlugin)
    , m_subID(subID)
    , m_icon(icon)
{
    m_MinWidth = m_DefaultWidth;
}

void CButtonStatusItem::Draw(HDC hdc, const RECT *pRect)
{
    int cmdID = m_ID - STATUS_ITEM_BUTTON;
    int subCmdID = m_subID - STATUS_ITEM_BUTTON;
    int iconPos = 0;

    if (cmdID == ID_COMMAND_REPEAT_CHAPTER) {
        iconPos = m_pPlugin->IsRepeatChapterEnabled() ? 1 : 0;
    }
    if (cmdID == ID_COMMAND_SKIP_X_CHAPTER) {
        iconPos = m_pPlugin->IsSkipXChapterEnabled() ? 1 : 0;
    }
    else if (cmdID == ID_COMMAND_LOOP) {
        iconPos = m_pPlugin->IsSingleRepeat() ? 2 : m_pPlugin->IsAllRepeat() ? 1 : 0;
    }
    else if (cmdID == ID_COMMAND_PAUSE) {
        iconPos = !m_pPlugin->IsOpen() || m_pPlugin->IsPaused() ? 1 : 0;
    }
    else if (cmdID == ID_COMMAND_STRETCH || cmdID == ID_COMMAND_STRETCH_RE || cmdID == ID_COMMAND_STRETCH_POPUP) {
        int stid = m_pPlugin->GetStretchID();
        iconPos = stid < 0 ? 0 : stid + 1;
    }
    else if (ID_COMMAND_STRETCH_A <= cmdID && cmdID < ID_COMMAND_STRETCH_A + COMMAND_S_MAX) {
        int stid = m_pPlugin->GetStretchID();
        iconPos = stid == cmdID - ID_COMMAND_STRETCH_A ? 1 :
                  /* サブコマンドもStretchなら3つ目も使う */
                  (ID_COMMAND_STRETCH_A <= subCmdID && subCmdID < ID_COMMAND_STRETCH_A + COMMAND_S_MAX) &&
                  stid == subCmdID - ID_COMMAND_STRETCH_A ? 2 : 0;
    }
    int iconSize = m_icon.GetHeight();
    DrawIcon(hdc, pRect, m_icon.GetHandle(), iconSize * iconPos, 0, iconSize, iconSize);
}

void CButtonStatusItem::OnLButtonDown(int x, int y)
{
    POINT pt;
    UINT flags;
    if (GetMenuPos(&pt, &flags)) {
        m_pPlugin->OnCommand(m_ID - STATUS_ITEM_BUTTON, &pt, flags);
    }
}

void CButtonStatusItem::OnRButtonDown(int x, int y)
{
    POINT pt;
    UINT flags;
    if (GetMenuPos(&pt, &flags)) {
        m_pPlugin->OnCommand(m_subID - STATUS_ITEM_BUTTON, &pt, flags);
    }
}
