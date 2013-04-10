#include <WinSock2.h>
#include <Windows.h>
#include "Util.h"
#include "AsyncFileReader.h"
#include "TsSender.h"

#ifndef __AFX_H__
#include <cassert>
#define ASSERT assert
#endif

#define MSB(x) ((x) & 0x80000000)

CTsTimestampShifter::CTsTimestampShifter()
    : m_shift45khz(0)
{
    memset(&m_pat, 0, sizeof(m_pat));
}

CTsTimestampShifter::~CTsTimestampShifter()
{
    Reset();
}

void CTsTimestampShifter::SetValue(DWORD shift45khz)
{
    m_shift45khz = shift45khz;
}

void CTsTimestampShifter::Reset()
{
    while (m_pat.pid_count > 0)
        delete m_pat.pmt[--m_pat.pid_count];
    memset(&m_pat, 0, sizeof(m_pat));
}

static void PcrToArray(BYTE *pDest, DWORD clk45khz)
{
    pDest[0] = (BYTE)(clk45khz>>24);
    pDest[1] = (BYTE)(clk45khz>>16);
    pDest[2] = (BYTE)(clk45khz>>8);
    pDest[3] = (BYTE)clk45khz;
}

static void PtsDtsToArray(BYTE *pDest, DWORD clk45khz)
{
    pDest[0] = (BYTE)((pDest[0]&0xf0)|(clk45khz>>28)|1); // 31:29
    pDest[1] = (BYTE)(clk45khz>>21);                     // 28:21
    pDest[2] = (BYTE)((clk45khz>>13)|1);                 // 20:14
    pDest[3] = (BYTE)(clk45khz>>6);                      // 13:6
    pDest[4] = (BYTE)((clk45khz<<2)|(pDest[4]&0x02)|1);  // 5:0
}

void CTsTimestampShifter::Transform(BYTE *pPacket)
{
    TS_HEADER header;
    extract_ts_header(&header, pPacket);

    if ((header.adaptation_field_control&2)/*2,3*/ &&
        !header.transport_error_indicator)
    {
        ADAPTATION_FIELD adapt;
        extract_adaptation_field(&adapt, pPacket + 4);
        if (adapt.pcr_flag && header.pid != 0) {
            // PMTで指定されたPCRのみ変更
            for (int i = 0; i < m_pat.pid_count; ++i) {
                if (header.pid == m_pat.pmt[i]->pcr_pid) {
                    // PCRをシフト
                    PcrToArray(pPacket + 6, (DWORD)adapt.pcr_45khz + m_shift45khz);
                    break;
                }
            }
        }
    }

    if (!(header.adaptation_field_control&1)/*0,2*/ ||
        header.transport_scrambling_control ||
        header.transport_error_indicator) return;

    BYTE *pPayload = pPacket + 4;
    if (header.adaptation_field_control == 3) {
        // アダプテーションに続けてペイロードがある
        ADAPTATION_FIELD adapt;
        extract_adaptation_field(&adapt, pPayload);
        if (!adapt.adaptation_field_length) return;
        pPayload += adapt.adaptation_field_length + 1;
    }
    int payloadSize = 188 - static_cast<int>(pPayload - pPacket);

    // PAT監視
    if (header.pid == 0) {
        extract_pat(&m_pat, pPayload, payloadSize,
                    header.payload_unit_start_indicator,
                    header.continuity_counter);
        return;
    }
    // PATリストにあるPMT監視
    for (int i = 0; i < m_pat.pid_count; ++i) {
        if (header.pid == m_pat.pid[i]/* && header.pid != 0*/) {
            extract_pmt(m_pat.pmt[i], pPayload, payloadSize,
                        header.payload_unit_start_indicator,
                        header.continuity_counter);
            return;
        }
    }
    if (header.payload_unit_start_indicator) {
        // ここに来る頻度はそれほど高くないので最適化していない
        // 全てのPMTリストにあるPES監視
        for (int i = 0; i < m_pat.pid_count; ++i) {
            PMT *pPmt = m_pat.pmt[i];
            for (int j = 0; j < pPmt->pid_count; ++j) {
                if (header.pid == pPmt->pid[j]) {
                    PES_HEADER pesHeader;
                    extract_pes_header(&pesHeader, pPayload, payloadSize/*, pPmt->stream_type[j]*/);
                    if (pesHeader.packet_start_code_prefix &&
                        pesHeader.pts_dts_flags >= 2)
                    {
                        // PTSをシフト
                        PtsDtsToArray(pPayload + 9, (DWORD)pesHeader.pts_45khz + m_shift45khz);
                        if (pesHeader.pts_dts_flags == 3) {
                            // DTSをシフト
                            PtsDtsToArray(pPayload + 14, (DWORD)pesHeader.dts_45khz + m_shift45khz);
                        }
                    }
                    return;
                }
            }
        }
    }
}


