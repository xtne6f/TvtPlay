#ifndef INCLUDE_READ_ONLY_MPEG4_FILE_H
#define INCLUDE_READ_ONLY_MPEG4_FILE_H

#include "PsiArchiveReader.h"
#include "ReadOnlyFile.h"
#include <map>
#include <utility>
#include <vector>

#ifndef MASK_OFF_SPS_CS45_FLAGS
#define MASK_OFF_SPS_CS45_FLAGS 1
#endif

class CReadOnlyMpeg4File : public IReadOnlyFile
{
    static const DWORD READ_BOX_SIZE_MAX = 64 * 1024 * 1024;
    static const DWORD BLOCK_SIZE_MAX = 65536;
    static const DWORD BLOCK_LIST_SIZE_MAX = 1000000;
    static const DWORD VIDEO_SAMPLE_MAX = 2 * 1024 * 1024;
    static const DWORD AUDIO_SAMPLE_MAX = 8184;
    static const DWORD CAPTION_FORWARD_MSEC = 500;
    static const WORD VIDEO_PID = 0x0100;
    static const WORD AUDIO1_PID = 0x0110;
    static const WORD CAPTION_PID = 0x0130;
    static const WORD PMT_PID = 0x01F0;
    static const WORD PCR_PID = 0x01FF;
    static const WORD DISPLACED_PID = 0x1E00;
    static const DWORD PSI_MAX_STREAMS = 32;
public:
    CReadOnlyMpeg4File() : m_hFile(INVALID_HANDLE_VALUE) {}
    ~CReadOnlyMpeg4File() { Close(); }
    bool Open(LPCTSTR path, int flags, LPCTSTR &errorMessage);
    void Close();
    int Read(BYTE *pBuf, int numToRead);
    __int64 SetPointer(__int64 distanceToMove, MOVE_METHOD moveMethod);
    __int64 GetSize() const;
    int GetDurationMsec() const;
    bool IsShareWrite() const { return false; }
private:
    struct BLOCK_100MSEC {
        DWORD pos;
        BYTE counterV;
        BYTE counterA[2];
        BYTE counterC;
    };
    struct PSI_COUNTER_INFO {
        WORD mappedPid;
        BYTE currentCounter;
        std::vector<BYTE> counterList;
    };
    static inline DWORD ArrayToDWORD(const BYTE *data) {
        return MAKEWORD(data[3], data[2]) | static_cast<DWORD>(MAKEWORD(data[1], data[0])) << 16;
    }
    bool LoadSettings();
    void InitializeMetaInfo(LPCTSTR path);
    void LoadCaption(LPCTSTR path);
    void OpenPsiData(LPCTSTR path);
    bool InitializeTable(LPCTSTR &errorMessage);
    bool ReadVideoSampleDesc(char index, bool &fHevc, std::vector<BYTE> &spsPps, std::vector<BYTE> &buf) const;
    bool ReadAudioSampleDesc(char index, BYTE *adtsHeader, std::vector<BYTE> &buf) const;
    bool ReadSampleTable(char index, std::vector<__int64> &stso, std::vector<DWORD> &stsz,
                         std::vector<__int64> &stts, std::vector<DWORD> *ctts, std::vector<BYTE> &buf) const;
    bool InitializeBlockList(LPCTSTR &errorMessage);
    bool ReadCurrentBlock();
    bool InitializePsiCounterInfo(LPCTSTR &errorMessage);
    int ReadBox(LPCSTR path, std::vector<BYTE> &data) const;
    int ReadSample(size_t index, const std::vector<__int64> &stso, const std::vector<DWORD> &stsz, std::vector<BYTE> *data) const;
    static void AddTsPacketsFromPsi(std::vector<BYTE> &buf, const BYTE *psi, size_t psiSize, BYTE &counter, WORD pid);
    static bool Add16TsPacketsFromPsi(std::vector<BYTE> &buf, const BYTE *psi, size_t psiSize, WORD pid);
    static size_t CreatePat(BYTE *data, WORD tsid, WORD sid);
    static size_t CreatePatFromPat(BYTE *data, const std::vector<BYTE> &pat, WORD &firstPmtPid);
    static size_t CreateNit(BYTE *data, WORD nid);
    static size_t CreateSdt(BYTE *data, WORD nid, WORD tsid, WORD sid);
    static size_t CreateEmptyEitPf(BYTE *data, WORD nid, WORD tsid, WORD sid);
    static size_t CreateTot(BYTE *data, SYSTEMTIME st);
    static size_t CreatePmt(BYTE *data, WORD sid, bool fHevc, bool fAudio2, bool fCaption);
    static bool AddPmtPacketsFromPmt(std::vector<BYTE> &buf, const std::vector<BYTE> &pmt, const std::map<WORD, PSI_COUNTER_INFO> &pidMap,
                                     bool fHevc, bool fAudio2, bool fCaption);
    static size_t CreatePmt2ndLoop(BYTE *data, bool fHevc, bool fAudio2, bool fCaption);
    static size_t CreateHeader(BYTE *data, BYTE unitStart, BYTE adaptation, BYTE counter, WORD pid);
    static size_t CreatePcrAdaptation(BYTE *data, DWORD pcr45khz);
    static size_t CreatePesHeader(BYTE *data, BYTE streamID, bool fDataAlignment, WORD packetLength, DWORD pts45khz, BYTE stuffingSize);
    static size_t CreateAdtsHeader(BYTE *data, int profile, int freq, int ch, int bufferSize);
    static size_t NalFileToByte(std::vector<BYTE> &data, bool &fIdr, bool fHevc);
    static DWORD CalcCrc32(const BYTE *data, size_t len, DWORD crc = 0xFFFFFFFF);

    HANDLE m_hFile;
    TCHAR m_metaName[MAX_PATH];
    TCHAR m_vttExtension[16];
    TCHAR m_psiDataExtension[16];
    TCHAR m_iniBroadcastID[15];
    TCHAR m_iniTime[20];
    WORD m_nid, m_tsid, m_sid;
    LARGE_INTEGER m_totStart;
    std::vector<std::pair<__int64, std::vector<BYTE>>> m_captionList;
    std::vector<__int64> m_stsoV, m_stsoA[2];
    std::vector<DWORD> m_stszV, m_stszA[2];
    std::vector<__int64> m_sttsV, m_sttsA[2];
    std::vector<DWORD> m_cttsV;
    DWORD m_timeScaleV, m_timeScaleA[2];
    bool m_fHevc;
    std::vector<BYTE> m_spsPps;
    BYTE m_adtsHeader[2][7];
    std::vector<BLOCK_100MSEC> m_blockList;
    std::vector<BLOCK_100MSEC>::const_iterator m_blockInfo;
    std::vector<BYTE> m_blockCache;
    __int64 m_pointer;
    CPsiArchiveReader m_psiDataReader;
    std::map<WORD, PSI_COUNTER_INFO> m_psiCounterInfoMap;
};

#endif // INCLUDE_READ_ONLY_MPEG4_FILE_H
