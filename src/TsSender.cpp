#include <WinSock2.h>
#include <Windows.h>
#include <Shlwapi.h>
#include "ReadOnlyMpeg4File.h"
#include "Util.h"
#include "TsSender.h"

#ifndef ASSERT
#include <cassert>
#define ASSERT assert
#endif

#if 0 // アセンブリ検索用
#define MAGIC_NUMBER(x) { g_dwMagic=(x); }
static DWORD g_dwMagic;
#else
#define MAGIC_NUMBER
#endif

CTsTimestampShifter::CTsTimestampShifter()
    : m_shift45khz(0)
    , m_fEnabled(false)
{
    Reset();
}

void CTsTimestampShifter::SetInitialPcr(DWORD pcr45khz)
{
    // PCR_INITIAL_MARGIN-PCR初期値だけPCR/PTS/DTSをシフト
    m_shift45khz = (DWORD)PCR_INITIAL_MARGIN - pcr45khz;
}

void CTsTimestampShifter::Reset()
{
    PAT zeroPat = {};
    m_pat = zeroPat;
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

void CTsTimestampShifter::Transform_(BYTE *pPacket)
{
    MAGIC_NUMBER(0x73658165);

    TS_HEADER header;
    extract_ts_header(&header, pPacket);

    if ((header.adaptation_field_control&2)/*2,3*/ &&
        !header.transport_error_indicator)
    {
        ADAPTATION_FIELD adapt;
        extract_adaptation_field(&adapt, pPacket + 4);
        if (adapt.pcr_flag && header.pid != 0) {
            // PMTで指定されたPCRのみ変更
            for (size_t i = 0; i < m_pat.pmt.size(); ++i) {
                if (header.pid == m_pat.pmt[i].pcr_pid) {
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
        if (adapt.adaptation_field_length < 0) return;
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
    for (size_t i = 0; i < m_pat.pmt.size(); ++i) {
        if (header.pid == m_pat.pmt[i].pmt_pid/* && header.pid != 0*/) {
            extract_pmt(&m_pat.pmt[i], pPayload, payloadSize,
                        header.payload_unit_start_indicator,
                        header.continuity_counter);
            return;
        }
    }
    if (header.payload_unit_start_indicator) {
        // ここに来る頻度はそれほど高くないので最適化していない
        // 全てのPMTリストにあるPES監視
        for (size_t i = 0; i < m_pat.pmt.size(); ++i) {
            const PMT *pPmt = &m_pat.pmt[i];
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
    : m_curr(nullptr)
    , m_head(nullptr)
    , m_tail(nullptr)
    , m_unitSize(0)
    , m_fTrimPacket(false)
    , m_fUnderrunCtrl(false)
    , m_pcrDisconThreshold(0xffffffff)
    , m_sock(INVALID_SOCKET)
    , m_udpPort(0)
    , m_hPipe(INVALID_HANDLE_VALUE)
    , m_hCtrlPipe(INVALID_HANDLE_VALUE)
    , m_pipeNumber(-1)
    , m_baseTick(0)
    , m_renewSizeTick(0)
    , m_renewDurTick(0)
    , m_renewFsrTick(0)
    , m_pcr(0)
    , m_basePcr(0)
    , m_initPcr(0)
    , m_prevPcr(0)
    , m_lastSentPcr(0)
    , m_rateCtrlMsec(0)
    , m_fileState(FILE_ST_FIXED)
    , m_fEnPcr(false)
    , m_fPause(false)
    , m_fPurged(false)
    , m_fForceSyncRead(false)
    , m_pcrPid(-1)
    , m_pcrPidsLen(0)
    , m_fileSize(0)
    , m_duration(0)
    , m_totBase(0)
    , m_totBasePcr(0)
    , m_hash(-1)
    , m_oldHash(-1)
    , m_speedNum(100)
    , m_speedDen(100)
    , m_initStore(INITIAL_STORE_MSEC)
    , m_specialExtendInitRate(0)
    , m_adjBaseTick(0)
    , m_adjFreq(0)
    , m_adjBase(0)
{
}


CTsSender::~CTsSender()
{
    Close();
}


bool CTsSender::Open(LPCTSTR path, DWORD salt, int bufSize, bool fConvTo188, bool fUnderrunCtrl, bool fUseQpc,
                     int pcrDisconThresholdMsec, const char *&errorMessage)
{
    Close();

    bool fMpeg4 = !_tcsicmp(::PathFindExtension(path), TEXT(".mp4"));
    if (fMpeg4) {
        m_file.reset(new CReadOnlyMpeg4File());
        m_fileState = FILE_ST_FIXED;
        if (!m_file->Open(path, IReadOnlyFile::OPEN_FLAG_NORMAL, errorMessage)) {
            m_file.reset();
            return false;
        }
        // 動画の長さなどは既知
        m_unitSize = 188;
        m_duration = static_cast<CReadOnlyMpeg4File*>(m_file.get())->GetPositionMsecFromBytes(m_file->GetSize());
    }
    else {
        // まず読み込み共有で開いてみる
        m_file.reset(new CReadOnlyLocalFile());
        m_fileState = FILE_ST_FIXED;
        if (!m_file->Open(path, IReadOnlyFile::OPEN_FLAG_NORMAL, errorMessage)) {
            errorMessage = nullptr;
            // 録画中かもしれない。書き込み共有で開く
            m_fileState = FILE_ST_MAYBE_EXTENDING;
            if (!m_file->Open(path, IReadOnlyFile::OPEN_FLAG_NORMAL | IReadOnlyFile::OPEN_FLAG_SHARE_WRITE, errorMessage)) {
                m_file.reset();
                return false;
            }
        }
    }
    m_reader.SetFile(m_file.get());

    BYTE buf[8192];
    int readBytes = m_file->Read(buf, sizeof(buf));
    if (readBytes < 0) goto ERROR_EXIT;

    if (!fMpeg4) {
        // TSパケットの単位を決定
        m_unitSize = select_unit_size(buf, buf + readBytes);
        if (m_unitSize < 188 || 320 < m_unitSize) goto ERROR_EXIT;
    }

    // 転送時にパケットを188Byteに詰めるかどうか
    // TimestampedTS(192Byte)の場合のみ変換する
    m_fTrimPacket = fConvTo188 && m_unitSize == 192;

    // (可能ならば)受信側バッファのアンダーランを防ぐかどうか
    m_fUnderrunCtrl = fUnderrunCtrl;

    // 識別情報としてファイル先頭の56bitハッシュ値をとる
    m_hash = CalcHash(buf, readBytes, salt, &m_oldHash);
    // 旧仕様のハッシュ値と区別するため
    m_hash |= 1LL << 56;

    // PCRの連続性を調べることでレート制御リセットの参考とする
    // PCRの挿入間隔は規定により100msを超えない
    m_pcrDisconThreshold = pcrDisconThresholdMsec <= 0 ? 0xffffffff :
                           min(max(pcrDisconThresholdMsec,200),100000) * PCR_PER_MSEC;

    // バッファ確保
    int bufNum = max(bufSize / (m_unitSize*BUFFER_LEN), 1);
    if (!m_reader.SetupBuffer(m_unitSize*BUFFER_LEN, m_unitSize, bufNum)) {
        goto ERROR_EXIT;
    }

    // PCR参照PIDをクリア
    m_pcrPid = -1;
    m_pcrPidsLen = 0;

    // TOT-PCR対応情報をクリア
    m_totBase = -1;

    // QueryPerformanceCounterによるTickカウント補正をするかどうか
    m_adjFreq = fUseQpc ? -1 : 0;

    // 初期状態はポーズのほうが都合がよいため
    m_fPause = true;
    m_fPurged = true;
    SetSpeed(100, 100);

    __int64 fileSize = m_reader.GetFileSize();
    if (fileSize < 0) goto ERROR_EXIT;

    // ファイル先頭のPCRを取得
    if (!SeekToBegin()) {
        errorMessage = "CTsSender::Open(): SeekToBegin() Error";
        goto ERROR_EXIT;
    }
    m_initPcr = m_pcr;

    if (!fMpeg4) {
        // 動画の長さを取得
        m_duration = 0;
        // 最大で標準的なTSの末尾8秒間を調べる
        for (int sec = 1; sec <= 8; sec *= 2) {
            if (Seek(-TS_SUPPOSED_RATE * sec, IReadOnlyFile::MOVE_METHOD_END)) {
                // ファイル末尾が正常である場合
                __int64 startPos = m_reader.GetFilePosition();
                if (startPos >= 0) {
                    // 終端まで読む
                    while (ReadToPcr(false, true)) {
                        m_duration = DiffPcr(m_pcr, m_initPcr) / PCR_PER_MSEC;
                         __int64 filePos = m_reader.GetFilePosition();
                         if (filePos < startPos || filePos > startPos + TS_SUPPOSED_RATE * sec) {
                             // 追記された等。想定以上は読まない
                             break;
                         }
                    }
                    break;
                }
            }
        }
        if (m_duration <= 0 && m_file->IsShareWrite() &&
            SeekToBoundary(fileSize / 2, fileSize, buf, sizeof(buf) / 2))
        {
            // 書き込み共有かつファイル末尾が正常でない場合
            __int64 filePos = m_reader.GetFilePosition();
            for (int sec = 2; sec <= 8; sec *= 2) {
                if (Seek(filePos - TS_SUPPOSED_RATE * sec, IReadOnlyFile::MOVE_METHOD_BEGIN)) {
                    filePos = m_reader.GetFilePosition();
                    m_duration = (int)(DiffPcr(m_pcr, m_initPcr) / PCR_PER_MSEC) + 1000 * sec;
                    // ファイルサイズからレートを計算できないため
                    m_specialExtendInitRate = filePos < 0 || m_duration <= 0 ? TS_SUPPOSED_RATE :
                                              static_cast<int>((filePos + TS_SUPPOSED_RATE * sec) * 1000 / m_duration);
                    m_fileState = FILE_ST_SPECIAL_EXTENDING;
                    break;
                }
            }
        }
        if (m_duration <= 0) {
            // 最低限、再生はできる場合
            m_duration = 0;
        }
    }

    // 再生レートが極端に小さい場合(ワンセグ等)はバッファを縮める
    if (GetRate() < m_unitSize*BUFFER_LEN) {
        if (!m_reader.SetupBuffer(GetRate() / m_unitSize * m_unitSize, m_unitSize, bufNum)) {
            goto ERROR_EXIT;
        }
    }

    if (!SeekToBegin()) {
        errorMessage = "CTsSender::Open(): SeekToBegin()-2 Error";
        goto ERROR_EXIT;
    }

    // 追っかけ時にファイルサイズ等の更新を取得するため
    if (m_file->IsShareWrite()) {
        m_renewSizeTick = m_renewDurTick = m_renewFsrTick = GetAdjTickCount();
        m_fileSize = fileSize;
    }
    m_fForceSyncRead = false;

    m_tsShifter.SetInitialPcr(m_initPcr);

    return true;
ERROR_EXIT:
    Close();
    return false;
}


// Tickカウント補正の初期設定をする
void CTsSender::SetupQpc()
{
    if (m_adjFreq < 0) {
        // msdnの推奨に従って現在スレッドのプロセッサ固定
        ::SetThreadAffinityMask(::GetCurrentThread(), 1);
        LARGE_INTEGER liFreq, liBase;
        if (::QueryPerformanceFrequency(&liFreq) &&
            ::QueryPerformanceCounter(&liBase))
        {
            m_adjBaseTick = GetAdjTickCount();
            m_adjFreq = liFreq.QuadPart;
            m_adjBase = liBase.QuadPart;
        }
    }
}


void CTsSender::SetUdpPort(unsigned short port)
{
    // パイプ転送を停止
    ClosePipe();
    m_pipeNumber = -1;

    if (port == 0) CloseSocket();
    m_udpPort = port;
}


void CTsSender::SetPipeNumber(int n)
{
    // UDP転送を停止
    CloseSocket();
    m_udpPort = 0;

    if (m_pipeNumber != n) {
        ClosePipe();
        m_pipeNumber = n;
    }
}


// ラップアラウンドをなるべく避けるようにPCR/PTS/DTSを変更するかどうか
void CTsSender::SetModTimestamp(bool fModTimestamp)
{
    m_tsShifter.Enable(fModTimestamp);
}


void CTsSender::Close()
{
    if (m_fPause) Pause(true, true);
    m_reader.SetFile(nullptr);
    m_file.reset();
    CloseSocket();
    ClosePipe();
    m_udpPort = 0;
    m_pipeNumber = -1;
    m_tsShifter.Reset();
}


// PCRが現れるまでTSパケットを読んで転送する
// 適当にレート制御される
// 戻り値: 0:ファイルの終端に達したかPCRが挿入されていない, 1:正常に転送された,
//         2:正常に転送されたが、レート制御がリセットされた
int CTsSender::Send()
{
    if (!IsOpen()) return 0;

    TCHAR readyState[BON_PIPE_MESSAGE_MAX];
    bool fReadToPcr = false;
    bool fReadError = false;
    if (m_fPause) {
        readyState[0] = 0;
        ::Sleep(20);
    }
    else {
        // (可能ならば)受信側のバッファ状態を取得しておく
        TransactMessage(TEXT("GET_READY_STATE"), readyState);
        if (readyState[0] == TEXT('H') || readyState[0] == TEXT('F')) {
            // 受信側のストア多->送らない
            m_rateCtrlMsec += 10;
            ::Sleep(10);
        }
        else {
            fReadToPcr = ReadToPcr(true, m_fForceSyncRead);
            fReadError = !fReadToPcr;
        }
    }
    // 補正により一気に増加する可能性を避けるため定期的に呼ぶ
    DWORD tick = GetAdjTickCount();

    if (m_file->IsShareWrite()) {
        // 動画の長さ情報を更新
        if (tick - m_renewSizeTick >= RENEW_SIZE_INTERVAL) {
            __int64 fileSize = m_reader.GetFileSize();
            if (fileSize >= 0) {
                if (m_fileState == FILE_ST_SPECIAL_EXTENDING && fileSize < m_fileSize) {
                    // 容量確保領域が録画終了によって削除されたと仮定する
                    m_fileState = FILE_ST_FIXED;
                }
                else if (m_fileState != FILE_ST_SPECIAL_EXTENDING && fileSize == m_fileSize) {
                    // 伸ばしすぎた分を戻す
                    if (m_fileState == FILE_ST_EXTENDING) m_duration -= RENEW_SIZE_INTERVAL;
                    m_fileState = FILE_ST_FIXED;
                }
                else if (m_fileState != FILE_ST_SPECIAL_EXTENDING) {
                    m_fileState = FILE_ST_EXTENDING;
                }
                m_fileSize = fileSize;
            }
            m_renewSizeTick = tick;
        }
        // 実際に変化したかどうかを最初に確認するまで加算を保留する
        if (m_fileState != FILE_ST_MAYBE_EXTENDING) {
            if (m_fileState != FILE_ST_FIXED) m_duration += tick - m_renewDurTick;
            m_renewDurTick = tick;
        }

        // ファイルを強制的に同期読み込みさせるかどうか判断する
        // (追っかけ時のファイル末尾はデータの有無が不安定なため)
        if (tick - m_renewFsrTick >= RENEW_FSR_INTERVAL) {
            if (m_fileState != FILE_ST_FIXED) {
                int bufSize = m_reader.GetBufferSize();
                int rate = GetRate();
                if (m_fileState != FILE_ST_SPECIAL_EXTENDING) {
                    // 追っかけ時の末尾付近では先読みしない
                    __int64 fileRemain = m_reader.GetFileSize() - m_reader.GetFilePosition();
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

    if (fReadToPcr) {
        // 転送レート制御
        DWORD tickDiff = tick - m_baseTick;
        DWORD pcrDiff = DiffPcr(m_pcr, m_basePcr);
        // 再生速度が上がる=PCRの進行が遅くなる
        int msec = (int)((long long)pcrDiff * m_speedDen / m_speedNum / PCR_PER_MSEC) - tickDiff;
        // PCRの連続性チェックのため
        int prevPcrDiff = CounterDiff(m_pcr, m_prevPcr);
        m_prevPcr = m_pcr;

        if (m_fUnderrunCtrl && readyState[0] == TEXT('E') &&
            !m_fForceSyncRead/*先読みを禁止しているのだから勝手に進行させない*/ &&
            msec + (int)((__int64)m_rateCtrlMsec * m_speedDen / m_speedNum) > 0)
        {
            // 受信側のストア空->増やす
            m_rateCtrlMsec -= 10;
        }
        msec += (int)((__int64)m_rateCtrlMsec * m_speedDen / m_speedNum);

        // 制御しきれない場合は一度リセット
        if (msec < -2000 || 2000 * m_speedDen / m_speedNum < msec ||
            (DWORD)(prevPcrDiff < 0 ? -prevPcrDiff : prevPcrDiff) > m_pcrDisconThreshold)
        {
            // 受信側のストアをパージ
            TransactMessage(TEXT("PURGE"));
            // 基準PCRを設定(受信側のストアを増やすため少し引く)
            m_baseTick = tick - m_initStore;
            m_basePcr = m_pcr;
            m_rateCtrlMsec = 0;
            return 2;
        }
        if (msec > 0) ::Sleep(msec);
    }
    return fReadError ? 0 : 1;
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
    // 受信側のストアをパージ
    if (!m_fPause) TransactMessage(TEXT("PURGE"));
    else Pause(true, true);

    return Seek(0, IReadOnlyFile::MOVE_METHOD_BEGIN);
}


// ファイルの末尾から約2秒前にシークする
bool CTsSender::SeekToEnd()
{
    if (m_fileState == FILE_ST_SPECIAL_EXTENDING) return false;

    // 受信側のストアをパージ
    if (!m_fPause) TransactMessage(TEXT("PURGE"));
    else Pause(true, true);

    m_reader.Flush();

    CReadOnlyMpeg4File *mpeg4File = dynamic_cast<CReadOnlyMpeg4File*>(m_file.get());
    if (mpeg4File) {
        // 時間ベースでシークできる
        return Seek(mpeg4File->GetPositionBytesFromMsec(max(m_duration - 2000, 0)), IReadOnlyFile::MOVE_METHOD_BEGIN);
    }
    return Seek(-GetRate()*2, IReadOnlyFile::MOVE_METHOD_END);
}


// 現在の再生位置からmsecだけシークする
// 現在の再生位置が不明の場合はSeekToBegin()
// シーク可能範囲を超えるor下回る場合は先頭or末尾の約2秒前までシークする
// シークできなかったorシークが打ち切られたときはfalseを返す
bool CTsSender::Seek(int msec)
{
    if (!m_fEnPcr) return SeekToBegin();

    // 受信側のストアをパージ
    if (!m_fPause) TransactMessage(TEXT("PURGE"));
    else Pause(true, true);

    m_reader.Flush();
    __int64 rate = GetRate();
    __int64 size = m_reader.GetFileSize();
    __int64 pos = m_reader.GetFilePosition();
    DWORD prevPcr = m_pcr;

    if (size < 0 || pos < 0) return false;

    CReadOnlyMpeg4File *mpeg4File = dynamic_cast<CReadOnlyMpeg4File*>(m_file.get());
    if (mpeg4File) {
        // 時間ベースでシークできる
        int posMsec = mpeg4File->GetPositionMsecFromBytes(pos);
        return Seek(mpeg4File->GetPositionBytesFromMsec(max(min(posMsec + msec - INITIAL_STORE_MSEC, m_duration - 2000), 0)),
                    IReadOnlyFile::MOVE_METHOD_BEGIN);
    }

    // msec==3000を目標に0<=msec<=6000になるまで動画レートから概算シーク
    // 6ループまでに収束しなければ失敗
    msec -= 3000;
    for (int i = 0; i < 6 && (msec < -3000 || 3000 < msec); i++) {
        // 前方or後方にこれ以上シークできない場合
        if (msec < 0 && pos <= rate ||
            msec > 0 && pos >= size - rate*2) return true;

        __int64 approx = pos + rate * msec / 1000;
        if (approx > size - rate*2) approx = size - rate*2;
        if (approx < 0) approx = 0;

        if (!Seek(approx, IReadOnlyFile::MOVE_METHOD_BEGIN)) return false;

        // 移動分を差し引く
        int pcrDiff = CounterDiff(m_pcr, prevPcr);
        int nextMsec = msec + (pcrDiff < 0 ? (int)(-pcrDiff / PCR_PER_MSEC) : -(int)(pcrDiff / PCR_PER_MSEC));
        if ((nextMsec < -3000 || 3000 < nextMsec) &&
            ((msec < 0 && nextMsec > -msec / 2) || (msec > 0 && nextMsec < -msec / 2))) {
            // 移動しすぎているのでレートを下げてやり直し
            rate = rate * 2 / 3;
        }
        else {
            if ((msec < 0 && nextMsec * 2 < msec) || (msec > 0 && nextMsec * 2 > msec)) {
                // あまり移動していないのでレートを上げる
                rate = rate * 3 / 2;
            }
            pos = approx;
            prevPcr = m_pcr;
            msec = nextMsec;
        }
    }
    if (msec < -3000 || 3000 < msec) {
        return false;
    }
    msec += 3000;

    // 目標位置まで進む
    int nextMsec = msec;
    while (nextMsec > INITIAL_STORE_MSEC) {
        m_fEnPcr = false;
        if (nextMsec > msec || !ReadToPcr(false, true)) {
            Seek(pos, IReadOnlyFile::MOVE_METHOD_BEGIN);
            return false;
        }
        m_prevPcr = m_pcr;
        m_lastSentPcr = m_pcr;

        int pcrDiff = CounterDiff(m_pcr, prevPcr);
        nextMsec = msec + (pcrDiff < 0 ? (int)(-pcrDiff / PCR_PER_MSEC) : -(int)(pcrDiff / PCR_PER_MSEC));
    }
    return true;
}


// 一時停止する
// !fPurgeのとき転送先がPAUSE命令に応答すればパージを省略する
void CTsSender::Pause(bool fPause, bool fPurge)
{
    if (m_fPause && fPause && !m_fPurged && fPurge) {
        TransactMessage(TEXT("PAUSE 0"));
        TransactMessage(TEXT("PURGE"));
        m_fPurged = true;
    }
    else if (m_fPause && !fPause) {
        if (m_fPurged) {
            // 基準PCRを設定(受信側のストアを増やすため少し引く)
            m_baseTick = GetAdjTickCount() - m_initStore;
            m_basePcr = m_pcr;
            m_rateCtrlMsec = 0;
        }
        else {
            // 基準PCRを設定
            m_baseTick = GetAdjTickCount();
            m_basePcr = m_pcr;
            TransactMessage(TEXT("PAUSE 0"));
        }
    }
    else if (!m_fPause && fPause) {
        if (!fPurge && TransactMessage(TEXT("PAUSE 1"))) {
            m_fPurged = false;
        }
        else {
            // 受信側のストアをパージ
            TransactMessage(TEXT("PURGE"));
            m_fPurged = true;
        }
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
    m_rateCtrlMsec = 0;
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
    if (m_fileState == FILE_ST_SPECIAL_EXTENDING) {
        rate = m_specialExtendInitRate;
    }
    else {
        __int64 fileSize = m_reader.GetFileSize();
        rate = fileSize < 0 || m_duration <= 0 ? TS_SUPPOSED_RATE :
               static_cast<int>(fileSize * 1000 / m_duration);
    }
    return min(max(rate, TS_SUPPOSED_RATE/128), TS_SUPPOSED_RATE*12);
}


// 補正済みのTickカウントを取得する
DWORD CTsSender::GetAdjTickCount()
{
    LARGE_INTEGER liNow;
    if (m_adjFreq > 0 && ::QueryPerformanceCounter(&liNow)) {
        return m_adjBaseTick + static_cast<DWORD>((liNow.QuadPart - m_adjBase) * 1000 / m_adjFreq);
    }
    return ::GetTickCount();
}


// PCRが現れるまでTSパケットを処理する
// 戻り値: false:終端に達したかPCRが現れる前に処理パケット数が上限に達した, true:正常に処理した
bool CTsSender::ReadToPcr(bool fSend, bool fSyncRead)
{
    MAGIC_NUMBER(0x76389426);
    bool fPcr = false;
    for (int limit = READ_TO_PCR_LIMIT_PACKETS; !fPcr && limit > 0; --limit) {
        // TSパケットを1つ処理する

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
                if ((m_curr = resync(m_curr, m_tail, m_unitSize)) != nullptr) break;
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
                        m_rateCtrlMsec = 0;
                        m_fEnPcr = true;
                    }
                    fPcr = true;
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
        m_tsShifter.Transform(m_curr);

        m_curr += m_unitSize;

        // あまり貯めすぎると再生に影響するため
        if (m_curr - m_head >= BON_UDP_TSDATASIZE - 1880 ||
            (fSend && CounterDiff(m_pcr, m_lastSentPcr) >= MAX_SEND_INTERVAL))
        {
            RotateBuffer(fSend, fSyncRead);
        }
    }
    return fPcr;
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
        m_lastSentPcr = m_pcr;
    }
    m_head = m_curr;

    // パケットを処理するだけのデータがなければ読む
    if (!m_curr || m_tail - m_curr <= m_unitSize) {
        // この繰越し処理によりm_headとパケットヘッダは同期する
        BYTE carry[320];
        int carrySize = !m_curr ? 0 : static_cast<int>(m_tail - m_curr);
        if (carrySize > 0) memcpy(carry, m_curr, carrySize);

        BYTE *pBuf;
        int readBytes = fSyncRead ? m_reader.SyncRead(&pBuf) : m_reader.Read(&pBuf);
        if (readBytes < 0) {
            m_curr = m_head = m_tail = nullptr;
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
bool CTsSender::Seek(__int64 distanceToMove, IReadOnlyFile::MOVE_METHOD moveMethod)
{
    m_reader.Flush();
    __int64 lastPos = m_reader.GetFilePosition();
    if (lastPos < 0) return false;

    // 同期済みのファイルがなるべく同期済みのままになるよう移動量をパケット単位にする
    __int64 moveUnits = distanceToMove < 0 ? -(-distanceToMove / m_unitSize) : distanceToMove / m_unitSize;
    if (m_file->SetPointer(moveUnits * m_unitSize, moveMethod) < 0) return false;

    m_curr = m_head = m_tail = nullptr;
    m_fEnPcr = false;
    if (!ReadToPcr(false, true)) {
        // なるべく呼び出し前の状態に回復させるが、完全とは限らない
        if (m_file->SetPointer(lastPos, IReadOnlyFile::MOVE_METHOD_BEGIN) >= 0) {
            m_curr = m_head = m_tail = nullptr;
            m_fEnPcr = false;
            if (ReadToPcr(false, true)) {
                m_prevPcr = m_pcr;
                m_lastSentPcr = m_pcr;
            }
        }
        return false;
    }
    m_prevPcr = m_pcr;
    m_lastSentPcr = m_pcr;
    return true;
}


// ファイル中にTSデータが存在する境目までシークする
// log2(ファイルサイズ/TS_SUPPOSED_RATE)回ぐらい再帰する
bool CTsSender::SeekToBoundary(__int64 predicted, __int64 range, BYTE *pWork, int workSize)
{
    m_reader.Flush();
    if (m_file->SetPointer(predicted, IReadOnlyFile::MOVE_METHOD_BEGIN) < 0) return false;

    if (range < TS_SUPPOSED_RATE) return true;

    int readBytes = m_file->Read(pWork, workSize);
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
            ::WSACleanup();
        }
    }
}

void CTsSender::CloseSocket()
{
    if (m_sock != INVALID_SOCKET) {
        ::closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        ::WSACleanup();
    }
}

void CTsSender::OpenPipe()
{
    if (m_pipeNumber < 0) return;
    ClosePipe();

    TCHAR name[64];
    _stprintf_s(name, TEXT("\\\\.\\pipe\\BonDriver_Pipe%02d"), m_pipeNumber);
    if (::WaitNamedPipe(name, NMPWAIT_USE_DEFAULT_WAIT)) {
        // TSデータ書き込み用
        m_hPipe = ::CreateFile(name, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (m_hPipe != INVALID_HANDLE_VALUE) {
        _tcscat_s(name, TEXT("Ctrl"));
        if (::WaitNamedPipe(name, NMPWAIT_USE_DEFAULT_WAIT)) {
            // 制御用
            m_hCtrlPipe = ::CreateFile(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (m_hCtrlPipe != INVALID_HANDLE_VALUE) {
                // メッセージストリームにする
                DWORD dwMode = PIPE_READMODE_MESSAGE;
                if (!::SetNamedPipeHandleState(m_hCtrlPipe, &dwMode, nullptr, nullptr)) {
                    CloseCtrlPipe();
                }
            }
        }
    }
}

void CTsSender::ClosePipe()
{
    CloseCtrlPipe();

    if (m_hPipe != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_hPipe);
        m_hPipe = INVALID_HANDLE_VALUE;
    }
}

void CTsSender::CloseCtrlPipe()
{
    if (m_hCtrlPipe != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_hCtrlPipe);
        m_hCtrlPipe = INVALID_HANDLE_VALUE;
    }
}

void CTsSender::SendData(BYTE *pData, int dataSize)
{
    if (m_udpPort != 0) {
        if (m_sock == INVALID_SOCKET) OpenSocket();
        if (m_sock != INVALID_SOCKET) {
            // UDP転送
            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(m_udpPort);
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
    if (m_pipeNumber >= 0) {
        if (m_hPipe == INVALID_HANDLE_VALUE) OpenPipe();
        if (m_hPipe != INVALID_HANDLE_VALUE) {
            // パイプ転送
            DWORD written;
            if (!::WriteFile(m_hPipe, pData, (DWORD)dataSize, &written, nullptr)) {
                ClosePipe();
            }
        }
    }
}

// 受信側に要求を送る
// 戻り値: 0(失敗)または受け取った応答文字数
int CTsSender::TransactMessage(LPCTSTR request, LPTSTR reply)
{
    if (m_pipeNumber >= 0 && m_hCtrlPipe != INVALID_HANDLE_VALUE) {
        TCHAR buf[BON_PIPE_MESSAGE_MAX];
        DWORD read;
        BOOL fSuccess = ::TransactNamedPipe(m_hCtrlPipe,
                                            const_cast<LPTSTR>(request),
                                            static_cast<DWORD>((_tcslen(request) + 1) * sizeof(TCHAR)),
                                            buf, sizeof(buf), &read, nullptr);
        if (fSuccess && read >= sizeof(TCHAR)) {
            buf[read / sizeof(TCHAR) - 1] = 0;
            if (buf[0] == TEXT('A') && buf[1] == TEXT(' ')) {
                if (reply) _tcscpy_s(reply, BON_PIPE_MESSAGE_MAX, buf + 2);
                return read / sizeof(TCHAR) - 3;
            }
        }
        else {
            CloseCtrlPipe();
        }
    }
    if (reply) reply[0] = 0;
    return 0;
}
