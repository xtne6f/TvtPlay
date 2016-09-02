#include <Windows.h>
#include <algorithm>
#include "ReadOnlyMpeg4File.h"

bool CReadOnlyMpeg4File::Open(LPCTSTR path, int flags)
{
    Close();
    if ((flags & OPEN_FLAG_NORMAL) && !(flags & OPEN_FLAG_SHARE_WRITE)) {
        m_hFile = ::CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (m_hFile == INVALID_HANDLE_VALUE || !InitializeTable()) {
            Close();
        }
        else {
            // TODO: 各値をメタデータあたりから抽出する仕組みがあれば便利かも
            m_nid = 0x0001;
            m_tsid = 0x0002;
            m_sid = 0x0003;
            m_totStart.QuadPart = 125911908000000000LL; // 2000-01-01T09:00:00
            FILETIME ft;
            if (::GetFileTime(m_hFile, NULL, NULL, &ft)) {
                m_totStart.LowPart = ft.dwLowDateTime;
                m_totStart.HighPart = ft.dwHighDateTime;
                m_totStart.QuadPart += 9 * 36000000000LL;
            }
            m_blockInfo = m_blockList.begin();
            m_blockCache.clear();
            m_pointer = 0;
        }
    }
    return m_hFile != INVALID_HANDLE_VALUE;
}

