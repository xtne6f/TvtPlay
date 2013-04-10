#ifndef INCLUDE_TS_SENDER_H
#define INCLUDE_TS_SENDER_H

class CTsTimestampShifter
{
public:
    CTsTimestampShifter();
    ~CTsTimestampShifter();
    void SetValue(DWORD shift45khz);
    void Reset();
    inline void Transform(BYTE *pPacket);
private:
    DWORD m_shift45khz;
    PAT m_pat;
};

class CTsSender
{
    static const int MAX_URI = 256;
    static const int BUFFER_LEN = 1536;
    static const int BON_UDP_TSDATASIZE = 48128;
    static const int PCR_PER_MSEC = 45;
    static const int TS_SUPPOSED_RATE = 2 * 1024 * 1024;
    static const int PCR_PIDS_MAX = 8;
    static const int RESYNC_FAILURE_LIMIT = 2;
    static const int ADJUST_TICK_INTERVAL = 10000;
    static const int RENEW_SIZE_INTERVAL = 3000;
    static const int INITIAL_STORE_MSEC = 500;
public:
    CTsSender();
    ~CTsSender();
    bool Open(LPCTSTR name, DWORD salt, bool fConvTo188, bool fUseQpc);
    void SetupQpc();
    void SetUdpAddress(LPCSTR addr, unsigned short port);
    void SetPipeName(LPCTSTR name);
    void SetModTimestamp(bool fModTimestamp);
    void Close();
    bool Send();
    void SendEmptyPat();
    bool SeekToBegin();
    bool SeekToEnd();
    bool Seek(int msec);
    void Pause(bool fPause);
    void SetSpeed(int num, int den);
    bool IsOpen() const { return m_hFile != NULL; }
    bool IsPaused() const { return m_fPause; }
    bool IsFixed(bool *pfSpecialExt = NULL) const { if (pfSpecialExt) *pfSpecialExt=m_fSpecialExtending; return m_fFixed; }
    void GetSpeed(int *pNum, int *pDen) const { *pNum=m_speedNum; *pDen=m_speedDen; }
    long long GetFileHash() const { return m_hash; }
    long long GetFileSize() const;
    long long GetFilePosition() const;
    int GetDuration() const;
    int GetPosition() const;
    int GetBroadcastTime() const;
    int GetRate() const;
private:
    DWORD GetAdjTickCount();
    bool ReadToPcr(int limit, bool fSend);
    inline bool ReadPacket(bool fSend);
    void RotateBuffer(bool fSend);
    bool SetPointer(long long distanceToMove, DWORD dwMoveMethod);
    bool Seek(long long distanceToMove, DWORD dwMoveMethod);
    bool SeekToBoundary(long long predicted, long long range, BYTE *pWork, int workSize);
    void OpenSocket();
    void CloseSocket();
    void OpenPipe();
    void ClosePipe();
    void SendData(BYTE *pData, int dataSize);

    HANDLE m_hFile;
    BYTE *m_pBuf, *m_curr, *m_head, *m_tail;
    int m_unitSize, m_bufSize;
    bool m_fTrimPacket, m_fModTimestamp;
    CTsTimestampShifter m_tsShifter;
    SOCKET m_sock;
    CHAR m_udpAddr[MAX_URI];
    unsigned short m_udpPort;
    HANDLE m_hPipe;
    TCHAR m_pipeName[MAX_PATH];
    DWORD m_baseTick, m_renewSizeTick, m_renewDurTick;
    DWORD m_pcr, m_basePcr, m_initPcr;
    bool m_fPcr, m_fEnPcr, m_fShareWrite, m_fFixed, m_fPause;
    int m_pcrPid, m_pcrPids[PCR_PIDS_MAX];
    int m_pcrPidCounts[PCR_PIDS_MAX];
    int m_pcrPidsLen;
    long long m_fileSize;
    int m_duration;
    int m_totBase;
    DWORD m_totBasePcr;
    long long m_hash;
    int m_speedNum, m_speedDen;
    DWORD m_initStore;
    bool m_fSpecialExtending;
    int m_specialExtendInitRate;
    int m_adjState;
    DWORD m_adjBaseTick, m_adjHoldTick, m_adjAmount, m_adjDelta;
    LARGE_INTEGER m_liAdjFreq, m_liAdjBase;
};

#endif // INCLUDE_TS_SENDER_H
