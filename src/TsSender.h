#ifndef INCLUDE_TS_SENDER_H
#define INCLUDE_TS_SENDER_H

#include "BufferedFileReader.h"
#include <memory>

#define BON_PIPE_MESSAGE_MAX  128

class CTsTimestampShifter
{
    static const int PCR_INITIAL_MARGIN = 600 * 1000 * PCR_PER_MSEC;
public:
    CTsTimestampShifter();
    void SetInitialPcr(DWORD pcr45khz);
    void Reset();
    void Transform(BYTE *pPacket) { if (m_fEnabled) Transform_(pPacket); }
    void Enable(bool fEnable) { m_fEnabled = fEnable; }
    bool IsEnabled() const { return m_fEnabled; };
private:
    void Transform_(BYTE *pPacket);
    DWORD m_shift45khz;
    PAT m_pat;
    bool m_fEnabled;
};

// 以下はClose()するまで呼び出すスレッドを一致させること
// SetupQpc()、Send()、Send()呼び出し後のSeek*()
class CTsSender
{
    static const int MAX_URI = 256;
    static const int BUFFER_LEN = 1536;
    static const int BON_UDP_TSDATASIZE = 48128;
    static const int TS_SUPPOSED_RATE = 2 * 1024 * 1024;
    static const int PCR_PIDS_MAX = 8;
    static const int RESYNC_FAILURE_LIMIT = 2;
    static const int RENEW_SIZE_INTERVAL = 3000;
    static const int RENEW_FSR_INTERVAL = 1000;
    static const int INITIAL_STORE_MSEC = 500;
    static const int PCR_LAP_THRESHOLD = 600 * 1000 * PCR_PER_MSEC;
public:
    CTsSender();
    ~CTsSender();
    bool Open(LPCTSTR path, DWORD salt, int bufSize, bool fConvTo188, bool fUnderrunCtrl, bool fUseQpc, int pcrDisconThresholdMsec);
    DWORD GetInitialPcr() { return m_initPcr; }
    void SetupQpc();
    void SetUdpAddress(LPCSTR addr, unsigned short port);
    void SetPipeName(LPCTSTR name);
    void SetModTimestamp(bool fModTimestamp);
    void Close();
    int Send();
    void SendEmptyPat();
    bool SeekToBegin();
    bool SeekToEnd();
    bool Seek(int msec);
    void Pause(bool fPause, bool fPurge = true);
    void SetSpeed(int num, int den);
    bool IsOpen() const { return m_file != nullptr; }
    bool IsPaused() const { return m_fPause; }
    bool IsFixed(bool *pfSpecialExt = nullptr) const { if (pfSpecialExt) *pfSpecialExt=m_fSpecialExtending; return m_fFixed; }
    void GetSpeed(int *pNum, int *pDen) const { *pNum=m_speedNum; *pDen=m_speedDen; }
    __int64 GetFileHash() const { return m_hash; }
    int GetDuration() const;
    int GetPosition() const;
    int GetBroadcastTime() const;
    int GetRate() const;
private:
    DWORD GetAdjTickCount();
    int ReadToPcr(int limit, bool fSend, bool fSyncRead);
    void RotateBuffer(bool fSend, bool fSyncRead);
    bool Seek(__int64 distanceToMove, IReadOnlyFile::MOVE_METHOD moveMethod);
    bool SeekToBoundary(__int64 predicted, __int64 range, BYTE *pWork, int workSize);
    void OpenSocket();
    void CloseSocket();
    void OpenPipe();
    void ClosePipe();
    void CloseCtrlPipe();
    void SendData(BYTE *pData, int dataSize);
    int TransactMessage(LPCTSTR request, LPTSTR reply = nullptr);
    static DWORD DiffPcr(DWORD a, DWORD b) { return ((a-b)&0x80000000) && b-a<PCR_LAP_THRESHOLD ? 0 : a-b; }

    std::unique_ptr<IReadOnlyFile> m_file;
    CBufferedFileReader m_reader;
    BYTE *m_curr, *m_head, *m_tail;
    int m_unitSize;
    bool m_fTrimPacket, m_fUnderrunCtrl;
    DWORD m_pcrDisconThreshold;
    CTsTimestampShifter m_tsShifter;

    SOCKET m_sock;
    CHAR m_udpAddr[MAX_URI];
    unsigned short m_udpPort;
    HANDLE m_hPipe, m_hCtrlPipe;
    TCHAR m_pipeName[MAX_PATH];

    DWORD m_baseTick, m_renewSizeTick, m_renewDurTick, m_renewFsrTick;
    DWORD m_pcr, m_basePcr, m_initPcr, m_prevPcr;
    int m_rateCtrlMsec;
    bool m_fEnPcr, m_fShareWrite, m_fFixed, m_fPause;
    bool m_fPurged;
    bool m_fForceSyncRead;
    int m_pcrPid, m_pcrPids[PCR_PIDS_MAX];
    int m_pcrPidCounts[PCR_PIDS_MAX];
    int m_pcrPidsLen;
    __int64 m_fileSize;
    int m_duration;
    int m_totBase;
    DWORD m_totBasePcr;
    __int64 m_hash;
    int m_speedNum, m_speedDen;
    DWORD m_initStore;
    bool m_fSpecialExtending;
    int m_specialExtendInitRate;

    DWORD m_adjBaseTick;
    __int64 m_adjFreq, m_adjBase;
};

#endif // INCLUDE_TS_SENDER_H
