#include <Windows.h>
#include "Util.h"
#include "B24CaptionUtil.h"

namespace
{
const uint32_t CAPTION_MAX_PER_SEC = 20;
const uint32_t CAPTION_MANAGEMENT_RESEND_MSEC = 5000;
const char B24CAPTION_MAGIC[] = "b24caption-2aaf6fcf-6388-4e59-88ff-46e1555d0edd";

bool DecodeBase64(std::vector<uint8_t> &dest, const char *src, size_t srcSize)
{
    if (srcSize % 4 != 0) {
        return false;
    }
    for (size_t i = 0; i + 3 < srcSize; i += 4) {
        uint8_t b[4];
        for (size_t j = 0; j < 4; ++j) {
            char c = src[i + j];
            b[j] = 'A' <= c && c <= 'Z' ? c - 'A' :
                   'a' <= c && c <= 'z' ? c - 'a' + 26 :
                   '0' <= c && c <= '9' ? c - '0' + 52 :
                   c == '+' ? 62 :
                   c == '/' ? 63 :
                   c == '=' ? 64 : 65;
            if (b[j] > 64) {
                return false;
            }
        }
        dest.push_back((b[0] << 2) | ((b[1] & 0x30) >> 4));
        if (b[2] < 64) {
            dest.push_back(((b[1] & 0xF) << 4) | ((b[2] & 0x3C) >> 2));
            if (b[3] < 64) {
                dest.push_back(((b[2] & 0x3) << 6) | (b[3] & 0x3F));
            }
        }
    }
    return true;
}

bool DecodeB24Caption(std::vector<uint8_t> &dest, const char *src)
{
    dest.clear();
    size_t bracePos[8];
    size_t braceCount = 0;
    bool fIgnoreTag = false;
    while (*src) {
        if (fIgnoreTag) {
            fIgnoreTag = *src != '>';
            ++src;
        }
        else if (*src == '<') {
            // '>'まで無視
            fIgnoreTag = true;
            ++src;
        }
        else if (*src == '%') {
            // tsreadex仕様のエスケープされた字幕データをデコード
            ++src;
            if (!src[0] || !src[1]) {
                return false;
            }
            char c = *(src++);
            char d = *(src++);
            if (c == '^') {
                // キャレット記法
                dest.push_back(0xC2);
                dest.push_back(static_cast<uint8_t>(d + 0x40));
            }
            else if (c == '=') {
                // 24bitサイズフィールド
                if (d == '{' && braceCount < _countof(bracePos)) {
                    dest.resize(dest.size() + 3);
                    bracePos[braceCount++] = dest.size();
                }
                else if (d == '}' && braceCount > 0) {
                    size_t pos = bracePos[--braceCount];
                    size_t sz = dest.size() - pos;
                    dest[pos - 3] = static_cast<uint8_t>(sz >> 16);
                    dest[pos - 2] = static_cast<uint8_t>(sz >> 8);
                    dest[pos - 1] = static_cast<uint8_t>(sz);
                }
                else {
                    // ネストが深すぎるなど
                    return false;
                }
            }
            else if (c == '+') {
                // Base64
                if (d != '{') {
                    return false;
                }
                const char *p = strchr(src, '%');
                if (!p || p[1] != '+' || p[2] != '}' || !DecodeBase64(dest, src, p - src)) {
                    return false;
                }
                src = p + 3;
            }
            else {
                // HEX
                dest.push_back(static_cast<uint8_t>((c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0') << 4 |
                                                    (d >= 'a' ? d - 'a' + 10 : d >= 'A' ? d - 'A' + 10 : d - '0')));
            }
        }
        else if (*src == '&') {
            // WebVTTの文字実体参照のうち主要なものをデコード
            ++src;
            static const char ent[5][6] = { "amp;", "lt;", "gt;", "quot;", "apos;" };
            static const uint8_t ref[5] = { '&', '<', '>', '"', '\'' };
            size_t i = 0;
            for (; i < sizeof(ref); ++i) {
                if (strncmp(src, ent[i], strlen(ent[i])) == 0) {
                    dest.push_back(ref[i]);
                    src += strlen(ent[i]);
                    break;
                }
            }
            if (i >= sizeof(ref)) {
                dest.push_back('&');
            }
        }
        else {
            dest.push_back(static_cast<uint8_t>(*(src++)));
        }
    }
    // PESに格納できるサイズであること
    return 3 <= dest.size() && dest.size() <= 65520 && braceCount == 0;
}
}

void LoadWebVttB24Caption(LPCTSTR path, std::vector<std::pair<int64_t, std::vector<uint8_t>>> &captionList)
{
    captionList.clear();
    FILE *fp;
    if (_tfopen_s(&fp, path, TEXT("rN")) != 0) {
        return;
    }

    bool fMagic = false;
    int64_t currentQueMsec = 0;
    uint32_t captionNumPerSec = 0;
    uint8_t currentDgiGroup = 0;
    size_t captionManagementIndex = 0;
    enum { VTT_SIGNATURE, VTT_IGNORE, VTT_BODY, VTT_CUE_ID, VTT_CUE_TIME, VTT_CUE } state = VTT_SIGNATURE;
    std::vector<uint8_t> decodeBuf;
    std::vector<char> buf;
    size_t bufLen = 0;
    size_t readFileCount = 0;

    while (readFileCount < READ_FILE_MAX_SIZE) {
        if (buf.size() < bufLen + 1024) {
            buf.resize(bufLen + 1024);
        }
        bool fEof = !fgets(buf.data() + bufLen, 1024, fp);
        if (!fEof) {
            size_t len = strlen(buf.data() + bufLen);
            readFileCount += len;
            bufLen += len;
            if (bufLen == 0 || buf[bufLen - 1] != '\n') {
                continue;
            }
            --bufLen;
        }
        buf[bufLen] = '\0';

        if (state == VTT_SIGNATURE) {
            size_t i = strncmp(buf.data(), "\xEF\xBB\xBF", 3) == 0 ? 3 : 0;
            if (strncmp(&buf[i], "WEBVTT", 6) != 0 || (bufLen != i + 6 && buf[i + 6] != ' ' && buf[i + 6] != '\t')) {
                // WebVTTでない
                break;
            }
            state = VTT_IGNORE;
            fMagic = fMagic || strstr(buf.data(), B24CAPTION_MAGIC);
        }
        else if (state == VTT_IGNORE) {
            if (bufLen == 0) {
                state = VTT_BODY;
            }
            else {
                fMagic = fMagic || strstr(buf.data(), B24CAPTION_MAGIC);
            }
        }
        else if (state == VTT_BODY) {
            if ((strncmp(buf.data(), "NOTE", 4) == 0 && (bufLen == 4 || buf[4] == ' ' || buf[4] == '\t')) ||
                (strncmp(buf.data(), "STYLE", 5) == 0 && (bufLen == 5 || buf[5] == ' ' || buf[5] == '\t')) ||
                (strncmp(buf.data(), "REGION", 6) == 0 && (bufLen == 6 || buf[6] == ' ' || buf[6] == '\t')))
            {
                state = VTT_IGNORE;
                fMagic = fMagic || strstr(buf.data(), B24CAPTION_MAGIC);
            }
            else if (bufLen != 0) {
                if (!fMagic) {
                    // 最初のキューまでにマジック文字列がなかった
                    break;
                }
                state = strstr(buf.data(), "-->") ? VTT_CUE_TIME : VTT_CUE_ID;
            }
        }
        else if (state == VTT_CUE_ID) {
            if (bufLen == 0) {
                state = VTT_BODY;
            }
            else if (strstr(buf.data(), "-->")) {
                state = VTT_CUE_TIME;
            }
            else {
                state = VTT_IGNORE;
            }
        }
        else if (state == VTT_CUE) {
            if (bufLen == 0) {
                state = VTT_BODY;
            }
            else if (strncmp(buf.data(), "<v b24caption", 13) == 0 && '0' <= buf[13] && buf[13] <= '8' && buf[14] == '>') {
                char *tagEnd = strstr(&buf[15], "</v>");
                if (tagEnd) {
                    *tagEnd = '\0';
                }
                // 秒あたりの追加数を制限する
                if (captionNumPerSec < CAPTION_MAX_PER_SEC && DecodeB24Caption(decodeBuf, &buf[15])) {
                    // 組情報を上書き
                    decodeBuf[0] = currentDgiGroup | (decodeBuf[0] & 0x7F);
                    if ((decodeBuf[0] & 0x7C) == 0) {
                        // 字幕管理
                        if (!captionList.empty() && decodeBuf != captionList[captionManagementIndex].second) {
                            // 内容が変化したので組を変更
                            currentDgiGroup = currentDgiGroup == 0 ? 0x80 : 0;
                            decodeBuf[0] = currentDgiGroup | (decodeBuf[0] & 0x7F);
                        }
                        captionManagementIndex = captionList.size();
                        captionList.push_back(std::make_pair(currentQueMsec, decodeBuf));
                    }
                    else if (!captionList.empty()) {
                        // 字幕データ
                        if (captionList[captionManagementIndex].first + CAPTION_MANAGEMENT_RESEND_MSEC < currentQueMsec) {
                            // 字幕管理を再送
                            captionList.push_back(std::make_pair(currentQueMsec, captionList[captionManagementIndex].second));
                            captionManagementIndex = captionList.size() - 1;
                        }
                        captionList.push_back(std::make_pair(currentQueMsec, decodeBuf));
                    }
                    ++captionNumPerSec;
                }
            }
        }

        if (state == VTT_CUE_TIME) {
            state = VTT_IGNORE;
            if (bufLen >= 12 && buf[2] == ':' && (buf[5] == '.' || (buf[5] == ':' && buf[8] == '.'))) {
                long t1 = strtol(&buf[0], nullptr, 10);
                long t2 = strtol(&buf[3], nullptr, 10);
                long t3 = strtol(&buf[6], nullptr, 10);
                long t;
                if (buf[5] == '.') {
                    // mm:ss.ttt
                    t = ((t1 * 60) + t2) * 1000 + t3;
                }
                else {
                    // hh:mm:ss.ttt
                    t = (((t1 * 60) + t2) * 60 + t3) * 1000 + strtol(&buf[9], nullptr, 10);
                }
                if (t >= currentQueMsec) {
                    if (t / 1000 > currentQueMsec / 1000) {
                        captionNumPerSec = 0;
                    }
                    if (!captionList.empty()) {
                        while (currentQueMsec + CAPTION_MANAGEMENT_RESEND_MSEC * 2 < t) {
                            // 字幕管理を再送
                            currentQueMsec += CAPTION_MANAGEMENT_RESEND_MSEC;
                            captionList.push_back(std::make_pair(currentQueMsec, captionList[captionManagementIndex].second));
                            captionManagementIndex = captionList.size() - 1;
                        }
                    }
                    currentQueMsec = t;
                    state = VTT_CUE;
                }
            }
        }

        if (fEof) {
            break;
        }
        bufLen = 0;
    }

    fclose(fp);
}

uint16_t CalcCrc16Ccitt(const uint8_t *data, size_t len, uint16_t crc)
{
    for (size_t i = 0; i < len; ++i) {
        uint16_t c = ((crc >> 8) ^ data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            c = (c << 1) ^ (c & 0x8000 ? 0x1021 : 0);
        }
        crc = (crc << 8) ^ c;
    }
    return crc;
}
