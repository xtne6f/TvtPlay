#ifndef INCLUDE_TS_SENDER_H
#define INCLUDE_TS_SENDER_H

#include <Windows.h>

class CTsSender {
    static const int MAX_URI = 256;
    static const int BUFFER_LEN = 192;
    static const int PCR_PER_MSEC = 45;
    static const int BASE_DIFF_MSEC = 300;
    static const int TS_SUPPOSED_RATE = 2 * 1024 * 1024;
    static const int PCR_PIDS_MAX = 8;
public:
    CTsSender();
    ~CTsSender();
    bool Open(LPCTSTR name, DWORD salt);
    void SetUdpAddress(LPCSTR addr, unsigned short port);
    void SetPipeName(LPCTSTR name);
    void Close();
    bool Send();
    bool SeekToBegin();
    bool SeekToEnd();
    bool Seek(int msec);
    void Pause(bool fPause);
    bool IsPaused() const;
    bool IsFixed() const;
    long long GetFileSize() const;
    long long GetFilePosition() const;
    int GetDuration() const;
    int GetPosition() const;
    int GetRate() const;
    int GetBroadcastTime() const;
    long long GetFileHash() const;
private:
    bool ReadPacket();
    void ConsumeBuffer(bool fSend);
    bool Seek(long long distanceToMove, DWORD dwMoveMethod);
    void OpenSocket();
    void CloseSocket();
    void OpenPipe();
    void ClosePipe();

    HANDLE m_hFile;
    BYTE *m_pBuf, *m_curr, *m_tail;
    int m_unitSize;
    SOCKET m_sock;
    CHAR m_udpAddr[MAX_URI];
    unsigned short m_udpPort;
    HANDLE m_hPipe;
    TCHAR m_pipeName[MAX_PATH];
    DWORD m_baseTick, m_renewSizeTick;
    DWORD m_pcrCount;
    DWORD m_pcr, m_basePcr, m_initPcr;
    bool m_fPcr, m_fFixed, m_fPause;
    int m_pcrPid, m_pcrPids[PCR_PIDS_MAX];
    int m_pcrPidCounts[PCR_PIDS_MAX];
    int m_pcrPidsLen;
    long long m_fileSize;
    int m_duration;
    int m_totBase;
    DWORD m_totBasePcr;
    long long m_hash;
};

#endif // INCLUDE_TS_SENDER_H