CTsSender::CTsSender()
    : m_curr(NULL)
    , m_head(NULL)
    , m_tail(NULL)
    , m_unitSize(0)
    , m_fTrimPacket(false)
    , m_fModTimestamp(false)
    , m_pcrDisconThreshold(0xffffffff)
    , m_sock(NULL)
    , m_udpPort(0)
    , m_hPipe(NULL)
    , m_baseTick(0)
    , m_renewSizeTick(0)
    , m_renewDurTick(0)
    , m_renewFsrTick(0)
    , m_pcr(0)
    , m_basePcr(0)
    , m_initPcr(0)
    , m_prevPcr(0)
    , m_fEnPcr(false)
    , m_fShareWrite(false)
    , m_fFixed(false)
    , m_fPause(false)
    , m_fForceSyncRead(false)
    , m_pcrPid(-1)
    , m_pcrPidsLen(0)
    , m_fileSize(0)
    , m_duration(0)
    , m_totBase(0)
    , m_totBasePcr(0)
    , m_hash(0)
    , m_speedNum(100)
    , m_speedDen(100)
    , m_initStore(INITIAL_STORE_MSEC)
    , m_fSpecialExtending(false)
    , m_specialExtendInitRate(0)
    , m_adjState(0)
    , m_adjBaseTick(0)
    , m_adjHoldTick(0)
    , m_adjAmount(0)
    , m_adjDelta(0)
{
    m_udpAddr[0] = 0;
    m_pipeName[0] = 0;
    m_liAdjFreq.QuadPart = 0;
    m_liAdjBase.QuadPart = 0;
}


CTsSender::~CTsSender()
{
    Close();
}