void CReadOnlyMpeg4File::Close()
{
    if (m_hFile != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
}

int CReadOnlyMpeg4File::Read(BYTE *pBuf, int numToRead)
{
    if (m_hFile != INVALID_HANDLE_VALUE) {
        int numRead = 0;
        while (numRead < numToRead) {
            if (m_blockCache.empty() && !ReadCurrentBlock()) {
                break;
            }
            int n = static_cast<int>(min(numToRead - numRead, static_cast<__int64>(m_blockCache.size()) - m_pointer));
            ::memcpy(pBuf + numRead, &m_blockCache[static_cast<size_t>(m_pointer)], n);
            numRead += n;
            m_pointer += n;
            if (m_pointer >= static_cast<__int64>(m_blockCache.size())) {
                ++m_blockInfo;
                m_blockCache.clear();
                m_pointer = 0;
            }
        }
        return numRead;
    }
    return -1;
}

__int64 CReadOnlyMpeg4File::SetPointer(__int64 distanceToMove, MOVE_METHOD moveMethod)
{
    if (m_hFile != INVALID_HANDLE_VALUE) {
        __int64 toMove = moveMethod == MOVE_METHOD_CURRENT ? static_cast<__int64>(m_blockInfo->pos) * 188 + m_pointer + distanceToMove :
                         moveMethod == MOVE_METHOD_END ? GetSize() + distanceToMove : distanceToMove;
        if (toMove >= 0) {
            BLOCK_100MSEC val;
            val.pos = static_cast<DWORD>(min(toMove / 188, MAXDWORD));
            auto it = std::upper_bound(m_blockList.begin(), m_blockList.end(), val,
                                       [](const BLOCK_100MSEC &a, const BLOCK_100MSEC &b) { return a.pos < b.pos; }) - 1;
            if (it != m_blockInfo) {
                m_blockCache.clear();
                m_blockInfo = it;
            }
            m_pointer = toMove - static_cast<__int64>(it->pos) * 188;
            return toMove;
        }
    }
    return -1;
}

__int64 CReadOnlyMpeg4File::GetSize() const
{
    if (m_hFile != INVALID_HANDLE_VALUE) {
        return static_cast<__int64>(m_blockList.back().pos) * 188;
    }
    return -1;
}

bool CReadOnlyMpeg4File::InitializeTable()
{
    std::vector<BYTE> buf;

    if (!ReadSampleTable('0', m_stsoV, m_stszV, m_sttsV, &m_cttsV, buf) ||
        std::find_if(m_stszV.begin(), m_stszV.end(), [](DWORD a) { return a > VIDEO_SAMPLE_MAX; }) != m_stszV.end())
    {
        return false;
    }
    m_timeScaleV = 0;
    if (ReadBox("/moov0/trak0/mdia0/mdhd0", buf) && buf.size() == 24) {
        if (ArrayToDWORD(&buf[0]) == 0) {
            m_timeScaleV = ArrayToDWORD(&buf[12]);
        }
    }
    m_spsPps.clear();
    if (ReadBox("/moov0/trak0/mdia0/minf0/stbl0/stsd0", buf) && buf.size() >= 110) {
        if (ArrayToDWORD(&buf[0]) == 0 &&
            ArrayToDWORD(&buf[4]) == 1 &&
            ArrayToDWORD(&buf[8]) >= 102 && // Sample description size
            ArrayToDWORD(&buf[12]) == 0x61766331 && // Data format == "avc1"
            ArrayToDWORD(&buf[98]) == 0x61766343 && // Type == "avcC" (TODO: Atomとしてパースすべき)
            ArrayToDWORD(&buf[94]) >= 16 &&
            (buf[107] & 0x1F) == 1)
        {
            size_t spsLen = MAKEWORD(buf[109], buf[108]);
            if (112 + spsLen < buf.size() && buf[110 + spsLen] == 1) {
                size_t ppsLen = MAKEWORD(buf[112 + spsLen], buf[111 + spsLen]);
                if (112 + spsLen + ppsLen < buf.size()) {
                    m_spsPps.insert(m_spsPps.end(), 3, 0);
                    m_spsPps.push_back(1);
                    m_spsPps.insert(m_spsPps.end(), buf.begin() + 110, buf.begin() + 110 + spsLen);
                    m_spsPps.insert(m_spsPps.end(), 3, 0);
                    m_spsPps.push_back(1);
                    m_spsPps.insert(m_spsPps.end(), buf.begin() + 113 + spsLen, buf.begin() + 113 + spsLen + ppsLen);
                }
            }
        }
    }
    if (m_timeScaleV == 0 || m_spsPps.empty()) {
        return false;
    }

    if (!ReadSampleTable('1', m_stsoA, m_stszA, m_sttsA, NULL, buf) ||
        std::find_if(m_stszA.begin(), m_stszA.end(), [](DWORD a) { return a > AUDIO_SAMPLE_MAX; }) != m_stszA.end())
    {
        return false;
    }
    m_timeScaleA = 0;
    if (ReadBox("/moov0/trak1/mdia0/mdhd0", buf) && buf.size() == 24) {
        if (ArrayToDWORD(&buf[0]) == 0) {
            m_timeScaleA = ArrayToDWORD(&buf[12]);
        }
    }
    bool fAdtsHeader = false;
    if (ReadBox("/moov0/trak1/mdia0/minf0/stbl0/stsd0", buf) && buf.size() >= 58) {
        if (ArrayToDWORD(&buf[0]) == 0 &&
            ArrayToDWORD(&buf[4]) == 1 &&
            ArrayToDWORD(&buf[8]) >= 50 && // Sample description size
            ArrayToDWORD(&buf[12]) == 0x6D703461 && // Data format == "mp4a"
            ArrayToDWORD(&buf[48]) == 0x65736473 && // Type == "esds" (TODO: Atomとしてパースすべき)
            ArrayToDWORD(&buf[44]) >= 14)
        {
            // 番兵
            buf.push_back(0);
            size_t i = 56;
            if (buf[i] == 0x03) {
                // ES (TODO: 仕様未確認なので想像)
                i += !(buf[i + 1] & 0x80) ? 2 : !(buf[i + 2] & 0x80) ? 3 : !(buf[i + 3] & 0x80) ? 4 : 5;
                i += 3;
                if (i + 1 < buf.size() && buf[i] == 0x04) {
                    // DecoderConfig
                    i += !(buf[i + 1] & 0x80) ? 2 : !(buf[i + 2] & 0x80) ? 3 : !(buf[i + 3] & 0x80) ? 4 : 5;
                    if (i + 4 < buf.size() - 1) {
                        int bufferSize = MAKEWORD(buf[i + 4], buf[i + 3]);
                        i += 13;
                        if (i + 1 < buf.size() && buf[i] == 0x05) {
                            // DecoderSpecificInfo
                            i += !(buf[i + 1] & 0x80) ? 2 : !(buf[i + 2] & 0x80) ? 3 : !(buf[i + 3] & 0x80) ? 4 : 5;
                            if (i + 1 < buf.size() - 1) {
                                int profile = buf[i] >> 3 & 0x03;
                                int freq = (buf[i] << 1 | buf[i + 1] >> 7) & 0x0F;
                                int ch = buf[i + 1] >> 3 & 0x0F;
                                CreateAdtsHeader(m_adtsHeader, profile, freq, ch, bufferSize);
                                fAdtsHeader = true;
                            }
                        }
                    }
                }
            }
        }
    }
    if (m_timeScaleA == 0 || !fAdtsHeader) {
        return false;
    }

    return InitializeBlockList();
}

bool CReadOnlyMpeg4File::ReadSampleTable(char index, std::vector<__int64> &stso, std::vector<DWORD> &stsz,
                                         std::vector<__int64> &stts, std::vector<DWORD> *ctts, std::vector<BYTE> &buf) const
{
    char path[37] = "/moov0/trak?/mdia0/minf0/stbl0/????0";
    path[11] = index;

    std::vector<__int64> stco;
    ::memcpy(&path[31], "co64", 4);
    if (ReadBox(path, buf)) {
        if (buf.size() >= 8) {
            size_t n = ArrayToDWORD(&buf[4]);
            if (ArrayToDWORD(&buf[0]) == 0 && n == (buf.size() - 8) / 8) {
                stco.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    stco.push_back(static_cast<__int64>(ArrayToDWORD(&buf[8 + 8 * i])) << 32 | ArrayToDWORD(&buf[12 + 8 * i]));
                }
            }
        }
    }
    else {
        ::memcpy(&path[31], "stco", 4);
        if (ReadBox(path, buf) && buf.size() >= 8) {
            size_t n = ArrayToDWORD(&buf[4]);
            if (ArrayToDWORD(&buf[0]) == 0 && n == (buf.size() - 8) / 4) {
                stco.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    stco.push_back(ArrayToDWORD(&buf[8 + 4 * i]));
                }
            }
        }
    }
    std::vector<std::pair<DWORD, DWORD>> stsc;
    ::memcpy(&path[31], "stsc", 4);
    if (ReadBox(path, buf) && buf.size() >= 8) {
        size_t n = ArrayToDWORD(&buf[4]);
        if (ArrayToDWORD(&buf[0]) == 0 && n == (buf.size() - 8) / 12) {
            stsc.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                stsc.push_back(std::make_pair(ArrayToDWORD(&buf[8 + 12 * i]) - 1, ArrayToDWORD(&buf[12 + 12 * i])));
                if (i == 0 && stsc[i].first != 0 || i != 0 && stsc[i].first <= stsc[i - 1].first || stsc[i].second == 0) {
                    return false;
                }
            }
        }
    }
    stsz.clear();
    ::memcpy(&path[31], "stsz", 4);
    if (ReadBox(path, buf) && buf.size() >= 12) {
        size_t n = ArrayToDWORD(&buf[8]);
        if (ArrayToDWORD(&buf[0]) == 0 && ArrayToDWORD(&buf[4]) == 0 && n == (buf.size() - 12) / 4) {
            stsz.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                stsz.push_back(ArrayToDWORD(&buf[12 + 4 * i]));
            }
        }
    }
    if (stco.empty() || stsc.empty() || stsz.empty()) {
        return false;
    }

    // 各サンプルのファイル位置を計算
    stso.clear();
    stso.reserve(stsz.size());
    for (size_t i = 0, j = 0; i < stco.size() && stso.size() < stsz.size(); ++i) {
        if (j + 1 < stsc.size() && i >= stsc[j + 1].first) {
            ++j;
        }
        stso.push_back(stco[i]);
        for (size_t k = 1; k < stsc[j].second && stso.size() < stsz.size(); ++k) {
            stso.push_back(stso.back() + stsz[stso.size() - 1]);
        }
    }
    LARGE_INTEGER fileSize;
    if (stso.size() != stsz.size() || !::GetFileSizeEx(m_hFile, &fileSize)) {
        return false;
    }
    // サンプルの総和や範囲がファイルサイズを超えていないかチェック
    __int64 sum = 0;
    for (size_t i = 0; i < stso.size(); ++i) {
        sum += stsz[i];
        if (sum > fileSize.QuadPart || stso[i] + stsz[i] > fileSize.QuadPart) {
            return false;
        }
    }

    stts.clear();
    stts.reserve(stsz.size());
    ::memcpy(&path[31], "stts", 4);
    if (ReadBox(path, buf) && buf.size() >= 8) {
        size_t n = ArrayToDWORD(&buf[4]);
        if (ArrayToDWORD(&buf[0]) == 0 && n == (buf.size() - 8) / 8) {
            if (n == 1 || n == 2 && ArrayToDWORD(&buf[16]) == 1) {
                // 固定レートのため節約
                stts.push_back(-1);
                stts.push_back(ArrayToDWORD(&buf[12]));
            }
            else {
                // STTSは積分済み
                stts.push_back(0);
                for (size_t i = 0; i < n; ++i) {
                    for (DWORD j = 0; j < ArrayToDWORD(&buf[8 + 8 * i]) && stts.size() < stsz.size(); ++j) {
                        stts.push_back(stts.back() + ArrayToDWORD(&buf[12 + 8 * i]));
                    }
                }
            }
        }
    }
    if (stts.empty() || stts[0] < 0 && stts[1] <= 0 || stts[0] >= 0 && stts.size() != stsz.size()) {
        return false;
    }

    if (ctts) {
        ctts->clear();
        ctts->reserve(stsz.size());
        ::memcpy(&path[31], "ctts", 4);
        if (ReadBox(path, buf) && buf.size() >= 8) {
            size_t n = ArrayToDWORD(&buf[4]);
            if (ArrayToDWORD(&buf[0]) == 0 && n == (buf.size() - 8) / 8) {
                for (size_t i = 0; i < n; ++i) {
                    for (DWORD j = 0; j < ArrayToDWORD(&buf[8 + 8 * i]) && ctts->size() < stsz.size(); ++j) {
                        ctts->push_back(ArrayToDWORD(&buf[12 + 8 * i]));
                    }
                }
            }
        }
        if (ctts->size() != stsz.size()) {
            return false;
        }
    }
    return true;
}

