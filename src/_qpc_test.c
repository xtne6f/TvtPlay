// 各計時APIの長期的な精度比較コード(2011-12-14)
#include <Windows.h>
#include <stdio.h>
#pragma comment(lib, "winmm.lib")

void PrintFileTime(FILETIME *pTime, LONGLONG offset)
{
    ULARGE_INTEGER li;
    FILETIME ft;
    SYSTEMTIME st;

    li.LowPart = pTime->dwLowDateTime;
    li.HighPart = pTime->dwHighDateTime;
    li.QuadPart += offset;
    ft.dwLowDateTime = li.LowPart;
    ft.dwHighDateTime = li.HighPart;

    FileTimeToSystemTime(&ft, &st);
    printf("%02d:%02d:%02d.%03d", (int)st.wHour, (int)st.wMinute, (int)st.wSecond, (int)st.wMilliseconds);
}

int main(int argc, char *argv[])
{
    SYSTEMTIME sysTime;
    FILETIME time;
    LARGE_INTEGER liFreq, liStart, liNow;
    DWORD tickStart, tgtStart;
    LONGLONG qpcDiff, tickDiff, tgtDiff;

    GetLocalTime(&sysTime);
    SystemTimeToFileTime(&sysTime, &time);

    if (argc>=2 && argv[1][0]=='-' && argv[1][1]=='a' &&
        SetThreadAffinityMask(GetCurrentThread(), 1))
    {
        printf("SetThreadAffinityMask Done.\n");
    }
    if (!QueryPerformanceFrequency(&liFreq) ||
        !QueryPerformanceCounter(&liStart))
    {
        printf("Error: Initialize QueryPerformanceCounter\n");
        return 1;
    }
    tickStart = GetTickCount();
    tgtStart = timeGetTime();

    printf("[QueryPerformanceCounter], [GetTickCount], [timeGetTime]\n");
    for (;;) {
        if (!QueryPerformanceCounter(&liNow)) return 2;
        qpcDiff = (liNow.QuadPart - liStart.QuadPart) * 10000000 / liFreq.QuadPart;
        tickDiff = (LONGLONG)(GetTickCount() - tickStart) * 10000;
        tgtDiff = (LONGLONG)(timeGetTime() - tgtStart) * 10000;

        PrintFileTime(&time, qpcDiff);
        printf(", ");
        PrintFileTime(&time, tickDiff);
        printf("(%5dmsec), ", (int)((tickDiff - qpcDiff) / 10000));
        PrintFileTime(&time, tgtDiff);
        printf("(%5dmsec)", (int)((tgtDiff - qpcDiff) / 10000));
        Sleep(1000);
        printf("     \r");
    }
    return 0;
}