bool CTsSender::Open(LPCTSTR path, DWORD salt, int bufSize, bool fConvTo188, bool fUseQpc, int pcrDisconThresholdMsec)
{
    Close();

    // まず読み込み共有で開いてみる
    m_fShareWrite = false;
    m_fFixed = true;
    if (!m_file.Open(path, FILE_SHARE_READ)) {
        // 録画中かもしれない。書き込み共有で開く
        m_fShareWrite = true;
        m_fFixed = false;
        if (!m_file.Open(path, FILE_SHARE_READ | FILE_SHARE_WRITE)) {
            return false;
        }
    }

    // TSパケットの単位を決定
    BYTE buf[8192];
    int readBytes = m_file.SyncRead(buf, sizeof(buf));
    if (readBytes < 0) goto ERROR_EXIT;
    m_unitSize = select_unit_size(buf, buf + readBytes);
    if (m_unitSize < 188 || 320 < m_unitSize) goto ERROR_EXIT;

    // 転送時にパケットを188Byteに詰めるかどうか
    // TimestampedTS(192Byte)の場合のみ変換する
    m_fTrimPacket = fConvTo188 && m_unitSize == 192;

    // 識別情報としてファイル先頭の56bitハッシュ値をとる
    m_hash = CalcHash(buf, min(readBytes, 2048), salt);
    if (m_hash < 0) goto ERROR_EXIT;

    // PCRの連続性を調べることでレート制御リセットの参考とする
    // PCRの挿入間隔は規定により100msを超えない
    m_pcrDisconThreshold = pcrDisconThresholdMsec <= 0 ? 0xffffffff :
                           min(max(pcrDisconThresholdMsec,200),100000) * PCR_PER_MSEC;

    // バッファ確保
    int bufNum = max(bufSize / (m_unitSize*BUFFER_LEN), 1);
    int olReqUnit = min(max(bufNum / 2, 2), 10);
    if (!m_file.SetupBuffer(m_unitSize*BUFFER_LEN, m_unitSize, bufNum, olReqUnit)) {
        goto ERROR_EXIT;
    }

    // PCR参照PIDをクリア
    m_pcrPid = -1;
    m_pcrPidsLen = 0;

    // TOT-PCR対応情報をクリア
    m_totBase = -1;

    // QueryPerformanceCounterによるTickカウント補正をするかどうか
    m_adjState = fUseQpc ? 1 : 0;

    m_fPause = false;
    SetSpeed(100, 100);

    __int64 fileSize = m_file.GetSize();
    if (fileSize < 0) goto ERROR_EXIT;

    // ファイル先頭のPCRを取得
    if (!SeekToBegin()) goto ERROR_EXIT;
    m_initPcr = m_pcr;

    // 動画の長さを取得
    m_fSpecialExtending = false;
    if (Seek(-TS_SUPPOSED_RATE * 2, FILE_END)) {
        // ファイル末尾が正常である場合
        m_duration = (int)(DiffPcr(m_pcr, m_initPcr) / PCR_PER_MSEC) + 2000;
        m_duration = (int)(DiffPcr(m_pcr, m_initPcr) / PCR_PER_MSEC) +
                     (int)((long long)TS_SUPPOSED_RATE * 2000 / GetRate());
    }
    else if (m_fShareWrite &&
             SeekToBoundary(fileSize / 2, fileSize, buf, sizeof(buf) / 2) &&
             Seek(-TS_SUPPOSED_RATE * 2, FILE_CURRENT))
    {
        // 書き込み共有かつファイル末尾が正常でない場合
        __int64 filePos = m_file.GetPosition();
        m_duration = (int)(DiffPcr(m_pcr, m_initPcr) / PCR_PER_MSEC) + 2000;
        // ファイルサイズからレートを計算できないため
        m_specialExtendInitRate = filePos < 0 || m_duration <= 0 ? TS_SUPPOSED_RATE :
                                  static_cast<int>((filePos + TS_SUPPOSED_RATE * 2) * 1000 / m_duration);
        m_fSpecialExtending = true;
    }
    else {
        // 最低限、再生はできる場合
        m_duration = 0;
    }

    // 再生レートが極端に小さい場合(ワンセグ等)はバッファを縮める
    if (GetRate() < m_unitSize*BUFFER_LEN) {
        if (!m_file.SetupBuffer(GetRate() / m_unitSize * m_unitSize, m_unitSize, bufNum, olReqUnit)) {
            goto ERROR_EXIT;
        }
    }

    if (!SeekToBegin()) goto ERROR_EXIT;

    // 追っかけ時にファイルサイズ等の更新を取得するため
    if (m_fShareWrite) {
        m_renewSizeTick = m_renewDurTick = m_renewFsrTick = GetAdjTickCount();
        m_fileSize = fileSize;
    }
    m_fForceSyncRead = false;

    // PCR_LAP_THRESHOLD-PCR初期値だけPCR/PTS/DTSをシフト
    m_tsShifter.SetValue((DWORD)PCR_LAP_THRESHOLD - m_initPcr);

    return true;
ERROR_EXIT:
    Close();
    return false;
}