bool CReadOnlyMpeg4File::InitializeBlockList()
{
    m_blockList.clear();
    BLOCK_100MSEC block = {};
    size_t indexV = 0;
    size_t indexA = 0;

    for (;;) {
        m_blockList.push_back(block);
        if (indexV >= m_stsoV.size() && indexA >= m_stsoA.size()) {
            break;
        }
        // PAT + NIT + PMT + PCR
        __int64 size = 4;
        if ((m_blockList.size() - 1) % 20 == 0) {
            // TOT
            ++size;
        }
        for (; indexV < m_stsoV.size(); ++indexV) {
            __int64 sampleTime = m_sttsV[0] < 0 ? static_cast<__int64>(indexV) * m_sttsV[1] : m_sttsV[indexV];
            if (sampleTime >= static_cast<__int64>(m_blockList.size()) * m_timeScaleV / 10) {
                break;
            }
            int n = ReadSample(indexV, m_stsoV, m_stszV, NULL);
            if (n > 0) {
                // (AUD or stuffing) + SPS + PPS
                n += 6 + static_cast<int>(m_spsPps.size());
                // PES header
                n += 14;
            }
            for (int i = 0; i < n; i += min(n - i, 184), ++size, ++block.counterV);
        }
        for (; indexA < m_stsoA.size(); ++indexA) {
            __int64 sampleTime = m_sttsA[0] < 0 ? static_cast<__int64>(indexA) * m_sttsA[1] : m_sttsA[indexA];
            if (sampleTime >= static_cast<__int64>(m_blockList.size()) * m_timeScaleA / 10) {
                break;
            }
            int n = ReadSample(indexA, m_stsoA, m_stszA, NULL);
            if (n > 0) {
                // ADTS header
                n += 7;
                // PES header
                n += 14;
            }
            for (int i = 0; i < n; i += min(n - i, 184), ++size, ++block.counterA);
        }
        // NUL
        size = max(size, BLOCK_SIZE_MIN);

        // ブロックサイズの異常をチェック
        if (block.pos + size > 0x7FFFFFFF || size > BLOCK_SIZE_MAX) {
            return false;
        }
        block.pos += static_cast<DWORD>(size);
    }
    return true;
}

