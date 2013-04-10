#ifndef INCLUDE_CAPTION_ANALYZER_H
#define INCLUDE_CAPTION_ANALYZER_H

class CCaptionAnalyzer
{
    static const int PATTERN_MAX = 256;
    static const int CAPTION_MAX = 512;
    static const int PCR_PER_MSEC = 45;
    static const int PCR_PIDS_MAX = 8;
public:
    CCaptionAnalyzer();
    ~CCaptionAnalyzer();
    bool Initialize(LPCTSTR captionDllPath, LPCTSTR bregonigDllPath, LPCTSTR blacklistPath, int showLateMsec, int clearEarlyMsec);
    void UnInitialize();
    void ClearShowState();
    bool CheckShowState();
    void AddPacket(BYTE *pPacket);
    bool IsInitialized() const { return m_hCaptionDll != NULL; }
    void SetPid(int pid) { m_captionPid = pid; }
    int GetPid() const { return m_captionPid; }
private:
    HMODULE m_hCaptionDll, m_hBregonigDll;
    DWORD m_showLate, m_clearEarly;
    bool m_fShowing;
    int m_commandRear, m_commandFront;
    bool m_fCommandShow[64];
    DWORD m_commandPcr[64];
    DWORD m_pcr;
    int m_pcrPid, m_pcrPids[PCR_PIDS_MAX];
    int m_pcrPidCounts[PCR_PIDS_MAX];
    int m_pcrPidsLen;
    DWORD m_captionPts;
    bool m_fEnCaptionPts;
    int m_captionPid;
    InitializeUNICODE   *m_pfnInitializeUNICODE;
    UnInitializeCP      *m_pfnUnInitializeCP;
    AddTSPacketCP       *m_pfnAddTSPacketCP;
    ClearCP             *m_pfnClearCP;
    GetTagInfoCP        *m_pfnGetTagInfoCP;
    GetCaptionDataCP    *m_pfnGetCaptionDataCP;
    BMatch              *m_pfnBMatch;
    BRegfree            *m_pfnBRegfree;
    struct RXP_PATTERN {
        BREGEXP *rxp;
        TCHAR str[PATTERN_MAX];
    };
    std::vector<RXP_PATTERN> m_rxpList;
    std::vector<TRex*> m_trexList;
};

#endif // INCLUDE_CAPTION_ANALYZER_H
