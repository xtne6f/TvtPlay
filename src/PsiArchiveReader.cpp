#include <Windows.h>
#include "PsiArchiveReader.h"

bool CPsiArchiveReader::Open(LPCTSTR path)
{
    Close();
    m_chunkFileOffset = -1;
    m_readingTimeMsec = INT_MAX;
    FILE *fp;
    if (_tfopen_s(&fp, path, TEXT("rbN")) == 0) {
        m_fp.reset(fp);
    }
    return !!m_fp;
}

bool CPsiArchiveReader::ReadCodeList(const std::function<void(int, uint16_t, uint16_t)> &proc, const char *&errorMessage)
{
    if (!m_fp) {
        errorMessage = "CPsiArchiveReader: File is not open";
        return false;
    }
    rewind(m_fp.get());
    m_chunkFileOffset = -1;
    m_readingTimeMsec = INT_MAX;
    std::vector<std::pair<uint16_t, uint16_t>> dict, lastDict;
    uint32_t initTime = UNKNOWN_TIME;

    for (;;) {
        uint16_t dictionaryLen;
        uint16_t dictionaryWindowLen;
        uint32_t dictionaryDataSize;
        uint32_t dictionaryBuffSize;
        uint32_t codeListLen;
        if (!ReadHeader(m_fp.get(), m_timeList, dictionaryLen, dictionaryWindowLen,
                        dictionaryDataSize, dictionaryBuffSize, codeListLen))
        {
            break;
        }
        if (dictionaryBuffSize > DICTIONARY_MAX_BUFF_SIZE) {
            errorMessage = "CPsiArchiveReader: Buffer limit exceeded";
            return false;
        }
        uint32_t trailerSize = (dictionaryLen + (dictionaryDataSize + 1) / 2 + codeListLen) % 2 ? 2 : 4;

        dict.assign(dictionaryWindowLen, std::make_pair(static_cast<uint16_t>(0), static_cast<uint16_t>(0xFFFF)));
        for (size_t i = 0; i < dictionaryLen; ++i) {
            if (fread(&dict[i].first, sizeof(uint16_t), 1, m_fp.get()) != 1) {
                errorMessage = "CPsiArchiveReader: Read error";
                return false;
            }
            if (dict[i].first >= CODE_NUMBER_BEGIN) {
                // 前回辞書を参照
                uint16_t lastIndex = dict[i].first - CODE_NUMBER_BEGIN;
                if (lastIndex >= lastDict.size() || lastDict[lastIndex].second == 0xFFFF) {
                    // 参照先が異常
                    errorMessage = "CPsiArchiveReader: Invalid dictionary";
                    return false;
                }
                dict[i] = lastDict[lastIndex];
                lastDict[lastIndex].second = 0xFFFF;
            }
            uint32_t addSize = dict[i].first + 3;
            if (dictionaryBuffSize < addSize) {
                // 辞書バッファサイズ超過
                errorMessage = "CPsiArchiveReader: Invalid dictionary";
                return false;
            }
            dictionaryBuffSize -= addSize;
        }
        for (size_t i = 0; i < dictionaryLen; ++i) {
            if (dict[i].second == 0xFFFF) {
                // 新規なのでPID情報あり
                if (dictionaryDataSize < 2) {
                    errorMessage = "CPsiArchiveReader: Invalid dictionary";
                    return false;
                }
                if (fread(&dict[i].second, sizeof(uint16_t), 1, m_fp.get()) != 1) {
                    errorMessage = "CPsiArchiveReader: Read error";
                    return false;
                }
                dictionaryDataSize -= 2;
                dict[i].second &= 0x1FFF;
            }
        }
        for (size_t i = dictionaryLen, lastIndex = 0; i < dictionaryWindowLen; ++lastIndex) {
            if (lastIndex >= lastDict.size()) {
                errorMessage = "CPsiArchiveReader: Invalid dictionary";
                return false;
            }
            if (lastDict[lastIndex].second != 0xFFFF) {
                // 未参照なので継承
                dict[i++] = lastDict[lastIndex];
            }
        }
        _fseeki64(m_fp.get(), (dictionaryDataSize + 1) / 2 * 2, SEEK_CUR);

        uint32_t currTime = UNKNOWN_TIME;
        uint32_t codeCount = 0;
        for (size_t i = 0; i < m_timeList.size(); ++i) {
            if (m_timeList[i] == UNKNOWN_TIME) {
                // 時間不明
                currTime = UNKNOWN_TIME;
            }
            else if (m_timeList[i] >= 0x80000000) {
                // 絶対時間
                currTime = m_timeList[i] & 0x3FFFFFFF;
                if (initTime == UNKNOWN_TIME) {
                    initTime = currTime;
                }
            }
            else {
                // 相対時間
                int timeMsec = 0;
                if (currTime != UNKNOWN_TIME) {
                    currTime += m_timeList[i] & 0xFFFF;
                    timeMsec = ((0x40000000 + currTime - initTime) & 0x3FFFFFFF) / 45 * 4;
                }
                for (size_t n = (m_timeList[i] >> 16) + 1; n > 0; --n) {
                    uint16_t code;
                    if (codeCount++ == codeListLen) {
                        errorMessage = "CPsiArchiveReader: Invalid codelist";
                        return false;
                    }
                    if (fread(&code, sizeof(uint16_t), 1, m_fp.get()) != 1) {
                        errorMessage = "CPsiArchiveReader: Read error";
                        return false;
                    }
                    if (code < CODE_NUMBER_BEGIN ||
                        static_cast<size_t>(code - CODE_NUMBER_BEGIN) >= dict.size())
                    {
                        errorMessage = "CPsiArchiveReader: Invalid codelist";
                        return false;
                    }
                    if (currTime != UNKNOWN_TIME) {
                        uint16_t psiSize = dict[code - CODE_NUMBER_BEGIN].first + 1;
                        uint16_t pid = dict[code - CODE_NUMBER_BEGIN].second;
                        proc(timeMsec, psiSize, pid);
                    }
                }
            }
        }

        if (codeListLen != codeCount) {
            errorMessage = "CPsiArchiveReader: Invalid codelist";
            return false;
        }
        uint8_t trailer[4];
        if (fread(trailer, 1, trailerSize, m_fp.get()) != trailerSize) {
            errorMessage = "CPsiArchiveReader: Read error";
            return false;
        }
        dict.swap(lastDict);
    }
    return true;
}