bool CReadOnlyMpeg4File::ReadCurrentBlock()
{
    m_blockCache.clear();
    std::vector<BYTE> sample;
    if (m_blockInfo + 1 == m_blockList.end()) {
        return false;
    }
    size_t blockIndex = m_blockInfo - m_blockList.begin();
    BYTE counterV = m_blockInfo->counterV;
    BYTE counterA = m_blockInfo->counterA;
    size_t indexV = m_sttsV[0] < 0 ? static_cast<size_t>((static_cast<__int64>(blockIndex) * m_timeScaleV / 10 + m_sttsV[1] - 1) / m_sttsV[1]) :
        std::lower_bound(m_sttsV.begin(), m_sttsV.end(), static_cast<__int64>(blockIndex) * m_timeScaleV / 10) - m_sttsV.begin();
    size_t indexA = m_sttsA[0] < 0 ? static_cast<size_t>((static_cast<__int64>(blockIndex) * m_timeScaleA / 10 + m_sttsA[1] - 1) / m_sttsA[1]) :
        std::lower_bound(m_sttsA.begin(), m_sttsA.end(), static_cast<__int64>(blockIndex) * m_timeScaleA / 10) - m_sttsA.begin();

    // PAT
    m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
    BYTE *packet = &m_blockCache.back() - 187;
    CreateHeader(packet, 1, 1, blockIndex & 0x0F, 0x0000);
    packet[4] = 0;
    CreatePat(packet + 5, m_tsid, m_sid);

    // NIT
    m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
    packet = & m_blockCache.back() - 187;
    CreateHeader(packet, 1, 1, blockIndex & 0x0F, 0x0010);
    packet[4] = 0;
    CreateNit(packet + 5, m_nid);

    // PMT
    m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
    packet = &m_blockCache.back() - 187;
    CreateHeader(packet, 1, 1, blockIndex & 0x0F, 0x01F0);
    packet[4] = 0;
    CreatePmt(packet + 5, m_sid);

    // PCR
    m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
    packet = &m_blockCache.back() - 187;
    CreateHeader(packet, 0, 2, 0, 0x01FF);
    CreatePcrAdaptation(packet + 4, static_cast<DWORD>(blockIndex) * 4500);

    if (blockIndex % 20 == 0) {
        // TOT
        m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
        packet = &m_blockCache.back() - 187;
        CreateHeader(packet, 1, 1, (blockIndex / 20) & 0x0F, 0x0014);
        packet[4] = 0;
        LARGE_INTEGER li;
        li.QuadPart = m_totStart.QuadPart + 1000000LL * blockIndex;
        FILETIME ft;
        ft.dwLowDateTime = li.LowPart;
        ft.dwHighDateTime = li.HighPart;
        SYSTEMTIME st;
        FileTimeToSystemTime(&ft, &st);
        CreateTot(packet + 5, st);
    }

    for (; indexV < m_stsoV.size(); ++indexV) {
        __int64 sampleTime = m_sttsV[0] < 0 ? static_cast<__int64>(indexV) * m_sttsV[1] : m_sttsV[indexV];
        if (sampleTime >= static_cast<__int64>(blockIndex + 1) * m_timeScaleV / 10) {
            break;
        }
        int n = ReadSample(indexV, m_stsoV, m_stszV, &sample);
        if (n > 0) {
            bool fIdr;
            size_t firstAudTail = NalFileToByte(sample, fIdr);
            // SPS + PPS
            sample.insert(sample.begin() + firstAudTail, m_spsPps.begin(), m_spsPps.end());
            n += static_cast<int>(m_spsPps.size());
            BYTE stuffingSize = 6;
            sample.insert(sample.begin(), stuffingSize, 0xFF);
            n += stuffingSize;
            // AUDがないときは付加しておく(規格に厳密ではない)
            if (firstAudTail == 0) {
                sample[0] = 0;
                sample[1] = 0;
                sample[2] = 0;
                sample[3] = 1;
                sample[4] = 0x09;
                sample[5] = fIdr ? 0x10 : 0x30;
                // 帳尻合わせ
                stuffingSize = 0;
            }
            // PES header
            sample.insert(sample.begin(), 14, 0xFF);
            CreatePesHeader(&sample.front(), 0xE0, 0, static_cast<DWORD>(45000 * (sampleTime + m_cttsV[indexV]) / m_timeScaleV + 22500), stuffingSize);
            n += 14;
        }
        for (int i = 0; i < n; ++counterV) {
            m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
            packet = &m_blockCache.back() - 187;
            CreateHeader(packet, i == 0, n - i < 184 ? 3 : 1, counterV, 0x0100);
            int pos = 4;
            if (n - i < 184) {
                packet[4] = (187 - (n - i + 4)) & 0xFF;
                if (packet[4] > 0) {
                    packet[5] = 0x00;
                }
                pos += packet[4] + 1;
            }
            ::memcpy(packet + pos, &sample[i], 188 - pos);
            i += 188 - pos;
        }
    }

    for (; indexA < m_stsoA.size(); ++indexA) {
        __int64 sampleTime = m_sttsA[0] < 0 ? static_cast<__int64>(indexA) * m_sttsA[1] : m_sttsA[indexA];
        if (sampleTime >= static_cast<__int64>(blockIndex + 1) * m_timeScaleA / 10) {
            break;
        }
        int n = ReadSample(indexA, m_stsoA, m_stszA, &sample);
        if (n > 0) {
            sample.insert(sample.begin(), 14 + 7, 0xFF);
            // ADTS header
            ::memcpy(&sample[14], m_adtsHeader, 7);
            n += 7;
            sample[17] |= n >> 11 & 0x03;
            sample[18] |= n >> 3 & 0xFF;
            sample[19] |= n << 5 & 0xFF;
            // PES header
            CreatePesHeader(&sample.front(), 0xC0, (n + 8) & 0xFFFF, static_cast<DWORD>(45000 * sampleTime / m_timeScaleA + 22500), 0);
            n += 14;
        }
        for (int i = 0; i < n; ++counterA) {
            m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
            packet = &m_blockCache.back() - 187;
            CreateHeader(packet, i == 0, n - i < 184 ? 3 : 1, counterA, 0x0110);
            int pos = 4;
            if (n - i < 184) {
                packet[4] = (187 - (n - i + 4)) & 0xFF;
                if (packet[4] > 0) {
                    packet[5] = 0x00;
                }
                pos += packet[4] + 1;
            }
            ::memcpy(packet + pos, &sample[i], 188 - pos);
            i += 188 - pos;
        }
    }

    // NUL
    while (m_blockCache.size() < ((m_blockInfo + 1)->pos - m_blockInfo->pos) * 188) {
        m_blockCache.insert(m_blockCache.end(), 188, 0x4E);
        CreateHeader(&m_blockCache.back() - 187, 0, 1, 0, 0x1FFF);
    }
    return true;
}

