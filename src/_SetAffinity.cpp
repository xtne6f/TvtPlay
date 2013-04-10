// プロセスアフィニティマスクを指定してアプリケーションを起動する
// 最終更新: 2012-09-23
#include <Windows.h>
#include <Shlwapi.h>
#include <tchar.h>

#ifdef NO_CRT
void wWinMainCRTStartup()
{
    _tWinMain(NULL, NULL, NULL, 0);
    ::ExitProcess(0);
}
#endif

int APIENTRY _tWinMain(HINSTANCE hInstance_, HINSTANCE hPrevInstance_, LPTSTR lpCmdLine_, int nCmdShow_)
{
    // iniファイルのパスを取得
    TCHAR modulePath[MAX_PATH], path[MAX_PATH];
    if (!::GetModuleFileName(::GetModuleHandle(NULL), modulePath, _countof(modulePath)) ||
        !::GetLongPathName(modulePath, path, _countof(path)) ||
        !::PathRenameExtension(path, TEXT(".ini"))) return 0;

    // デフォルトiniファイルを作成
    if (::GetPrivateProfileInt(TEXT("Settings"), TEXT("AffinityMask"), 0xFFFFFFFF, path) == 0xFFFFFFFF) {
        ::WritePrivateProfileString(TEXT("Settings"), TEXT("AppName"), TEXT("TVTest.exe"), path);
        ::WritePrivateProfileString(TEXT("Settings"), TEXT("AffinityMask"), TEXT("1"), path);
        ::MessageBox(NULL, TEXT("設定キーAppName\n")
                           TEXT("    起動するアプリケーションを絶対パスか\n")
                           TEXT("    iniファイルのあるフォルダからの相対パスで指定\n")
                           TEXT("    ※無限ループになるのでSetAffinity.exe自身を指定してはいけない\n")
                           TEXT("    　嵌ったときはiniファイルを削除して抜けだす\n")
                           TEXT("設定キーAffinityMask\n")
                           TEXT("    アフィニティマスクを指定\n")
                           TEXT("    例えばビット0,1を立てる[=3]とCPU0,CPU1が使用される"),
                           TEXT("iniファイルを作成しました。適宜編集してください"), MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // iniファイルを読み込み
    TCHAR appName[MAX_PATH];
    ::GetPrivateProfileString(TEXT("Settings"), TEXT("AppName"), TEXT(""), appName, _countof(appName), path);
    UINT affinityMask = ::GetPrivateProfileInt(TEXT("Settings"), TEXT("AffinityMask"), 0, path);

    // コマンド整形
    if (::PathIsRelative(appName)) {
        if (!::PathRemoveFileSpec(path) || !::PathAppend(path, appName)) return 0;
    }
    else {
        ::lstrcpy(path, appName);
    }
    LPTSTR pCmdLine = ::GetCommandLine();
    if (*pCmdLine == TEXT('"')) {
        for (++pCmdLine; *pCmdLine && *pCmdLine != TEXT('"'); ++pCmdLine);
        if (*pCmdLine == TEXT('"')) ++pCmdLine;
    }
    else {
        for (; *pCmdLine && *pCmdLine != TEXT(' '); ++pCmdLine);
    }
    if (*pCmdLine && *pCmdLine != TEXT(' ')) {
        ::MessageBox(NULL, TEXT("Error: Invalid command line"), NULL, MB_OK | MB_ICONERROR);
        return 0;
    }

    // 起動
    STARTUPINFO si;
    PROCESS_INFORMATION ps;
    si.dwFlags = 0;
    ::GetStartupInfo(&si);
    if (!::CreateProcess(path, pCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &ps)) {
        TCHAR text[1024];
        ::wsprintf(text, TEXT("Error: CreateProcess = 0x%08x\n%s\n%.512s"), ::GetLastError(), path, pCmdLine);
        ::MessageBox(NULL, text, NULL, MB_OK | MB_ICONERROR);
        return 0;
    }

    // アフィニティマスクを指定
    DWORD_PTR maskProcess, maskSystem;
    if (!::GetProcessAffinityMask(ps.hProcess, &maskProcess, &maskSystem)) return 0;
    maskProcess &= affinityMask;
    if (!::SetProcessAffinityMask(ps.hProcess, maskProcess)) {
        TCHAR text[128];
        ::wsprintf(text, TEXT("Error: SetProcessAffinityMask = 0x%08x"), ::GetLastError());
        ::MessageBox(NULL, text, NULL, MB_OK | MB_ICONERROR);
        return 0;
    }

    return 0;
}
