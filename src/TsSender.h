#ifndef INCLUDE_TS_SENDER_H
#define INCLUDE_TS_SENDER_H

#include <Windows.h>

class CTsSender {
    static const int BUFFER_SIZE = 32768;
    static const int PCR_PER_MSEC = 45;
    static const int TS_SUPPOSED_RATE = 2 * 1024 * 1024;
public:
    CTsSender();
    ~CTsSender();
    bool Open(LPCTSTR name, LPCSTR addr, unsigned short port);
    void SetAddress(LPCSTR addr, unsigned short port);
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
private:
    bool ReadPacket();
    void ConsumeBuffer(bool fSend);
    bool Seek(long long distanceToMove, DWORD dwMoveMethod);

    HANDLE m_hFile;
    BYTE *m_pBuf, *m_curr, *m_tail;
    int m_unitSize;
    SOCKET m_sock;
    struct sockaddr_in m_addr;
    DWORD m_tick, m_baseTick, m_renewSizeTick;
    DWORD m_pcrCount;
    DWORD m_pcr, m_basePcr, m_initPcr, m_pcrPool[3];
    bool m_fPcr, m_fFixed, m_fPause;
    long long m_fileSize;
    int m_duration;
};

#endif // INCLUDE_TS_SENDER_H