bool CReadOnlyMpeg4File::ReadBox(LPCSTR path, std::vector<BYTE> &data) const
{
    if (path[0] == '/') {
        LARGE_INTEGER toMove = {};
        if (!::SetFilePointerEx(m_hFile, toMove, NULL, FILE_BEGIN)) {
            return false;
        }
        ++path;
    }
    int index = path[4] - '0';
    BYTE head[8];
    DWORD numRead;
    while (::ReadFile(m_hFile, head, 8, &numRead, NULL) && numRead == 8) {
        DWORD boxSize = ArrayToDWORD(head);
        if (boxSize < 8) {
            break;
        }
        if (::memcmp(path, head + 4, 4) == 0 && --index < 0) {
            if (path[5] == '\0') {
                if (boxSize - 8 <= READ_BOX_SIZE_MAX) {
                    data.resize(boxSize - 8);
                    if (boxSize <= 8 || ::ReadFile(m_hFile, &data.front(), boxSize - 8, &numRead, NULL) && numRead == boxSize - 8) {
                        return true;
                    }
                }
                break;
            }
            return ReadBox(path + 6, data);
        }
        LARGE_INTEGER toMove;
        toMove.QuadPart = boxSize - 8;
        if (!::SetFilePointerEx(m_hFile, toMove, NULL, FILE_CURRENT)) {
            break;
        }
    }
    return false;
}

