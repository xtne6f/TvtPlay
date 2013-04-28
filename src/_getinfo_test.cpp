// 起動中のTvtPlay(ver.2.1以降)から各種情報を取得する(2013-04-28)
// 商用私用問わず改変流用自由
#include <Windows.h>
#include <tchar.h>
#include <locale.h>
#include <stdio.h>

// TvtPlayから他プラグインに情報提供するメッセージ(From: TvtPlay.cpp)
#define TVTP_CURRENT_MSGVER 1
#define WM_TVTP_GET_MSGVER      (WM_APP + 50)
#define WM_TVTP_IS_OPEN         (WM_APP + 51)
#define WM_TVTP_GET_POSITION    (WM_APP + 52)
#define WM_TVTP_GET_DURATION    (WM_APP + 53)
#define WM_TVTP_GET_TOT_TIME    (WM_APP + 54)
#define WM_TVTP_IS_EXTENDING    (WM_APP + 55)
#define WM_TVTP_IS_PAUSED       (WM_APP + 56)
#define WM_TVTP_GET_PLAY_FLAGS  (WM_APP + 57)
#define WM_TVTP_GET_STRETCH     (WM_APP + 58)
#define WM_TVTP_GET_PATH        (WM_APP + 59)
#define WM_TVTP_SEEK            (WM_APP + 60)
#define WM_TVTP_SEEK_ABSOLUTE   (WM_APP + 61)

#if 1 // TvtPlayのウィンドウを探す関数(コピペして使う)
static BOOL CALLBACK FindTvtPlayFrameEnumProc(HWND hwnd, LPARAM lParam)
{
    TCHAR className[32];
    if (GetClassName(hwnd, className, _countof(className)) && !lstrcmp(className, TEXT("TvtPlay Frame"))) {
        *(HWND*)lParam = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND FindTvtPlayFrame()
{
    HWND hwnd = NULL;
    EnumWindows(FindTvtPlayFrameEnumProc, (LPARAM)&hwnd); // Call from another process
    //EnumThreadWindows(GetCurrentThreadId(), FindTvtPlayFrameEnumProc, (LPARAM)&hwnd); // Call from plugin
    return hwnd;
}
#endif

static DWORD MySendMessage(HWND hwnd, UINT msg, WPARAM wParam = 0, LPARAM lParam = 0)
{
    // プラグインから呼ぶ場合はSendMessageでOK
    DWORD_PTR dwpResult = 0;
    if (SendMessageTimeout(hwnd, msg, wParam, lParam, SMTO_NORMAL, 5000, &dwpResult)) {
        return (DWORD)dwpResult;
    }
    return 0;
}

int _tmain(int argc, TCHAR *argv[])
{
    _tsetlocale(LC_ALL, TEXT(""));

    // 複数起動している場合は最初に見つかったものが返る
    HWND hwnd = FindTvtPlayFrame();
    if (!hwnd) {
        _tprintf(TEXT("TvtPlayが見つかりません。\n"));
        return 0;
    }
    // メッセージインタフェースのバージョン
    DWORD msgVer = MySendMessage(hwnd, WM_TVTP_GET_MSGVER);
    if (msgVer < TVTP_CURRENT_MSGVER) {
        _tprintf(TEXT("TvtPlayのバージョンが古いです。\n"));
        return 0;
    }
    // TSファイルを開いているかどうか
    bool fOpen = MySendMessage(hwnd, WM_TVTP_IS_OPEN) != 0;
    // 現在の再生位置(ミリ秒)
    int position = (int)MySendMessage(hwnd, WM_TVTP_GET_POSITION);
    // 総再生時間(ミリ秒)
    int duration = (int)MySendMessage(hwnd, WM_TVTP_GET_DURATION);
    // TSファイル先頭における放送時刻(ミリ秒)。不明のとき負値
    int totTime = (int)MySendMessage(hwnd, WM_TVTP_GET_TOT_TIME);
    // 追っかけ再生中かどうか
    bool fExtending = MySendMessage(hwnd, WM_TVTP_IS_EXTENDING) != 0;
    // 一時停止中かどうか
    bool fPaused = MySendMessage(hwnd, WM_TVTP_IS_PAUSED) != 0;
    // 各種再生フラグ。LSBから順に全体リピート、1ファイルリピート、チャプターリピート、チャプタースキップ
    DWORD dwPlayFlags = MySendMessage(hwnd, WM_TVTP_GET_PLAY_FLAGS);
    DWORD dwStretch = MySendMessage(hwnd, WM_TVTP_GET_STRETCH);
    // 現在の倍速再生ID。0から順にStretch[A-Z]に対応。等速のとき-1
    int stretchID = (signed short)LOWORD(dwStretch);
    // 現在の倍速再生速度(パーセント)
    int stretchSpeed = HIWORD(dwStretch);

    _tprintf(TEXT("MsgVer: %u\nIsOpen: %d\nPos: %dmsec\nDur: %dmsec\nTotTime: %dmsec\nIsExtending: %d\nIsPaused: %d\nPlayFlags: %u%u%u%u\nStretchID: %d\nStretchSpeed: %d\n"),
             msgVer, (int)fOpen, position, duration, totTime, (int)fExtending, (int)fPaused,
             dwPlayFlags>>3&1,dwPlayFlags>>2&1, dwPlayFlags>>1&1, dwPlayFlags&1, stretchID, stretchSpeed);

    // 開いているTSファイルの絶対パス名をワイド文字列で取得する。同一プロセス内専用
    // wParamにバッファの文字数、lParamに文字列バッファ(NULL可)を渡す
    // 格納された文字数(NULを除く)または0(=失敗)が返る
    //WCHAR path[MAX_PATH];
    //if (SendMessage(hwnd, WM_TVTP_GET_PATH, _countof(path), (LPARAM)path) != 0) {
    //    // 取得成功
    //}

    // 相対/絶対シークする
    // wParamに0、lParamにシーク量(ミリ秒)を渡す
    // メッセージが解釈された場合は非0が返る(実際にシークが成功した事を意味しない)
    //MySendMessage(hwnd, WM_TVTP_SEEK, 0, (LPARAM)-10000);
    //MySendMessage(hwnd, WM_TVTP_SEEK_ABSOLUTE, 0, (LPARAM)10000);

    return 0;
}