// Tickカウント補正の初期設定をする
void CTsSender::SetupQpc()
{
    if (m_adjState == 1) {
        // msdnの推奨に従って現在スレッドのプロセッサ固定
        ::SetThreadAffinityMask(::GetCurrentThread(), 1);
        if (::QueryPerformanceFrequency(&m_liAdjFreq) &&
            ::QueryPerformanceCounter(&m_liAdjBase))
        {
            m_adjBaseTick = m_adjHoldTick = GetAdjTickCount();
            m_adjAmount = m_adjDelta = 0;
            m_adjState = 2;
        }
    }
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


// ラップアラウンドをなるべく避けるようにPCR/PTS/DTSを変更するかどうか
void CTsSender::SetModTimestamp(bool fModTimestamp)
{
    m_fModTimestamp = fModTimestamp;
}


void CTsSender::Close()
{
    m_file.Close();
    CloseSocket();
    ClosePipe();
    m_udpAddr[0] = 0;
    m_pipeName[0] = 0;
    m_tsShifter.Reset();
    m_tsShifter.SetValue(0);
}


// PCRが現れるまでTSパケットを読んで転送する
// 適当にレート制御される
// 戻り値: 0:ファイルの終端に達したかPCRが挿入されていない, 1:正常に転送された,
//         2:正常に転送されたが、レート制御がリセットされた
int CTsSender::Send()
{
    if (!IsOpen()) return 0;

    int rv = 1;
    if (m_fPause) ::Sleep(100);
    else {
        if (ReadToPcr(40000, true, m_fForceSyncRead) != 2) rv = 0;
    }
    // 補正により一気に増加する可能性を避けるため定期的に呼ぶ
    DWORD tick = GetAdjTickCount();

    if (m_fShareWrite) {
        // 動画の長さ情報を更新
        if (tick - m_renewSizeTick >= RENEW_SIZE_INTERVAL) {
            __int64 fileSize = m_file.GetSize();
            if (fileSize >= 0) {
                if (m_fSpecialExtending && fileSize < m_fileSize) {
                    // 容量確保領域が録画終了によって削除されたと仮定する
                    m_fFixed = true;
                    m_fSpecialExtending = false;
                }
                else if (!m_fSpecialExtending && fileSize == m_fileSize) {
                    // 伸ばしすぎた分を戻す
                    if (!m_fFixed) m_duration -= RENEW_SIZE_INTERVAL;
                    m_fFixed = true;
                }
                else {
                    m_fFixed = false;
                }
                m_fileSize = fileSize;
            }
            m_renewSizeTick = tick;
        }
        if (!m_fFixed) m_duration += tick - m_renewDurTick;
        m_renewDurTick = tick;

        // ファイルを強制的に同期読み込みさせるかどうか判断する
        // (追っかけ時のファイル末尾はデータの有無が不安定なため)
        if (tick - m_renewFsrTick >= RENEW_FSR_INTERVAL) {
            if (!m_fFixed) {
                int bufSize = m_file.GetBufferSize();
                int rate = GetRate();
                if (!m_fSpecialExtending) {
                    // 追っかけ時の末尾付近では先読みしない
                    __int64 fileRemain = m_file.GetSize() - m_file.GetPosition();
                    if (!m_fForceSyncRead && fileRemain < bufSize + rate*3) {
                        m_fForceSyncRead = true;
                        DEBUG_OUT(TEXT("CTsSender::Send(): ForceSyncRead On\n"));
                    }
                    else if (m_fForceSyncRead && fileRemain > bufSize + rate*6) {
                        m_fForceSyncRead = false;
                        DEBUG_OUT(TEXT("CTsSender::Send(): ForceSyncRead Off\n"));
                    }
                }
                else {
                    // 容量確保録画の追っかけ時は(多少不正確でも)動画の長さから末尾を判断する
                    int msecRemain = GetDuration() - GetPosition();
                    if (!m_fForceSyncRead && msecRemain < (__int64)bufSize*1000/rate + 6000) {
                        m_fForceSyncRead = true;
                        DEBUG_OUT(TEXT("CTsSender::Send(): ForceSyncRead On\n"));
                    }
                    else if (m_fForceSyncRead && msecRemain > (__int64)bufSize*1000/rate + 9000) {
                        m_fForceSyncRead = false;
                        DEBUG_OUT(TEXT("CTsSender::Send(): ForceSyncRead Off\n"));
                    }
                }
            }
            else {
                m_fForceSyncRead = false;
            }
            m_renewFsrTick = tick;
        }
    }

    if (!m_fPause && rv) {
        // 転送レート制御
        DWORD tickDiff = tick - m_baseTick;
        DWORD pcrDiff = DiffPcr(m_pcr, m_basePcr);
        // 再生速度が上がる=PCRの進行が遅くなる
        int msec = (int)((long long)pcrDiff * m_speedDen / m_speedNum / PCR_PER_MSEC) - tickDiff;
        // PCRの連続性チェックのため
        DWORD prevPcrAbsDiff = MSB(m_pcr-m_prevPcr) ? m_prevPcr-m_pcr : m_pcr-m_prevPcr;
        m_prevPcr = m_pcr;

        // 制御しきれない場合は一度リセット
        if (msec < -2000 || 2000 * m_speedDen / m_speedNum < msec ||
            prevPcrAbsDiff > m_pcrDisconThreshold)
        {
            // 基準PCRを設定(受信側のストアを増やすため少し引く)
            m_baseTick = tick - m_initStore;
            m_basePcr = m_pcr;
            rv = 2;
        }
        else if (msec > 0) ::Sleep(msec);
    }
    return rv;
}


static const BYTE NULL_PACKET[] = {
    0x47, 0x1F, 0xFF, 0x10, 'E', 'M', 'P', 'T',
    'Y', 'P', 'A', 'T'
};

static const BYTE EMPTY_PAT[] = {
    0x47, 0x60, 0x00, 0x10, 0x00, 0x00, 0xB0, 0x09,
    0x00, 0x00, 0xC1, 0x00, 0x00, 0x33, 0x4F, 0xF8,
    0xA0
};

// PMTリストが空のPATを送る
void CTsSender::SendEmptyPat()
{
    if (!IsOpen()) return;
    ASSERT(m_unitSize <= 320);

    BYTE buf[320 * 9];
    int unitSize = m_fTrimPacket ? 188 : m_unitSize;

    // 同期できるようにNULLパケットで囲っておく
    memset(buf, 0xFF, unitSize*9);
    for (int i = 0; i < 9; ++i)
        memcpy(buf + unitSize*i, NULL_PACKET, sizeof(NULL_PACKET));
    memcpy(buf + unitSize*4, EMPTY_PAT, sizeof(EMPTY_PAT));

    SendData(buf, unitSize*9);
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
    if (!m_fEnPcr) return SeekToBegin();

    __int64 rate = GetRate();
    __int64 size = m_file.GetSize();
    __int64 pos;

    if (size < 0) return false;

    // -5000<=msec<=5000になるまで動画レートから概算シーク
    // 5ループまでに収束しなければ失敗
    for (int i = 0; i < 5 && (msec < -5000 || 5000 < msec); i++) {
        if ((pos = m_file.GetPosition()) < 0) return false;

        // 前方or後方にこれ以上シークできない場合
        if (msec < 0 && pos <= rate ||
            msec > 0 && pos >= size - rate*2) return true;

        __int64 approx = pos + rate * msec / 1000;
        if (approx > size - rate*2) approx = size - rate*2;
        if (approx < 0) approx = 0;

        DWORD prevPcr = m_pcr;
        if (!Seek(approx, FILE_BEGIN)) return false;

        // 移動分を差し引く
        msec += MSB(m_pcr-prevPcr) ? (int)(prevPcr-m_pcr) / PCR_PER_MSEC :
                                    -(int)(m_pcr-prevPcr) / PCR_PER_MSEC;
    }
    if (msec < -5000 || 5000 < msec) return false;

    if (msec < 0) {
        // 約1秒ずつ前方シーク
        // 10ループまでに収束しなければ失敗
        int mul = 1;
        for (int i = 0; i < 10 && msec < -500; i++) {
            if ((pos = m_file.GetPosition()) < 0) return false;
            if (pos <= rate) return true;

            DWORD prevPcr = m_pcr;
            if (!Seek(-rate * mul, FILE_CURRENT) || MSB(prevPcr-m_pcr)) return false;
            msec += (int)(prevPcr-m_pcr) / PCR_PER_MSEC;
            // 動画レートが極端に小さいとき動かない可能性があるため
            if (m_pcr == prevPcr) ++mul;
        }
        if (msec < -500) return false;
    }
    else {
        // 約1秒ずつ後方シーク
        // 10ループまでに収束しなければ失敗
        for (int i = 0; i < 10 && msec > 500; i++) {
            if ((pos = m_file.GetPosition()) < 0) return false;
            if (pos >= size - rate*2) return true;

            DWORD prevPcr = m_pcr;
            if (!Seek(rate, FILE_CURRENT) || MSB(m_pcr-prevPcr)) return false;
            msec -= (int)(m_pcr-prevPcr) / PCR_PER_MSEC;
        }
        if (msec > 500) return false;
    }
    return true;
}


void CTsSender::Pause(bool fPause)
{
    if (m_fPause && !fPause) {
        // 基準PCRを設定(受信側のストアを増やすため少し引く)
        m_baseTick = GetAdjTickCount() - m_initStore;
        m_basePcr = m_pcr;
    }
    m_fPause = fPause;
}


// 等速に対する再生速度比を分子分母で指定
// 0以下の値を与えてはいけない
void CTsSender::SetSpeed(int num, int den)
{
    m_speedNum = num;
    m_speedDen = den;
    m_initStore = min(INITIAL_STORE_MSEC * den / num, INITIAL_STORE_MSEC);
    // 基準PCRを設定
    m_baseTick = GetAdjTickCount();
    m_basePcr = m_pcr;
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
    return !m_fEnPcr ? 0 : DiffPcr(m_pcr, m_initPcr) / PCR_PER_MSEC;
}


// TOT時刻から逆算した動画の放送時刻(ミリ秒)を取得する
// 取得失敗時は負を返す
int CTsSender::GetBroadcastTime() const
{
    if (m_totBase < 0) return -1;

    int totInit = m_totBase - (int)(DiffPcr(m_totBasePcr, m_initPcr) / PCR_PER_MSEC);
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
        __int64 fileSize = m_file.GetSize();
        rate = fileSize < 0 || m_duration <= 0 ? TS_SUPPOSED_RATE :
               static_cast<int>(fileSize * 1000 / m_duration);
    }
    return min(max(rate, TS_SUPPOSED_RATE/128), TS_SUPPOSED_RATE*4);
}