int CReadOnlyMpeg4File::ReadSample(size_t index, const std::vector<__int64> &stso, const std::vector<DWORD> &stsz, std::vector<BYTE> *data) const
{
    if (index >= stso.size()) {
        return -1;
    }
    if (data) {
        data->resize(stsz[index]);
        LARGE_INTEGER toMove;
        toMove.QuadPart = stso[index];
        DWORD numRead;
        if (data->empty() ||
            !::SetFilePointerEx(m_hFile, toMove, NULL, FILE_BEGIN) ||
            !::ReadFile(m_hFile, &data->front(), stsz[index], &numRead, NULL) || numRead != stsz[index]) {
            data->clear();
        }
        return static_cast<int>(data->size());
    }
    return stsz[index];
}

size_t CReadOnlyMpeg4File::CreatePat(BYTE *data, WORD tsid, WORD sid)
{
    data[0] = 0x00;
    data[1] = 0xB0;
    data[2] = 17;
    data[3] = HIBYTE(tsid);
    data[4] = LOBYTE(tsid);
    data[5] = 0xC1;
    data[6] = 0;
    data[7] = 0;
    data[8] = 0x00;
    data[9] = 0x00;
    data[10] = 0xE0;
    data[11] = 0x10;
    data[12] = HIBYTE(sid);
    data[13] = LOBYTE(sid);
    data[14] = 0xE1;
    data[15] = 0xF0; // PMT_PID=0x01F0
    DWORD crc = CalcCrc32(data, 16);
    data[16] = HIBYTE(HIWORD(crc));
    data[17] = LOBYTE(HIWORD(crc));
    data[18] = HIBYTE(crc);
    data[19] = LOBYTE(crc);
    return 20;
}