void CPsiArchiveReader::Read(int beginTimeMsec, int endTimeMsec,
                             const std::function<void(const std::vector<uint8_t> &, uint16_t)> &proc)
{
    if (!m_fp || beginTimeMsec < 0 || beginTimeMsec > endTimeMsec) {
        return;
    }
    // 前回の失敗位置の時間より後ろは読み直さない
    if (m_chunkFileOffset < 0 && beginTimeMsec > m_readingTimeMsec) {
        return;
    }
    if (m_chunkFileOffset >= 0 && beginTimeMsec <= m_readingTimeMsec) {
        // 前方に移動するとき、現在のチャンクの開始時間以後への移動なら
        if (beginTimeMsec >= m_chunkBeginTimeMsec) {
            // 現在のチャンクを読み直し
            _fseeki64(m_fp.get(), m_chunkFileOffset, SEEK_SET);
            m_readingTimeMsec = m_chunkBeginTimeMsec - 1;
            m_chunkBeginTimeMsec = -1;
            m_timeListIndex = 0;
            m_currTime = UNKNOWN_TIME;
            m_lastPat = m_lastChunkLastPat.empty() ? nullptr : &m_lastChunkLastPat;
        }
        else {
            m_chunkFileOffset = -1;
        }
    }
    if (m_chunkFileOffset < 0) {
        // 最初のチャンクから読み直し
        rewind(m_fp.get());
        m_dict.clear();
        m_chunkFileOffset = MoveToNextChunk();
        if (m_chunkFileOffset < 0) {
            return;
        }
        m_readingTimeMsec = -1;
        m_chunkBeginTimeMsec = -1;
        m_timeListIndex = 0;
        m_initTime = UNKNOWN_TIME;
        m_currTime = UNKNOWN_TIME;
        m_lastChunkLastPat.clear();
        m_lastPat = nullptr;
    }

    bool fProcPat = false;
    for (;;) {
        for (size_t i = m_timeListIndex; i < m_timeList.size(); ++i, ++m_timeListIndex) {
            if (m_timeList[i] == UNKNOWN_TIME) {
                // 時間不明
                m_currTime = UNKNOWN_TIME;
            }
            else if (m_timeList[i] >= 0x80000000) {
                // 絶対時間
                m_currTime = m_timeList[i] & 0x3FFFFFFF;
                if (m_initTime == UNKNOWN_TIME) {
                    m_initTime = m_currTime;
                }
            }
            else {
                // 相対時間
                int timeMsec = 0;
                if (m_currTime != UNKNOWN_TIME) {
                    uint32_t t = m_currTime + (m_timeList[i] & 0xFFFF);
                    timeMsec = ((0x40000000 + t - m_initTime) & 0x3FFFFFFF) / 45 * 4;
                    if (m_readingTimeMsec < timeMsec) {
                        if (timeMsec >= endTimeMsec) {
                            // 追い越したので完了。PATはなるべく送る
                            if (!fProcPat && m_lastPat) {
                                proc(*m_lastPat, 0);
                            }
                            return;
                        }
                        m_readingTimeMsec = timeMsec;
                        if (m_chunkBeginTimeMsec < 0) {
                            m_chunkBeginTimeMsec = m_readingTimeMsec;
                        }
                    }
                    m_currTime = t;
                }
                for (size_t n = (m_timeList[i] >> 16) + 1; n > 0; --n) {
                    uint16_t code;
                    if (fread(&code, sizeof(uint16_t), 1, m_fp.get()) != 1 ||
                        code < CODE_NUMBER_BEGIN ||
                        static_cast<size_t>(code - CODE_NUMBER_BEGIN) >= m_dict.size())
                    {
                        // 異常
                        m_chunkFileOffset = -1;
                        return;
                    }
                    const std::vector<uint8_t> &psi = m_dict[code - CODE_NUMBER_BEGIN].first;
                    uint16_t pid = m_dict[code - CODE_NUMBER_BEGIN].second;
                    if (pid == 0) {
                        m_lastPat = &psi;
                    }
                    else if (m_currTime != UNKNOWN_TIME && timeMsec >= beginTimeMsec) {
                        // 利用側がPMTを特定できるよう直近のPATを最初に送る
                        if (!fProcPat && m_lastPat) {
                            proc(*m_lastPat, 0);
                            fProcPat = true;
                        }
                        proc(psi, pid);
                    }
                }
            }
        }

        // 次を読む
        uint8_t trailer[4];
        if (fread(trailer, 1, m_trailerSize, m_fp.get()) != m_trailerSize) {
            // 異常
            m_chunkFileOffset = -1;
            return;
        }
        if (m_lastPat) {
            // 辞書が更新されるのでPATを退避
            m_lastChunkLastPat = *m_lastPat;
            m_lastPat = &m_lastChunkLastPat;
        }
        m_chunkFileOffset = MoveToNextChunk();
        if (m_chunkFileOffset < 0) {
            return;
        }
        m_chunkBeginTimeMsec = -1;
        m_timeListIndex = 0;
        m_currTime = UNKNOWN_TIME;
    }
}

