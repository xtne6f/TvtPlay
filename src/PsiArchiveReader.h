#ifndef INCLUDE_PSI_ARCHIVE_READER_H
#define INCLUDE_PSI_ARCHIVE_READER_H

#include <stdint.h>
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
    bool ReadCodeList(const std::function<void(int, uint16_t, uint16_t)> &proc, const char *&errorMessage);
    void Read(int beginTimeMsec, int endTimeMsec,
              const std::function<void(const std::vector<uint8_t> &, uint16_t)> &proc);
private:
    int64_t MoveToNextChunk();
    static bool ReadHeader(FILE *fp, std::vector<uint32_t> &timeList, uint16_t &dictionaryLen, uint16_t &dictionaryWindowLen,
                           uint32_t &dictionaryDataSize, uint32_t &dictionaryBuffSize, uint32_t &codeListLen);

    static const uint32_t UNKNOWN_TIME = 0xFFFFFFFF;
    static const uint16_t CODE_NUMBER_BEGIN = 4096;
    static const uint32_t DICTIONARY_MAX_BUFF_SIZE = 32 * 1024 * 1024;
    std::unique_ptr<FILE, decltype(&fclose)> m_fp;
    std::vector<uint32_t> m_timeList;
    std::vector<std::pair<std::vector<uint8_t>, uint16_t>> m_dict;
    int64_t m_chunkFileOffset;
    int m_readingTimeMsec;
    int m_chunkBeginTimeMsec;
    size_t m_timeListIndex;
    uint32_t m_initTime;
    uint32_t m_currTime;
    std::vector<uint8_t> m_lastChunkLastPat;
    const std::vector<uint8_t> *m_lastPat;
    uint32_t m_trailerSize;
};

#endif // INCLUDE_PSI_ARCHIVE_READER_H
