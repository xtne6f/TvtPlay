#include <Windows.h>
#include <Shlwapi.h>
#include <algorithm>
#include <vector>
#include "Util.h"
#include "Playlist.h"

CPlaylist::CPlaylist()
    : m_pos(0)
{
}

// プレイリストファイルまたはTSファイルを再生リストに加える
// 成功: 加えられた位置, 失敗: 負
int CPlaylist::PushBackListOrFile(LPCTSTR path, bool fMovePos)
{
    // カレントからの絶対パスに変換
    TCHAR fullPath[MAX_PATH];
    DWORD rv = ::GetFullPathName(path, _countof(fullPath), fullPath, NULL);
    if (rv == 0 || rv >= MAX_PATH) return -1;

    int pos = -1;
    LPCTSTR ext = ::PathFindExtension(fullPath);
    if (ext && (!::lstrcmpi(ext, TEXT(".m3u")) || !::lstrcmpi(ext, TEXT(".tslist")))) {
        // プレイリストファイルとして処理
        pos = PushBackList(fullPath);
    }
    else {
        // TSファイルとして処理
        PLAY_INFO pi;
        ::lstrcpyn(pi.path, fullPath, _countof(pi.path));
        pos = static_cast<int>(size());
        push_back(pi);
    }
    if (fMovePos && pos >= 0) m_pos = pos;
    return pos;
}

// プレイリストファイルを再生リストに加える
int CPlaylist::PushBackList(LPCTSTR fullPath)
{
    int pos = -1;
    TCHAR *pRet = NewReadUtfFileToEnd(fullPath, FILE_SHARE_READ);
    if (pRet) {
        // 相対パスとの結合用
        TCHAR dirName[MAX_PATH];
        ::lstrcpyn(dirName, fullPath, _countof(dirName));
        ::PathRemoveFileSpec(dirName);

        for (TCHAR *p = pRet; *p;) {
            // 1行取得してpを進める
            TCHAR line[512];
            int len = ::StrCSpn(p, TEXT("\r\n"));
            ::lstrcpyn(line, p, min(len+1, _countof(line)));
            p += len;
            if (*p == TEXT('\r') && *(p+1) == TEXT('\n')) ++p;
            if (*p) ++p;

            // 左右の空白文字を取り除く
            ::StrTrim(line, TEXT(" \t"));
            if (line[0] != TEXT('#')) {
                // 左右に"の対応があれば取り除く
                if (line[0] == TEXT('"') && line[::lstrlen(line)-1] == TEXT('"')) ::StrTrim(line, TEXT("\""));
                // スラッシュ→バックスラッシュ
                for (TCHAR *q=line; *q; ++q) if (*q==TEXT('/')) *q=TEXT('\\');
                if (line[0]) {
                    PLAY_INFO pi;
                    if (::PathIsRelative(line)) {
                        // 相対パス
                        if (!::PathCombine(pi.path, dirName, line)) pi.path[0] = 0;
                    }
                    else {
                        // 絶対パス
                        ::lstrcpyn(pi.path, line, _countof(pi.path));
                    }
                    if (::PathFileExists(pi.path)) {
                        if (pos < 0) pos = static_cast<int>(size());
                        push_back(pi);
                    }
                }
            }
        }
        delete [] pRet;
    }
    return pos;
}

// 現在位置のPLAY_INFOを前に移動する
bool CPlaylist::MoveCurrentToPrev()
{
    if (m_pos != 0 && m_pos < size()) {
        std::swap((*this)[m_pos], (*this)[m_pos-1]);
        --m_pos;
        return true;
    }
    return false;
}

// 現在位置のPLAY_INFOを次に移動する
bool CPlaylist::MoveCurrentToNext()
{
    if (m_pos+1 < size()) {
        std::swap((*this)[m_pos], (*this)[m_pos+1]);
        ++m_pos;
        return true;
    }
    return false;
}

static inline bool CompareAsc(const PLAY_INFO& l, const PLAY_INFO& r) { return ::lstrcmpi(l.path, r.path) < 0; }

static inline bool CompareDesc(const PLAY_INFO& l, const PLAY_INFO& r) { return ::lstrcmpi(l.path, r.path) > 0; }

// ソートまたはシャッフルする
bool CPlaylist::Sort(SORT_MODE mode)
{
    size_t sz = size();
    if (m_pos < sz) {
        for (size_t i = 0; i < sz; ++i) {
            (*this)[i].fWork = m_pos == i;
        }
        if (mode == SORT_ASC) {
            std::sort(begin(), end(), CompareAsc);
        }
        else if (mode == SORT_DESC) {
            std::sort(begin(), end(), CompareDesc);
        }
        else if (mode == SORT_SHUFFLE) {
            std::random_shuffle(begin(), end());
        }
        for (size_t i = 0; i < sz; ++i) {
            if ((*this)[i].fWork) {
                m_pos = i;
                break;
            }
        }
        return true;
    }
    return false;
}

// 現在位置のPLAY_INFOを削除する
bool CPlaylist::EraseCurrent()
{
    if (m_pos < size()) {
        erase(begin() + m_pos);
        // 現在位置はまず次のPLAY_INFOに移すが、なければ前のPLAY_INFOに移す
        if (m_pos != 0 && m_pos >= size()) --m_pos;
        return true;
    }
    return false;
}

// 現在位置のPLAY_INFO以外を削除する
bool CPlaylist::ClearWithoutCurrent()
{
    if (m_pos < size()) {
        PLAY_INFO pi = (*this)[m_pos];
        clear();
        push_back(pi);
        m_pos = 0;
        return true;
    }
    return false;
}

// 現在位置を前に移動する
// 移動できなければfalseを返す
bool CPlaylist::Prev(bool fLoop)
{
    if (fLoop && !empty() && m_pos == 0) {
        m_pos = size() - 1;
        return true;
    }
    else if (m_pos != 0) {
        --m_pos;
        return true;
    }
    return false;
}

// 現在位置を次に移動する
// 移動できなければfalseを返す
bool CPlaylist::Next(bool fLoop)
{
    if (fLoop && !empty() && m_pos+1 >= size()) {
        m_pos = 0;
        return true;
    }
    else if (m_pos+1 < size()) {
        ++m_pos;
        return true;
    }
    return false;
}

// 文字列として出力する
int CPlaylist::ToString(TCHAR *pStr, int max, bool fFileNameOnly) const
{
    if (!pStr) {
        // 出力に必要な要素数を算出
        int strSize = 1;
        for (const_iterator it = begin(); it != end(); ++it) {
            LPCTSTR path = fFileNameOnly ? ::PathFindFileName((*it).path) : (*it).path;
            strSize += ::lstrlen(path) + 2;
        }
        return strSize;
    }
    if (max >= 1) {
        // 出力
        int strPos = 0;
        pStr[0] = 0;
        for (const_iterator it = begin(); max-strPos-2 > 0 && it != end(); ++it) {
            LPCTSTR path = fFileNameOnly ? ::PathFindFileName((*it).path) : (*it).path;
            ::lstrcpyn(pStr + strPos, path, max-strPos-2);
            ::lstrcat(pStr + strPos, TEXT("\r\n"));
            strPos += ::lstrlen(pStr + strPos);
        }
        return strPos + 1;
    }
    return 0;
}