int64_t CPsiArchiveReader::MoveToNextChunk()
{
    std::vector<std::pair<std::vector<uint8_t>, uint16_t>> lastDict;
    m_dict.swap(lastDict);

    uint16_t dictionaryLen;
    uint16_t dictionaryWindowLen;
    uint32_t dictionaryDataSize;
    uint32_t dictionaryBuffSize;
    uint32_t codeListLen;
    if (!ReadHeader(m_fp.get(), m_timeList, dictionaryLen, dictionaryWindowLen,
                    dictionaryDataSize, dictionaryBuffSize, codeListLen) ||
        dictionaryBuffSize > DICTIONARY_MAX_BUFF_SIZE)
    {
        return -1;
    }
    bool fAlignment = dictionaryDataSize % 2 != 0;
    m_trailerSize = (dictionaryLen + (dictionaryDataSize + 1) / 2 + codeListLen) % 2 ? 2 : 4;

    m_dict.assign(dictionaryWindowLen, std::make_pair(std::vector<uint8_t>(), static_cast<uint16_t>(0xFFFF)));
    for (size_t i = 0; i < dictionaryLen; ++i) {
        uint16_t codeOrSize;
        if (fread(&codeOrSize, sizeof(uint16_t), 1, m_fp.get()) != 1) {
            return -1;
        }
        if (codeOrSize >= CODE_NUMBER_BEGIN) {
            // 前回辞書を参照
            uint16_t lastIndex = codeOrSize - CODE_NUMBER_BEGIN;
            if (lastIndex >= lastDict.size() || lastDict[lastIndex].second == 0xFFFF) {
                // 参照先が異常
                return -1;
            }
            m_dict[i].swap(lastDict[lastIndex]);
        }
        else {
            m_dict[i].first.resize(codeOrSize + 1);
        }
        uint32_t addSize = static_cast<uint32_t>(m_dict[i].first.size() + 2);
        if (dictionaryBuffSize < addSize) {
            // 辞書バッファサイズ超過
            return -1;
        }
        dictionaryBuffSize -= addSize;
    }
    for (size_t i = 0; i < dictionaryLen; ++i) {
        if (m_dict[i].second == 0xFFFF) {
            // 新規なのでPID情報あり
            if (dictionaryDataSize < 2 || fread(&m_dict[i].second, sizeof(uint16_t), 1, m_fp.get()) != 1) {
                return -1;
            }
            dictionaryDataSize -= 2;
            m_dict[i].second |= 0xE000;
        }
    }
    for (size_t i = 0; i < dictionaryLen; ++i) {
        if (m_dict[i].second & 0xE000) {
            // 新規なので辞書データあり
            if (dictionaryDataSize < m_dict[i].first.size() ||
                fread(m_dict[i].first.data(), 1, m_dict[i].first.size(), m_fp.get()) != m_dict[i].first.size()) {
                return -1;
            }
            dictionaryDataSize -= static_cast<uint32_t>(m_dict[i].first.size());
            m_dict[i].second &= 0x1FFF;
        }
    }
    for (size_t i = dictionaryLen, lastIndex = 0; i < dictionaryWindowLen; ++lastIndex) {
        if (lastIndex >= lastDict.size()) {
            // 辞書ウィンドウ長が異常
            return -1;
        }
        if (lastDict[lastIndex].second != 0xFFFF) {
            // 未参照なので継承
            m_dict[i++].swap(lastDict[lastIndex]);
        }
    }

    uint8_t alignment;
    if (dictionaryDataSize > 0 || (fAlignment && fread(&alignment, 1, 1, m_fp.get()) != 1)) {
        return -1;
    }
    return _ftelli64(m_fp.get());
}

