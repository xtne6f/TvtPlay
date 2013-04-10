// .chapterファイルへのチャプター書き込みコード(2011-12-30)
// 商用私用問わず改変流用自由
// [追加説明]
// TvtPlay(ver.1.4以降)がTSファイルを開くとき、そのファイル名に.chapter拡張子を
// 付加したファイルが存在すれば、TvtPlayはその変更を監視します。後述の"チャプタ
// ーコマンド仕様"に従って.chapterファイルを作成・編集することで、リアルタイムに
// チャプターを付加するといった、外部アプリケーションとの連携が可能です。
#include <Windows.h>
#include <Shlwapi.h>
#include <tchar.h>
#include <locale.h>
#include <stdio.h>
#pragma comment(lib, "shlwapi.lib")

#define CHAPTER_NAME_MAX 16
#define READ_FILE_MAX_SIZE (256 * 1024)

WCHAR *NewReadUtfFileToEnd(HANDLE hFile, int extraSpace);
bool WriteUtfFileToEnd(HANDLE hFile, const WCHAR *pStr);

int _tmain(int argc, TCHAR *argv[])
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    WCHAR *pCmd = NULL;
    TCHAR chName[CHAPTER_NAME_MAX] = {0};
    int pos = 0, rv = 1;

    _tsetlocale(LC_ALL, TEXT(""));
    if (argc < 3) {
        _tprintf(TEXT("Usage: %s <ファイル名> <チャプター位置(msec)> [チャプター名]\n"), argv[0]);
        goto EXIT;
    }
    if (lstrcmpi(PathFindExtension(argv[1]), TEXT(".chapter"))) {
        _tprintf(TEXT("Error: 拡張子は.chapterである必要があります\n"));
        goto EXIT;
    }
    if ((pos = StrToInt(argv[2])) < 0) {
        _tprintf(TEXT("Error: チャプター位置は負の整数を指定できません\n"));
        goto EXIT;
    }

    if (argc >= 4) {
        if (lstrlen(argv[3]) >= CHAPTER_NAME_MAX) {
            _tprintf(TEXT("Error: チャプター名は%d文字までです\n"), CHAPTER_NAME_MAX - 1);
            goto EXIT;
        }
        if (StrChr(argv[3], TEXT('-'))) {
            _tprintf(TEXT("Error: チャプター名に-を含めることはできません\n"));
            goto EXIT;
        }
        lstrcpy(chName, argv[3]);
    }

    // [チャプターコマンド仕様]
    // ・ファイルの文字コードはBOM付きUTF-8であること
    // ・Caseはできるだけ保存するが区別しない
    // ・"c-"で始めて"c"で終わる
    // ・チャプターごとに"{正整数}{接頭英文字}{文字列}-"を追加する
    //   ・{接頭英文字}が"c"以外のとき、そのチャプターを無視する
    //   ・{文字列}は0～CHAPTER_NAME_MAX-1文字
    // ・仕様を満たさないコマンドは(できるだけ)全体を無視する
    // ・例1: "c-c" (仕様を満たす最小コマンド)
    // ・例2: "c-1234cName1-2345c-3456c2ndName-c"
    if (PathFileExists(argv[1])) {
        // ファイルが存在すれば追記
        // 不整合を防ぐためにSHARE_READ、SHARE_WRITEは指定しない
        hFile = CreateFile(argv[1], GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            _tprintf(TEXT("Error: ファイルをオープンできません\n"));
            goto EXIT;
        }
        WCHAR *pCmd = NewReadUtfFileToEnd(hFile, 64);
        // 先頭2文字は"c-"、末尾2文字は"-c"でなければ異常とみなす
        if (!pCmd || StrCmpNI(pCmd, L"c-", 2) || StrCmpI(pCmd + lstrlen(pCmd) - 2, L"-c")) {
            _tprintf(TEXT("Error: 書き込むファイルの中身が異常です\n"));
            goto EXIT;
        }
        // 末尾にチャプターを追記
        wsprintf(pCmd + lstrlen(pCmd) - 1, L"%dc%s-c", pos, chName);
        if (!WriteUtfFileToEnd(hFile, pCmd)) {
            _tprintf(TEXT("Error: 書き込みエラー\n"));
            goto EXIT;
        }
    }
    else {
        // ファイルが存在しなければ作成
        hFile = CreateFile(argv[1], GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            _tprintf(TEXT("Error: ファイルをオープンできません\n"));
            goto EXIT;
        }
        WCHAR cmd[64];
        wsprintf(cmd, L"c-%dc%s-c", pos, chName);
        if (!WriteUtfFileToEnd(hFile, cmd)) {
            _tprintf(TEXT("Error: 書き込みエラー\n"));
            goto EXIT;
        }
    }
    _tprintf(TEXT("%s に書き込みました: チャプター位置=%dmsec,チャプター名=%s\n"), argv[1], pos, chName);

    rv = 0;
EXIT:
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    delete [] pCmd;
    return rv;
}


// BOM付きUTF-8テキストファイルを文字列として全て読む
// 成功するとnewされた配列のポインタが返るので、必ずdeleteすること
WCHAR *NewReadUtfFileToEnd(HANDLE hFile, int extraSpace)
{
    BYTE *pBuf = NULL;
    WCHAR *pRet = NULL;

    // 読み込み位置をファイル先頭に移動
    if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) goto EXIT;

    DWORD fileBytes = GetFileSize(hFile, NULL);
    if (fileBytes == 0xFFFFFFFF || fileBytes >= READ_FILE_MAX_SIZE) goto EXIT;

    pBuf = new BYTE[fileBytes + 1];
    DWORD readBytes;
    if (!ReadFile(hFile, pBuf, fileBytes, &readBytes, NULL)) goto EXIT;
    pBuf[readBytes] = 0;

    // BOM付きUTF-8のみ対応
    if (pBuf[0]!=0xEF || pBuf[1]!=0xBB || pBuf[2]!=0xBF) goto EXIT;

    // 出力サイズ算出
    int retSize = MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(pBuf + 3), -1, NULL, 0);
    if (retSize <= 0) goto EXIT;

    // 文字コード変換
    pRet = new WCHAR[retSize + extraSpace];
    if (!MultiByteToWideChar(CP_UTF8, 0, (LPCSTR)(pBuf + 3), -1, pRet, retSize)) {
        delete [] pRet;
        pRet = NULL;
        goto EXIT;
    }

EXIT:
    delete [] pBuf;
    return pRet;
}


// 文字列をBOM付きUTF-8テキストファイルとして書き込む
bool WriteUtfFileToEnd(HANDLE hFile, const WCHAR *pStr)
{
    BYTE *pBuf = NULL;
    bool rv = false;

    // 書き込み位置をファイル先頭に移動
    if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) goto EXIT;

    // 出力サイズ算出
    int bufSize = WideCharToMultiByte(CP_UTF8, 0, pStr, -1, NULL, 0, NULL, NULL);
    if (bufSize <= 0) goto EXIT;

    // 文字コード変換(NULL文字含む)
    pBuf = new BYTE[3 + bufSize];
    pBuf[0] = 0xEF; pBuf[1] = 0xBB; pBuf[2] = 0xBF;
    bufSize = WideCharToMultiByte(CP_UTF8, 0, pStr, -1, (LPSTR)(pBuf + 3), bufSize, NULL, NULL);
    if (bufSize <= 0) goto EXIT;

    DWORD written;
    if (!WriteFile(hFile, pBuf, bufSize + 3 - 1, &written, NULL)) goto EXIT;

    rv = true;
EXIT:
    delete [] pBuf;
    return rv;
}