size_t CReadOnlyMpeg4File::CreateNit(BYTE *data, WORD nid)
{
    data[0] = 0x40;
    data[1] = 0xF0;
    data[2] = 13;
    data[3] = HIBYTE(nid);
    data[4] = LOBYTE(nid);
    data[5] = 0xC1;
    data[6] = 0;
    data[7] = 0;
    data[8] = 0xF0;
    data[9] = 0;
    data[10] = 0xF0;
    data[11] = 0;
    DWORD crc = CalcCrc32(data, 12);
    data[12] = HIBYTE(HIWORD(crc));
    data[13] = LOBYTE(HIWORD(crc));
    data[14] = HIBYTE(crc);
    data[15] = LOBYTE(crc);
    return 16;
}

size_t CReadOnlyMpeg4File::CreateTot(BYTE *data, SYSTEMTIME st)
{
    data[0] = 0x73;
    data[1] = 0x70;
    data[2] = 11;
    data[5] = ((st.wHour / 10) << 4 | (st.wHour % 10)) & 0xFF;
    data[6] = ((st.wMinute / 10) << 4 | (st.wMinute % 10)) & 0xFF;
    data[7] = ((st.wSecond / 10) << 4 | (st.wSecond % 10)) & 0xFF;
    st.wHour = st.wMinute = st.wSecond = 0;
    FILETIME ft;
    ::SystemTimeToFileTime(&st, &ft);
    LARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    __int64 mjd = (li.QuadPart - 81377568000000000LL) / (24 * 60 * 60 * 10000000LL);
    data[3] = HIBYTE(mjd);
    data[4] = LOBYTE(mjd);
    data[8] = 0xF0;
    data[9] = 0;
    DWORD crc = CalcCrc32(data, 10);
    data[10] = HIBYTE(HIWORD(crc));
    data[11] = LOBYTE(HIWORD(crc));
    data[12] = HIBYTE(crc);
    data[13] = LOBYTE(crc);
    return 14;
}

