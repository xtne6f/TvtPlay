#ifndef INCLUDE_READ_ONLY_MPEG4_FILE_H
#define INCLUDE_READ_ONLY_MPEG4_FILE_H

#include "ReadOnlyFile.h"
#include <vector>

class CReadOnlyMpeg4File : public IReadOnlyFile
{
    static const DWORD READ_BOX_SIZE_MAX = 64 * 1024 * 1024;
    static const DWORD BLOCK_SIZE_MIN = 256;
    static const DWORD BLOCK_SIZE_MAX = 65536;
    static const DWORD VIDEO_SAMPLE_MAX = 2 * 1024 * 1024;
    static const DWORD AUDIO_SAMPLE_MAX = 8184;
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
        BYTE counterA;
    };
    static inline DWORD ArrayToDWORD(const BYTE *data) {
        return MAKEWORD(data[3], data[2]) | static_cast<DWORD>(MAKEWORD(data[1], data[0])) << 16;
    }
    bool InitializeTable();
    bool ReadVideoSampleDesc(char index, std::vector<BYTE> &spsPps, std::vector<BYTE> &buf) const;
    bool ReadAudioSampleDesc(char index, BYTE *adtsHeader, std::vector<BYTE> &buf) const;
    bool ReadSampleTable(char index, std::vector<__int64> &stso, std::vector<DWORD> &stsz,
                         std::vector<__int64> &stts, std::vector<DWORD> *ctts, std::vector<BYTE> &buf) const;
    bool InitializeBlockList();
    bool ReadCurrentBlock();
    bool ReadBox(LPCSTR path, std::vector<BYTE> &data) const;
    int ReadSample(size_t index, const std::vector<__int64> &stso, const std::vector<DWORD> &stsz, std::vector<BYTE> *data) const;
    static size_t CreatePat(BYTE *data, WORD tsid, WORD sid);
    static size_t CreateNit(BYTE *data, WORD nid);
    static size_t CreateSdt(BYTE *data, WORD nid, WORD tsid, WORD sid);
    static size_t CreateTot(BYTE *data, SYSTEMTIME st);
    static size_t CreatePmt(BYTE *data, WORD sid);
    static size_t CreateHeader(BYTE *data, BYTE unitStart, BYTE adaptation, BYTE counter, WORD pid);
    static size_t CreatePcrAdaptation(BYTE *data, DWORD pcr45khz);
    static size_t CreatePesHeader(BYTE *data, BYTE streamID, WORD packetLength, DWORD pts45khz, BYTE stuffingSize);
    static size_t CreateAdtsHeader(BYTE *data, int profile, int freq, int ch, int bufferSize);
    static size_t NalFileToByte(std::vector<BYTE> &data, bool &fIdr);
    static DWORD CalcCrc32(const BYTE *data, size_t len, DWORD crc = 0xFFFFFFFF);

    HANDLE m_hFile;
    WORD m_nid, m_tsid, m_sid;
    LARGE_INTEGER m_totStart;
    std::vector<__int64> m_stsoV, m_stsoA;
    std::vector<DWORD> m_stszV, m_stszA;
    std::vector<__int64> m_sttsV, m_sttsA;
    std::vector<DWORD> m_cttsV;
    DWORD m_timeScaleV, m_timeScaleA;
    std::vector<BYTE> m_spsPps;
    BYTE m_adtsHeader[7];
    std::vector<BLOCK_100MSEC> m_blockList;
    std::vector<BLOCK_100MSEC>::const_iterator m_blockInfo;
    std::vector<BYTE> m_blockCache;
    __int64 m_pointer;
};

#endif // INCLUDE_READ_ONLY_MPEG4_FILE_H
