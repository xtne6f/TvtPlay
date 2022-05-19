#ifndef INCLUDE_READ_ONLY_MPEG4_FILE_H
#define INCLUDE_READ_ONLY_MPEG4_FILE_H

#include "ReadOnlyFile.h"
#include <utility>
#include <vector>

#ifndef MASK_OFF_SPS_CS45_FLAGS
#define MASK_OFF_SPS_CS45_FLAGS 1
#endif

class CReadOnlyMpeg4File : public IReadOnlyFile
{
    static const DWORD READ_BOX_SIZE_MAX = 64 * 1024 * 1024;
    static const DWORD BLOCK_SIZE_MIN = 256;
    static const DWORD BLOCK_SIZE_MAX = 65536;
    static const DWORD BLOCK_LIST_SIZE_MAX = 1000000;
    static const DWORD VIDEO_SAMPLE_MAX = 2 * 1024 * 1024;
    static const DWORD AUDIO_SAMPLE_MAX = 8184;
    static const DWORD CAPTION_FORWARD_MSEC = 500;
public:
    CReadOnlyMpeg4File() : m_hFile(INVALID_HANDLE_VALUE) {}
    ~CReadOnlyMpeg4File() { Close(); }
    bool Open(LPCTSTR path, int flags);
    void Close();
    int Read(BYTE *pBuf, int numToRead);
    __int64 SetPointer(__int64 distanceToMove, MOVE_METHOD moveMethod);
    __int64 GetSize() const;
private:
    struct BLOCK_100MSEC {
        DWORD pos;
        BYTE counterV;
        BYTE counterA[2];
        BYTE counterC;
    };
    static inline DWORD ArrayToDWORD(const BYTE *data) {
        return MAKEWORD(data[3], data[2]) | static_cast<DWORD>(MAKEWORD(data[1], data[0])) << 16;
    }
    bool LoadSettings();
    void InitializeMetaInfo(LPCTSTR path);
    void LoadCaption(LPCTSTR path);
    bool InitializeTable();
    bool ReadVideoSampleDesc(char index, std::vector<BYTE> &spsPps, std::vector<BYTE> &buf) const;
    bool ReadAudioSampleDesc(char index, BYTE *adtsHeader, std::vector<BYTE> &buf) const;
    bool ReadSampleTable(char index, std::vector<__int64> &stso, std::vector<DWORD> &stsz,
                         std::vector<__int64> &stts, std::vector<DWORD> *ctts, std::vector<BYTE> &buf) const;
    bool InitializeBlockList();
    bool ReadCurrentBlock();
    int ReadBox(LPCSTR path, std::vector<BYTE> &data) const;
    int ReadSample(size_t index, const std::vector<__int64> &stso, const std::vector<DWORD> &stsz, std::vector<BYTE> *data) const;
    static size_t CreatePat(BYTE *data, WORD tsid, WORD sid);
    static size_t CreateNit(BYTE *data, WORD nid);
    static size_t CreateSdt(BYTE *data, WORD nid, WORD tsid, WORD sid);
    static size_t CreateEmptyEitPf(BYTE *data, WORD nid, WORD tsid, WORD sid);
    static size_t CreateTot(BYTE *data, SYSTEMTIME st);
    static size_t CreatePmt(BYTE *data, WORD sid, bool fAudio2, bool fCaption);
    static size_t CreateHeader(BYTE *data, BYTE unitStart, BYTE adaptation, BYTE counter, WORD pid);
    static size_t CreatePcrAdaptation(BYTE *data, DWORD pcr45khz);
    static size_t CreatePesHeader(BYTE *data, BYTE streamID, bool fDataAlignment, WORD packetLength, DWORD pts45khz, BYTE stuffingSize);
    static size_t CreateAdtsHeader(BYTE *data, int profile, int freq, int ch, int bufferSize);
    static size_t NalFileToByte(std::vector<BYTE> &data, bool &fIdr);
    static DWORD CalcCrc32(const BYTE *data, size_t len, DWORD crc = 0xFFFFFFFF);

    HANDLE m_hFile;
    TCHAR m_metaName[MAX_PATH];
    TCHAR m_vttExtension[16];
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
    std::vector<BYTE> m_spsPps;
    BYTE m_adtsHeader[2][7];
    std::vector<BLOCK_100MSEC> m_blockList;
    std::vector<BLOCK_100MSEC>::const_iterator m_blockInfo;
    std::vector<BYTE> m_blockCache;
    __int64 m_pointer;
};

#endif // INCLUDE_READ_ONLY_MPEG4_FILE_H
