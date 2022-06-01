#ifndef INCLUDE_PSI_ARCHIVE_READER_H
#define INCLUDE_PSI_ARCHIVE_READER_H

#include <tchar.h>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

class CPsiArchiveReader
{
public:
    CPsiArchiveReader() : m_fp(nullptr, fclose) {}
    bool Open(LPCTSTR path);
    void Close() { m_fp.reset(); }
    bool IsOpen() const { return !!m_fp; }
    bool ReadCodeList(const std::function<void(int, WORD, WORD)> &proc, LPCTSTR &errorMessage);
    void Read(int beginTimeMsec, int endTimeMsec,
              const std::function<void(const std::vector<BYTE> &, WORD)> &proc);
private:
    __int64 MoveToNextChunk();
    static bool ReadHeader(FILE *fp, std::vector<DWORD> &timeList, WORD &dictionaryLen, WORD &dictionaryWindowLen,
                           DWORD &dictionaryDataSize, DWORD &dictionaryBuffSize, DWORD &codeListLen);

    static const DWORD UNKNOWN_TIME = 0xFFFFFFFF;
    static const WORD CODE_NUMBER_BEGIN = 4096;
    static const DWORD DICTIONARY_MAX_BUFF_SIZE = 32 * 1024 * 1024;
    std::unique_ptr<FILE, decltype(&fclose)> m_fp;
    std::vector<DWORD> m_timeList;
    std::vector<std::pair<std::vector<BYTE>, WORD>> m_dict;
    __int64 m_chunkFileOffset;
    int m_readingTimeMsec;
    int m_chunkBeginTimeMsec;
    size_t m_timeListIndex;
    DWORD m_initTime;
    DWORD m_currTime;
    std::vector<BYTE> m_lastChunkLastPat;
    const std::vector<BYTE> *m_lastPat;
    DWORD m_trailerSize;
};

#endif // INCLUDE_PSI_ARCHIVE_READER_H