size_t CReadOnlyMpeg4File::CreatePmt(BYTE *data, WORD sid)
{
    data[0] = 0x02;
    data[1] = 0xB0;
    data[2] = 23;
    data[3] = HIBYTE(sid);
    data[4] = LOBYTE(sid);
    data[5] = 0xC1;
    data[6] = 0;
    data[7] = 0;
    data[8] = 0xE1;
    data[9] = 0xFF; // PCR_PID=0x01FF
    data[10] = 0xF0;
    data[11] = 0;
    data[12] = 0x1B; // AVC video
    data[13] = 0xE1;
    data[14] = 0x00; // 0x0100
    data[15] = 0xF0;
    data[16] = 0;
    data[17] = 0x0F; // ADTS transport
    data[18] = 0xE1;
    data[19] = 0x10; // 0x0110
    data[20] = 0xF0;
    data[21] = 0;
    DWORD crc = CalcCrc32(data, 22);
    data[22] = HIBYTE(HIWORD(crc));
    data[23] = LOBYTE(HIWORD(crc));
    data[24] = HIBYTE(crc);
    data[25] = LOBYTE(crc);
    return 26;
}

size_t CReadOnlyMpeg4File::CreateHeader(BYTE *data, BYTE unitStart, BYTE adaptation, BYTE counter, WORD pid)
{
    data[0] = 0x47;
    data[1] = (unitStart << 6 | HIBYTE(pid)) & 0xFF;
    data[2] = LOBYTE(pid);
    data[3] = (adaptation << 4 | counter & 0x0F) & 0xFF;
    return 4;
}

size_t CReadOnlyMpeg4File::CreatePcrAdaptation(BYTE *data, DWORD pcr45khz)
{
    data[0] = 183;
    data[1] = 0x10;
    data[2] = HIBYTE(HIWORD(pcr45khz));
    data[3] = LOBYTE(HIWORD(pcr45khz));
    data[4] = HIBYTE(pcr45khz);
    data[5] = LOBYTE(pcr45khz);
    data[6] = 0x7E;
    data[7] = 0;
    return 8;
}

size_t CReadOnlyMpeg4File::CreatePesHeader(BYTE *data, BYTE streamID, WORD packetLength, DWORD pts45khz, BYTE stuffingSize)
{
    data[0] = 0;
    data[1] = 0;
    data[2] = 1;
    data[3] = streamID;
    data[4] = HIBYTE(packetLength);
    data[5] = LOBYTE(packetLength);
    data[6] = 0x84;
    data[7] = 0x80;
    data[8] = (5 + stuffingSize) & 0xFF;
    data[9] = (pts45khz >> 28 | 0x21) & 0xFF;
    data[10] = pts45khz >> 21 & 0xFF;
    data[11] = (pts45khz >> 13 | 0x01) & 0xFF;
    data[12] = pts45khz >> 6 & 0xFF;
    data[13] = (pts45khz << 2 | 0x01) & 0xFF;
    return 14;
}

size_t CReadOnlyMpeg4File::CreateAdtsHeader(BYTE *data, int profile, int freq, int ch, int bufferSize)
{
    data[0] = 0xFF;
    data[1] = 0xF9;
    data[2] = ((profile - 1) << 6 | freq << 2 | ch >> 2) & 0xFF;
    data[3] = ch << 6 & 0xFF;
    data[4] = 0x00;
    data[5] = bufferSize >> 6 & 0x1F;
    data[6] = bufferSize << 2 & 0xFF;
    return 7;
}

size_t CReadOnlyMpeg4File::NalFileToByte(std::vector<BYTE> &data, bool &fIdr)
{
    fIdr = false;
    size_t firstAudTail = 0;
    for (size_t i = 0; i + 3 < data.size(); ) {
        DWORD len = ArrayToDWORD(&data[i]);
        data[i++] = 0;
        data[i++] = 0;
        data[i++] = 0;
        data[i++] = 1;
        if (len > data.size() - i) {
            break;
        }
        if (len >= 1 && (data[i] & 0x1F) == 0x05) {
            fIdr = true;
        }
        if (firstAudTail == 0 && len >= 1 && (data[i] & 0x1F) == 0x09) {
            firstAudTail = i + len;
        }
        i += len;
    }
    return firstAudTail;
}

DWORD CReadOnlyMpeg4File::CalcCrc32(const BYTE *data, size_t len, DWORD crc)
{
    for (size_t i = 0; i < len; ++i) {
        DWORD c = ((crc >> 24) ^ data[i]) << 24;
        for (int j = 0; j < 8; ++j) {
            c = (c << 1) ^ (c & 0x80000000 ? 0x04C11DB7 : 0);
        }
        crc = (crc << 8) ^ c;
    }
    return crc;
}
