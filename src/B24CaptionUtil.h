#ifndef INCLUDE_B24_CAPTION_UTIL_H
#define INCLUDE_B24_CAPTION_UTIL_H

#include <stdint.h>
#include <utility>
#include <vector>

void LoadWebVttB24Caption(LPCTSTR path, std::vector<std::pair<int64_t, std::vector<uint8_t>>> &captionList);
uint16_t CalcCrc16Ccitt(const uint8_t *data, size_t len, uint16_t crc = 0);

#endif // INCLUDE_B24_CAPTION_UTIL_H
