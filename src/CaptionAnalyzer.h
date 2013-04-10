#ifndef INCLUDE_CAPTION_ANALYZER_H
#define INCLUDE_CAPTION_ANALYZER_H

class CCaptionAnalyzer
{
    static const int CAPTION_MAX = 512;
public:
    CCaptionAnalyzer();
    ~CCaptionAnalyzer();
    bool Initialize(LPCTSTR captionDllPath, LPCTSTR blacklistPath, int showLateMsec, int clearEarlyMsec);
    void UnInitialize();
    void Clear();
    void ClearShowState();
    bool CheckShowState(DWORD currentPcr);
    void AddPacket(BYTE *pPacket);
    bool IsInitialized() const { return m_hCaptionDll != NULL; }
private:
    HMODULE m_hCaptionDll;
    int m_showLate, m_clearEarly;
    bool m_fShowing;
    int m_commandRear, m_commandFront;
    bool m_fCommandShow[64];
    DWORD m_commandPcr[64];
    DWORD m_pts;
    bool m_fEnPts;
    UnInitializeCP      *m_pfnUnInitializeCP;
    AddTSPacketCP       *m_pfnAddTSPacketCP;
    ClearCP             *m_pfnClearCP;
    GetCaptionDataCP    *m_pfnGetCaptionDataCP;
    std::vector<std::basic_regex<TCHAR>> m_reList;
};

#endif // INCLUDE_CAPTION_ANALYZER_H
