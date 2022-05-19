#ifndef INCLUDE_B24_CAPTION_UTIL_H
#define INCLUDE_B24_CAPTION_UTIL_H

#include <utility>
#include <vector>

void LoadWebVttB24Caption(LPCTSTR path, std::vector<std::pair<__int64, std::vector<BYTE>>> &captionList);
WORD CalcCrc16Ccitt(const BYTE *data, size_t len, WORD crc = 0);

#endif // INCLUDE_B24_CAPTION_UTIL_H