// 補正済みのTickカウントを取得する
DWORD CTsSender::GetAdjTickCount()
{
    DWORD tick = ::GetTickCount();
    if (m_adjState != 2) return tick;

    if (!m_adjDelta) {
        if (tick - m_adjHoldTick >= ADJUST_TICK_INTERVAL) {
            LARGE_INTEGER liNow;
            if (::QueryPerformanceCounter(&liNow)) {
                LONGLONG llDiff = (liNow.QuadPart - m_liAdjBase.QuadPart) * 1000 / m_liAdjFreq.QuadPart;
                m_adjDelta = (static_cast<DWORD>(llDiff) - (tick - m_adjBaseTick)) - m_adjAmount;
            }
            m_adjHoldTick = tick;
        }
    }
    if (m_adjDelta) {
        // 巻き戻り防止
        if (!MSB(tick + m_adjDelta - m_adjHoldTick)) {
            m_adjAmount += m_adjDelta;
            m_adjDelta = 0;
        }
    }
    DWORD adjTick = (m_adjDelta ? m_adjHoldTick : tick) + m_adjAmount;
#if 0
    static DWORD last;
    // 2回目以降のOpen()時には必ずしも成立しない
    ASSERT(!last || !MSB(adjTick - last));
    last = adjTick;
#endif
    return adjTick;
}


