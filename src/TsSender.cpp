#include <WinSock2.h>
#include "TsSender.h"
#include "Util.h"


#define MSB(x) ((x) & 0x80000000)


CTsSender::CTsSender()
    : m_hFile(NULL)
    , m_pBuf(NULL)
    , m_curr(NULL)
    , m_tail(NULL)
    , m_unitSize(0)
    , m_sock(NULL)
    , m_tick(0)
    , m_baseTick(0)
    , m_renewSizeTick(0)
    , m_pcrCount(0)
    , m_pcr(0)
    , m_basePcr(0)
    , m_initPcr(0)
    , m_fPcr(false)
    , m_fFixed(false)
    , m_fPause(false)
    , m_fileSize(0)
    , m_duration(0)
{
}


CTsSender::~CTsSender()
{
    Close();
}


bool CTsSender::Open(LPCTSTR name, LPCSTR addr, unsigned short port)
{
    Close();
    
    m_hFile = ::CreateFile(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hFile == INVALID_HANDLE_VALUE) {
        m_hFile = NULL;
        return false;
    }
    // バッファの生滅はファイルハンドルと同期
    m_pBuf = new BYTE[BUFFER_SIZE];
    
    // TSパケットの単位を決定
    DWORD readBytes;
    if (!::ReadFile(m_hFile, m_pBuf, BUFFER_SIZE, &readBytes, NULL)) goto ERROR_EXIT;
    m_unitSize = select_unit_size(m_pBuf, m_pBuf + readBytes);
    if (m_unitSize < 188) goto ERROR_EXIT;
    
    // ファイルサイズは拡大中であると仮定
    m_fFixed = false;

    // 定期的にファイルサイズの更新状況を取得するため
    m_renewSizeTick = ::GetTickCount();
    if ((m_fileSize = GetFileSize()) < 0) goto ERROR_EXIT;
    
    // ファイル先頭のPCRと動画の長さを取得
    if (Seek(-TS_SUPPOSED_RATE, FILE_END) && m_pcrCount) {
        DWORD finPcr = m_pcr;

        if (!SeekToBegin() || !m_pcrCount) goto ERROR_EXIT;
        m_initPcr = m_pcr;
        m_duration = (int)((finPcr - m_initPcr) / PCR_PER_MSEC) + 1000;
        m_duration = (int)((finPcr - m_initPcr) / PCR_PER_MSEC) +
                     (int)((long long)TS_SUPPOSED_RATE * 1000 / GetRate());
    }
    else {
        if (!SeekToBegin() || !m_pcrCount) goto ERROR_EXIT;
        m_initPcr = m_pcr;
        m_duration = 0;
    }

    WSADATA wsaData;
    if (::WSAStartup(MAKEWORD(2,0), &wsaData) != 0) goto ERROR_EXIT;
    m_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    SetAddress(addr, port);

    return true;
ERROR_EXIT:
    Close();
    return false;
}


void CTsSender::SetAddress(LPCSTR addr, unsigned short port)
{
    m_addr.sin_family = AF_INET;
    m_addr.sin_port = htons(port);
    m_addr.sin_addr.S_un.S_addr = inet_addr(addr);
}


void CTsSender::Close()
{
    if (m_hFile) {
        CloseHandle(m_hFile);
        m_hFile = NULL;
        delete [] m_pBuf;
        m_pBuf = NULL;
    }
    if (m_sock) {
        closesocket(m_sock);
        m_sock = NULL;
        WSACleanup();
    }
}


bool CTsSender::Send()
{
    if (!m_fPause) {
        if (!ReadPacket()) {
            ConsumeBuffer(true);
            if (!ReadPacket()) return false;
        }

        // 転送レート制御
        if (m_fPcr) {
            DWORD tickDiff = m_tick - m_baseTick;
            if (MSB(tickDiff)) tickDiff = 0;

            DWORD pcrDiff = m_pcr - m_basePcr;
            if (MSB(pcrDiff)) pcrDiff = 0;

            int msec = (int)pcrDiff / PCR_PER_MSEC - (int)tickDiff;

            // 制御しきれない場合は一度リセット
            if (msec < -2000 || 2000 < msec) m_pcrCount = 0;
            else if (msec > 0) ::Sleep(msec);
        }
    }

    // 動画の長さ情報を更新
    DWORD tick = ::GetTickCount();
    DWORD diff = tick - m_renewSizeTick;
    if (2000 <= diff && !MSB(diff)) {
        long long fileSize;
        if ((fileSize = GetFileSize()) >= 0) {
            if (fileSize == m_fileSize) {
                m_fFixed = true;
            }
            else {
                m_fFixed = false;
                m_fileSize = fileSize;
                m_duration += diff;
            }
        }
        m_renewSizeTick = tick;
    }
    return true;
}


