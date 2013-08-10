#include <Windows.h>
#include "AsyncFileReader.h"

#define XFER_E_PENDING  -2
#define XFER_E_FAIL     -1

CAsyncFileReader::CAsyncFileReader()
    : m_hFile(INVALID_HANDLE_VALUE)
    , m_bufSize(0)
    , m_bufPreSize(0)
    , m_olNum(0)
    , m_olReqUnit(2)
    , m_olRear(0)
    , m_olFront(0)
    , m_posRear(0)
    , m_posFront(0)
{
    SetupBuffer(64, 0, 1);
}

CAsyncFileReader::~CAsyncFileReader()
{
    Close();
    SetupBuffer(64, 0, 0);
}

// 非同期読み込みバッファを設定する
// bufPrefixSizeだけ、Read()で返されるポインタより手前にも確保される
bool CAsyncFileReader::SetupBuffer(int bufSize, int bufPrefixSize, int bufNum, int olReqUnit)
{
    if (bufSize <= 0 || bufPrefixSize < 0 || bufNum < 0 || olReqUnit < 2) return false;

    // キュー構造上使われないOVERLAPPEDが1つあるため、1つ余分に確保
    int olNum = bufNum<=1 ? bufNum : min(bufNum+1, OVERLAPPED_MAX);

    Clear();
    if (m_bufSize != bufSize || m_bufPreSize != bufPrefixSize || m_olNum != olNum) {
        while (m_olNum > 0) {
            delete [] (m_pBuf[--m_olNum] - m_bufPreSize);
            ::CloseHandle(m_hEvent[m_olNum]);
        }
        // バッファを再確保
        m_bufSize = bufSize;
        m_bufPreSize = bufPrefixSize;
        while (m_olNum < olNum) {
            m_hEvent[m_olNum] = ::CreateEvent(NULL, TRUE, FALSE, NULL);
            if (!m_hEvent[m_olNum]) return false;
            m_pBuf[m_olNum++] = new BYTE[m_bufPreSize + m_bufSize] + m_bufPreSize;
        }
    }
    m_olReqUnit = olReqUnit;
    return true;
}

bool CAsyncFileReader::Open(LPCTSTR path, DWORD dwShareMode, DWORD dwFlagsAndAttributes)
{
    Close();
    m_hFile = ::CreateFile(path, GENERIC_READ, dwShareMode, NULL,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED | dwFlagsAndAttributes, NULL);
    return IsOpen();
}

void CAsyncFileReader::Close()
{
    if (IsOpen()) {
        ::CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
    m_olRear = m_olFront = 0;
    m_posRear = m_posFront = 0;
}

// 非同期要求をクリアする
void CAsyncFileReader::Clear()
{
    if (IsOpen() && m_olNum >= 2 && m_olFront != m_olRear) {
        // CancelIo()は同一スレッドが発行した操作のみ中止することに注意
        ::CancelIo(m_hFile);
        while (m_olFront != m_olRear) {
            if (m_xferred[m_olFront] == XFER_E_PENDING) {
                // 非同期読み込みを完了させる
                DWORD read;
                ::GetOverlappedResult(m_hFile, &m_ol[m_olFront], &read, TRUE);
            }
            m_olFront = OlNext(m_olFront);
        }
    }
    m_olRear = m_olFront = 0;
    m_posRear = m_posFront;
}

// 非同期読み込みの結果を受け取る
int CAsyncFileReader::Read(BYTE **ppBuf)
{
    if (m_olNum == 1) return SyncRead(ppBuf);
    if (!IsOpen() || m_olNum < 2) return -1;

    DWORD read;
    if (OlCount() <= 1 || m_olNum-OlCount()-1 >= m_olReqUnit) {
        // 最大m_olReqUnitまで非同期要求する
        for (int i = 0; OlNext(m_olRear) != m_olFront && i < m_olReqUnit; ++i) {
            memset(&m_ol[m_olRear], 0, sizeof(OVERLAPPED));
            m_ol[m_olRear].Offset = static_cast<DWORD>(m_posRear);
            m_ol[m_olRear].OffsetHigh = static_cast<DWORD>(m_posRear>>32);
            m_ol[m_olRear].hEvent = m_hEvent[m_olRear];
            if (::ReadFile(m_hFile, m_pBuf[m_olRear], m_bufSize, &read, &m_ol[m_olRear])) {
                m_posRear += read;
                m_xferred[m_olRear] = read;
            }
            else if (::GetLastError() == ERROR_IO_PENDING) {
                m_posRear += m_bufSize;
                m_xferred[m_olRear] = XFER_E_PENDING;
            }
            else {
                m_xferred[m_olRear] = XFER_E_FAIL;
            }
            m_olRear = OlNext(m_olRear);
        }
    }

    // 1つだけ非同期読み込みを完了させる
    *ppBuf = m_pBuf[m_olFront];
    int xferred = m_xferred[m_olFront];
    bool fClear = false;
    if (xferred == XFER_E_PENDING) {
        if (::GetOverlappedResult(m_hFile, &m_ol[m_olFront], &read, TRUE)) {
            xferred = read;
        }
        if (xferred < m_bufSize) {
            // (終端に達したなど)m_posRearとの不整合を防ぐためクリアする
            fClear = true;
        }
    }
    if (xferred >= 0) {
        m_posFront += xferred;
    }
    m_olFront = OlNext(m_olFront);
    if (fClear) Clear();
    return xferred;
}

// 同期読み込みする
// 非同期要求はクリアされる
int CAsyncFileReader::SyncRead(BYTE *pBuf, int numToRead)
{
    if (!IsOpen() || m_olNum < 1) return -1;
    Clear();

    DWORD read;
    memset(m_ol, 0, sizeof(OVERLAPPED));
    m_ol[0].Offset = static_cast<DWORD>(m_posRear);
    m_ol[0].OffsetHigh = static_cast<DWORD>(m_posRear>>32);
    m_ol[0].hEvent = m_hEvent[0];
    if (::ReadFile(m_hFile, pBuf, numToRead, &read, m_ol) ||
        ::GetLastError() == ERROR_IO_PENDING &&
        ::GetOverlappedResult(m_hFile, m_ol, &read, TRUE))
    {
        m_posRear += read;
        m_posFront = m_posRear;
        return read;
    }
    return -1;
}

int CAsyncFileReader::SyncRead(BYTE **ppBuf)
{
    *ppBuf = m_pBuf[0];
    return SyncRead(*ppBuf, m_bufSize);
}

// ファイルポインタの位置を設定する
// 戻り値がfalseのとき内部状態は変化しない
// 非同期要求はクリアされる
bool CAsyncFileReader::SetPointer(__int64 distanceToMove, DWORD dwMoveMethod)
{
    __int64 moved = -1;
    if (IsOpen()) {
        if (dwMoveMethod == FILE_BEGIN) {
            moved = distanceToMove;
        }
        else if (dwMoveMethod == FILE_CURRENT) {
            moved = m_posFront + distanceToMove;
        }
        else if (dwMoveMethod == FILE_END) {
            moved = GetSize();
            if (moved >= 0) moved += distanceToMove;
        }
    }
    if (moved < 0) return false;

    Clear();
    m_posRear = m_posFront = moved;
    return true;
}

// 現在のファイルサイズを取得する
__int64 CAsyncFileReader::GetSize() const
{
    LARGE_INTEGER liFileSize;
    if (IsOpen() && ::GetFileSizeEx(m_hFile, &liFileSize)) {
        return liFileSize.QuadPart;
    }
    return -1;
}
