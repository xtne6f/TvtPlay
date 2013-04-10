#include <Windows.h>
#include <Shlwapi.h>
#include <map>
#include "Util.h"
#include "ChapterMap.h"

#ifndef __AFX_H__
#include <cassert>
#define ASSERT assert
#endif

void CHAPTER_NAME::InvertPrefix(TCHAR c)
{
    ASSERT(c);
    TCHAR tmp[_countof(val)];
    if (!::ChrCmpI(val[0], c)) {
        ::lstrcpy(tmp, val + 1);
        ::lstrcpy(val, tmp);
    }
    else {
        ::lstrcpy(tmp, val);
        val[0] = c;
        ::lstrcpyn(val + 1, tmp, _countof(val) - 1);
    }
}

CChapterMap::CChapterMap()
    : m_hDir(INVALID_HANDLE_VALUE)
    , m_hEvent(NULL)
    , m_fWritable(false)
    , m_fWaiting(false)
    , m_retryCount(0)
{
    m_path[0] = m_longName[0] = m_shortName[0] = 0;
}

CChapterMap::~CChapterMap()
{
    Close();
}

// pathに対応する.chapterファイルを読み込む
// .chapterファイルが存在すれば変更監視を開始する
bool CChapterMap::Open(LPCTSTR path)
{
    Close();

    // カレントからの絶対パスに変換
    TCHAR fullPath[MAX_PATH];
    DWORD rv = ::GetFullPathName(path, _countof(fullPath), fullPath, NULL);
    if (!rv || rv >= _countof(fullPath)) return false;

    // 長い名前に変換
    TCHAR tmpPath[MAX_PATH];
    rv = ::GetLongPathName(fullPath, tmpPath, _countof(tmpPath));
    if (!rv || rv >= _countof(tmpPath) - 12/* 拡張子付加の余裕分 */) return false;

    // 動画ファイルの存在確認
    if (!::PathFileExists(tmpPath)) return false;

    // .chapterファイルのパスを生成
    ::PathRemoveExtension(tmpPath);
    if (!::lstrcpyn(m_path, tmpPath, _countof(m_path) - 8) ||
        !::lstrcat(m_path, TEXT(".chapter"))/* 念のため戻値確認 */)
    {
        m_path[0] = 0;
        return false;
    }
    // OGMスタイルチャプターのパスを生成
    TCHAR ogmStylePath[MAX_PATH];
    ::lstrcpyn(ogmStylePath, tmpPath, _countof(ogmStylePath) - 12);
    ::lstrcat(ogmStylePath, TEXT(".chapter.txt"));

    // .chapterファイルが存在するときは、中身がチャプターコマンド
    // 仕様に従っている場合のみ書き込み可能(m_fWritable)
    if (::PathFileExists(m_path)) {
        // .chapterファイルからロード
        for (int i = 0; i < RETRY_LIMIT; ++i) {
            TCHAR *pCmd = NewReadUtfFileToEnd(m_path, FILE_SHARE_READ);
            if (pCmd) {
                m_fWritable = InsertCommand(pCmd);
                delete [] pCmd;
                break;
            }
            ::Sleep(200);
        }

        // ReadDirectoryChangesW()のために長短ファイル名を用意
        rv = ::GetShortPathName(m_path, tmpPath, _countof(tmpPath));
        if (rv && rv < _countof(tmpPath)) {
            ::lstrcpy(m_shortName, ::PathFindFileName(tmpPath));
        }
        ::lstrcpy(m_longName, ::PathFindFileName(m_path));

        // 変更監視のためにディレクトリを開く
        m_hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
        ::lstrcpy(tmpPath, m_path);
        if (m_hEvent && ::PathRemoveFileSpec(tmpPath)) {
            m_hDir = ::CreateFile(tmpPath, FILE_LIST_DIRECTORY,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
            Sync();
        }
    }
    else if (::PathFileExists(ogmStylePath)) {
        m_fWritable = true;
        // BOMがなければANSIコードページで読む
        TCHAR *pCmd = NewReadUtfFileToEnd(ogmStylePath, FILE_SHARE_READ, true);
        if (pCmd) {
            InsertOgmStyleCommand(pCmd);
            delete [] pCmd;
        }
    }
    else {
        m_fWritable = true;
        // もしあればファイル名からロード
        TCHAR *pCmd = ::StrStrI(::PathFindFileName(m_path), TEXT("c-"));
        if (pCmd) InsertCommand(pCmd);
    }
    return true;
}

void CChapterMap::Close()
{
    clear();
    if (NeedToSync()) {
        ::CloseHandle(m_hDir);
        m_hDir = INVALID_HANDLE_VALUE;
    }
    if (m_hEvent) {
        ::CloseHandle(m_hEvent);
        m_hEvent = NULL;
    }
    m_fWritable = m_fWaiting = false;
    m_retryCount = 0;
    m_path[0] = m_longName[0] = m_shortName[0] = 0;
}

// .chapterファイルの変更を監視し、変更があれば読み込む
// マップに変更があればtrueを返す
// TODO: Save()後に読み込みが発生してしまう
bool CChapterMap::Sync()
{
    if (!NeedToSync()) return false;

    if (m_fWaiting && HasOverlappedIoCompleted(&m_ol)) {
        // ディレクトリに変更があった
        DWORD xferred;
        if (::GetOverlappedResult(m_hDir, &m_ol, &xferred, FALSE)) {
            if (xferred == 0) {
                m_retryCount = RETRY_LIMIT;
            }
            else {
                for (BYTE *pBuf = m_buf;;) {
                    FILE_NOTIFY_INFORMATION *pInfo = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(pBuf);
                    TCHAR tmpName[MAX_PATH];
                    ::lstrcpyn(tmpName, pInfo->FileName,
                               min(_countof(tmpName), pInfo->FileNameLength / sizeof(WCHAR) + 1));
                    if (m_longName[0] && !::lstrcmpi(tmpName, m_longName) ||
                        m_shortName[0] && !::lstrcmpi(tmpName, m_shortName))
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
        m_fWaiting = false;
    }

    if (!m_fWaiting) {
        // ディレクトリの監視をはじめる
        memset(&m_ol, 0, sizeof(m_ol));
        m_ol.hEvent = m_hEvent;
        m_fWaiting = ::ReadDirectoryChangesW(m_hDir, &m_buf, sizeof(m_buf), FALSE,
                                             FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                             NULL, &m_ol, NULL) != 0;
        ASSERT(m_fWaiting);
    }

    // 書き込み中かもしれないので必要ならリトライする
    if (m_retryCount > 0) {
        if (!::PathFileExists(m_path)) {
            clear();
            m_retryCount = 0;
            return true;
        }
        TCHAR *pCmd = NewReadUtfFileToEnd(m_path, FILE_SHARE_READ);
        if (pCmd) {
            InsertCommand(pCmd);
            delete [] pCmd;
            m_retryCount = 0;
            return true;
        }
        --m_retryCount;
    }
    return false;
}

bool CChapterMap::Save() const
{
    if (!IsOpen() || !m_fWritable) return false;

    if (empty() && ::PathFileExists(m_path)) {
        return ::DeleteFile(m_path) != 0;
    }

    // 全チャプターが100msecの倍数なら短い形式にする
    bool fShortStyle = true;
    for (const_iterator it = begin(); it != end(); ++it) {
        if (it->first < CHAPTER_POS_MAX && it->first % 100 != 0) {
            fShortStyle = false;
            break;
        }
    }

    // 作業領域を確保(ワーストケース)
    TCHAR *pCmd = new TCHAR[size() * (13 + CHAPTER_NAME_MAX) + 4];
    TCHAR *p = pCmd;

    ::lstrcpy(p, TEXT("c-"));
    p += 2;
    for (const_iterator it = begin(); it != end(); ++it) {
        TCHAR rpl[CHAPTER_NAME_MAX];
        ::lstrcpy(rpl, it->second.val);
        // ハイフンは使用できないので全角マイナスに置換
        for (TCHAR *q = rpl; *q; ++q) if (*q == TEXT('-')) *q = TEXT('－');
        ASSERT(it->first >= 0);
        p += ::wsprintf(p, TEXT("%d%c%s-"),
                        it->first>=CHAPTER_POS_MAX ? 0 : fShortStyle ? max(it->first/100,0) : max(it->first,0),
                        it->first>=CHAPTER_POS_MAX ? TEXT('e') : fShortStyle ? TEXT('d') : TEXT('c'), rpl);
    }
    ::lstrcat(p, TEXT("c"));

    bool rv = WriteUtfFileToEnd(m_path, 0, pCmd);
    delete [] pCmd;
    return rv;
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
    //   ・{文字列}は0～CHAPTER_NAME_MAX-1文字
    // ・仕様を満たさないコマンドは(できるだけ)全体を無視する
    // ・例1: "c-c" (仕様を満たす最小コマンド)
    // ・例2: "c-1234cName1-3456c-2345c2ndName-0e-c"
    clear();
    if (!::StrCmpNI(p, TEXT("c-"), 2)) {
        p += 2;
        for (;;) {
            if (!::ChrCmpI(*p, TEXT('c'))) return true;
            CHAPTER ch;
            if (!::StrToIntEx(p, STIF_DEFAULT, &ch.first)) break;
            while (TEXT('0') <= *p && *p <= TEXT('9')) ++p;

            LPCTSTR q = ::StrChr(p, TEXT('-'));
            if (!q || q==p || q-p > CHAPTER_NAME_MAX) break;

            TCHAR c = (TCHAR)::CharLower((LPTSTR)(*p));
            if (c==TEXT('c') || c==TEXT('d') || c==TEXT('e')) {
                if (c==TEXT('e')) ch.first = CHAPTER_POS_MAX;
                else if (c==TEXT('d')) ch.first *= 100;
                if (ch.first >= 0) {
                    ch.first = min(ch.first, CHAPTER_POS_MAX);
                    ::lstrcpyn(ch.second.val, p+1, static_cast<int>(q-p));
                    insert(ch);
                }
            }
            p = q+1;
        }
    }
    clear();
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
    clear();
    TCHAR idStr[32] = {0};
    CHAPTER ch;
    while (*p) {
        // 1行取得してpを進める
        TCHAR line[CHAPTER_NAME_MAX + 64];
        int len = ::StrCSpn(p, TEXT("\r\n"));
        ::lstrcpyn(line, p, min(len+1, _countof(line)));
        p += len;
        if (*p == TEXT('\r') && *(p+1) == TEXT('\n')) ++p;
        if (*p) ++p;
        // 左右の空白文字を取り除く
        ::StrTrim(line, TEXT(" \t"));

        if (!::StrCmpNI(line, TEXT("CHAPTER"), 7)) {
            if (idStr[0] && !::StrCmpNI(line+7, idStr, ::lstrlen(idStr))) {
                // "CHAPTER[0-9]*NAME="
                ::lstrcpyn(ch.second.val, line+7 + ::lstrlen(idStr), CHAPTER_NAME_MAX);
                insert(ch);
                idStr[0] = 0;
            }
            else {
                // 例えば"CHAPTER[0-9]*COMMENT="などは無視する
                LPCTSTR q = line+7;
                while (TEXT('0') <= *q && *q <= TEXT('9')) ++q;
                if (*q == TEXT('=')) {
                    idStr[0] = 0;
                    // "CHAPTER[0-9]*=HH:MM:SS.sss"
                    if (::lstrlen(q) >= 13 && q[3]==TEXT(':') && q[6]==TEXT(':') && q[9]==TEXT('.')) {
                        ch.first = ::StrToInt(q+1)*3600000 + ::StrToInt(q+4)*60000 + ::StrToInt(q+7)*1000 + ::StrToInt(q+10);
                        if (ch.first >= 0) {
                            ch.first = min(ch.first, CHAPTER_POS_MAX);
                            ::lstrcpyn(idStr, line+7, min(static_cast<int>(q-(line+7)+1), _countof(idStr)-5));
                            ::lstrcat(idStr, TEXT("NAME="));
                        }
                    }
                }
            }
        }
        else if (line[0]) {
            // 空行以外は認めない
            clear();
            return false;
        }
    }
    return true;
}