bool CTsSender::SeekToBegin()
{
    return Seek(0, FILE_BEGIN);
}


bool CTsSender::SeekToEnd()
{
    return Seek(-GetRate(), FILE_END);
}


// 現在の再生位置からmsecだけシークする
// シーク可能範囲を超えるor下回る場合は先頭or末尾までシークする
// シークできなかったorシークが打ち切られたときはfalseを返す
bool CTsSender::Seek(int msec)
{
    long long rate = (long long)GetRate();
    long long size = GetFileSize();
    long long pos;

    if (!m_pcrCount || size < 0) return false;

    // -5000<=msec<=5000になるまで動画レートから概算シーク
    // 5ループまでに収束しなければ失敗
    for (int i = 0; i < 5 && (msec < -5000 || 5000 < msec); i++) {
        if ((pos = GetFilePosition()) < 0) return false;

        // 前方or後方にこれ以上シークできない場合
        if (msec < 0 && pos <= rate ||
            msec > 0 && pos >= size - rate) return true;

        long long approx = pos + rate * msec / 1000;
        if (approx > size - rate) approx = size - rate;
        if (approx < 0) approx = 0;
        
        DWORD prevPcr = m_pcr;
        if (!Seek(approx, FILE_BEGIN) || !m_pcrCount) return false;
        
        // 移動分を差し引く
        msec += MSB(m_pcr-prevPcr) ? (int)(prevPcr-m_pcr) / PCR_PER_MSEC :
                                    -(int)(m_pcr-prevPcr) / PCR_PER_MSEC;
    }
    if (!m_pcrCount || msec < -5000 || 5000 < msec) return false;
    
    if (msec < 0) {
        // 約1秒ずつ前方シーク
        // 10ループまでに収束しなければ失敗
        for (int i = 0; i < 10 && msec < -500; i++) {
            if ((pos = GetFilePosition()) < 0) return false;
            if (pos <= rate) return true;

            DWORD prevPcr = m_pcr;
            if (!Seek(-rate, FILE_CURRENT) || !m_pcrCount || MSB(prevPcr-m_pcr)) return false;
            msec += (int)(prevPcr-m_pcr) / PCR_PER_MSEC;
        }
        if (msec < -500) return false;
    }
    else {
        // 約1秒ずつ後方シーク
        // 10ループまでに収束しなければ失敗
        for (int i = 0; i < 10 && msec > 500; i++) {
            if ((pos = GetFilePosition()) < 0) return false;
            if (pos >= size - rate) return true;

            DWORD prevPcr = m_pcr;
            if (!Seek(rate, FILE_CURRENT) || !m_pcrCount || MSB(m_pcr-prevPcr)) return false;
            msec -= (int)(m_pcr-prevPcr) / PCR_PER_MSEC;
        }
        if (msec > 500) return false;
    }
    return true;
}


void CTsSender::Pause(bool fPause)
{
    if (m_fPause && !fPause) m_pcrCount = 0;
    m_fPause = fPause;
}


bool CTsSender::IsPaused() const
{
    return m_fPause;
}


// ファイルサイズが固定されているかどうか
bool CTsSender::IsFixed() const
{
    return m_fFixed;
}


// 現在のファイルサイズを取得する
long long CTsSender::GetFileSize() const
{
    LARGE_INTEGER liFileSize;
    if (m_hFile && ::GetFileSizeEx(m_hFile, &liFileSize)) {
        return liFileSize.QuadPart;
    }
    return -1;
}


// 現在のファイルポインタの位置を取得する
long long CTsSender::GetFilePosition() const
{
    LARGE_INTEGER liNew, liMove = {0};
    if (m_hFile && ::SetFilePointerEx(m_hFile, liMove, &liNew, FILE_CURRENT)) {
        return liNew.QuadPart;
    }
    return -1;
}


// 動画の長さ(ミリ秒)を取得する
// 正確でない場合がある
// ファイルサイズが拡大中の場合は等速での増加を仮定
int CTsSender::GetDuration() const
{
    return m_duration;
}


// 動画の再生位置(ミリ秒)を取得する
int CTsSender::GetPosition() const
{
    return !m_pcrCount ? 0 : (m_pcr - m_initPcr) / PCR_PER_MSEC;
}