bool CPsiArchiveReader::ReadHeader(FILE *fp, std::vector<uint32_t> &timeList, uint16_t &dictionaryLen, uint16_t &dictionaryWindowLen,
                                   uint32_t &dictionaryDataSize, uint32_t &dictionaryBuffSize, uint32_t &codeListLen)
{
    static const uint8_t HEADER_MAGIC[] = { 0x50, 0x73, 0x73, 0x63, 0x0D, 0x0A, 0x9A, 0x0A };
    uint8_t magicAndReserved[10];
    uint16_t timeListLen;
    if (fread(&magicAndReserved, 1, 10, fp) != 10 ||
        memcmp(magicAndReserved, HEADER_MAGIC, sizeof(HEADER_MAGIC)) != 0 ||
        fread(&timeListLen, sizeof(timeListLen), 1, fp) != 1 ||
        fread(&dictionaryLen, sizeof(dictionaryLen), 1, fp) != 1 ||
        fread(&dictionaryWindowLen, sizeof(dictionaryWindowLen), 1, fp) != 1 ||
        fread(&dictionaryDataSize, sizeof(dictionaryDataSize), 1, fp) != 1 ||
        fread(&dictionaryBuffSize, sizeof(dictionaryBuffSize), 1, fp) != 1 ||
        fread(&codeListLen, sizeof(codeListLen), 1, fp) != 1 ||
        fread(magicAndReserved, 1, 4, fp) != 4 ||
        dictionaryWindowLen < dictionaryLen ||
        dictionaryBuffSize < dictionaryDataSize ||
        dictionaryWindowLen > 65536 - CODE_NUMBER_BEGIN)
    {
        return false;
    }
    timeList.resize(timeListLen);
    if (!timeList.empty() && fread(timeList.data(), sizeof(uint32_t), timeList.size(), fp) != timeList.size()) {
        return false;
    }
    return true;
}
