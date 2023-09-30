﻿#include <Windows.h>
#include <Shlwapi.h>
#include <algorithm>
#include "Util.h"
#include "B24CaptionUtil.h"
#include "ReadOnlyMpeg4File.h"

extern HINSTANCE g_hinstDLL;

bool CReadOnlyMpeg4File::Open(LPCTSTR path, int flags, LPCTSTR &errorMessage)
{
    Close();
    if ((flags & OPEN_FLAG_NORMAL) && !(flags & OPEN_FLAG_SHARE_WRITE) && LoadSettings()) {
        m_hFile = ::CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_hFile != INVALID_HANDLE_VALUE) {
            LoadCaption(path);
            OpenPsiData(path);
            if (InitializeTable(errorMessage)) {
                InitializeMetaInfo(path);
                m_blockInfo = m_blockList.begin();
                m_blockCache.clear();
                m_pointer = 0;
                return true;
            }
            Close();
        }
    }
    return false;
}

void CReadOnlyMpeg4File::Close()
{
    m_psiDataReader.Close();
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

int CReadOnlyMpeg4File::GetPositionMsecFromBytes(__int64 posBytes) const
{
    if (m_hFile != INVALID_HANDLE_VALUE && posBytes >= 0) {
        BLOCK_100MSEC val;
        val.pos = static_cast<DWORD>(min(posBytes / 188, MAXDWORD));
        auto it = std::upper_bound(m_blockList.begin(), m_blockList.end(), val,
                                   [](const BLOCK_100MSEC &a, const BLOCK_100MSEC &b) { return a.pos < b.pos; }) - 1;
        return static_cast<int>(it - m_blockList.begin()) * 100;
    }
    return -1;
}

__int64 CReadOnlyMpeg4File::GetPositionBytesFromMsec(int msec) const
{
    if (m_hFile != INVALID_HANDLE_VALUE && msec >= 0) {
        int index = min(msec / 100, static_cast<int>(m_blockList.size() - 1));
        return static_cast<__int64>(m_blockList[index].pos) * 188;
    }
    return -1;
}

bool CReadOnlyMpeg4File::LoadSettings()
{
    TCHAR iniPath[MAX_PATH];
    if (!::GetModuleFileName(g_hinstDLL, iniPath, _countof(iniPath)) ||
        !::PathRenameExtension(iniPath, TEXT(".ini")))
    {
        return false;
    }
    // プラグイン設定の[MP4]セクション
    std::vector<TCHAR> buf = GetPrivateProfileSectionBuffer(TEXT("MP4"), iniPath);
    GetBufferedProfileString(buf.data(), TEXT("Meta"), TEXT("metadata.ini"), m_metaName, _countof(m_metaName));
    GetBufferedProfileString(buf.data(), TEXT("VttExtension"), TEXT(".vtt"), m_vttExtension, _countof(m_vttExtension));
    GetBufferedProfileString(buf.data(), TEXT("PsiDataExtension"), TEXT(".psc"), m_psiDataExtension, _countof(m_psiDataExtension));
    GetBufferedProfileString(buf.data(), TEXT("BroadcastID"), TEXT("0x000100020003"), m_iniBroadcastID, _countof(m_iniBroadcastID));
    GetBufferedProfileString(buf.data(), TEXT("Time"), TEXT(""), m_iniTime, _countof(m_iniTime));
    if (!buf[0]) {
        WritePrivateProfileInt(TEXT("MP4"), TEXT("Enabled"), 1, iniPath);
        ::WritePrivateProfileString(TEXT("MP4"), TEXT("Meta"), m_metaName, iniPath);
        ::WritePrivateProfileString(TEXT("MP4"), TEXT("VttExtension"), m_vttExtension, iniPath);
        ::WritePrivateProfileString(TEXT("MP4"), TEXT("PsiDataExtension"), m_psiDataExtension, iniPath);
        ::WritePrivateProfileString(TEXT("MP4"), TEXT("BroadcastID"), m_iniBroadcastID, iniPath);
        ::WritePrivateProfileString(TEXT("MP4"), TEXT("Time"), TEXT(""), iniPath);
    }
    return GetBufferedProfileInt(buf.data(), TEXT("Enabled"), 1) != 0;
}

void CReadOnlyMpeg4File::InitializeMetaInfo(LPCTSTR path)
{
    if (m_metaName[0] && _tcslen(path) < MAX_PATH) {
        TCHAR metaPath[MAX_PATH];
        _tcscpy_s(metaPath, path);
        if (::PathRemoveFileSpec(metaPath) && ::PathAppend(metaPath, m_metaName)) {
            // "Meta"設定の[*]セクションで上書き
            std::vector<TCHAR> buf = GetPrivateProfileSectionBuffer(TEXT("*"), metaPath);
            TCHAR szBroadcastID[15];
            GetBufferedProfileString(buf.data(), TEXT("BroadcastID"), m_iniBroadcastID, szBroadcastID, _countof(szBroadcastID));
            TCHAR szTime[20];
            GetBufferedProfileString(buf.data(), TEXT("Time"), m_iniTime, szTime, _countof(szTime));
            // "Meta"設定の[ファイル名]セクションで上書き
            buf = GetPrivateProfileSectionBuffer(::PathFindFileName(path), metaPath);
            GetBufferedProfileString(buf.data(), TEXT("BroadcastID"), szBroadcastID, m_iniBroadcastID, _countof(m_iniBroadcastID));
            GetBufferedProfileString(buf.data(), TEXT("Time"), szTime, m_iniTime, _countof(m_iniTime));
        }
    }
    LARGE_INTEGER broadcastID = {};
    ::StrToInt64Ex(m_iniBroadcastID, STIF_SUPPORT_HEX, &broadcastID.QuadPart);
    m_nid = max(LOWORD(broadcastID.HighPart), 1);
    m_tsid = max(HIWORD(broadcastID.LowPart), 1);
    m_sid = max(LOWORD(broadcastID.LowPart), 1);
    m_totStart.QuadPart = 125911908000000000LL; // 2000-01-01T09:00:00
    if (!m_iniTime[0]) {
        // ファイルの更新日時をTOTとする
        FILETIME ft;
        if (::GetFileTime(m_hFile, nullptr, nullptr, &ft)) {
            m_totStart.LowPart = ft.dwLowDateTime;
            m_totStart.HighPart = ft.dwHighDateTime;
            m_totStart.QuadPart += 9 * 36000000000LL;
        }
    }
    else if (_tcslen(m_iniTime) == 19 &&
             m_iniTime[4] == TEXT('-') && m_iniTime[7] == TEXT('-') &&
             m_iniTime[10] == TEXT('T') && m_iniTime[13] == TEXT(':') && m_iniTime[16] == TEXT(':'))
    {
        SYSTEMTIME st = {};
        st.wYear = static_cast<WORD>(_tcstol(&m_iniTime[0], nullptr, 10));
        st.wMonth = static_cast<WORD>(_tcstol(&m_iniTime[5], nullptr, 10));
        st.wDay = static_cast<WORD>(_tcstol(&m_iniTime[8], nullptr, 10));
        st.wHour = static_cast<WORD>(_tcstol(&m_iniTime[11], nullptr, 10));
        st.wMinute = static_cast<WORD>(_tcstol(&m_iniTime[14], nullptr, 10));
        st.wSecond = static_cast<WORD>(_tcstol(&m_iniTime[17], nullptr, 10));
        FILETIME ft;
        if (::SystemTimeToFileTime(&st, &ft)) {
            m_totStart.LowPart = ft.dwLowDateTime;
            m_totStart.HighPart = ft.dwHighDateTime;
        }
    }
}

void CReadOnlyMpeg4File::LoadCaption(LPCTSTR path)
{
    m_captionList.clear();

    TCHAR vttPath[MAX_PATH];
    if (!m_vttExtension[0] || _tcslen(path) >= _countof(vttPath)) {
        return;
    }
    _tcscpy_s(vttPath, path);
    if (!::PathRenameExtension(vttPath, m_vttExtension)) {
        return;
    }
    LoadWebVttB24Caption(vttPath, m_captionList);
}

void CReadOnlyMpeg4File::OpenPsiData(LPCTSTR path)
{
    TCHAR psiDataPath[MAX_PATH];
    if (!m_psiDataExtension[0] || _tcslen(path) >= _countof(psiDataPath)) {
        return;
    }
    _tcscpy_s(psiDataPath, path);
    if (!::PathRenameExtension(psiDataPath, m_psiDataExtension)) {
        return;
    }
    m_psiDataReader.Open(psiDataPath);
}

bool CReadOnlyMpeg4File::InitializeTable(LPCTSTR &errorMessage)
{
    std::vector<BYTE> buf;
    m_stsoV.clear();
    m_stsoA[0].clear();
    m_stsoA[1].clear();
    char path[] = "/moov0/trak0/mdia0/mdhd0";
    for (char i = '0'; i <= '9'; ++i, ++path[11]) {
        DWORD timeScale = 0;
        if (ReadBox(path, buf) >= 24) {
            if ((ArrayToDWORD(&buf[0]) & 0xFEFFFFFF) == 0) {
                timeScale = ArrayToDWORD(&buf[buf[0] ? 20 : 12]);
            }
        }
        if (timeScale != 0) {
            __int64 editTimeOffset;
            if (m_stsoV.empty() && ReadVideoSampleDesc(i, m_fHevc, m_spsPps, buf)) {
                m_timeScaleV = timeScale;
                if (ReadSampleTable(i, m_stsoV, m_stszV, m_sttsV, &m_cttsV, editTimeOffset, buf) &&
                    std::find_if(m_stszV.begin(), m_stszV.end(), [](DWORD a) { return a > VIDEO_SAMPLE_MAX; }) == m_stszV.end()) {
                    // 0.5秒の範囲でPTSを利用してエディットリストの内容を反映する
                    m_offsetPtsV = static_cast<DWORD>(22500 + min(max(editTimeOffset * 45000 / m_timeScaleV, 0), 22500));
                }
                else {
                    m_stsoV.clear();
                }
            }
            else if (m_stsoA[0].empty() && ReadAudioSampleDesc(i, m_adtsHeader[0], buf)) {
                m_timeScaleA[0] = timeScale;
                if (ReadSampleTable(i, m_stsoA[0], m_stszA[0], m_sttsA[0], nullptr, editTimeOffset, buf) &&
                    std::find_if(m_stszA[0].begin(), m_stszA[0].end(), [](DWORD a) { return a > AUDIO_SAMPLE_MAX; }) == m_stszA[0].end()) {
                    m_offsetPtsA[0] = static_cast<DWORD>(22500 + min(max(editTimeOffset * 45000 / m_timeScaleA[0], 0), 22500));
                }
                else {
                    m_stsoA[0].clear();
                }
            }
            else if (m_stsoA[1].empty() && !m_stsoA[0].empty() && ReadAudioSampleDesc(i, m_adtsHeader[1], buf)) {
                m_timeScaleA[1] = timeScale;
                if (ReadSampleTable(i, m_stsoA[1], m_stszA[1], m_sttsA[1], nullptr, editTimeOffset, buf) &&
                    std::find_if(m_stszA[1].begin(), m_stszA[1].end(), [](DWORD a) { return a > AUDIO_SAMPLE_MAX; }) == m_stszA[1].end()) {
                    m_offsetPtsA[1] = static_cast<DWORD>(22500 + min(max(editTimeOffset * 45000 / m_timeScaleA[1], 0), 22500));
                }
                else {
                    m_stsoA[1].clear();
                }
            }
        }
    }
    if (m_stsoV.empty()) {
        errorMessage = TEXT("CReadOnlyMpeg4File: No video track");
        return false;
    }
    // 音声2は必須でない
    if (m_stsoA[0].empty()) {
        errorMessage = TEXT("CReadOnlyMpeg4File: No audio track");
        return false;
    }
    return InitializeBlockList(errorMessage);
}

bool CReadOnlyMpeg4File::ReadVideoSampleDesc(char index, bool &fHevc, std::vector<BYTE> &spsPps, std::vector<BYTE> &buf) const
{
    const DWORD DATA_FORMAT_AVC1 = 0x61766331;
    const DWORD DATA_FORMAT_HVC1 = 0x68766331;
    char path[] = "/moov0/trak?/mdia0/minf0/stbl0/stsd0";
    path[11] = index;
    if (ReadBox(path, buf) >= 16) {
        size_t boxLen = ArrayToDWORD(&buf[8]);
        DWORD dataFormat = ArrayToDWORD(&buf[12]);
        // TODO: "hev1"は未対応
        if (ArrayToDWORD(&buf[0]) == 0 &&
            ArrayToDWORD(&buf[4]) == 1 &&
            (dataFormat == DATA_FORMAT_AVC1 || dataFormat == DATA_FORMAT_HVC1) &&
            boxLen >= 86 && // Sample description size
            boxLen <= buf.size() - 8)
        {
            buf.resize(boxLen + 8);
            buf.erase(buf.begin(), buf.begin() + 94);
            for (; buf.size() >= 8; buf.erase(buf.begin(), buf.begin() + boxLen)) {
                boxLen = ArrayToDWORD(&buf[0]);
                if (boxLen < 8 || boxLen > buf.size()) {
                    break;
                }
                // TODO: PAR情報は無視
                // Type == "avcC"
                if (ArrayToDWORD(&buf[4]) == 0x61766343 && dataFormat == DATA_FORMAT_AVC1) {
                    buf.resize(boxLen);
                    if (15 < buf.size() && (buf[13] & 0x1F) == 1) {
                        size_t spsLen = MAKEWORD(buf[15], buf[14]);
                        if (18 + spsLen < buf.size() && buf[16 + spsLen] == 1) {
                            size_t ppsLen = MAKEWORD(buf[18 + spsLen], buf[17 + spsLen]);
                            if (18 + spsLen + ppsLen < buf.size()) {
                                spsPps.assign(3, 0);
                                spsPps.push_back(1);
                                spsPps.insert(spsPps.end(), buf.begin() + 16, buf.begin() + 16 + spsLen);
#if MASK_OFF_SPS_CS45_FLAGS != 0
                                if (spsPps.size() >= 7 && (spsPps[6] & 0x0C) != 0) {
                                    spsPps[6] &= 0xF3;
                                    OutputDebugString(TEXT("CReadOnlyMpeg4File::ReadVideoSampleDesc(): Masked SPS constraint_set[45]_flags off\n"));
                                }
#endif
                                spsPps.insert(spsPps.end(), 3, 0);
                                spsPps.push_back(1);
                                spsPps.insert(spsPps.end(), buf.begin() + 19 + spsLen, buf.begin() + 19 + spsLen + ppsLen);
                                fHevc = false;
                                return true;
                            }
                        }
                    }
                    break;
                }
                // Type == "hvcC"
                else if (ArrayToDWORD(&buf[4]) == 0x68766343 && dataFormat == DATA_FORMAT_HVC1) {
                    buf.resize(boxLen);
                    if (30 < buf.size()) {
                        int numArray = buf[30];
                        static const int NALU_TYPES[] = { 0x20, 0x21, 0x22 }; // VPS, SPS, PPS
                        spsPps.clear();
                        bool fFoundAll = true;
                        for (size_t naluTypeIndex = 0; fFoundAll && naluTypeIndex < _countof(NALU_TYPES); ++naluTypeIndex) {
                            fFoundAll = false;
                            size_t pos = 31;
                            for (int i = 0; !fFoundAll && i < numArray && pos + 2 < buf.size(); ++i) {
                                int naluType = buf[pos] & 0x3F;
                                int numNalu = MAKEWORD(buf[pos + 2], buf[pos + 1]);
                                pos += 3;
                                for (int j = 0; j < numNalu && pos + 1 < buf.size(); ++j) {
                                    size_t naluLen = MAKEWORD(buf[pos + 1], buf[pos]);
                                    pos += 2;
                                    if (naluType == NALU_TYPES[naluTypeIndex] && numNalu == 1 && pos + naluLen <= buf.size()) {
                                        spsPps.insert(spsPps.end(), 3, 0);
                                        spsPps.push_back(1);
                                        spsPps.insert(spsPps.end(), buf.begin() + pos, buf.begin() + pos + naluLen);
                                        fFoundAll = true;
                                        break;
                                    }
                                    pos += naluLen;
                                }
                            }
                        }
                        if (fFoundAll) {
                            fHevc = true;
                            return true;
                        }
                    }
                    break;
                }
            }
        }
    }
    return false;
}

bool CReadOnlyMpeg4File::ReadAudioSampleDesc(char index, BYTE *adtsHeader, std::vector<BYTE> &buf) const
{
    char path[] = "/moov0/trak?/mdia0/minf0/stbl0/stsd0";
    path[11] = index;
    if (ReadBox(path, buf) >= 16) {
        size_t boxLen = ArrayToDWORD(&buf[8]);
        if (ArrayToDWORD(&buf[0]) == 0 &&
            ArrayToDWORD(&buf[4]) == 1 &&
            ArrayToDWORD(&buf[12]) == 0x6D703461 && // Data format == "mp4a"
            boxLen >= 36 && // Sample description size
            boxLen <= buf.size() - 8)
        {
            buf.resize(boxLen + 8);
            buf.erase(buf.begin(), buf.begin() + 44);
            for (; buf.size() >= 8; buf.erase(buf.begin(), buf.begin() + boxLen)) {
                boxLen = ArrayToDWORD(&buf[0]);
                if (boxLen < 8 || boxLen > buf.size()) {
                    break;
                }
                // Type == "esds"
                if (ArrayToDWORD(&buf[4]) == 0x65736473) {
                    buf.resize(boxLen);
                    // 番兵
                    buf.push_back(0);
                    size_t i = 12;
                    if (i + 1 < buf.size() && buf[i] == 0x03) {
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
                                        CreateAdtsHeader(adtsHeader, profile, freq, ch, bufferSize);
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
    return false;
}

bool CReadOnlyMpeg4File::ReadSampleTable(char index, std::vector<__int64> &stso, std::vector<DWORD> &stsz,
                                         std::vector<__int64> &stts, std::vector<DWORD> *ctts, __int64 &editTimeOffset, std::vector<BYTE> &buf) const
{
    char path[37] = "/moov0/trak?/mdia0/minf0/stbl0/????0";
    path[11] = index;

    std::vector<__int64> stco;
    ::memcpy(&path[31], "co64", 4);
    if (ReadBox(path, buf) >= 0) {
        if (buf.size() >= 8) {
            size_t n = ArrayToDWORD(&buf[4]);
            if (ArrayToDWORD(&buf[0]) == 0 && n == (buf.size() - 8) / 8) {
                stco.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    stco.push_back(ArrayToInt64(&buf[8 + 8 * i]));
                }
            }
        }
    }
    else {
        ::memcpy(&path[31], "stco", 4);
        if (ReadBox(path, buf) >= 8) {
            size_t n = ArrayToDWORD(&buf[4]);
            if (ArrayToDWORD(&buf[0]) == 0 && n == (buf.size() - 8) / 4) {
                stco.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    stco.push_back(ArrayToDWORD(&buf[8 + 4 * i]));
                }
            }
        }
    }
    stsz.clear();
    ::memcpy(&path[31], "stsz", 4);
    if (ReadBox(path, buf) >= 12) {
        size_t n = ArrayToDWORD(&buf[8]);
        if (ArrayToDWORD(&buf[0]) == 0 && ArrayToDWORD(&buf[4]) == 0 && n == (buf.size() - 12) / 4) {
            stsz.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                stsz.push_back(ArrayToDWORD(&buf[12 + 4 * i]));
            }
        }
    }
    if (stco.empty() || stsz.empty()) {
        return false;
    }

    // 各サンプルのファイル位置を計算
    stso.clear();
    stso.reserve(stsz.size());
    ::memcpy(&path[31], "stsc", 4);
    if (ReadBox(path, buf) < 8) {
        return false;
    }
    size_t stscNum = ArrayToDWORD(&buf[4]);
    if (ArrayToDWORD(&buf[0]) != 0 || stscNum != (buf.size() - 8) / 12) {
        return false;
    }
    for (size_t i = 0; i < stscNum; ++i) {
        DWORD currChunk = ArrayToDWORD(&buf[8 + 12 * i]) - 1;
        DWORD sampleNum = ArrayToDWORD(&buf[12 + 12 * i]);
        DWORD nextChunk = i + 1 < stscNum ? ArrayToDWORD(&buf[8 + 12 * (i + 1)]) - 1 : MAXDWORD;
        if (nextChunk <= currChunk || sampleNum == 0) {
            return false;
        }
        for (size_t j = currChunk; j < nextChunk && j < stco.size() && stso.size() < stsz.size(); ++j) {
            stso.push_back(stco[j]);
            for (size_t k = 1; k < sampleNum && stso.size() < stsz.size(); ++k) {
                stso.push_back(stso.back() + stsz[stso.size() - 1]);
            }
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
    if (ReadBox(path, buf) >= 8) {
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
        if (ReadBox(path, buf) < 0) {
            // out-of-orderなし
            ctts->resize(stsz.size(), 0);
        }
        else if (buf.size() >= 8) {
            size_t n = ArrayToDWORD(&buf[4]);
            if (ArrayToDWORD(&buf[0]) == 0 && n == (buf.size() - 8) / 8) {
                for (size_t i = 0; i < n; ++i) {
                    for (DWORD j = 0; j < ArrayToDWORD(&buf[8 + 8 * i]) && ctts->size() < stsz.size(); ++j) {
                        ctts->push_back(ArrayToDWORD(&buf[12 + 8 * i]));
                        if (ctts->back() >= 0x80000000) {
                            // TODO: 負のオフセット
                            return false;
                        }
                    }
                }
            }
        }
        if (ctts->size() != stsz.size()) {
            return false;
        }
    }

    editTimeOffset = 0;
    char elstPath[] = "/moov0/trak?/edts0/elst0";
    elstPath[11] = index;
    if (ReadBox(elstPath, buf) >= 8) {
        DWORD verFlags = ArrayToDWORD(&buf[0]);
        size_t n = ArrayToDWORD(&buf[4]);
        if ((verFlags & 0xFEFFFFFF) == 0 && n == 1 && (verFlags ? 28U : 20U) == buf.size()) {
            // 最もシンプルなエディットリストのみ対応
            __int64 segmentDuration;
            __int64 mediaTime;
            DWORD mediaRate;
            if (verFlags) {
                segmentDuration = ArrayToInt64(&buf[8]);
                mediaTime = ArrayToInt64(&buf[16]);
                mediaRate = ArrayToDWORD(&buf[24]);
            }
            else {
                segmentDuration = ArrayToDWORD(&buf[8]);
                mediaTime = ArrayToDWORD(&buf[12]);
                if (mediaTime >= 0x80000000) {
                    mediaTime = -1;
                }
                mediaRate = ArrayToDWORD(&buf[16]);
            }
            if (segmentDuration > 0 && mediaTime >= 0 && mediaRate == 0x10000) {
                // 遅延分を加える
                editTimeOffset += mediaTime;
            }
        }
    }
    return true;
}

bool CReadOnlyMpeg4File::InitializeBlockList(LPCTSTR &errorMessage)
{
    m_blockList.clear();
    BLOCK_100MSEC block = {};
    size_t indexV = 0;
    size_t indexA[2] = {};
    auto itCaption = m_captionList.cbegin();

    while (m_blockList.size() < BLOCK_LIST_SIZE_MAX) {
        m_blockList.push_back(block);
        if (indexV >= m_stsoV.size() && indexA[0] >= m_stsoA[0].size() && indexA[1] >= m_stsoA[1].size()) {
            break;
        }
        __int64 size;
        if (m_psiDataReader.IsOpen()) {
            // PAT + PCR + PMT
            size = 2 + 16;
        }
        else {
            // PAT + NIT + SDT + PMT + PCR
            size = 5;
            if ((m_blockList.size() - 1) % 10 == 9) {
                // EIT
                ++size;
            }
            if ((m_blockList.size() - 1) % 20 == 0) {
                // TOT
                ++size;
            }
        }
        for (; indexV < m_stsoV.size(); ++indexV) {
            __int64 sampleTime = m_sttsV[0] < 0 ? static_cast<__int64>(indexV) * m_sttsV[1] : m_sttsV[indexV];
            if (sampleTime >= static_cast<__int64>(m_blockList.size()) * m_timeScaleV / 10) {
                break;
            }
            int n = ReadSample(indexV, m_stsoV, m_stszV, nullptr);
            if (n > 0) {
                // (AUD or stuffing) + SPS + PPS
                n += (m_fHevc ? 7 : 6) + static_cast<int>(m_spsPps.size());
                // PES header
                n += 14;
            }
            for (; n > 0; n -= 184, ++size, ++block.counterV);
        }
        for (int a = 0; a < 2; ++a) {
            for (; indexA[a] < m_stsoA[a].size(); ++indexA[a]) {
                __int64 sampleTime = m_sttsA[a][0] < 0 ? static_cast<__int64>(indexA[a]) * m_sttsA[a][1] : m_sttsA[a][indexA[a]];
                if (sampleTime >= static_cast<__int64>(m_blockList.size()) * m_timeScaleA[a] / 10) {
                    break;
                }
                int n = ReadSample(indexA[a], m_stsoA[a], m_stszA[a], nullptr);
                if (n > 0) {
                    // ADTS header
                    n += 7;
                    // PES header
                    n += 14;
                }
                for (; n > 0; n -= 184, ++size, ++block.counterA[a]);
            }
        }

        // 放送では字幕は映像や音声にたいして早めに送られるのでそれに似せる
        __int64 captionEndTime = static_cast<__int64>(m_blockList.size()) * 100 + CAPTION_FORWARD_MSEC;
        for (; itCaption != m_captionList.end() && itCaption->first < captionEndTime; ++itCaption) {
            int n = static_cast<int>(itCaption->second.size());
            // 同期型PES(STD-B24)の先頭 + サイズフィールド + CRC16 + PES header
            n += 3 + 2 + 2 + 14;
            for (; n > 0; n -= 184, ++size, ++block.counterC);
        }

        // ブロックサイズの異常をチェック
        if (size > BLOCK_SIZE_MAX) {
            return false;
        }
        m_blockList.back().pos = static_cast<DWORD>(size);
    }

    if (!InitializePsiCounterInfo(errorMessage)) {
        return false;
    }

    // posを積分してサイズから位置に変換
    DWORD nextPos = 0;
    for (auto it = m_blockList.begin(); it != m_blockList.end(); ++it) {
        DWORD pos = nextPos;
        nextPos += it->pos;
        if (nextPos > 0x7FFFFFFF) {
            return false;
        }
        it->pos = pos;
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
    BYTE counterA[2] = { m_blockInfo->counterA[0], m_blockInfo->counterA[1] };
    BYTE counterC = m_blockInfo->counterC;
    size_t indexV = m_sttsV[0] < 0 ? static_cast<size_t>((static_cast<__int64>(blockIndex) * m_timeScaleV / 10 + m_sttsV[1] - 1) / m_sttsV[1]) :
        std::lower_bound(m_sttsV.begin(), m_sttsV.end(), static_cast<__int64>(blockIndex) * m_timeScaleV / 10) - m_sttsV.begin();
    size_t indexA[2] = {};
    for (int a = 0; a < 2 && !m_stsoA[a].empty(); ++a) {
        indexA[a] = m_sttsA[a][0] < 0 ? static_cast<size_t>((static_cast<__int64>(blockIndex) * m_timeScaleA[a] / 10 + m_sttsA[a][1] - 1) / m_sttsA[a][1]) :
            std::lower_bound(m_sttsA[a].begin(), m_sttsA[a].end(), static_cast<__int64>(blockIndex) * m_timeScaleA[a] / 10) - m_sttsA[a].begin();
    }
    auto itCaption = std::lower_bound(m_captionList.cbegin(), m_captionList.cend(),
        std::make_pair(static_cast<__int64>(blockIndex) * 100 + CAPTION_FORWARD_MSEC, std::vector<BYTE>()),
        [](const std::pair<__int64, std::vector<BYTE>> &a, const std::pair<__int64, std::vector<BYTE>> &b) { return a.first < b.first; });

    if (m_psiDataReader.IsOpen()) {
        // PSI/SIをマージ
        bool fPatAdded = false;
        bool fPmtAdded = false;
        WORD pmtPid = 0;
        for (auto it = m_psiCounterInfoMap.begin(); it != m_psiCounterInfoMap.end(); ++it) {
            // 事前に計算された連続性指標の開始値
            it->second.currentCounter = (it->second.counterList[blockIndex / 2] >> ((1 - blockIndex % 2) * 4)) & 0x0F;
        }
        m_psiDataReader.Read(static_cast<int>(blockIndex * 100), static_cast<int>((blockIndex + 1) * 100),
                             [this, blockIndex, &fPatAdded, &fPmtAdded, &pmtPid](const std::vector<BYTE> &psi, WORD pid) {
            if (pid == 0) {
                // PAT
                if (!fPatAdded) {
                    m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
                    BYTE *packet = &m_blockCache.back() - 187;
                    CreateHeader(packet, 1, 1, blockIndex & 0x0F, 0x0000);
                    packet[4] = 0;
                    fPatAdded = CreatePatFromPat(packet + 5, psi, pmtPid) != 0;
                    if (!fPatAdded) {
                        m_blockCache.resize(m_blockCache.size() - 188);
                    }
                }
            }
            else {
                auto it = m_psiCounterInfoMap.find(pid);
                if (it != m_psiCounterInfoMap.end()) {
                    AddTsPacketsFromPsi(m_blockCache, psi.data(), psi.size(), it->second.currentCounter, it->second.mappedPid);
                    if (!fPmtAdded && pid == pmtPid) {
                        // PMT
                        fPmtAdded = AddPmtPacketsFromPmt(m_blockCache, psi, m_psiCounterInfoMap, m_fHevc, !m_stsoA[1].empty(), !m_captionList.empty());
                    }
                }
            }
        });
        // PATやPMTが追加されなければNULで補われる
    }
    else {
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

        // SDT
        m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
        packet = & m_blockCache.back() - 187;
        CreateHeader(packet, 1, 1, blockIndex & 0x0F, 0x0011);
        packet[4] = 0;
        CreateSdt(packet + 5, m_nid, m_tsid, m_sid);

        // PMT
        m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
        packet = &m_blockCache.back() - 187;
        CreateHeader(packet, 1, 1, blockIndex & 0x0F, PMT_PID);
        packet[4] = 0;
        CreatePmt(packet + 5, m_sid, m_fHevc, !m_stsoA[1].empty(), !m_captionList.empty());

        if (blockIndex % 10 == 9) {
            // EIT
            m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
            packet = &m_blockCache.back() - 187;
            CreateHeader(packet, 1, 1, (blockIndex / 10) & 0x0F, 0x0012);
            packet[4] = 0;
            CreateEmptyEitPf(packet + 5, m_nid, m_tsid, m_sid);
        }

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
    }

    // PCR
    m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
    BYTE *packet = &m_blockCache.back() - 187;
    CreateHeader(packet, 0, 2, 0, PCR_PID);
    CreatePcrAdaptation(packet + 4, static_cast<DWORD>(blockIndex) * 4500);

    for (; indexV < m_stsoV.size(); ++indexV) {
        __int64 sampleTime = m_sttsV[0] < 0 ? static_cast<__int64>(indexV) * m_sttsV[1] : m_sttsV[indexV];
        if (sampleTime >= static_cast<__int64>(blockIndex + 1) * m_timeScaleV / 10) {
            break;
        }
        int n = ReadSample(indexV, m_stsoV, m_stszV, &sample);
        if (n > 0) {
            bool fIdr;
            size_t firstAudTail = NalFileToByte(sample, fIdr, m_fHevc);
            // SPS + PPS
            sample.insert(sample.begin() + firstAudTail, m_spsPps.begin(), m_spsPps.end());
            n += static_cast<int>(m_spsPps.size());
            BYTE stuffingSize = m_fHevc ? 7 : 6;
            sample.insert(sample.begin(), stuffingSize, 0xFF);
            n += stuffingSize;
            // AUDがないときは付加しておく(規格に厳密ではない)
            if (firstAudTail == 0) {
                sample[0] = 0;
                sample[1] = 0;
                sample[2] = 0;
                sample[3] = 1;
                if (m_fHevc) {
                    sample[4] = 0x46;
                    sample[5] = 0x01;
                    sample[6] = 0x50;
                }
                else {
                    sample[4] = 0x09;
                    sample[5] = fIdr ? 0x10 : 0x30;
                }
                // 帳尻合わせ
                stuffingSize = 0;
            }
            // PES header
            sample.insert(sample.begin(), 14, 0xFF);
            CreatePesHeader(sample.data(), 0xE0, true, 0, static_cast<DWORD>(45000 * (sampleTime + m_cttsV[indexV]) / m_timeScaleV + m_offsetPtsV), stuffingSize);
            n += 14;
        }
        for (int i = 0; i < n; ++counterV) {
            m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
            packet = &m_blockCache.back() - 187;
            CreateHeader(packet, i == 0, n - i < 184 ? 3 : 1, counterV, VIDEO_PID);
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

    for (BYTE a = 0; a < 2; ++a) {
        for (; indexA[a] < m_stsoA[a].size(); ++indexA[a]) {
            __int64 sampleTime = m_sttsA[a][0] < 0 ? static_cast<__int64>(indexA[a]) * m_sttsA[a][1] : m_sttsA[a][indexA[a]];
            if (sampleTime >= static_cast<__int64>(blockIndex + 1) * m_timeScaleA[a] / 10) {
                break;
            }
            int n = ReadSample(indexA[a], m_stsoA[a], m_stszA[a], &sample);
            if (n > 0) {
                sample.insert(sample.begin(), 14 + 7, 0xFF);
                // ADTS header
                ::memcpy(&sample[14], m_adtsHeader[a], 7);
                n += 7;
                sample[17] |= n >> 11 & 0x03;
                sample[18] |= n >> 3 & 0xFF;
                sample[19] |= n << 5 & 0xFF;
                // PES header
                CreatePesHeader(sample.data(), 0xC0, true, (n + 8) & 0xFFFF, static_cast<DWORD>(45000 * sampleTime / m_timeScaleA[a] + m_offsetPtsA[a]), 0);
                n += 14;
            }
            for (int i = 0; i < n; ++counterA[a]) {
                m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
                packet = &m_blockCache.back() - 187;
                CreateHeader(packet, i == 0, n - i < 184 ? 3 : 1, counterA[a], AUDIO1_PID + a);
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
    }

    __int64 captionEndTime = static_cast<__int64>(blockIndex + 1) * 100 + CAPTION_FORWARD_MSEC;
    for (; itCaption != m_captionList.end() && itCaption->first < captionEndTime; ++itCaption) {
        sample.assign(17, 0xFF);
        // 同期型PES(STD-B24)
        sample[14] = 0x80;
        sample[15] = 0xFF;
        sample[16] = 0xF0;
        sample.insert(sample.end(), itCaption->second.begin(), itCaption->second.begin() + 3);
        sample.push_back(HIBYTE(itCaption->second.size() - 3));
        sample.push_back(LOBYTE(itCaption->second.size() - 3));
        sample.insert(sample.end(), itCaption->second.begin() + 3, itCaption->second.end());
        WORD crc = CalcCrc16Ccitt(sample.data() + 17, sample.size() - 17);
        sample.push_back(HIBYTE(crc));
        sample.push_back(LOBYTE(crc));
        CreatePesHeader(sample.data(), 0xBD, false, static_cast<WORD>(sample.size() - 6), static_cast<DWORD>(45 * itCaption->first + 22500), 0);
        for (size_t i = 0; i < sample.size(); ++counterC) {
            m_blockCache.insert(m_blockCache.end(), 188, 0xFF);
            packet = &m_blockCache.back() - 187;
            CreateHeader(packet, i == 0, sample.size() - i < 184 ? 3 : 1, counterC, CAPTION_PID);
            int pos = 4;
            if (sample.size() - i < 184) {
                packet[4] = (187 - (sample.size() - i + 4)) & 0xFF;
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

bool CReadOnlyMpeg4File::InitializePsiCounterInfo(LPCTSTR &errorMessage)
{
    m_psiCounterInfoMap.clear();
    if (!m_psiDataReader.IsOpen()) {
        return true;
    }

    size_t readingBlockIndex = 0;
    bool blockSizeMaxExceeded = false;
    return m_psiDataReader.ReadCodeList([this, &readingBlockIndex, &blockSizeMaxExceeded](int timeMsec, WORD psiSize, WORD pid) {
        size_t blockIndex = timeMsec / 100;
        if (readingBlockIndex < blockIndex) {
            if (blockIndex < m_blockList.size()) {
                // 計算された連続性指標の開始値をリストに置く
                for (auto it = m_psiCounterInfoMap.begin(); it != m_psiCounterInfoMap.end(); ++it) {
                    // 1要素に2つ詰めこむ(ケチくさい)
                    it->second.counterList[blockIndex / 2] |= it->second.currentCounter << ((1 - blockIndex % 2) * 4);
                }
            }
            readingBlockIndex = blockIndex;
        }
        if (pid != 0 && blockIndex == readingBlockIndex && blockIndex < m_blockList.size()) {
            // ランダムアクセスのためブロックごとのパケットサイズと連続性指標の開始値を計算しておく
            auto it = m_psiCounterInfoMap.find(pid);
            if (it == m_psiCounterInfoMap.end() && m_psiCounterInfoMap.size() < PSI_MAX_STREAMS) {
                WORD mappedPid = pid;
                while (mappedPid == VIDEO_PID ||
                       mappedPid == AUDIO1_PID ||
                       mappedPid == AUDIO1_PID + 1 ||
                       mappedPid == CAPTION_PID ||
                       mappedPid == PMT_PID ||
                       mappedPid == PCR_PID ||
                       m_psiCounterInfoMap.end() != std::find_if(m_psiCounterInfoMap.begin(), m_psiCounterInfoMap.end(),
                           [=](const std::pair<WORD, PSI_COUNTER_INFO> &a) { return a.second.mappedPid == mappedPid; }))
                {
                    // PIDが衝突するのでずらす
                    if (mappedPid == pid) mappedPid = DISPLACED_PID;
                    if (++mappedPid == pid) ++mappedPid;
                }
                it = m_psiCounterInfoMap.insert(std::make_pair(pid, CReadOnlyMpeg4File::PSI_COUNTER_INFO())).first;
                it->second.mappedPid = mappedPid;
                it->second.currentCounter = 0;
                it->second.counterList.resize((m_blockList.size() + 1) / 2, 0);
            }
            if (it != m_psiCounterInfoMap.end()) {
                m_blockList[blockIndex].pos += (psiSize + 1 + 183) / 184;
                it->second.currentCounter = (it->second.currentCounter + (psiSize + 1 + 183) / 184) & 0x0F;
                blockSizeMaxExceeded = blockSizeMaxExceeded || m_blockList[blockIndex].pos > BLOCK_SIZE_MAX;
            }
        }
    }, errorMessage) && !blockSizeMaxExceeded;
}

int CReadOnlyMpeg4File::ReadBox(LPCSTR path, std::vector<BYTE> &data) const
{
    if (path[0] == '/') {
        LARGE_INTEGER toMove = {};
        if (!::SetFilePointerEx(m_hFile, toMove, nullptr, FILE_BEGIN)) {
            return -1;
        }
        ++path;
    }
    int index = path[4] - '0';
    BYTE head[16];
    DWORD numRead;
    while (::ReadFile(m_hFile, head, 8, &numRead, nullptr) && numRead == 8) {
        LARGE_INTEGER boxSize;
        boxSize.QuadPart = ArrayToDWORD(head);
        if (boxSize.QuadPart == 1) {
            // 64bit形式
            if (!::ReadFile(m_hFile, head + 8, 8, &numRead, nullptr) || numRead != 8) {
                break;
            }
            numRead = 16;
            boxSize.HighPart = ArrayToDWORD(head + 8);
            boxSize.LowPart = ArrayToDWORD(head + 12);
        }
        if (boxSize.QuadPart < numRead) {
            break;
        }
        if (::memcmp(path, head + 4, 4) == 0 && --index < 0) {
            if (path[5] == '\0') {
                if (boxSize.QuadPart - numRead <= READ_BOX_SIZE_MAX) {
                    data.resize(static_cast<size_t>(boxSize.QuadPart - numRead));
                    if (data.empty() || ::ReadFile(m_hFile, data.data(), static_cast<DWORD>(data.size()), &numRead, nullptr) && numRead == data.size()) {
                        return static_cast<int>(data.size());
                    }
                }
                break;
            }
            return ReadBox(path + 6, data);
        }
        LARGE_INTEGER toMove;
        toMove.QuadPart = boxSize.QuadPart - numRead;
        if (!::SetFilePointerEx(m_hFile, toMove, nullptr, FILE_CURRENT)) {
            break;
        }
    }
    return -1;
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
            !::SetFilePointerEx(m_hFile, toMove, nullptr, FILE_BEGIN) ||
            !::ReadFile(m_hFile, data->data(), stsz[index], &numRead, nullptr) || numRead != stsz[index]) {
            data->clear();
        }
        return static_cast<int>(data->size());
    }
    return stsz[index];
}

void CReadOnlyMpeg4File::AddTsPacketsFromPsi(std::vector<BYTE> &buf, const BYTE *psi, size_t psiSize, BYTE &counter, WORD pid)
{
    for (size_t i = 0; i < psiSize; ) {
        size_t pointer = i == 0;
        buf.resize(buf.size() + 4 + pointer, 0);
        CreateHeader(&buf.back() - 3 - pointer, i == 0, 1, ++counter, pid);
        buf.insert(buf.end(), psi + i, psi + min(i + 184 - pointer, psiSize));
        buf.resize(((buf.size() - 1) / 188 + 1) * 188, 0xFF);
        i += 184 - pointer;
    }
}

bool CReadOnlyMpeg4File::Add16TsPacketsFromPsi(std::vector<BYTE> &buf, const BYTE *psi, size_t psiSize, WORD pid)
{
    if (psiSize < 16 || 1024 < psiSize) {
        return false;
    }
    // アダプテーションを加えてセクションを16パケット(2セクションx8)に散らし、連続性指標を常に0で始める
    for (BYTE counter = 0; counter < 16; ) {
        for (size_t i = 0; i < psiSize; ++counter) {
            buf.resize(buf.size() + 4, 0);
            CreateHeader(&buf.back() - 3, i == 0, 3, counter, pid);
            size_t pointer = i == 0;
            size_t n = (psiSize - i) / (8 - counter % 8);
            // セクションヘッダはTSを跨がないようにする
            if (i == 0 && n < 8) {
                n = 8;
            }
            buf.push_back(static_cast<BYTE>(183 - n - pointer));
            buf.push_back(0);
            buf.insert(buf.end(), 182 - n - pointer, 0xFF);
            if (pointer) {
                buf.push_back(0);
            }
            buf.insert(buf.end(), psi + i, psi + i + n);
            buf.resize(((buf.size() - 1) / 188 + 1) * 188, 0xFF);
            i += n;
        }
    }
    return true;
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
    data[14] = HIBYTE(PMT_PID) | 0xE0;
    data[15] = LOBYTE(PMT_PID);
    DWORD crc = CalcCrc32(data, 16);
    data[16] = HIBYTE(HIWORD(crc));
    data[17] = LOBYTE(HIWORD(crc));
    data[18] = HIBYTE(crc);
    data[19] = LOBYTE(crc);
    return 20;
}

size_t CReadOnlyMpeg4File::CreatePatFromPat(BYTE *data, const std::vector<BYTE> &pat, WORD &firstPmtPid)
{
    if (pat.size() < 8 || pat[0] != 0x00) {
        return 0;
    }
    firstPmtPid = 0;
    const BYTE *nitPid = nullptr;
    const BYTE *firstPmtSid = nullptr;
    for (size_t i = 8; i + 3 < pat.size() - 4; i += 4) {
        if (pat[i] == 0 && pat[i + 1] == 0) {
            nitPid = &pat[i + 2];
        }
        else if (!firstPmtSid) {
            firstPmtSid = &pat[i];
            firstPmtPid = MAKEWORD(pat[i + 3], pat[i + 2] & 0x1F);
        }
    }

    data[0] = 0x00;
    data[1] = 0xB0;
    data[2] = 9 + (nitPid ? 4 : 0) + (firstPmtSid ? 4 : 0);
    data[3] = pat[3];
    data[4] = pat[4];
    data[5] = pat[5];
    data[6] = 0;
    data[7] = 0;
    size_t x = 8;
    if (nitPid) {
        data[x++] = 0x00;
        data[x++] = 0x00;
        data[x++] = nitPid[0];
        data[x++] = nitPid[1];
    }
    if (firstPmtSid) {
        data[x++] = firstPmtSid[0];
        data[x++] = firstPmtSid[1];
        data[x++] = HIBYTE(PMT_PID) | 0xE0;
        data[x++] = LOBYTE(PMT_PID);
    }
    DWORD crc = CalcCrc32(data, x);
    data[x++] = HIBYTE(HIWORD(crc));
    data[x++] = LOBYTE(HIWORD(crc));
    data[x++] = HIBYTE(crc);
    data[x++] = LOBYTE(crc);
    return x;
}

size_t CReadOnlyMpeg4File::CreateNit(BYTE *data, WORD nid)
{
    data[0] = 0x40;
    data[1] = 0xF0;
    data[2] = 17;
    data[3] = HIBYTE(nid);
    data[4] = LOBYTE(nid);
    data[5] = 0xC1;
    data[6] = 0;
    data[7] = 0;
    data[8] = 0xF0;
    data[9] = 4;
    data[10] = 0x40;
    data[11] = 2;
    data[12] = 0x0E;
    data[13] = 0x4E; // ネットワーク名"N"
    data[14] = 0xF0;
    data[15] = 0;
    DWORD crc = CalcCrc32(data, 16);
    data[16] = HIBYTE(HIWORD(crc));
    data[17] = LOBYTE(HIWORD(crc));
    data[18] = HIBYTE(crc);
    data[19] = LOBYTE(crc);
    return 20;
}

size_t CReadOnlyMpeg4File::CreateSdt(BYTE *data, WORD nid, WORD tsid, WORD sid)
{
    data[0] = 0x42;
    data[1] = 0xF0;
    data[2] = 24;
    data[3] = HIBYTE(tsid);
    data[4] = LOBYTE(tsid);
    data[5] = 0xC1;
    data[6] = 0;
    data[7] = 0;
    data[8] = HIBYTE(nid);
    data[9] = LOBYTE(nid);
    data[10] = 0xFF;
    data[11] = HIBYTE(sid);
    data[12] = LOBYTE(sid);
    data[13] = 0xFF;
    data[14] = 0x00;
    data[15] = 7;
    data[16] = 0x48;
    data[17] = 5;
    data[18] = 0x01;
    data[19] = 0;
    data[20] = 2;
    data[21] = 0x0E;
    data[22] = 0x53; // サービス名"S"
    DWORD crc = CalcCrc32(data, 23);
    data[23] = HIBYTE(HIWORD(crc));
    data[24] = LOBYTE(HIWORD(crc));
    data[25] = HIBYTE(crc);
    data[26] = LOBYTE(crc);
    return 27;
}

size_t CReadOnlyMpeg4File::CreateEmptyEitPf(BYTE *data, WORD nid, WORD tsid, WORD sid)
{
    data[0] = 0x4E;
    data[1] = 0xF0;
    data[2] = 15;
    data[3] = HIBYTE(sid);
    data[4] = LOBYTE(sid);
    data[5] = 0xC1;
    data[6] = 0;
    data[7] = 0;
    data[8] = HIBYTE(tsid);
    data[9] = LOBYTE(tsid);
    data[10] = HIBYTE(nid);
    data[11] = LOBYTE(nid);
    data[12] = 0;
    data[13] = 0x4E;
    DWORD crc = CalcCrc32(data, 14);
    data[14] = HIBYTE(HIWORD(crc));
    data[15] = LOBYTE(HIWORD(crc));
    data[16] = HIBYTE(crc);
    data[17] = LOBYTE(crc);
    return 18;
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

size_t CReadOnlyMpeg4File::CreatePmt(BYTE *data, WORD sid, bool fHevc, bool fAudio2, bool fCaption)
{
    data[0] = 0x02;
    data[3] = HIBYTE(sid);
    data[4] = LOBYTE(sid);
    data[5] = 0xC1;
    data[6] = 0;
    data[7] = 0;
    data[8] = HIBYTE(PCR_PID) | 0xE0;
    data[9] = LOBYTE(PCR_PID);
    data[10] = 0xF0;
    data[11] = 0;
    size_t x = 12 + CreatePmt2ndLoop(data + 12, fHevc, fAudio2, fCaption);
    data[1] = HIBYTE(x + 4 - 3) | 0xB0;
    data[2] = LOBYTE(x + 4 - 3);
    DWORD crc = CalcCrc32(data, x);
    data[x++] = HIBYTE(HIWORD(crc));
    data[x++] = LOBYTE(HIWORD(crc));
    data[x++] = HIBYTE(crc);
    data[x++] = LOBYTE(crc);
    return x;
}

bool CReadOnlyMpeg4File::AddPmtPacketsFromPmt(std::vector<BYTE> &buf, const std::vector<BYTE> &pmt, const std::map<WORD, PSI_COUNTER_INFO> &pidMap,
                                              bool fHevc, bool fAudio2, bool fCaption)
{
    if (pmt.size() < 12 || pmt[0] != 0x02) {
        return false;
    }
    BYTE data[1024];
    data[0] = 0x02;
    data[3] = pmt[3];
    data[4] = pmt[4];
    data[5] = pmt[5];
    data[6] = 0;
    data[7] = 0;
    data[8] = HIBYTE(PCR_PID) | 0xE0;
    data[9] = LOBYTE(PCR_PID);
    data[10] = 0xF0;
    data[11] = 0;
    size_t x = 12 + CreatePmt2ndLoop(data + 12, fHevc, fAudio2, fCaption);

    WORD programInfoLength = MAKEWORD(pmt[11], pmt[10] & 0x03);
    for (size_t pos = 12 + programInfoLength; pos + 4 < pmt.size() - 4; ) {
        WORD esPid = MAKEWORD(pmt[pos + 2], pmt[pos + 1] & 0x1F);
        WORD esInfoLength = MAKEWORD(pmt[pos + 4], pmt[pos + 3] & 0x03);
        auto it = pidMap.find(esPid);
        if (it != pidMap.end() && pos + 5 + esInfoLength <= pmt.size() - 4) {
            if (x + 5 + esInfoLength > 1024 - 4) {
                break;
            }
            ::memcpy(data + x, &pmt[pos], 5 + esInfoLength);
            data[x + 1] = HIBYTE(it->second.mappedPid) | 0xE0;
            data[x + 2] = LOBYTE(it->second.mappedPid);
            x += 5 + esInfoLength;
        }
        pos += 5 + esInfoLength;
    }
    data[1] = HIBYTE(x + 4 - 3) | 0xB0;
    data[2] = LOBYTE(x + 4 - 3);
    DWORD crc = CalcCrc32(data, x);
    data[x++] = HIBYTE(HIWORD(crc));
    data[x++] = LOBYTE(HIWORD(crc));
    data[x++] = HIBYTE(crc);
    data[x++] = LOBYTE(crc);
    return Add16TsPacketsFromPsi(buf, data, x, PMT_PID);
}

size_t CReadOnlyMpeg4File::CreatePmt2ndLoop(BYTE *data, bool fHevc, bool fAudio2, bool fCaption)
{
    size_t x = 0;
    data[x++] = fHevc ? H_265_VIDEO : AVC_VIDEO;
    data[x++] = HIBYTE(VIDEO_PID) | 0xE0;
    data[x++] = LOBYTE(VIDEO_PID);
    data[x++] = 0xF0;
    data[x++] = 3;
    data[x++] = 0x52; // ストリーム識別記述子
    data[x++] = 1;
    data[x++] = 0x00; // 部分受信階層の場合を考慮すると厳密ではない
    data[x++] = ADTS_TRANSPORT;
    data[x++] = HIBYTE(AUDIO1_PID) | 0xE0;
    data[x++] = LOBYTE(AUDIO1_PID);
    data[x++] = 0xF0;
    data[x++] = 3;
    data[x++] = 0x52; // ストリーム識別記述子
    data[x++] = 1;
    data[x++] = 0x10;
    if (fAudio2) {
        data[x++] = ADTS_TRANSPORT;
        data[x++] = HIBYTE(AUDIO1_PID + 1) | 0xE0;
        data[x++] = LOBYTE(AUDIO1_PID + 1);
        data[x++] = 0xF0;
        data[x++] = 3;
        data[x++] = 0x52; // ストリーム識別記述子
        data[x++] = 1;
        data[x++] = 0x11;
    }
    if (fCaption) {
        data[x++] = PES_PRIVATE_DATA;
        data[x++] = HIBYTE(CAPTION_PID) | 0xE0;
        data[x++] = LOBYTE(CAPTION_PID);
        data[x++] = 0xF0;
        data[x++] = 3;
        data[x++] = 0x52; // ストリーム識別記述子
        data[x++] = 1;
        data[x++] = 0x30;
    }
    return x;
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

size_t CReadOnlyMpeg4File::CreatePesHeader(BYTE *data, BYTE streamID, bool fDataAlignment, WORD packetLength, DWORD pts45khz, BYTE stuffingSize)
{
    data[0] = 0;
    data[1] = 0;
    data[2] = 1;
    data[3] = streamID;
    data[4] = HIBYTE(packetLength);
    data[5] = LOBYTE(packetLength);
    data[6] = fDataAlignment ? 0x84 : 0x80;
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

size_t CReadOnlyMpeg4File::NalFileToByte(std::vector<BYTE> &data, bool &fIdr, bool fHevc)
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
        if (len >= 1) {
            int naluType = fHevc ? (data[i] >> 1) & 0x3F : data[i] & 0x1F;
            if ((fHevc && (naluType == 19 || naluType == 20)) || (!fHevc && naluType == 5)) {
                fIdr = true;
            }
            if (firstAudTail == 0 && ((fHevc && naluType == 35) || (!fHevc && naluType == 9))) {
                firstAudTail = i + len;
            }
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