// 動画全体のレート(Bytes/秒)を取得する
// 正確でない場合がある
// 極端な値は抑制される
int CTsSender::GetRate() const
{
    long long fileSize = GetFileSize();

    int rate = fileSize < 0 || m_duration <= 0 ? TS_SUPPOSED_RATE :
               static_cast<int>(fileSize * 1000 / m_duration);

    return rate < TS_SUPPOSED_RATE/128 ? TS_SUPPOSED_RATE/128 :
           rate > TS_SUPPOSED_RATE*4 ? TS_SUPPOSED_RATE*4 : rate;
}


// TSパケットを1つ読む
bool CTsSender::ReadPacket()
{
    if (!m_hFile) return false;

    if (m_curr + m_unitSize >= m_tail) {
        int n = m_pBuf + BUFFER_SIZE - m_tail;
        if (n > 0) {
            DWORD readBytes;
            if (!::ReadFile(m_hFile, m_tail, n, &readBytes, NULL)) return false;
            m_tail += readBytes;
        }
    }
    
    // バッファにTSパケットを読めるだけのデータがない
    if (m_curr + m_unitSize >= m_tail) return false;

    if (m_curr[0] != 0x47 || m_curr[m_unitSize] != 0x47) {
        BYTE *p = resync(m_curr, m_tail, m_unitSize);
        if (!p) {
            m_curr = m_tail = m_pBuf;
            return ReadPacket();
        }
        m_curr = p;
        if (m_curr + m_unitSize > m_tail) return ReadPacket();
    }
    
    m_fPcr = false;

    TS_HEADER header;
    extract_ts_header(&header, m_curr);
    
    if (header.adaptation_field_control & 2) {
        // アダプテーションフィールドがある
        ADAPTATION_FIELD adapt;
        extract_adaptation_field(&adapt, m_curr + 4);

        if (adapt.pcr_flag) {
            m_tick = ::GetTickCount();

            m_pcrPool[2] = m_pcrPool[1];
            m_pcrPool[1] = m_pcrPool[0];
            m_pcrPool[0] = (DWORD)adapt.pcr_45khz;

            // 最近取得された3つのPCRの中央値を現在のPCRとする
            if (m_pcrCount < 2) {
                m_pcr = m_pcrPool[0];
            }
            else if (MSB(m_pcrPool[1] - m_pcrPool[0]) && MSB(m_pcrPool[0] - m_pcrPool[2]) ||
                     MSB(m_pcrPool[2] - m_pcrPool[0]) && MSB(m_pcrPool[0] - m_pcrPool[1])) {
                m_pcr = m_pcrPool[0];
            }
            else if (MSB(m_pcrPool[0] - m_pcrPool[1]) && MSB(m_pcrPool[1] - m_pcrPool[2]) ||
                     MSB(m_pcrPool[2] - m_pcrPool[1]) && MSB(m_pcrPool[1] - m_pcrPool[0])) {
                m_pcr = m_pcrPool[1];
            }
            else {
                m_pcr = m_pcrPool[2];
            }
            
            if (m_pcrCount++ == 0) {
                m_baseTick = m_tick;
                m_basePcr = m_pcr;
            }
            m_fPcr = true;
        }
    }

    m_curr += m_unitSize;
    return true;
}


// 読み込まれたTSパケットを転送・消費する
void CTsSender::ConsumeBuffer(bool fSend)
{
    if (!m_pBuf) return;

    if (fSend && m_sock && m_curr > m_pBuf) {
        ::sendto(m_sock, (const char*)m_pBuf, m_curr - m_pBuf,
                 0, (struct sockaddr*)&m_addr, sizeof(m_addr));
    }
    int n = m_tail - m_curr;
    if (n > 0) memcpy(m_pBuf, m_curr, n);

    m_curr = m_pBuf;
    m_tail = m_pBuf + n;
}


// シークする
// シーク後、可能ならば最初のPCRの位置まで読まれる
bool CTsSender::Seek(long long distanceToMove, DWORD dwMoveMethod)
{
    if (!m_hFile) return false;

    LARGE_INTEGER lDistanceToMove;
    lDistanceToMove.QuadPart = distanceToMove;
    if (!::SetFilePointerEx(m_hFile, lDistanceToMove, NULL, dwMoveMethod)) return false;

    // シーク後はバッファを空にする
    m_curr = m_tail = m_pBuf;
    m_pcrCount = 0;
    do {
        if (!ReadPacket()) {
            ConsumeBuffer(false);
            if (!ReadPacket()) break;
        }
    } while (!m_pcrCount);

    return true;
}
