#include <Windows.h>
#include <Shlwapi.h>
#include <algorithm>
#include <vector>
#include <map>
#include "Util.h"
#include "ChapterMap.h"

#ifndef ASSERT
#include <cassert>
#define ASSERT assert
#endif

CChapterMap::CChapterMap()
    : m_hDir(INVALID_HANDLE_VALUE)
    , m_hEvent(nullptr)
    , m_fWritable(false)
    , m_retryCount(0)
{
    m_path[0] = 0;
}

CChapterMap::~CChapterMap()
{
    Close();
}

// pathに対応する.chapterファイルを読み込む
// .chapterファイルが存在すれば変更監視を開始する
bool CChapterMap::Open(LPCTSTR path, LPCTSTR subDirName)
{
    Close();

    // カレントからの絶対パスに変換
    TCHAR fullPath[MAX_PATH];
    DWORD rv = ::GetFullPathName(path, _countof(fullPath), fullPath, nullptr);
    if (!rv || rv >= _countof(fullPath)) return false;

    // 長い名前に変換
    TCHAR pathWoExt[MAX_PATH];
    rv = ::GetLongPathName(fullPath, pathWoExt, _countof(pathWoExt));
    if (!rv || rv >= _countof(pathWoExt)) return false;

    // 動画ファイルの存在確認
    if (!::PathFileExists(pathWoExt)) return false;

    // 拡張子除去
    ::PathRemoveExtension(pathWoExt);

    // 動画ファイルと同じ階層の.chapter[.txt][s.txt]へのパスを生成
    TCHAR chPath[MAX_PATH], ogmPath[MAX_PATH], ogm2Path[MAX_PATH];
    chPath[0] = ogmPath[0] = ogm2Path[0] = 0;
    int len = ::lstrlen(pathWoExt);
    if (len < _countof(chPath) - 8) {
        // PathAddExtension()を使ってはいけない!
        len = ::wsprintf(chPath, TEXT("%s.chapter"), pathWoExt);
        if (len <= _countof(ogmPath) - 4) ::wsprintf(ogmPath, TEXT("%s.txt"), chPath);
        if (len <= _countof(ogm2Path) - 5) ::wsprintf(ogm2Path, TEXT("%ss.txt"), chPath);
    }
    else {
        // 少なくとも.chapterへのパスは生成できなければならない
        return false;
    }

    // ディレクトリが存在すれば、chapters階層の.chapter[.txt][s.txt]へのパスを生成
    TCHAR subChPath[MAX_PATH], subOgmPath[MAX_PATH], subOgm2Path[MAX_PATH];
    subChPath[0] = subOgmPath[0] = subOgm2Path[0] = 0;
    if (subDirName[0]) {
        TCHAR subPathWoExt[MAX_PATH];
        ::lstrcpy(subPathWoExt, pathWoExt);
        if (::PathRemoveFileSpec(subPathWoExt) &&
            ::PathAppend(subPathWoExt, subDirName) &&
            ::PathIsDirectory(subPathWoExt) &&
            ::PathAppend(subPathWoExt, ::PathFindFileName(pathWoExt)))
        {
            len = ::lstrlen(subPathWoExt);
            if (len < _countof(subChPath) - 8) {
                len = ::wsprintf(subChPath, TEXT("%s.chapter"), subPathWoExt);
                if (len <= _countof(subOgmPath) - 4) ::wsprintf(subOgmPath, TEXT("%s.txt"), subChPath);
                if (len <= _countof(subOgm2Path) - 5) ::wsprintf(subOgm2Path, TEXT("%ss.txt"), subChPath);
            }
        }
    }

    LPCTSTR chReadPath = subChPath[0] && ::PathFileExists(subChPath) ? subChPath :
                         ::PathFileExists(chPath) ? chPath : nullptr;
    if (chReadPath) {
        // 中身がチャプターコマンド仕様に従っている場合のみ書き込み可能(m_fWritable)
        for (int i = 0; i < RETRY_LIMIT; ++i) {
            std::vector<WCHAR> cmd = ReadUtfFileToEnd(chReadPath, FILE_SHARE_READ);
            if (!cmd.empty()) {
                m_fWritable = InsertCommand(&cmd.front());
                break;
            }
            ::Sleep(200);
        }
        ::lstrcpy(m_path, chReadPath);

        // 変更監視のためにディレクトリを開く
        TCHAR tmpPath[MAX_PATH];
        ::lstrcpy(tmpPath, m_path);
        if (::PathRemoveFileSpec(tmpPath)) {
            m_hDir = ::CreateFile(tmpPath, FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            Sync();
        }
    }
    else {
        m_fWritable = true;
        ::lstrcpy(m_path, subChPath[0] ? subChPath : chPath);

        LPCTSTR ogmReadPath = subOgmPath[0] && ::PathFileExists(subOgmPath) ? subOgmPath :
                              ogmPath[0] && ::PathFileExists(ogmPath) ? ogmPath :
                              subOgm2Path[0] && ::PathFileExists(subOgm2Path) ? subOgm2Path :
                              ogm2Path[0] && ::PathFileExists(ogm2Path) ? ogm2Path : nullptr;
        if (ogmReadPath) {
            // BOMがなければANSIコードページで読む
            std::vector<WCHAR> cmd = ReadUtfFileToEnd(ogmReadPath, FILE_SHARE_READ, true);
            if (!cmd.empty()) {
                InsertOgmStyleCommand(&cmd.front());
            }
        }
        else {
            // もしあればファイル名からロード
            TCHAR *pCmd = ::StrStrI(::PathFindFileName(m_path), TEXT("c-"));
            if (pCmd) InsertCommand(pCmd);
        }
    }
    return true;
}

void CChapterMap::Close()
{
    m_map.clear();
    if (NeedToSync()) {
        ::CloseHandle(m_hDir);
        m_hDir = INVALID_HANDLE_VALUE;
    }
    if (m_hEvent) {
        ::CloseHandle(m_hEvent);
        m_hEvent = nullptr;
    }
    m_fWritable = false;
    m_retryCount = 0;
    m_path[0] = 0;
}

// .chapterファイルの変更を監視し、変更があれば読み込む
// マップに変更があればtrueを返す
// TODO: Save()後に読み込みが発生してしまう
bool CChapterMap::Sync()
{
    if (!NeedToSync()) return false;

    if (m_hEvent && HasOverlappedIoCompleted(&m_ol)) {
        // ディレクトリに変更があった
        DWORD xferred;
        if (::GetOverlappedResult(m_hDir, &m_ol, &xferred, FALSE)) {
            if (xferred == 0) {
                m_retryCount = RETRY_LIMIT;
            }
            else {
                TCHAR shortPath[MAX_PATH];
                DWORD rv = ::GetShortPathName(m_path, shortPath, _countof(shortPath));
                if (!rv || rv >= _countof(shortPath)) {
                    shortPath[0] = 0;
                }
                LPCTSTR longName = ::PathFindFileName(m_path);
                LPCTSTR shortName = ::PathFindFileName(shortPath);
                for (BYTE *pBuf = m_buf;;) {
                    FILE_NOTIFY_INFORMATION *pInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(pBuf);
                    TCHAR tmpName[MAX_PATH];
                    ::lstrcpyn(tmpName, pInfo->FileName,
                               min(_countof(tmpName), pInfo->FileNameLength / sizeof(WCHAR) + 1));
                    if (longName[0] && !::lstrcmpi(tmpName, longName) ||
                        shortName[0] && !::lstrcmpi(tmpName, shortName))
                    {
                        m_retryCount = RETRY_LIMIT;
                        break;
                    }
                    if (!pInfo->NextEntryOffset) break;
                    pBuf += pInfo->NextEntryOffset;
                }
            }
        }
        else {
            ASSERT(false);
        }
        ::CloseHandle(m_hEvent);
        m_hEvent = nullptr;
    }

    if (!m_hEvent) {
        // ディレクトリの監視をはじめる
        m_hEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (m_hEvent) {
            memset(&m_ol, 0, sizeof(m_ol));
            m_ol.hEvent = m_hEvent;
            if (::ReadDirectoryChangesW(m_hDir, &m_buf, sizeof(m_buf), FALSE,
                                        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                        nullptr, &m_ol, nullptr) == 0)
            {
                ::CloseHandle(m_hEvent);
                m_hEvent = nullptr;
            }
        }
    }

    // 書き込み中かもしれないので必要ならリトライする
    if (m_retryCount > 0) {
        if (!::PathFileExists(m_path)) {
            m_map.clear();
            m_retryCount = 0;
            return true;
        }
        std::vector<WCHAR> cmd = ReadUtfFileToEnd(m_path, FILE_SHARE_READ);
        if (!cmd.empty()) {
            InsertCommand(&cmd.front());
            m_retryCount = 0;
            return true;
        }
        --m_retryCount;
    }
    return false;
}

bool CChapterMap::Insert(const std::pair<int, CHAPTER> &ch, int pos)
{
    if (IsOpen() && 0 <= ch.first && ch.first <= CHAPTER_POS_MAX &&
        (ch.first == pos || m_map.count(ch.first) == 0) &&
        (pos < 0 || m_map.count(pos) != 0))
    {
        m_map.erase(pos);
        m_map.insert(ch);
        Save();
        return true;
    }
    return false;
}

bool CChapterMap::Erase(int pos)
{
    if (m_map.erase(pos) != 0) {
        Save();
        return true;
    }
    return false;
}

void CChapterMap::ShiftAll(int offset)
{
    std::map<int, CHAPTER>::const_iterator it;
    if (offset < 0) {
        for (it = m_map.begin(); it != m_map.end(); ++it) {
            std::pair<int, CHAPTER> ch = *it;
            if (ch.first < CHAPTER_POS_MAX) ch.first = max(ch.first + offset, 0);
            it = m_map.insert(m_map.erase(it), ch);
        }
    }
    else {
        for (it = m_map.end(); it != m_map.begin(); ) {
            std::pair<int, CHAPTER> ch = *(--it);
            if (ch.first > 0) ch.first = min(ch.first + offset, CHAPTER_POS_MAX);
            it = m_map.insert(m_map.erase(it), ch);
        }
    }
    Save();
}

bool CChapterMap::Save() const
{
    if (!IsOpen() || !m_fWritable) return false;

    if (m_map.empty() && ::PathFileExists(m_path)) {
        return ::DeleteFile(m_path) != 0;
    }

    // 全チャプターが100msecの倍数なら短い形式にする
    bool fShortStyle = true;
    for (std::map<int, CHAPTER>::const_iterator it = m_map.begin(); it != m_map.end(); ++it) {
        if (it->first < CHAPTER_POS_MAX && it->first % 100 != 0) {
            fShortStyle = false;
            break;
        }
    }

    std::vector<TCHAR> cmd;
    cmd.push_back(TEXT('c'));
    cmd.push_back(TEXT('-'));
    for (std::map<int, CHAPTER>::const_iterator it = m_map.begin(); it != m_map.end(); ++it) {
        TCHAR str[16];
        cmd.insert(cmd.end(), str, str + ::wsprintf(str, TEXT("%d"), it->first >= CHAPTER_POS_MAX ? 0 : fShortStyle ? it->first / 100 : it->first));
        cmd.push_back(it->first >= CHAPTER_POS_MAX ? TEXT('e') : fShortStyle ? TEXT('d') : TEXT('c'));
        cmd.insert(cmd.end(), it->second.name.begin(), it->second.name.end() - 1);
        // ハイフンは使用できないので全角マイナスに置換
        std::replace(cmd.end() - (it->second.name.size() - 1), cmd.end(), TEXT('-'), TEXT('－'));
        cmd.push_back(TEXT('-'));
    }
    cmd.push_back(TEXT('c'));
    cmd.push_back(TEXT('\0'));

    return WriteUtfFileToEnd(m_path, 0, &cmd.front());
}

bool CChapterMap::InsertCommand(LPCTSTR p)
{
    // [チャプターコマンド仕様]
    // ・Caseはできるだけ保存するが区別しない
    // ・"c-"で始めて"c"で終わる
    // ・チャプターごとに"{正整数}{接頭英文字}{文字列}-"を追加する
    //   ・{接頭英文字}が"c" "d" "e"以外のとき、そのチャプターを無視する
    //     ・"c"なら{正整数}の単位はmsec
    //     ・"d"なら{正整数}の単位は100msec
    //     ・"e"なら{正整数}はCHAPTER_POS_MAX(動画の末尾)
    // ・仕様を満たさないコマンドは(できるだけ)全体を無視する
    // ・例1: "c-c" (仕様を満たす最小コマンド)
    // ・例2: "c-1234cName1-3456c-2345c2ndName-0e-c"
    m_map.clear();
    if (!::StrCmpNI(p, TEXT("c-"), 2)) {
        p += 2;
        std::pair<int, CHAPTER> ch;
        for (;;) {
            if (!::ChrCmpI(*p, TEXT('c'))) return true;
            if (!::StrToIntEx(p, STIF_DEFAULT, &ch.first)) break;
            while (TEXT('0') <= *p && *p <= TEXT('9')) ++p;

            LPCTSTR q = ::StrChr(p, TEXT('-'));
            if (!q || q==p) break;

            TCHAR c = (TCHAR)::CharLower((LPTSTR)(*p));
            if (c==TEXT('c') || c==TEXT('d') || c==TEXT('e')) {
                if (c==TEXT('e')) ch.first = CHAPTER_POS_MAX;
                else if (c==TEXT('d')) ch.first *= 100;
                if (ch.first >= 0) {
                    ch.first = min(ch.first, CHAPTER_POS_MAX);
                    ch.second.name.assign(p+1, q);
                    ch.second.name.push_back(TEXT('\0'));
                    m_map.insert(ch);
                }
            }
            p = q+1;
        }
    }
    m_map.clear();
    return false;
}

bool CChapterMap::InsertOgmStyleCommand(LPCTSTR p)
{
    // [OGM(dvdxchap)スタイルチャプター]
    // 例:
    // CHAPTER01=HH:MM:SS.sss
    // CHAPTER01NAME=the first chapter
    // CHAPTER02=HH:MM:SS.sss
    // CHAPTER02NAME=another chapter
    m_map.clear();
    TCHAR idStr[32] = {};
    std::pair<int, CHAPTER> ch;
    std::vector<TCHAR> line;
    while (*p) {
        // 1行取得してpを進める
        int len = ::StrCSpn(p, TEXT("\r\n"));
        line.assign(p, p + len);
        line.push_back(TEXT('\0'));
        p += len;
        if (*p == TEXT('\r') && *(p+1) == TEXT('\n')) ++p;
        if (*p) ++p;
        // 左右の空白文字を取り除く
        ::StrTrim(&line.front(), TEXT(" \t"));
        line.resize(::lstrlen(&line.front()) + 1);

        if (!::StrCmpNI(&line.front(), TEXT("CHAPTER"), 7)) {
            line.erase(line.begin(), line.begin() + 7);
            if (idStr[0] && !::StrCmpNI(&line.front(), idStr, ::lstrlen(idStr))) {
                // "CHAPTER[0-9]*NAME="
                ch.second.name.assign(line.begin() + ::lstrlen(idStr), line.end());
                m_map.insert(ch);
                idStr[0] = 0;
            }
            else {
                // 例えば"CHAPTER[0-9]*COMMENT="などは無視する
                LPCTSTR q = &line.front();
                while (TEXT('0') <= *q && *q <= TEXT('9')) ++q;
                if (*q == TEXT('=')) {
                    idStr[0] = 0;
                    // "CHAPTER[0-9]*=HH:MM:SS.sss"
                    if (::lstrlen(q) >= 13 && q[3]==TEXT(':') && q[6]==TEXT(':') && q[9]==TEXT('.')) {
                        ch.first = ::StrToInt(q+1)*3600000 + ::StrToInt(q+4)*60000 + ::StrToInt(q+7)*1000 + ::StrToInt(q+10);
                        if (ch.first >= 0) {
                            ch.first = min(ch.first, CHAPTER_POS_MAX);
                            ::lstrcpyn(idStr, &line.front(), min(static_cast<int>(q-&line.front()+1), _countof(idStr)-5));
                            ::lstrcat(idStr, TEXT("NAME="));
                        }
                    }
                }
            }
        }
        else if (line[0]) {
            // 空行以外は認めない
            m_map.clear();
            return false;
        }
    }
    return true;
}
