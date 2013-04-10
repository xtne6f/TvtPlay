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
    , m_udpPort(0)
    , m_hPipe(NULL)
    , m_baseTick(0)
    , m_renewSizeTick(0)
    , m_pcrCount(0)
    , m_pcr(0)
    , m_basePcr(0)
    , m_initPcr(0)
    , m_fPcr(false)
    , m_fShareWrite(false)
    , m_fFixed(false)
    , m_fPause(false)
    , m_pcrPid(0)
    , m_pcrPidsLen(0)
    , m_fileSize(0)
    , m_duration(0)
    , m_totBase(0)
    , m_totBasePcr(0)
    , m_hash(0)
    , m_speedNum(100)
    , m_speedDen(100)
    , m_fSpecialExtending(false)
    , m_specialExtendInitRate(0)
{
    m_udpAddr[0] = 0;
    m_pipeName[0] = 0;
}


CTsSender::~CTsSender()
{
    Close();
}


bool CTsSender::Open(LPCTSTR name, DWORD salt)
{
    Close();

    // まず読み込み共有で開いてみる
    m_fShareWrite = false;
    m_fFixed = true;
    m_hFile = ::CreateFile(name, GENERIC_READ, FILE_SHARE_READ,
                           0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hFile == INVALID_HANDLE_VALUE) {
        // 録画中かもしれない。書き込み共有で開く
        m_fShareWrite = true;
        m_fFixed = false;
        m_hFile = ::CreateFile(name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (m_hFile == INVALID_HANDLE_VALUE) {
            m_hFile = NULL;
            return false;
        }
    }

    // TSパケットの単位を決定
    BYTE buf[8192];
    DWORD readBytes;
    if (!::ReadFile(m_hFile, buf, sizeof(buf), &readBytes, NULL)) goto ERROR_EXIT;
    m_unitSize = select_unit_size(buf, buf + readBytes);
    if (m_unitSize < 188) goto ERROR_EXIT;

    // 識別情報としてファイル先頭のハッシュ値をとる
    m_hash = CalcHash(buf, min(readBytes, 2048), salt);
    if (m_hash < 0) goto ERROR_EXIT;

    // バッファ確保
    m_pBuf = new BYTE[m_unitSize * BUFFER_LEN];

    // PCR参照PIDをクリア
    m_pcrPid = m_pcrPidsLen = 0;

    // TOT-PCR対応情報をクリア
    m_totBase = -1;

    long long fileSize = GetFileSize();
    if (fileSize < 0) goto ERROR_EXIT;

    // ファイル先頭のPCRと動画の長さを取得
    m_fSpecialExtending = false;
    if (Seek(-TS_SUPPOSED_RATE * 2, FILE_END) && m_pcrCount) {
        // ファイル末尾が正常である場合
        DWORD finPcr = m_pcr;

        if (!SeekToBegin() || !m_pcrCount) goto ERROR_EXIT;
        m_initPcr = m_pcr;
        m_duration = (int)((finPcr - m_initPcr) / PCR_PER_MSEC) + 2000;
        m_duration = (int)((finPcr - m_initPcr) / PCR_PER_MSEC) +
                     (int)((long long)TS_SUPPOSED_RATE * 2000 / GetRate());
    }
    else if (m_fShareWrite &&
             SeekToBoundary(fileSize / 2, fileSize, buf, sizeof(buf) / 2) &&
             Seek(-TS_SUPPOSED_RATE * 2, FILE_CURRENT) && m_pcrCount)
    {
        // 書き込み共有かつファイル末尾が正常でない場合
        long long filePos = GetFilePosition();
        DWORD finPcr = m_pcr;

        if (!SeekToBegin() || !m_pcrCount) goto ERROR_EXIT;
        m_initPcr = m_pcr;
        m_duration = (int)((finPcr - m_initPcr) / PCR_PER_MSEC) + 2000;

        // ファイルサイズからレートを計算できないため
        m_specialExtendInitRate = filePos < 0 || m_duration <= 0 ? TS_SUPPOSED_RATE :
                                  static_cast<int>((filePos + TS_SUPPOSED_RATE * 2) * 1000 / m_duration);
        m_fSpecialExtending = true;
    }
    else {
        // 最低限、再生はできる場合
        if (!SeekToBegin() || !m_pcrCount) goto ERROR_EXIT;
        m_initPcr = m_pcr;
        m_duration = 0;
    }

    // 定期的にファイルサイズの更新状況を取得するため
    if (m_fShareWrite) {
        m_renewSizeTick = ::GetTickCount();
        m_fileSize = fileSize;
    }

    return true;
ERROR_EXIT:
    Close();
    return false;
}


void CTsSender::SetUdpAddress(LPCSTR addr, unsigned short port)
{
    // パイプ転送を停止
    ClosePipe();
    m_pipeName[0] = 0;

    if (!addr[0]) CloseSocket();
    ::lstrcpynA(m_udpAddr, addr, MAX_URI);
    m_udpPort = port;
}


void CTsSender::SetPipeName(LPCTSTR name)
{
    // UDP転送を停止
    CloseSocket();
    m_udpAddr[0] = 0;

    if (::lstrcmp(m_pipeName, name)) {
        ClosePipe();
        ::lstrcpyn(m_pipeName, name, MAX_PATH-1);
    }
}


void CTsSender::Close()
{
    if (m_hFile) {
        ::CloseHandle(m_hFile);
        m_hFile = NULL;
    }
    if (m_pBuf) {
        delete [] m_pBuf;
        m_pBuf = NULL;
    }
    CloseSocket();
    ClosePipe();
    m_udpAddr[0] = 0;
    m_pipeName[0] = 0;
}


// TSパケットを読んで、一度だけ転送する
// ファイルの終端に達した場合はfalseを返す
// 適当にレート制御される
bool CTsSender::Send()
{
    // 動画の長さ情報を更新
    if (m_fShareWrite) {
        DWORD tick = ::GetTickCount();
        DWORD diff = tick - m_renewSizeTick;
        if (2000 <= diff) {
            long long fileSize = GetFileSize();
            if (fileSize >= 0) {
                if (m_fSpecialExtending && fileSize < m_fileSize) {
                    // 容量確保領域が録画終了によって削除されたと仮定する
                    m_fFixed = true;
                    m_fSpecialExtending = false;
                }
                else if (!m_fSpecialExtending && fileSize == m_fileSize) {
                    m_fFixed = true;
                }
                else {
                    m_fFixed = false;
                    m_duration += diff;
                }
                m_fileSize = fileSize;
            }
            m_renewSizeTick = tick;
        }
    }

    if (m_fPause) return true;

    // パケットの読み込みと転送
    for (bool fSent = false; !fSent;) {
        if (!ReadPacket()) {
            ConsumeBuffer(true);
            if (!ReadPacket()) return false;
            fSent = true;
        }
        // 転送レート制御
        if (m_fPcr) {
            DWORD tickDiff = ::GetTickCount() - m_baseTick;
            if (MSB(tickDiff)) tickDiff = 0;

            DWORD pcrDiff = m_pcr - m_basePcr;
            if (MSB(pcrDiff)) pcrDiff = 0;
            
            // 再生速度が上がる=PCRの進行が遅くなる
            int msec = (int)((long long)pcrDiff * m_speedDen / m_speedNum / PCR_PER_MSEC) - tickDiff;

            // 制御しきれない場合は一度リセット
            if (msec < -2000 || 2000 < msec) {
                // 基準PCRを設定(TVTest側のストアを増やすため少し引く)
                m_baseTick = ::GetTickCount() - BASE_DIFF_MSEC;
                m_basePcr = m_pcr;
            }
            else if (msec > 0) ::Sleep(msec);
        }
    }
    return true;
}


// ファイルの先頭にシークする
bool CTsSender::SeekToBegin()
{
    return Seek(0, FILE_BEGIN);
}


// ファイルの末尾から約2秒前にシークする
bool CTsSender::SeekToEnd()
{
    return m_fSpecialExtending ? false : Seek(-GetRate()*2, FILE_END);
}


// 現在の再生位置からmsecだけシークする
// 現在の再生位置が不明の場合はSeekToBegin()
// シーク可能範囲を超えるor下回る場合は先頭or末尾の約2秒前までシークする
// シークできなかったorシークが打ち切られたときはfalseを返す
bool CTsSender::Seek(int msec)
{
    if (!m_pcrCount) return SeekToBegin();

    long long rate = (long long)GetRate();
    long long size = GetFileSize();
    long long pos;

    if (size < 0) return false;

    // -5000<=msec<=5000になるまで動画レートから概算シーク
    // 5ループまでに収束しなければ失敗
    for (int i = 0; i < 5 && (msec < -5000 || 5000 < msec); i++) {
        if ((pos = GetFilePosition()) < 0) return false;

        // 前方or後方にこれ以上シークできない場合
        if (msec < 0 && pos <= rate ||
            msec > 0 && pos >= size - rate*2) return true;

        long long approx = pos + rate * msec / 1000;
        if (approx > size - rate*2) approx = size - rate*2;
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
            if (pos >= size - rate*2) return true;

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
    if (m_fPause && !fPause) {
        // 基準PCRを設定(TVTest側のストアを増やすため少し引く)
        m_baseTick = ::GetTickCount() - BASE_DIFF_MSEC;
        m_basePcr = m_pcr;
    }
    m_fPause = fPause;
}


// 等速に対する再生速度比を分子分母で指定
// 負の値は想定していない
// denに0を与えてはいけない
void CTsSender::SetSpeed(int num, int den)
{
    m_speedNum = num;
    m_speedDen = den;
    // 基準PCRを設定
    m_baseTick = ::GetTickCount();
    m_basePcr = m_pcr;
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
    return m_duration < 0 ? 0 : m_duration;
}


// 動画の再生位置(ミリ秒)を取得する
int CTsSender::GetPosition() const
{
    return !m_pcrCount ? 0 : (m_pcr - m_initPcr) / PCR_PER_MSEC;
}


// TOT時刻から逆算した動画の放送時刻(ミリ秒)を取得する
// 取得失敗時は負を返す
int CTsSender::GetBroadcastTime() const
{
    if (m_totBase < 0) return -1;

    int totInit = m_totBase - (int)((m_totBasePcr - m_initPcr) / PCR_PER_MSEC);
    if (totInit < 0) totInit += 24 * 3600000; // 1日足す
    return totInit;
}


// 動画全体のレート(Bytes/秒)を取得する
// 正確でない場合がある
// 極端な値は抑制される
int CTsSender::GetRate() const
{
    int rate;
    if (m_fSpecialExtending) {
        rate = m_specialExtendInitRate;
    }
    else {
        long long fileSize = GetFileSize();
        rate = fileSize < 0 || m_duration <= 0 ? TS_SUPPOSED_RATE :
               static_cast<int>(fileSize * 1000 / m_duration);
    }
    return rate < TS_SUPPOSED_RATE/128 ? TS_SUPPOSED_RATE/128 :
           rate > TS_SUPPOSED_RATE*4 ? TS_SUPPOSED_RATE*4 : rate;
}


// ファイル先頭の56bitハッシュ値を取得する
long long CTsSender::GetFileHash() const
{
    return m_hash;
}


// TSパケットを1つ読む
bool CTsSender::ReadPacket(int count)
{
    if (!m_hFile || !m_pBuf || count <= 0) return false;

    if (m_curr + m_unitSize >= m_tail) {
        int n = static_cast<int>(m_pBuf + m_unitSize * BUFFER_LEN - m_tail);
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
            // 不正なパケットが続けばカウントは減っていく
            return ReadPacket(count - 1);
        }
        m_curr = p;
        if (m_curr + m_unitSize > m_tail) return ReadPacket(count - 1);
    }
    
    m_fPcr = false;

    TS_HEADER header;
    extract_ts_header(&header, m_curr);
    
    if (header.adaptation_field_control & 2 &&
        !header.transport_error_indicator)
    {
        // アダプテーションフィールドがある
        ADAPTATION_FIELD adapt;
        extract_adaptation_field(&adapt, m_curr + 4);

        if (adapt.pcr_flag) {
            // 参照PIDが決まっていないとき、最初に3回PCRが出現したPIDを参照PIDとする
            // 参照PIDのPCRが現れることなく3回別のPCRが出現すれば、参照PIDを変更する
            if (header.pid != m_pcrPid) {
                bool fFound = false;
                for (int i = 0; i < m_pcrPidsLen; i++) {
                    if (m_pcrPids[i] == header.pid) {
                        if (++m_pcrPidCounts[i] >= 3) m_pcrPid = header.pid;
                        fFound = true;
                        break;
                    }
                }
                if (!fFound && m_pcrPidsLen < PCR_PIDS_MAX) {
                    m_pcrPids[m_pcrPidsLen] = header.pid;
                    m_pcrPidCounts[m_pcrPidsLen] = 1;
                    m_pcrPidsLen++;
                }
            }

            // 参照PIDのときはPCRを取得する
            if (header.pid == m_pcrPid) {
                m_pcrPidsLen = 0;
                m_pcr = (DWORD)adapt.pcr_45khz;
            
                if (m_pcrCount++ == 0) {
                    // 基準PCRを設定(TVTest側のストアを増やすため少し引く)
                    m_baseTick = ::GetTickCount() - BASE_DIFF_MSEC;
                    m_basePcr = m_pcr;
                }
                m_fPcr = true;
            }
        }
    }

    if (header.pid == 0x14 && m_pcrCount &&
        !header.transport_error_indicator &&
        header.payload_unit_start_indicator &&
        header.adaptation_field_control == 1)
    {
        // TOT-PCR対応情報を取得する
        int pointerField = m_curr[4];
        BYTE *pTable = m_curr + 5 + pointerField;
        if (pTable + 7 < m_curr + 188 && (pTable[0] == 0x73 || pTable[0] == 0x70)) {
            // BCD解析(ARIB STD-B10)
            m_totBase = ((pTable[5]>>4)*10 + (pTable[5]&0x0f)) * 3600000 +
                        ((pTable[6]>>4)*10 + (pTable[6]&0x0f)) * 60000 +
                        ((pTable[7]>>4)*10 + (pTable[7]&0x0f)) * 1000;
            m_totBasePcr = m_pcr;
        }
    }

    m_curr += m_unitSize;
    return true;
}


// 読み込まれたTSパケットを転送・消費する
void CTsSender::ConsumeBuffer(bool fSend)
{
    if (!m_pBuf) return;

    if (fSend && m_curr > m_pBuf) {
        if (m_udpAddr[0]) {
            if (!m_sock) OpenSocket();
            if (m_sock) {
                // UDP転送
                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                addr.sin_port = htons(m_udpPort);
                addr.sin_addr.S_un.S_addr = inet_addr(m_udpAddr);
                if (::sendto(m_sock, (const char*)m_pBuf, (int)(m_curr - m_pBuf),
                             0, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
                {
                    CloseSocket();
                }
            }
        }
        if (m_pipeName[0]) {
            if (!m_hPipe) OpenPipe();
            if (m_hPipe) {
                // パイプ転送
                DWORD written;
                if (!::WriteFile(m_hPipe, m_pBuf, (DWORD)(m_curr - m_pBuf), &written, NULL)) {
                    ClosePipe();
                }
            }
        }
    }
    int n = static_cast<int>(m_tail - m_curr);
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
    for (int i = 0; i < 20000 && !m_pcrCount; i++) {
        if (!ReadPacket()) {
            ConsumeBuffer(false);
            if (!ReadPacket()) break;
        }
    }

    return true;
}


// ファイル中にTSデータが存在する境目までシークする
// log2(ファイルサイズ/TS_SUPPOSED_RATE)回ぐらい再帰する
bool CTsSender::SeekToBoundary(long long predicted, long long range, LPBYTE pWork, int workSize)
{
    if (!m_hFile) return false;

    LARGE_INTEGER lDistanceToMove;
    lDistanceToMove.QuadPart = predicted;
    if (!::SetFilePointerEx(m_hFile, lDistanceToMove, NULL, FILE_BEGIN)) return false;

    if (range < TS_SUPPOSED_RATE) return true;

    DWORD readBytes;
    if (!::ReadFile(m_hFile, pWork, workSize, &readBytes, NULL)) return false;
    int unitSize = select_unit_size(pWork, pWork + readBytes);

    ::Sleep(20); // ディスクへの思いやり
    return SeekToBoundary(predicted + (unitSize < 188 ? -range/4 : range/4), range/2, pWork, workSize);
}


void CTsSender::OpenSocket()
{
    CloseSocket();

    WSADATA wsaData;
    if (::WSAStartup(MAKEWORD(2,0), &wsaData) == 0) {
        m_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (m_sock == INVALID_SOCKET) {
            m_sock = NULL;
            ::WSACleanup();
        }
    }
}


void CTsSender::CloseSocket()
{
    if (m_sock) {
        ::closesocket(m_sock);
        m_sock = NULL;
        ::WSACleanup();
    }
}


void CTsSender::OpenPipe()
{
    if (!m_pipeName[0]) return;
    ClosePipe();

    if (::WaitNamedPipe(m_pipeName, NMPWAIT_USE_DEFAULT_WAIT)) {
        m_hPipe = ::CreateFile(m_pipeName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (m_hPipe == INVALID_HANDLE_VALUE) {
            m_hPipe = NULL;
        }
    }
}


void CTsSender::ClosePipe()
{
    if (m_hPipe) {
        ::CloseHandle(m_hPipe);
        m_hPipe = NULL;
    }
}