// PCRが現れるまでTSパケットを処理する
// 戻り値: 0:終端に達した, 1:PCRが現れる前に処理パケット数がlimitに達した, 2:正常に処理した
int CTsSender::ReadToPcr(int limit, bool fSend, bool fSyncRead)
{
    bool fPcr;
    do {
        if (!ReadPacket(fSend, fSyncRead, &fPcr)) return 0;
        // あまり貯めすぎると再生に影響するため
        if (m_curr - m_head >= BON_UDP_TSDATASIZE - 1880) RotateBuffer(fSend, fSyncRead);
    } while (!fPcr && --limit > 0);
    return fPcr ? 2 : 1;
}


// TSパケットを1つ処理する
bool CTsSender::ReadPacket(bool fSend, bool fSyncRead, bool *pfPcr)
{
    *pfPcr = false;

    if (!m_curr || m_tail - m_curr <= m_unitSize) {
        RotateBuffer(fSend, fSyncRead);
        if (!m_curr || m_tail - m_curr <= m_unitSize) {
            return false;
        }
    }

    if (m_curr[0] != 0x47 || m_curr[m_unitSize] != 0x47) {
        // m_currをパケットヘッダと同期させる
        int i = 0;
        for (; i < RESYNC_FAILURE_LIMIT; ++i) {
            if ((m_curr = resync(m_curr, m_tail, m_unitSize)) != NULL) break;
            m_curr = m_tail; // 処理済にする
            RotateBuffer(fSend, fSyncRead);
            if (!m_curr) return false;
        }
        if (i == RESYNC_FAILURE_LIMIT) return false;
        ASSERT(m_tail - m_curr > m_unitSize);
    }

    TS_HEADER header;
    extract_ts_header(&header, m_curr);
    // [統計(地上波)]
    // adaptation_field_control == 0:0.00%, 1:99.03%, 2:0.21%, 3:0.76%
    // payload_unit_start_indicator:2.09%

    if ((header.adaptation_field_control&2)/*2,3*/ &&
        !header.transport_error_indicator)
    {
        // アダプテーションフィールドがある
        ADAPTATION_FIELD adapt;
        extract_adaptation_field(&adapt, m_curr + 4);

        if (adapt.pcr_flag) {
            // 参照PIDが決まっていないとき、最初に3回PCRが出現したPIDを参照PIDとする
            // 参照PIDのPCRが現れることなく5回別のPCRが出現すれば、参照PIDを変更する
            if (header.pid != m_pcrPid) {
                bool fFound = false;
                for (int i = 0; i < m_pcrPidsLen; i++) {
                    if (m_pcrPids[i] == header.pid) {
                        ++m_pcrPidCounts[i];
                        if (m_pcrPid < 0 && m_pcrPidCounts[i] >= 3 || m_pcrPidCounts[i] >= 5) m_pcrPid = header.pid;
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
                if (!m_fEnPcr) {
                    // 基準PCRを設定(受信側のストアを増やすため少し引く)
                    m_baseTick = GetAdjTickCount() - m_initStore;
                    m_basePcr = m_pcr;
                    m_fEnPcr = true;
                }
                *pfPcr = true;
            }
        }
    }

    if (header.pid == 0x14 && m_fEnPcr &&
        !header.transport_scrambling_control &&
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

    // PCR/PTS/DTSを変更
    if (m_fModTimestamp) m_tsShifter.Transform(m_curr);

    m_curr += m_unitSize;
    return true;
}


// 処理済みのTSパケットを転送し、必要ならファイルから読み込む
void CTsSender::RotateBuffer(bool fSend, bool fSyncRead)
{
    ASSERT(IsOpen() && m_unitSize <= 320);

    // m_curr:パケット処理位置, m_head:転送開始位置, m_tail:有効データの末尾
    if (fSend && m_curr && m_curr > m_head) {
        if (m_fTrimPacket) {
            // 転送する部分だけを188Byte単位に詰める
            BYTE *p = m_head;
            BYTE *q = m_head;
            while (p <= m_curr - m_unitSize) {
                memmove(q, p, 188);
                p += m_unitSize;
                q += 188;
            }
            SendData(m_head, static_cast<int>(q - m_head));
        }
        else {
            SendData(m_head, static_cast<int>(m_curr - m_head));
        }
    }
    m_head = m_curr;

    // パケットを処理するだけのデータがなければ読む
    if (!m_curr || m_tail - m_curr <= m_unitSize) {
        // この繰越し処理によりm_headとパケットヘッダは同期する
        BYTE carry[320];
        int carrySize = !m_curr ? 0 : static_cast<int>(m_tail - m_curr);
        if (carrySize > 0) memcpy(carry, m_curr, carrySize);

        BYTE *pBuf;
        int readBytes = fSyncRead ? m_file.SyncRead(&pBuf) : m_file.Read(&pBuf);
        if (readBytes < 0) {
            m_curr = m_head = m_tail = NULL;
        }
        else {
            m_curr = m_head = pBuf - carrySize;
            m_tail = pBuf + readBytes;
            if (carrySize > 0) memcpy(m_curr, carry, carrySize);
        }
    }
}


// シークする
// シーク後、最初のPCRの位置まで読むことができればtrueを返す
bool CTsSender::Seek(__int64 distanceToMove, DWORD dwMoveMethod)
{
    __int64 lastPos = m_file.GetPosition();
    if (lastPos < 0) return false;

    if (!m_file.SetPointer(distanceToMove, dwMoveMethod)) return false;

    m_curr = m_head = m_tail = NULL;
    m_fEnPcr = false;
    if (ReadToPcr(40000, false, true) != 2) {
        // なるべく呼び出し前の状態に回復させるが、完全とは限らない
        if (m_file.SetPointer(lastPos, FILE_BEGIN)) {
            m_curr = m_head = m_tail = NULL;
            m_fEnPcr = false;
            if (ReadToPcr(40000, false, true) == 2) m_prevPcr = m_pcr;
        }
        return false;
    }
    m_prevPcr = m_pcr;
    return true;
}


// ファイル中にTSデータが存在する境目までシークする
// log2(ファイルサイズ/TS_SUPPOSED_RATE)回ぐらい再帰する
bool CTsSender::SeekToBoundary(__int64 predicted, __int64 range, BYTE *pWork, int workSize)
{
    if (!m_file.SetPointer(predicted, FILE_BEGIN)) return false;

    if (range < TS_SUPPOSED_RATE) return true;

    int readBytes = m_file.SyncRead(pWork, workSize);
    if (readBytes < 0) return false;
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

void CTsSender::SendData(BYTE *pData, int dataSize)
{
    if (m_udpAddr[0]) {
        if (!m_sock) OpenSocket();
        if (m_sock) {
            // UDP転送
            sockaddr_in addr = {0};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(m_udpPort);
            addr.sin_addr.S_un.S_addr = inet_addr(m_udpAddr);
            // BonDriver_UDPのキューサイズ以下に分割して送る
            int sendUnit = (BON_UDP_TSDATASIZE-1880) / m_unitSize * m_unitSize;
            for (BYTE *p = pData; p < pData + dataSize; p += sendUnit) {
                int sendSize = min(sendUnit, (int)(pData + dataSize - p));
                if (::sendto(m_sock, (const char*)p, sendSize, 0,
                             (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
                {
                    CloseSocket();
                    break;
                }
            }
        }
    }
    if (m_pipeName[0]) {
        if (!m_hPipe) OpenPipe();
        if (m_hPipe) {
            // パイプ転送
            DWORD written;
            if (!::WriteFile(m_hPipe, pData, (DWORD)dataSize, &written, NULL)) {
                ClosePipe();
            }
        }
    }
    /*static HANDLE hFile;
    if (!hFile) hFile = CreateFile(TEXT("foo.ts"), GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written;
    ::WriteFile(hFile, pData, (DWORD)dataSize, &written, NULL);*/
}
